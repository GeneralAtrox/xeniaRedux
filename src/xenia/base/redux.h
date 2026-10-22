/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_REDUX_H_
#define XENIA_BASE_REDUX_H_

#include <atomic>
#include <cstdint>
#include <functional>

// XeniaRedux recorder state shared between subsystems that do not otherwise
// depend on each other (GPU vblank source, HID replay, CPU call log, app).
namespace xe {
namespace redux {

// Guest vblanks marked since the title opened (GraphicsSystem::MarkVblank).
inline std::atomic<uint64_t> vblank_count{0};

// Called from the vblank thread after vblank_count is incremented. Set once
// before the title launches; never changed while it runs.
inline std::function<void(uint64_t vblank)> vblank_hook;

// Flushes recorder output; set by the call log so the app can flush it
// before a forced exit.
inline std::function<void()> flush_hook;

}  // namespace redux
}  // namespace xe

#endif  // XENIA_BASE_REDUX_H_
