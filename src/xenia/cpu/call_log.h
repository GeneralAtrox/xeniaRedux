/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_CALL_LOG_H_
#define XENIA_CPU_CALL_LOG_H_

#include <cstdint>

#include "xenia/base/cvar.h"

DECLARE_string(call_log_functions);
DECLARE_path(call_log_path);

namespace xe {
class Memory;
namespace cpu {

// XeniaRedux ordered call log: for a chosen list of guest functions, the JIT
// calls Entry() at the function's start and Exit() at its end, and each call
// appends one fixed-size record to a binary file (tools/redux/calllog_read.py
// reads it). Records are written in one global order (seq), so the log is the
// order the calls happened in across all guest threads.
//
// File: a 64-byte header, then 216-byte records, all little-endian.
//   Header: "XRCL" magic, u32 version (1), u32 record size (216),
//           u32 header size (64), the rest zero.
//   Record: see CallLogRecord.
namespace call_log {

enum RecordKind : uint32_t {
  kEntry = 1,         // registers as the function starts
  kExit = 2,          // registers as the function returns (r3/f1 = results)
  kFunctionInfo = 3,  // one per logged function, when it is compiled
  kTruncated = 4,     // the size cap was reached; nothing follows
  kHostInfo = 5,      // properties of the host that change JIT results
};

// kFunctionInfo flags: instructions whose Xenia result is known to differ
// from the Xenon CPU or to depend on the host, so DP's judge, not Xenia,
// decides a disagreement in the function.
enum FunctionFlags : uint64_t {
  kUsesEstimate = 1 << 0,  // fres, frsqrte, vrefp, vrsqrtefp, vexptefp,
                           // vlogefp: Xenia computes exact values, the
                           // hardware returns table estimates.
  kUsesFma = 1 << 1,       // fmadd family, vmaddfp, vnmsubfp: fused only
                           // when the host CPU has FMA (see kHostInfo).
  kUsesStfs = 1 << 2,      // stfs of an out-of-range double rounds in
                           // Xenia; the hardware selects bits.
  kUsesFctiw = 1 << 3,     // fctiw/fctiwz: only the low word is defined.
  kUsesTrap = 1 << 4,      // tw/td: ignored by default in Xenia.
};

#pragma pack(push, 1)
struct CallLogRecord {
  uint32_t kind;       // RecordKind
  uint32_t thread_id;  // guest thread id (PPCContext::thread_id)
  uint32_t function;   // guest address of the function
  uint32_t lr;         // low word of LR; kFunctionInfo: end address
  uint64_t seq;        // global order; kFunctionInfo/kHostInfo: 0
  uint64_t entry_seq;  // kExit: seq of the matching kEntry (0: none);
                       // kFunctionInfo: FunctionFlags; kHostInfo: bit 0 =
                       // host FMA used by the JIT
  uint64_t vblank;     // xe::redux::vblank_count; kFunctionInfo: count of
                       // instructions
  uint64_t r1;         // stack pointer
  uint64_t r[8];       // r3..r10; kFunctionInfo: per-flag instruction counts
  uint64_t f[13];      // f1..f13, raw IEEE double bits
};
#pragma pack(pop)
static_assert(sizeof(CallLogRecord) == 216, "record layout is fixed");

// Opens the log if --call_log_functions is set. Returns false on error.
bool Initialize();
bool IsEnabled();
// Whether the function at this guest address is logged.
bool IsLogged(uint32_t guest_address);
// Called by the JIT once per compiled logged function.
void DescribeFunction(Memory* memory, uint32_t start_address,
                      uint32_t end_address, bool host_fma);
void Flush();

// JIT helpers: raw_context is the PPCContext.
uint64_t Entry(void* raw_context, uint64_t guest_address);
uint64_t Exit(void* raw_context, uint64_t guest_address);

}  // namespace call_log
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_CALL_LOG_H_
