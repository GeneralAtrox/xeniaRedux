/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/call_log.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/redux.h"
#include "xenia/base/utf8.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/memory.h"

DEFINE_string(call_log_functions, "",
              "XeniaRedux: guest function addresses (hex, comma separated, or "
              "@file with one per line) whose calls are written, in order, to "
              "--call_log_path.",
              "CPU");
DEFINE_path(call_log_path, "calllog.bin",
            "XeniaRedux: binary call log file (see call_log.h).", "CPU");
DEFINE_uint64(call_log_max_mb, 450,
              "XeniaRedux: call log size cap in MiB; logging stops there.",
              "CPU");
DEFINE_uint64(call_log_per_function_limit, 20000,
              "XeniaRedux: calls logged per function (0 = no limit), so one "
              "hot function cannot use up the size cap.",
              "CPU");

namespace xe {
namespace cpu {
namespace call_log {

namespace {

constexpr uint32_t kVersion = 1;
constexpr uint32_t kHeaderSize = 64;

struct Frame {
  uint32_t function;
  uint64_t entry_seq;  // 0: the entry was not written (per-function limit)
};

std::unordered_set<uint32_t> logged_functions_;
std::mutex mutex_;
std::FILE* file_ = nullptr;
uint64_t bytes_written_ = 0;
uint64_t byte_cap_ = 0;
uint64_t next_seq_ = 1;
bool truncated_ = false;
bool host_info_written_ = false;
std::unordered_map<uint32_t, uint64_t> calls_per_function_;
thread_local std::vector<Frame> frames_;

// Must hold mutex_.
bool WriteLocked(const CallLogRecord& record) {
  if (!file_ || truncated_) {
    return false;
  }
  if (bytes_written_ + 2 * sizeof(record) > byte_cap_) {
    CallLogRecord marker = {};
    marker.kind = kTruncated;
    marker.seq = next_seq_;
    std::fwrite(&marker, sizeof(marker), 1, file_);
    std::fflush(file_);
    truncated_ = true;
    XELOGW("Call log: size cap of {} MiB reached, logging stopped",
           cvars::call_log_max_mb);
    return false;
  }
  std::fwrite(&record, sizeof(record), 1, file_);
  bytes_written_ += sizeof(record);
  return true;
}

void FillRegisters(const ppc::PPCContext* context, CallLogRecord* record) {
  record->thread_id = context->thread_id;
  record->lr = uint32_t(context->lr);
  record->vblank = redux::vblank_count.load(std::memory_order_relaxed);
  record->r1 = context->r[1];
  for (int i = 0; i < 8; ++i) {
    record->r[i] = context->r[3 + i];
  }
  static_assert(sizeof(context->f[0]) == sizeof(record->f[0]));
  std::memcpy(record->f, &context->f[1], sizeof(record->f));
}

bool ParseFunctions(std::string list) {
  if (!list.empty() && list.front() == '@') {
    std::ifstream file(xe::to_path(list.substr(1)));
    if (!file) {
      XELOGE("Call log: cannot read {}", list.substr(1));
      return false;
    }
    std::string line;
    list.clear();
    while (std::getline(file, line)) {
      line = line.substr(0, line.find('#'));
      list += line;
      list += ',';
    }
  }
  for (std::string_view part : xe::utf8::split(list, ", \t\r\n", true)) {
    std::string token(part);
    char* end = nullptr;
    unsigned long long value = std::strtoull(token.c_str(), &end, 16);
    if (!end || *end || value > 0xFFFFFFFFull) {
      XELOGE("Call log: bad function address {}", token);
      return false;
    }
    logged_functions_.insert(uint32_t(value));
  }
  return !logged_functions_.empty();
}

void FlushLoop() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    Flush();
  }
}

}  // namespace

bool Initialize() {
  if (cvars::call_log_functions.empty()) {
    return true;
  }
  if (!ParseFunctions(cvars::call_log_functions)) {
    return false;
  }
  file_ = xe::filesystem::OpenFile(cvars::call_log_path, "wb");
  if (!file_) {
    XELOGE("Call log: cannot create {}", cvars::call_log_path);
    logged_functions_.clear();
    return false;
  }
  std::setvbuf(file_, nullptr, _IOFBF, 4 * 1024 * 1024);
  uint8_t header[kHeaderSize] = {};
  std::memcpy(header, "XRCL", 4);
  const uint32_t header_words[3] = {kVersion, uint32_t(sizeof(CallLogRecord)),
                                    kHeaderSize};
  std::memcpy(header + 4, header_words, sizeof(header_words));
  std::fwrite(header, sizeof(header), 1, file_);
  bytes_written_ = sizeof(header);
  byte_cap_ = cvars::call_log_max_mb * 1024 * 1024;
  redux::flush_hook = Flush;
  std::thread(FlushLoop).detach();
  XELOGI("Call log: {} functions to {}", logged_functions_.size(),
         cvars::call_log_path);
  return true;
}

bool IsEnabled() { return file_ != nullptr; }

bool IsLogged(uint32_t guest_address) {
  return file_ && logged_functions_.count(guest_address) != 0;
}

void DescribeFunction(Memory* memory, uint32_t start_address,
                      uint32_t end_address, bool host_fma) {
  using ppc::PPCOpcode;
  CallLogRecord record = {};
  record.kind = kFunctionInfo;
  record.function = start_address;
  record.lr = end_address;
  uint64_t flags = 0;
  for (uint32_t address = start_address; address <= end_address;
       address += 4) {
    const uint32_t code =
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    int bit = -1;
    switch (ppc::LookupOpcode(code)) {
      case PPCOpcode::fresx:
      case PPCOpcode::frsqrtex:
      case PPCOpcode::vrefp:
      case PPCOpcode::vrefp128:
      case PPCOpcode::vrsqrtefp:
      case PPCOpcode::vrsqrtefp128:
      case PPCOpcode::vexptefp:
      case PPCOpcode::vexptefp128:
      case PPCOpcode::vlogefp:
      case PPCOpcode::vlogefp128:
        bit = 0;
        break;
      case PPCOpcode::fmaddx:
      case PPCOpcode::fmaddsx:
      case PPCOpcode::fmsubx:
      case PPCOpcode::fmsubsx:
      case PPCOpcode::fnmaddx:
      case PPCOpcode::fnmaddsx:
      case PPCOpcode::fnmsubx:
      case PPCOpcode::fnmsubsx:
      case PPCOpcode::vmaddfp:
      case PPCOpcode::vmaddfp128:
      case PPCOpcode::vmaddcfp128:
      case PPCOpcode::vnmsubfp:
      case PPCOpcode::vnmsubfp128:
        bit = 1;
        break;
      case PPCOpcode::stfs:
      case PPCOpcode::stfsu:
      case PPCOpcode::stfsux:
      case PPCOpcode::stfsx:
        bit = 2;
        break;
      case PPCOpcode::fctiwx:
      case PPCOpcode::fctiwzx:
        bit = 3;
        break;
      case PPCOpcode::tw:
      case PPCOpcode::twi:
      case PPCOpcode::td:
      case PPCOpcode::tdi:
        bit = 4;
        break;
      default:
        break;
    }
    if (bit >= 0) {
      flags |= uint64_t(1) << bit;
      ++record.r[bit];
    }
  }
  record.entry_seq = flags;
  record.vblank = (end_address - start_address) / 4 + 1;

  std::lock_guard<std::mutex> lock(mutex_);
  if (!host_info_written_) {
    CallLogRecord host = {};
    host.kind = kHostInfo;
    host.entry_seq = host_fma ? 1 : 0;
    WriteLocked(host);
    host_info_written_ = true;
  }
  WriteLocked(record);
}

void Flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_) {
    std::fflush(file_);
  }
}

