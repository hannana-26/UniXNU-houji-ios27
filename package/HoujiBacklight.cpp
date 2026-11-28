// SPDX-License-Identifier: APSL-2.0
// houji backlight (r72, 2026-11-26): panel brightness for the houji AMOLED -
// DSI DCS 0x51 write (16-bit, high byte first) / 0x52 readback over the DSI0
// command controller the firmware leaves enabled at 0x0ae94000 (houji.dts:41981,
// dsi-ctrl-hw-v2.8; panel constants init 0x133 / fac-max 0xb32 / inverted-dbv,
// houji.dts:31645-31693).
//
// Borrowed from lonzzi/nian0114 kernel/overlay/iokit/Kernel/arm/HoujiBacklight.inc
// (APSL-2.0; the two repos' copies are byte-identical, sha256
// 528b5391dc74b97a6a86f7e8ec27997a607286e4fb08d0084b0eb42c88067cba, kept in
// work/HoujiBacklight.inc.upstream and recorded in the porting record). Their
// device evidence on this phone family: lonzzi docs/performance.md:218-227 -
// r227 DSI initial-level readback + service registration ok, r229 the user
// confirms dimming responds with no DSI errors. The iOS 27 consumer contract the
// IOUserClient hook (patches/xnu-12377/houji-backlight-hooks.patch) serves is
// sheng's package/AppleARMBacklight.cpp:349-359 verbatim: CoreBrightness drives a
// service only when backlight-control = true AND IOObjectConformsTo(service,
// "AppleARMBacklight") (CBDisplayContaineriOS findBacklight, int-236: publish both
// or the libEDR send deadlocks the display), and backboardd's sandbox refuses
// FadePeriod so the set-properties gate needs the grant (int-276).
//
// Adaptations (each marked inline; everything else is the borrowed text):
//   1. printf replaces IOLog (4 sites) - the logfs readback carries only the
//      printf stream (r68 lesson; IOLog lines never reach it).
//   2. A 15 s heartbeat timer prints the in-memory trace counters even when the
//      service is idle: the borrowed tick stops rescheduling once no command/fade
//      is pending, which would silence the readback tail (纲领 §4.12, the r71
//      readback lesson: heartbeat lines survive ring-buffer wrap, init lines do
//      not). Print-only: it never touches DSI registers.
//   3. houji_backlight_start is non-static AND extern "C": our late probe
//      (LegacyDisplayProbe.cpp) starts the service with
//      IOService::getResourceService() as the registry provider (KeyStoreProbe
//      precedent), not a framebuffer object - and that file wraps its probe region
//      in a file-wide extern "C" block (the CI 45920620688 linker frames prove it:
//      _native_legacy_display_late_probe is a plain C symbol), so this definition
//      must carry C linkage too or the reference stays undefined. It also registers
//      the conformance answer with IOUserClient.cpp's self-contained hook
//      (houji_backlight_set_conforms_hook): the fileset-release-reference link
//      (board bcm2837) does not carry the device archives, so core must not
//      reference archive symbols directly (rev 2, CI 45912991195 lesson) - there
//      the hook pointer stays null and the fallback never runs.
//   4. proc_selfname is declared locally with its bsd/sys/proc.h:278 signature
//      (void proc_selfname(char *, int)) and the call site casts sizeof to int
//      (-Wsign-conversion). The snapshot hook is kept unchanged.
//   5. Includes: this is a standalone TU now (the .inc was included by a TU that
//      already had the IOKit headers).
//   6. The backboardd property grant: the resolved patch stack ALREADY carries the
//      int-276 mechanism - IOUserClient.cpp's set-properties gate calls
//      native_sandbox_grant() for services that publish NativeSandboxGrantProcess
//      (its comment names backboardd's FadePeriod refusal verbatim) - so no MACF
//      patch is needed: publishFirst publishes NativeSandboxGrantProcess =
//      "backboardd" (sheng AppleARMBacklight.cpp:360 does the same). The borrowed
//      houji_backlight_allows_properties hook stays as an unused fallback.
// The DCS/DSI register protocol lives in HoujiBacklightProtocol.h, which IS
// byte-identical to the upstream copy (sha256
// b15e853021bc2307233663ba6ce62259fcc5ade623cafe7b4c9f21c62489c5c3).
// Statics are POD zero-initialized only (no static OSSharedPtr): this file joins
// libNativeLegacyDisplay.late.a where a non-trivial static constructor would add a
// __mod_init_func entry (纲领 §4.11; the borrowed file already complies - its only
// statics are the unsigned trace array and the service pointer).
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <kern/clock.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSCollectionIterator.h>
#include <pexpert/pexpert.h>
#include <string.h>

