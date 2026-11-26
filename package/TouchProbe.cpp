// SPDX-License-Identifier: BSD-2-Clause-Patent
// houji TouchProbe: the Synaptics S3910 over GPIO bit-bang SPI (TouchCom v1).
//
// Protocol layer borrowed VERBATIM from lonzzi archive/native/touch (device-proven
// on this exact phone at their r52: GPIO 48/49/50/51/161/162, TLMM 0xf100000+n*0x1000,
// SE 0xa90000 read-only): HoujiTouchGpio.h / HoujiTouchWire.h / HoujiTouchReport.h /
// HoujiTouchDescriptor.h / HoujiTouchProbe.h (copied byte-identical, hash-checked).
// Shell (thread, seqlock publish, sink, stage counters) follows sheng's
// TouchProbe.cpp contract; the finger set is published to NativeHidInput's
// native_touch_* seqlock and the consumer wakes on its own work loop.
//
// Power-on is NOT done here: the firmware's PrepareTouchHandoff (HOUJI TOUCH UEFI,
// r70 device-verified) votes ldob9/ldob12 and the three QUP clocks before every
// boot. This worker only talks on the bus; the read-only probe capture gates it on
// the SE state exactly like lonzzi's driver does (errors 20/21 below).
//
// Diagnostics go through printf ONLY (the r68 lesson: the logfs readback carries
// the printf stream; IOLog lines never reach it). Gated by offline-touch=1.
// No ADT node and no kernel interrupt (P12: the ADT has no touch node); the IRQ
// GPIO is polled from its TLMM register at 10 ms like lonzzi does.
//
// Driver body adapted from lonzzi's HoujiTouchDriver.inc: printf replaces IOLog,
// IOSleep replaces the assert_wait/thread_block wait, houji_ufs_checkpoint hooks
// are dropped, and the per-event sweet_hid_touch(id,down,x,y) becomes one
// publish-per-frame of the currently-down set (sheng's contract: lifted fingers
// vanish from the arrays; NativeHidInput sends their tip-up at the last position).
// Every static is plain POD zero-initialized on purpose: no static OSSharedPtr
// (a non-trivial default constructor would add a __mod_init_func entry to this
// late archive - the sheng shell avoids static OSSharedPtr for the same reason),
// and the bus mappings are retained-but-never-released IOMemoryMap objects, which
// is the worker's lifetime anyway.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <string.h>
#include <kern/thread.h>
#include <kern/panic_call.h>
#include <pexpert/pexpert.h>

#include "HoujiTouchGpio.h"
#include "HoujiTouchWire.h"
#include "HoujiTouchReport.h"
#include "HoujiTouchDescriptor.h"
#include "HoujiTouchProbe.h"

extern "C" {
// The consumer side (package/NativeHidInput.cpp) reads this seqlock and registers
// its sink here; these are the 7 symbols the r68 stand-ins carried.
volatile uint32_t native_touch_seq, native_touch_count, native_touch_hid_reports;
volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];
}

static void (*gTouchSink)(void *);
static void *gTouchSinkContext;
extern "C" void native_touch_set_sink(void (*sink)(void *), void *context) {
    gTouchSinkContext = context;
    __atomic_store_n(&gTouchSink, sink, __ATOMIC_RELEASE);
}

static uintptr_t mapPhys(uint64_t phys, uint64_t length, IOMemoryMap **keep) {
    IODeviceMemory *memory = IODeviceMemory::withRange(phys, length);
    if (!memory) return 0;
    IOMemoryMap *map = memory->map(kIOMapAnywhere | kIOMapInhibitCache);
    memory->release();
    if (!map) return 0;
    *keep = map;  // retained by map(); deliberately never released (worker lifetime).
    return static_cast<uintptr_t>(map->getVirtualAddress());
}

// File-local extent counters (sheng's native_touch_extent idea): the min/max seen per
// axis judge the panel's Y orientation on the device from the logfs readback alone.
static volatile int32_t touchExtent[4] = {0x7fffffff, -1, 0x7fffffff, -1};