uint64_t Entry(void* raw_context, uint64_t guest_address) {
  auto context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  const uint32_t function = uint32_t(guest_address);
  CallLogRecord record = {};
  record.kind = kEntry;
  record.function = function;
  FillRegisters(context, &record);
  uint64_t seq = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t& calls = calls_per_function_[function];
    if (!cvars::call_log_per_function_limit ||
        calls < cvars::call_log_per_function_limit) {
      record.seq = next_seq_;
      if (WriteLocked(record)) {
        seq = next_seq_++;
        ++calls;
      }
    }
  }
  frames_.push_back({function, seq});
  return 0;
}

uint64_t Exit(void* raw_context, uint64_t guest_address) {
  auto context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  const uint32_t function = uint32_t(guest_address);
  // Match the innermost open call of this function; frames above it unwound
  // without an exit (longjmp and the like).
  uint64_t entry_seq = 0;
  bool matched = false;
  for (size_t i = frames_.size(); i-- > 0;) {
    if (frames_[i].function == function) {
      entry_seq = frames_[i].entry_seq;
      frames_.resize(i);
      matched = true;
      break;
    }
  }
  if (matched && !entry_seq) {
    return 0;  // The entry was skipped, so is the exit.
  }
  CallLogRecord record = {};
  record.kind = kExit;
  record.function = function;
  record.entry_seq = entry_seq;
  FillRegisters(context, &record);
  std::lock_guard<std::mutex> lock(mutex_);
  record.seq = next_seq_;
  if (WriteLocked(record)) {
    ++next_seq_;
  }
  return 0;
}

}  // namespace call_log
}  // namespace cpu
}  // namespace xe