#include "HoujiBacklightProtocol.h"

// Adaptation 4: bsd/sys/proc.h:278 in the xnu feed (xnu-3199e8bc).
extern "C" void proc_selfname(char *buf, int size);

// Adaptation 3: the conformance hook storage lives in IOUserClient.cpp, self-contained
// in core (see the header note); the archive registers its answer at service start.
extern "C" void houji_backlight_set_conforms_hook(bool (*hook)(const OSObject *, const char *));

// Cached facts survive message-ring wrap. Snapshot readers never touch DSI
// or acquire the backlight work loop, including during recovery shutdown.
// iOS 27 CoreBrightness contract constants (sheng AppleARMBacklight.cpp:57-59
// verbatim values): the range CoreBrightness works in; kMidNits is
// CoreBrightness's own default. These set the slider's scale, not an absolute
// brightness - the houji panel's luminance is not measured (porting.toml
// display findings records them as such). constexpr = POD, no __mod_init_func
// entry (纲领 §4.11).
constexpr uint32_t HBL_MIN_NITS=2, HBL_MID_NITS=140, HBL_MAX_NITS=500;
namespace HoujiBacklightTrace {
enum Field { Stage, Failure, Registered, Requests, Rejected, Grants, Conforms,
    Requested, Applied, Measured, Control, Status, Interrupt, Fifo, ReadCount, Packet,
    AckError, RequestFields, BrightnessKind, FadeKind, RawBrightnessLo,
    RawBrightnessHi, RawFadeLo, RawFadeHi, Count };
static unsigned values[Count];
static void set(Field field,unsigned value){__atomic_store_n(&values[field],value,__ATOMIC_RELAXED);}
static void bump(Field field){__atomic_fetch_add(&values[field],1u,__ATOMIC_RELAXED);}
}
extern "C" unsigned houji_backlight_snapshot(char *out,unsigned capacity) {
    if(!out || !capacity)return 0;
    using namespace HoujiBacklightTrace;
    unsigned v[Count];for(unsigned i=0;i<Count;++i)v[i]=__atomic_load_n(&values[i],__ATOMIC_RELAXED);
    int n=snprintf(out,capacity,
        "HOUJI BACKLIGHT SNAPSHOT: stage=%u failure=%u registered=%u requests=%u rejected=%u grants=%u conforms=%u requested=%u last-level=%u boot-measured=%u\n"
        "HOUJI BACKLIGHT DSI: ctrl=%x status=%x irq=%x fifo=%x read-count=%x packet=%x ack-error=%x; cached observations\n"
        "HOUJI BACKLIGHT INPUT: fields=%u brightness-kind=%u raw=%x:%x fade-kind=%u raw=%x:%x; kind 1=number 2=value-dictionary 3=other\n",
        v[Stage],v[Failure],v[Registered],v[Requests],v[Rejected],v[Grants],v[Conforms],v[Requested],v[Applied],v[Measured],
        v[Control],v[Status],v[Interrupt],v[Fifo],v[ReadCount],v[Packet],v[AckError],
        v[RequestFields],v[BrightnessKind],v[RawBrightnessHi],v[RawBrightnessLo],
        v[FadeKind],v[RawFadeHi],v[RawFadeLo]);
    return n>0?((unsigned)n<capacity?(unsigned)n:capacity-1):0;
}
#include "HoujiBacklightProtocol.h"
class HoujiBacklight : public IOService {
    OSDeclareDefaultStructors(HoujiBacklight)
    IOWorkLoop *loop=nullptr;
    IOTimerEventSource *timer=nullptr;
    IOTimerEventSource *hb=nullptr;  // adaptation 2: the 15 s heartbeat timer
    IOMemoryMap *map=nullptr;
    uintptr_t base=0;
    HoujiDsiCommand command={};
    uint64_t request=0,fadeStart=0,fadeDuration=0,readbackDeadline=0;
    unsigned from=0,to=0,current=0,written=~0u,sending=0,stage=0;
    bool failed=false;
    void observe() {
        using namespace HoujiBacklightTrace;
        set(Stage,stage);
        if(!base)return;
        set(Control,read(this,4));set(Status,read(this,8));
        set(Interrupt,read(this,0x110));set(Fifo,read(this,0x15c));
        set(ReadCount,read(this,0x1d4));set(Packet,read(this,0x6c));set(AckError,read(this,0x68));
    }
    // Failure codes: map/mode=1, workloop=2, maximum-return command=3,
    // command completion=4, read submission=5, response timeout/error=6,
    // brightness submission=7. Do not retry an uncertain FIFO transaction.
    void fail(unsigned reason) {
        observe();HoujiBacklightTrace::set(HoujiBacklightTrace::Failure,reason);
        __atomic_store_n(&failed,true,__ATOMIC_RELEASE);
        setProperty("BacklightWriteFailed",true);
    }
    static uint64_t now(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
    static uint32_t read(void *ctx,unsigned off) {
        auto *s=static_cast<HoujiBacklight *>(ctx);
        uint32_t v=*(volatile uint32_t *)(s->base+off);
        __asm__ volatile("dmb oshld" ::: "memory");return v;
    }
    static void write(void *ctx,unsigned off,uint32_t v) {
        auto *s=static_cast<HoujiBacklight *>(ctx);
        __asm__ volatile("dmb oshst" ::: "memory");
        *(volatile uint32_t *)(s->base+off)=v;
        __asm__ volatile("dsb osh" ::: "memory");
    }
    void publish(unsigned value) {
        OSDictionary *b=OSDictionary::withCapacity(3),*m=OSDictionary::withCapacity(3),*p=OSDictionary::withCapacity(2);
        OSNumber *min=OSNumber::withNumber(0ULL,32),*max=OSNumber::withNumber((uint64_t)HBL_SCALE,32),*v=OSNumber::withNumber(value,32);
        // BrightnessControl takes the nits range and the current nits from here on
        // iOS 27 (sheng AppleARMBacklight.cpp:342-343): millinits = nits * 1000.
        OSNumber *minMilli=OSNumber::withNumber((uint64_t)HBL_MIN_NITS*1000,32),*maxMilli=OSNumber::withNumber((uint64_t)HBL_MAX_NITS*1000,32);
        OSNumber *curMilli=OSNumber::withNumber((uint64_t)value*HBL_MAX_NITS*1000/HBL_SCALE,32);
        if(b && m && p && min && max && v && minMilli && maxMilli && curMilli){
            b->setObject("min",min);b->setObject("max",max);b->setObject("value",v);
            m->setObject("min",minMilli);m->setObject("max",maxMilli);m->setObject("value",curMilli);
            p->setObject("brightness",b);
            p->setObject("BrightnessMilliNits",m);
            setProperty("IODisplayParameters",p);
        }
        OSSafeReleaseNULL(b);OSSafeReleaseNULL(m);OSSafeReleaseNULL(p);OSSafeReleaseNULL(min);OSSafeReleaseNULL(max);OSSafeReleaseNULL(v);
        OSSafeReleaseNULL(minMilli);OSSafeReleaseNULL(maxMilli);OSSafeReleaseNULL(curMilli);
    }
    // CoreBrightness's Display.m fails without a marketing table (equally spaced
    // slider samples, signed 16.16 nits), and without calibration parameters it
    // reads that table before loading it. Both describe the same range as
    // BrightnessMilliNits. BrightnessControl takes the product luminances from
    // their own properties, not from the calibration dictionary: at zero the
    // table rescales to infinity and every slider position means the maximum.
    // (sheng AppleARMBacklight.cpp:349-383 verbatim; constants are houji's.)
    void publishCalibration() {
        const int32_t table[]={int32_t(HBL_MIN_NITS<<16),int32_t(HBL_MID_NITS<<16),int32_t(HBL_MAX_NITS<<16)};
        if(OSData *marketing=OSData::withBytes(table,sizeof table)){setProperty("backlight-marketing-table",marketing);marketing->release();}
        // LmaxProduct is read as 4 bytes of 16.16. LmidProduct and LminProduct take
        // a number as it is; as data they would be read as a float.
        if(OSData *value=OSData::withBytes(&table[2],sizeof table[2])){setProperty("LmaxProduct",value);value->release();}
        const struct{const char *key;uint32_t nits;} products[]={{"LminProduct",HBL_MIN_NITS},{"LmidProduct",HBL_MID_NITS}};
        for(const auto &product:products){if(OSNumber *value=OSNumber::withNumber(product.nits,32)){setProperty(product.key,value);value->release();}}
        OSDictionary *calibration=OSDictionary::withCapacity(6);
        if(!calibration)return;
        const struct{const char *key;uint32_t nits;} fields[]={{"lMinProduct",HBL_MIN_NITS},{"lMidProduct",HBL_MID_NITS},{"lMaxProduct",HBL_MAX_NITS},{"lMinPanel",HBL_MIN_NITS},{"lMidPanel",HBL_MID_NITS},{"lMaxPanel",HBL_MAX_NITS}};
        for(const auto &field:fields){if(OSNumber *number=OSNumber::withNumber((uint64_t)field.nits<<16,32)){calibration->setObject(field.key,number);number->release();}}
        setProperty("backlight-calibration-parameters",calibration);
        calibration->release();
    }
    void publishFirst(unsigned level,bool measured) {
        current=from=to=houjiBacklightFraction(level);written=level;
        publishCalibration();publish(current);setProperty("BootBrightnessMeasured",measured);
        setProperty("backlight-control",true);setProperty("NativeSandboxGrantProcess","backboardd");registerService();  // adaptation 6: int-276 grant via the stack's native_sandbox_grant
        HoujiBacklightTrace::set(HoujiBacklightTrace::Registered,1);
        HoujiBacklightTrace::set(HoujiBacklightTrace::Measured,measured);
        HoujiBacklightTrace::set(HoujiBacklightTrace::Applied,level);observe();
        printf("HOUJI BACKLIGHT: n3 DSI0, normal range 0..%u, initial=%u measured=%u\n",HBL_MAX,level,measured);  // adaptation 1
    }
    static void tick(OSObject *owner,IOTimerEventSource *) {
        auto *s=static_cast<HoujiBacklight *>(owner);uint64_t t=now();
        if(__atomic_load_n(&s->failed,__ATOMIC_ACQUIRE))return;
        if(s->stage==4){
            uint32_t control=read(s,0x1d4);unsigned level=0;
            if((read(s,0x110)&(1u<<24)) || ((control>>16)<4 && t>=s->readbackDeadline)) {
                s->fail(6);
                printf("HOUJI BACKLIGHT: panel readback failed; further writes disabled\n");return;  // adaptation 1
            }
            if((control>>16)<4){s->timer->setTimeoutMS(1);return;}
            bool valid=houjiBacklightReadback(control,read(s,0x6c),&level);
            s->stage=3;s->publishFirst(valid?level:307,valid);return;
        }
        if(s->command.pending) {
            int result=s->command.poll(t);
            if(!result){s->timer->setTimeoutMS(1);return;}
            if(result<0){s->fail(4);printf("HOUJI BACKLIGHT: DSI command failed; further writes disabled\n");return;}  // adaptation 1
            if(s->stage==1){
                s->stage=2;
                if(!s->command.readBrightness(t)){s->fail(5);return;}
                s->observe();
                s->timer->setTimeoutMS(1);return;
            }
            if(s->stage==2){
                // Wait for a fresh complete packet, not a fixed BTA latency.
                s->stage=4;s->observe();s->readbackDeadline=t+50000;s->timer->setTimeoutMS(1);return;
            } else {s->written=s->sending;s->setProperty("AppliedPanelLevel",s->written,32);HoujiBacklightTrace::set(HoujiBacklightTrace::Applied,s->written);}
        }
        uint64_t r=__atomic_exchange_n(&s->request,0ull,__ATOMIC_ACQ_REL);
        if(r>>63){s->from=s->current;s->to=(unsigned)((r>>32)&0x1ffff);s->fadeStart=t;s->fadeDuration=(uint32_t)r;}
        s->current=houjiBacklightFade(s->from,s->to,t-s->fadeStart,s->fadeDuration);
        unsigned level=houjiBacklightLevel(s->current);
        if(level!=s->written) {
            s->sending=level;
            if(!s->command.brightness(level,t)){s->fail(7);return;}
            s->timer->setTimeoutMS(1);return;
        }
        if(s->current!=s->to || __atomic_load_n(&s->request,__ATOMIC_ACQUIRE))s->timer->setTimeoutMS(16);
    }
    // Adaptation 2: 15 s heartbeat of the in-memory counters. Print-only (no DSI
    // register access) so it cannot disturb the panel conversation; it keeps the
    // logfs tail alive while the service idles between fades.
    static void hbTick(OSObject *owner,IOTimerEventSource *) {
        auto *s=static_cast<HoujiBacklight *>(owner);
        using namespace HoujiBacklightTrace;
        unsigned v[Count];
        // Qualified on purpose: unqualified "values" would find this class's static
        // member function values(), not the trace array (纲领 §4.10 identifier check).
        for(unsigned i=0;i<Count;++i)v[i]=__atomic_load_n(&HoujiBacklightTrace::values[i],__ATOMIC_RELAXED);
        printf("HOUJI BACKLIGHT: hb stage=%u failure=%u registered=%u requests=%u rejected=%u grants=%u conforms=%u requested=%u applied=%u measured=%u failed=%u\n",
               v[Stage],v[Failure],v[Registered],v[Requests],v[Rejected],v[Grants],v[Conforms],v[Requested],v[Applied],v[Measured],
               __atomic_load_n(&s->failed,__ATOMIC_ACQUIRE)?1u:0u);
        if(s->hb)s->hb->setTimeoutMS(15000);
    }
public:
    static OSNumber *numberValue(OSObject *object,unsigned *kind) {
        *kind=object?3:0;
        if(auto *n=OSDynamicCast(OSNumber,object)){*kind=1;return n;}
        if(auto *d=OSDynamicCast(OSDictionary,object)) {
            if(auto *n=OSDynamicCast(OSNumber,d->getObject("value"))){*kind=2;return n;}
        }
        return nullptr;
    }
    // Level (in the 0..HBL_SCALE fraction space the request pipeline runs in) for
    // signed 16.16 nits, proportional as the LED current is; a lit panel never
    // rounds down to off (sheng AppleARMBacklight.cpp:472-478; sheng's kMaxLevel
    // panel unit maps to our fraction space, the existing curve then applies).
    static unsigned levelForNits(int32_t fixed) {
        if(fixed<=0)return 0;
        const uint64_t scale=(uint64_t)HBL_MAX_NITS<<16;
        const uint64_t scaled=((uint64_t)fixed*HBL_SCALE+scale/2)/scale;
        return scaled<1?1:scaled>HBL_SCALE?HBL_SCALE:(unsigned)scaled;
    }
    static bool values(OSObject *object,unsigned *level,uint64_t *duration) {
        using namespace HoujiBacklightTrace;
        auto *d=OSDynamicCast(OSDictionary,object);
        set(RequestFields,d?d->getCount():0);
        unsigned bk=0,fk=0,nk=0;
        // iPadOS 27 sends signed 16.16 nits and ramps them itself (zero or less is
        // the display going dark); iOS 17 sends a level and a fade period in 16.16
        // seconds (sheng AppleARMBacklight.cpp:482-493). The nits branch wins, as
        // in sheng; the raw nits value goes to the trace fields so the readback
        // shows the requested nits, not the converted level.
        auto *n=numberValue(d?d->getObject("brightness-nits"):nullptr,&nk);
        auto *b=numberValue(d?d->getObject("brightness"):nullptr,&bk);
        auto *f=numberValue(d?d->getObject("FadePeriod"):nullptr,&fk);
        uint64_t raw=n?n->unsigned64BitValue():(b?b->unsigned64BitValue():0),fade=f?f->unsigned64BitValue():0;
        set(BrightnessKind,n?nk:bk);set(FadeKind,fk);
        set(RawBrightnessLo,(unsigned)raw);set(RawBrightnessHi,(unsigned)(raw>>32));
        set(RawFadeLo,(unsigned)fade);set(RawFadeHi,(unsigned)(fade>>32));
        // CoreBrightness may send a parameter dictionary's value and unrelated
        // display metadata alongside brightness. Only these numeric keys
        // affect this service; never forward the dictionary to setProperties
        // on a superclass or copy arbitrary keys into the registry.
        if(!d || d->getCount()>16)return false;
        if(n){*level=levelForNits((int32_t)n->unsigned32BitValue());*duration=0;return true;}
        if(!b || raw>HBL_SCALE || (fk && !f))return false;
        if(fade>10u*65536u)return false;
        *level=b->unsigned32BitValue();*duration=fade*1000000/65536;return true;
    }
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE {
        if(!IOService::start(provider))return false;
        IODeviceMemory *m=IODeviceMemory::withRange(0x0ae94000ULL,0x1000);
        map=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;if(m)m->release();
        base=map?map->getVirtualAddress():0;
        if(!base || (read(this,4)&5)!=5){fail(1);return false;}
        loop=IOWorkLoop::workLoop();timer=IOTimerEventSource::timerEventSource(this,&HoujiBacklight::tick);
        if(!loop || !timer || loop->addEventSource(timer)!=kIOReturnSuccess){fail(2);return false;}
        hb=IOTimerEventSource::timerEventSource(this,&HoujiBacklight::hbTick);  // adaptation 2
        if(hb && loop->addEventSource(hb)==kIOReturnSuccess)hb->setTimeoutMS(15000);
        command.io={this,read,write};stage=1;uint32_t word=0x80370002u;
        if(!command.begin(&word,1,now())){fail(3);return false;}
        observe();
        timer->setTimeoutMS(1);return true;
    }
    IOReturn setProperties(OSObject *object) APPLE_KEXT_OVERRIDE {
        unsigned level=0;uint64_t duration=0;
        HoujiBacklightTrace::bump(HoujiBacklightTrace::Requests);
        if(!values(object,&level,&duration)){HoujiBacklightTrace::bump(HoujiBacklightTrace::Rejected);return kIOReturnBadArgument;}
        if(__atomic_load_n(&failed,__ATOMIC_ACQUIRE)){HoujiBacklightTrace::bump(HoujiBacklightTrace::Rejected);return kIOReturnNotReady;}
        HoujiBacklightTrace::set(HoujiBacklightTrace::Requested,level);
        __atomic_store_n(&request,(1ull<<63)|((uint64_t)level<<32)|duration,__ATOMIC_RELEASE);
        publish(level);timer->setTimeoutUS(1);return kIOReturnSuccess;
    }
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE {
        __atomic_store_n(&failed,true,__ATOMIC_RELEASE);if(timer){timer->cancelTimeout();timer->disable();if(loop)loop->removeEventSource(timer);}
        if(hb){hb->cancelTimeout();hb->disable();if(loop)loop->removeEventSource(hb);}  // adaptation 2
        IOService::stop(provider);
    }
    void free() APPLE_KEXT_OVERRIDE {
        OSSafeReleaseNULL(hb);OSSafeReleaseNULL(timer);OSSafeReleaseNULL(loop);OSSafeReleaseNULL(map);IOService::free();  // adaptation 2: hb released
    }
};
OSDefineMetaClassAndStructors(HoujiBacklight,IOService)
static HoujiBacklight *houjiBacklightService;
// Only the user-space compatibility query is extended. The kernel retains
// the real metaclass; no copied vtable or unsafe AppleARMBacklight downcast.
extern "C" bool houji_backlight_conforms(const OSObject *object,const char *name) {
    bool matches=object && object==houjiBacklightService && !strcmp(name,"AppleARMBacklight");
    if(matches)HoujiBacklightTrace::bump(HoujiBacklightTrace::Conforms);
    return matches;
}
extern "C" bool houji_backlight_allows_properties(IORegistryEntry *entry,OSObject *object) {
    if(!entry || entry!=houjiBacklightService)return false;
    char process[32]={};proc_selfname(process,(int)sizeof(process));  // adaptation 4: int cast
    unsigned level=0;uint64_t duration=0;
    bool allowed=!strcmp(process,"backboardd") && HoujiBacklight::values(object,&level,&duration);
    if(allowed)HoujiBacklightTrace::bump(HoujiBacklightTrace::Grants);
    else HoujiBacklightTrace::bump(HoujiBacklightTrace::Rejected);
    return allowed;
}
// Adaptation 3: extern "C" (the late probe's file-wide extern "C" block references this
// as the C symbol _houji_backlight_start - see the header note) and non-static.
extern "C" void houji_backlight_start(IOService *provider) {
    unsigned enabled=1;PE_parse_boot_argn("houji_bl",&enabled,sizeof(enabled));if(!enabled || houjiBacklightService)return;
    houji_backlight_set_conforms_hook(houji_backlight_conforms);  // adaptation 3: register the AppleARMBacklight conformance answer
    auto *s=new HoujiBacklight;
    if(!s)return;
    if(!s->init()){s->release();return;}
    if(!s->attach(provider)){s->release();return;}
    houjiBacklightService=s;
    if(!s->start(provider)){houjiBacklightService=nullptr;s->stop(provider);s->detach(provider);s->release();printf("HOUJI BACKLIGHT: command-mode handoff unavailable\n");return;}  // adaptation 1
    s->release();
}