// One published frame: the currently-down finger set with a seqlock around it, then
// the sink wakes the consumer. Coordinates are raw device pixels (gt_decode scaled
// the chip's 1199x2669 app coordinates onto the 1200x2670 panel); mirror Y here
// (2669-y) only if the on-device extent proves the panel is inverted.
static void publish(unsigned count, const struct gt_event *e) {
    int ids[12], xs[12], ys[12];
    for (unsigned i = 0; i < count && i < 12; ++i) {
        ids[i] = e[i].id; xs[i] = e[i].x; ys[i] = e[i].y;
        if (xs[i] < touchExtent[0]) touchExtent[0] = xs[i];
        if (xs[i] > touchExtent[1]) touchExtent[1] = xs[i];
        if (ys[i] < touchExtent[2]) touchExtent[2] = ys[i];
        if (ys[i] > touchExtent[3]) touchExtent[3] = ys[i];
    }
    native_touch_seq = native_touch_seq + 1;
    __asm__ volatile("dmb ish" ::: "memory");
    native_touch_count = count;
    for (unsigned i = 0; i < count && i < 12; ++i) {
        native_touch_xs[i] = xs[i];
        native_touch_ys[i] = ys[i];
        native_touch_ids[i] = ids[i];
    }
    __asm__ volatile("dmb ish" ::: "memory");
    native_touch_seq = native_touch_seq + 1;
    if (void (*sink)(void *) = __atomic_load_n(&gTouchSink, __ATOMIC_ACQUIRE)) sink(gTouchSinkContext);
}

// The read-only handoff capture (HoujiTouchProbe.h, verbatim logic from lonzzi's
// HoujiTouchProbe.inc): SE + GPIO state before any driving, printed once.
static struct ht_probe gTouchProbe;
static bool gTouchProbeEnabled;
static uintptr_t gTouchGcc, gTouchSe;
static IOMemoryMap *gTouchGccMap, *gTouchSeMap, *gTouchGpioMap[6];
static int touchProbeRead32(void *, uint32_t physical, uint32_t *value) {
    if (!ht_probe_address_allowed(physical) || !value) return 0;
    uintptr_t base = 0; uint32_t offset = 0;
    if (physical >= 0x100000 && physical < 0x154000) {
        if (!gTouchGcc) gTouchGcc = mapPhys(0x100000, 0x54000, &gTouchGccMap);
        base = gTouchGcc; offset = physical - 0x100000;
    } else if (physical >= 0xa90000 && physical < 0xa94000) {
        if (!gTouchSe) gTouchSe = mapPhys(0xa90000, 0x4000, &gTouchSeMap);
        base = gTouchSe; offset = physical - 0xa90000;
    } else for (unsigned i = 0; i < 6; ++i) {
        uint32_t page = 0xf100000 + ht_gpio[i] * 0x1000;
        if (physical < page || physical > page + 4) continue;
        if (!gTouchGpio[i]) gTouchGpio[i] = mapPhys(page, 0x1000, &gTouchGpioMap[i]);
        base = gTouchGpio[i]; offset = physical - page; break;
    }
    if (!base) return 0;
    *value = *(volatile uint32_t *)(base + offset);
    __asm__ volatile("dmb oshld" ::: "memory");
    return 1;
}
static void touchProbeOnce(void) {
    struct ht_probe_ops ops = {nullptr, touchProbeRead32};
    ht_probe_capture(&ops, &gTouchProbe);
    gTouchProbeEnabled = true;
    printf("HOUJI TOUCH RO: result=%u reads=%u GCC branch=%x vote=%x root=%x/%x\n",
           gTouchProbe.result, gTouchProbe.completed_reads, gTouchProbe.branch, gTouchProbe.vote,
           gTouchProbe.root_command, gTouchProbe.root_config);
    printf("HOUJI TOUCH RO: SE firmware=%x interface=%x status=%x dma=%x tx=%x rx=%x dma-if=%x\n",
           gTouchProbe.firmware, gTouchProbe.interface, gTouchProbe.status, gTouchProbe.dma_mode,
           gTouchProbe.tx_fifo, gTouchProbe.rx_fifo, gTouchProbe.dma_interface);
    for (unsigned i = 0; i < 6; ++i)
        printf("HOUJI TOUCH RO: gpio%u cfg=%x io=%x\n", ht_gpio[i], gTouchProbe.gpio_config[i], gTouchProbe.gpio_io[i]);
}

