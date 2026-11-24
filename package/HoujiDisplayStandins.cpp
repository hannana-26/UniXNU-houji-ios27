// SPDX-License-Identifier: BSD-2-Clause-Patent
// houji: no Adreno driver is linked, so the GopFramebuffer component's weak GPU fence/trace
// hooks have no definitions and the static kernel link rejects them (the xnu kernel link has
// no weak-import precedent; asteroids T62/T64 hit the same and defined board stand-ins). These
// no-op stand-ins keep the component's CPU copy path (our only present path: no dpu-scanout,
// no fb-gpu) equal to the weak-null behavior on GPU boards: gpu trace stays disabled, the
// fences answer unknown, and gpuWrote() itself is unreachable without dpu-scanout=2
// (NativeLegacyDisplay startDirect exits before directOn is ever set).
#include <stdint.h>

extern "C" {

// NativeGpuTrace.h (NATIVE_GPU_TRACE_OPTIONAL): enabled()=false keeps every trace event out,
// exactly like an unlinked trace ring; record() is then never called (the event macro checks
// the pointer first) and is a no-op regardless.
bool native_gpu_trace_enabled(void) { return false; }
void native_gpu_trace_record(uint32_t, uint32_t, const uint64_t *, uint32_t) {}

// NativeLegacyDisplay.cpp gpuWrote(): unreachable in this configuration (no dpu-scanout);
// "unknown" answers keep the fence state machine's documented CPU-only fallback available if
// a later round ever enables direct scanout.
bool native_adreno_fences(uint32_t *, uint32_t *) { return false; }
bool native_adreno_fences_detail(uint32_t *, uint32_t *, uint32_t *) { return false; }

}