// The driver (lonzzi HoujiTouchDriver.inc, adapted; see the header comment).
static struct {
    uintptr_t pin[6], gpi;
    IOMemoryMap *maps[6], *gpiMap;
    uint32_t cfg[6], io[6];
    struct gt_state contacts;
    bool changed, started;
    unsigned stage, error, frames, events, irqPolls, deviceMaxWrite, effectiveMaxWrite, configBytes, lastHeader, discarded;
    unsigned workerPhase, loops, irqLevel, hidEntered, hidCompleted;
    uint8_t reportConfig[512];
    unsigned descriptorBytes;
    uint8_t largeReport[8199];  // Fixed BSS scratch, never on the 16 KiB thread stack.
} ht;

static void htWait(unsigned ms) { IOSleep(ms); }
static uint32_t htRead(uintptr_t base, unsigned off) {
    uint32_t v = *(volatile uint32_t *)(base + off);
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
}
static void htPinWrite(unsigned index, unsigned offset, uint32_t value) {
    if (index >= 6 || (offset != 0 && offset != 4)) panic("HOUJI touch write boundary");
    *(volatile uint32_t *)(ht.pin[index] + offset) = value;
    __asm__ volatile("dsb osh" ::: "memory");
}
static void htSet(unsigned pin, unsigned high) {
    // Only the four board-confirmed outputs are ever driven.
    if (pin != 1 && pin != 2 && pin != 3 && pin != 4) panic("HOUJI touch output boundary");
    htPinWrite(pin, 4, (htRead(ht.pin[pin], 4) & ~2u) | (high ? 2u : 0));
}
static unsigned htSample(void) { return htRead(ht.pin[0], 4) & 1; }
static void htPause(void) { IODelay(1); }
static const struct gg_bus htGpioBus = {htSet, htSample, htPause};
static int htBusRead(uint8_t *data, unsigned bytes) {
    int result = gg_transfer(&htGpioBus, nullptr, data, bytes);
    if (!result && bytes == 4) {
        uint32_t header = data[0] | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16 | uint32_t(data[3]) << 24;
        __atomic_store_n(&ht.lastHeader, header, __ATOMIC_RELAXED);
    }
    return result;
}
static int htBusWrite(const uint8_t *data, unsigned bytes) {
    return gg_transfer(&htGpioBus, data, nullptr, bytes);
}
static int htDiscardReport(unsigned bytes) {
    if (bytes > 8192 || htBusRead(ht.largeReport, bytes + 7)) return 1;
    if (ht.largeReport[0] != 0xa5 || ht.largeReport[1] != 3) return 2;
    for (unsigned i = bytes + 2; i < bytes + 7; ++i) if (ht.largeReport[i] != 0x5a) return 3;
    __atomic_add_fetch(&ht.discarded, 1, __ATOMIC_RELAXED);
    return 0;
}
static void htStage(unsigned stage) {
    __atomic_store_n(&ht.stage, stage, __ATOMIC_RELEASE);
    printf("HOUJI TOUCH: stage=%u\n", stage);
}
static void htRestore(void) {
    if (!ht.changed) return;
    htSet(3, 1); htSet(2, 0);
    for (unsigned i = 0; i < 6; ++i) { htPinWrite(i, 4, ht.io[i]); htPinWrite(i, 0, ht.cfg[i]); }
    ht.changed = false;
}
static void htSummary(void) {
    printf("HOUJI TOUCH: stage=%u error=%u frames=%u events=%u irq-polls=%u device-write=%u host-write=%u config=%u last-header=%08x discarded=%u extent x[%d..%d] y[%d..%d]; phases 1=sleep 2=IRQ 3=wire 4=decode 5=publish 6=done 7=stopped\n",
           __atomic_load_n(&ht.stage, __ATOMIC_ACQUIRE), __atomic_load_n(&ht.error, __ATOMIC_ACQUIRE),
           __atomic_load_n(&ht.frames, __ATOMIC_RELAXED), __atomic_load_n(&ht.events, __ATOMIC_RELAXED),
           __atomic_load_n(&ht.irqPolls, __ATOMIC_RELAXED),
           __atomic_load_n(&ht.deviceMaxWrite, __ATOMIC_RELAXED), __atomic_load_n(&ht.effectiveMaxWrite, __ATOMIC_RELAXED),
           __atomic_load_n(&ht.configBytes, __ATOMIC_RELAXED), __atomic_load_n(&ht.lastHeader, __ATOMIC_RELAXED),
           __atomic_load_n(&ht.discarded, __ATOMIC_RELAXED),
           touchExtent[0], touchExtent[1], touchExtent[2], touchExtent[3]);
}
static int htPrepare(void) {
    if (!gTouchProbeEnabled || gTouchProbe.result != 0) return 20;
    if (((gTouchProbe.firmware >> 8) & 255) != 1 || (gTouchProbe.status & 0x1001) ||
        (gTouchProbe.dma_interface & 3) || (gTouchProbe.tx_fifo & 0xfffffff) ||
        (gTouchProbe.rx_fifo & 0x1ffffff)) return 21;
    ht.gpi = mapPhys(0xa00000, 0x60000, &ht.gpiMap); if (!ht.gpi) return 22;
    // Live DT explicitly assigns dynamic GPI instances 2/3 to the AP.
    for (unsigned n = 2; n <= 3; ++n) for (unsigned channel = 0; channel < 2; ++channel) {
        unsigned off = 0x10000 + 0x4000 * n + 0x80 * channel;
        uint32_t cfg = htRead(ht.gpi, off), scratch = htRead(ht.gpi, off + 0x60);
        unsigned state = (cfg >> 20) & 15;
        printf("HOUJI TOUCH: GPI%u ch%u cfg=%x scratch=%x\n", n, channel, cfg, scratch);
        if (cfg == 0xffffffffu || scratch == 0xffffffffu) return 23;
        if ((scratch & 15) == 4 && state != 0 && state != 1 && state != 3) return 24;
    }
    for (unsigned i = 0; i < 6; ++i) {
        ht.pin[i] = mapPhys(0xf100000 + ht_gpio[i] * 0x1000, 0x1000, &ht.maps[i]);
        if (!ht.pin[i]) return 25;
        ht.cfg[i] = htRead(ht.pin[i], 0); ht.io[i] = htRead(ht.pin[i], 4);
        if (ht.cfg[i] == 0xffffffffu || !gg_pin_owned(ht.cfg[i])) return 26;
    }
    htStage(2); ht.changed = true;
    htSet(1, 1); htSet(2, 0); htSet(3, 1); htSet(4, 1);
    for (unsigned i = 0; i < 6; ++i) {
        bool output = i != 0 && i != 5;
        uint32_t cfg = (ht.cfg[i] & ~0x3ffu) | (i < 4 ? 2u << 6 : 0) | (output ? 1u << 9 : 0);
        htPinWrite(i, 0, cfg);
        if (htRead(ht.pin[i], 0) != cfg) return 27;
    }
    htWait(50); htSet(4, 0); htWait(10); htSet(4, 1); htWait(200);
    return 0;
}
static int htRun(void) {
    htStage(1); int err = htPrepare(); if (err) return err;
    htStage(3);
    struct gw_bus bus = {htBusRead, htBusWrite, htWait, 64, htDiscardReport}; struct gw_message msg = {0};
    err = gw_read(&bus, &msg);
    for (unsigned retry = 0; err == 2 && retry < 100; retry++) { htWait(10); err = gw_read(&bus, &msg); }
    printf("HOUJI TOUCH: first read result=%d code=%x bytes=%u\n", err, msg.code, msg.size);
    if (err) return 100 + err;
    if (msg.code != 0x10) { err = gw_command(&bus, 2, nullptr, 0, &msg); if (err) return 120 + err; }
    if (msg.size < 24 || msg.data[1] != 1 || memcmp(msg.data + 2, "S3910", 5)) return 140;
    uint32_t build = msg.data[18] | uint32_t(msg.data[19]) << 8 | uint32_t(msg.data[20]) << 16 | uint32_t(msg.data[21]) << 24;
    printf("HOUJI TOUCH: TouchCom v1 S3910 application firmware=%u\n", build);
    if (build != 4323384) return 141;
    unsigned advertised = gw_le16(msg.data + 22);
    bus.max_write = advertised && advertised < GW_MAX + 3 ? advertised : GW_MAX + 3;
    __atomic_store_n(&ht.deviceMaxWrite, advertised, __ATOMIC_RELAXED);
    __atomic_store_n(&ht.effectiveMaxWrite, bus.max_write, __ATOMIC_RELAXED);
    printf("HOUJI TOUCH: advertised write=%u bounded host write=%u\n", advertised, bus.max_write);
    if (bus.max_write < sizeof(gt_report_config) + 3) return 142;
    err = gw_command(&bus, 0x20, nullptr, 0, &msg); if (err) return 160 + err;
    if (msg.size < 40 || gw_le16(msg.data + 2) || gw_le16(msg.data + 32) != 1199 ||
        gw_le16(msg.data + 34) != 2669 || gw_le16(msg.data + 36) != 10) return 180;
    unsigned size = gw_le16(msg.data + 12);
    __atomic_store_n(&ht.configBytes, size, __ATOMIC_RELAXED);
    printf("HOUJI TOUCH: app coordinates verified; report config bytes=%u\n", size);
    if (size < sizeof(gt_report_config) || size > GW_MAX || sizeof(gt_report_config) + 3 > bus.max_write) return 181;
    htStage(4);
    // Stop optional raw frame streams; an unsupported report ID is nonfatal.
    uint8_t raw = 0xc0; err = gw_command(&bus, 6, &raw, 1, &msg);
    printf("HOUJI TOUCH: disable THP result=%d\n", err);
    raw = 0x23; err = gw_command(&bus, 6, &raw, 1, &msg);
    printf("HOUJI TOUCH: disable raw result=%d\n", err);
    // Read the controller's native descriptor; do not force a custom layout.
    err = gw_command(&bus, 0x25, nullptr, 0, &msg); if (err) return 220 + err;
    if (!msg.size || msg.size > sizeof(ht.reportConfig)) return 239;
    memcpy(ht.reportConfig, msg.data, msg.size);
    __atomic_store_n(&ht.descriptorBytes, msg.size, __ATOMIC_RELEASE);
    struct hd_layout layout;
    if (hd_layout_parse(ht.reportConfig, msg.size, &layout)) return 240;
    char hex[97]; unsigned used = 0;
    for (unsigned at = 0; at < msg.size && at < 32 && used + 4 < sizeof(hex); ++at)
        used += snprintf(hex + used, sizeof(hex) - used, "%02x ", (unsigned)ht.reportConfig[at]);
    printf("HOUJI TOUCH: descriptor %u bytes head: %s\n", msg.size, hex);
    uint8_t touch = 0x11; err = gw_command(&bus, 5, &touch, 1, &msg); if (err) return 260 + err;
    htStage(5); printf("HOUJI TOUCH: native contact descriptor validated; physical polling enabled\n");
    unsigned errors = 0;
    for (;;) {
        __atomic_store_n(&ht.workerPhase, 1, __ATOMIC_RELEASE);
        htWait(10);
        __atomic_add_fetch(&ht.loops, 1, __ATOMIC_RELAXED);
        __atomic_store_n(&ht.workerPhase, 2, __ATOMIC_RELEASE);
        unsigned irqLevel = htRead(ht.pin[5], 4) & 1;
        __atomic_store_n(&ht.irqLevel, irqLevel, __ATOMIC_RELAXED);
        if (irqLevel) { if (!(ht.loops % 1500)) htSummary(); continue; }
        __atomic_add_fetch(&ht.irqPolls, 1, __ATOMIC_RELAXED);
        __atomic_store_n(&ht.workerPhase, 3, __ATOMIC_RELEASE);
        err = gw_read(&bus, &msg);
        if (!err && msg.code == 0x10) return 280;
        if (!err && msg.code == 0x11) {
            __atomic_store_n(&ht.workerPhase, 4, __ATOMIC_RELEASE);
            struct gt_frame frame;
            err = hd_decode(ht.reportConfig, ht.descriptorBytes, msg.data, msg.size, 1199, 2669, &ht.contacts, &frame);
            if (!err) {
                // The sheng publish contract carries the currently-down set (lonzzi's
                // per-event sweet_hid_touch maps onto it: lifts vanish from the arrays
                // and the consumer's vanished-identifier logic sends their tip-ups).
                __atomic_store_n(&ht.workerPhase, 5, __ATOMIC_RELEASE);
                struct gt_event down[GT_CONTACTS]; unsigned downCount = 0;
                for (unsigned id = 0; id < GT_CONTACTS; ++id)
                    if (ht.contacts.active & (1u << id)) {
                        down[downCount].id = (uint8_t)id;
                        down[downCount].down = 1;
                        down[downCount].x = ht.contacts.x[id];
                        down[downCount].y = ht.contacts.y[id];
                        ++downCount;
                    }
                publish(downCount, down);
                __atomic_add_fetch(&ht.hidEntered, 1, __ATOMIC_RELAXED);
                __atomic_add_fetch(&ht.hidCompleted, 1, __ATOMIC_RELAXED);
                __atomic_add_fetch(&ht.events, 1, __ATOMIC_RELAXED);
                __atomic_add_fetch(&ht.frames, 1, __ATOMIC_RELAXED);
                __atomic_store_n(&ht.workerPhase, 6, __ATOMIC_RELEASE);
            }
        }
        if (err) { htSummary(); if (++errors >= 3) return 300 + err; } else errors = 0;
        if (!(ht.loops % 1500)) htSummary();
    }
}
static void htThread(void *, wait_result_t) {
    htWait(1000);
    touchProbeOnce();
    int err = htRun();
    __atomic_store_n(&ht.workerPhase, 7, __ATOMIC_RELEASE);
    __atomic_store_n(&ht.error, unsigned(err), __ATOMIC_RELEASE);
    // The worker stopped (three consecutive bus errors): empty the set so the
    // consumer lifts every held finger at its last known position, restore the
    // pins, and leave the summary on the logfs record. The GUI continues.
    publish(0, nullptr);
    htRestore();
    htSummary();
    printf("HOUJI TOUCH: worker stopped result=%d; GUI continues\n", err);
    thread_terminate(current_thread());
}
extern "C" void native_touch_probe(void) {
    unsigned enabled = 0;
    if (!PE_parse_boot_argn("offline-touch", &enabled, sizeof enabled) || enabled != 1 || ht.started) return;
    ht.started = true;
    thread_t thread;
    kern_return_t kr = kernel_thread_start(htThread, nullptr, &thread);
    if (kr == KERN_SUCCESS) thread_deallocate(thread);
    else { __atomic_store_n(&ht.error, unsigned(kr), __ATOMIC_RELEASE); printf("HOUJI TOUCH: kernel_thread_start failed %d\n", kr); }
}
