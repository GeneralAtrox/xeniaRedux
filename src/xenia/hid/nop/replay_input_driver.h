/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_NOP_REPLAY_INPUT_DRIVER_H_
#define XENIA_HID_NOP_REPLAY_INPUT_DRIVER_H_

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "xenia/hid/input_driver.h"

namespace xe {
namespace hid {
namespace nop {

// Plays a scripted button list on controller 0, keyed by the guest vblank
// count (xe::redux::vblank_count). Same syntax as DecompPipeline's lockstep
// --input: comma-separated NAME@VBLANK[+LENGTH], LENGTH defaulting to 10
// vblanks, e.g. "START@900,A@1500+10,LUP@2000+300". "@path" reads the list
// from a file (commas or newlines, '#' starts a comment).
class ReplayInputDriver final : public InputDriver {
 public:
  struct Press {
    uint64_t at;
    uint64_t length;
    uint32_t keys;  // ReplayInputDriver::kKey* bits
  };

  explicit ReplayInputDriver(xe::ui::Window* window, size_t window_z_order,
                             std::vector<Press> presses);
  ~ReplayInputDriver() override;

  // Parses the list; on error returns false and sets *error.
  static bool Parse(std::string_view text, std::vector<Press>* out,
                    std::string* error);

  X_STATUS Setup() override;

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;
  InputType GetInputType() const override;

 private:
  // Bits 0-15 are X_INPUT_GAMEPAD buttons; the rest are analog inputs held at
  // full deflection.
  static constexpr uint32_t kKeyLT = 1u << 16;
  static constexpr uint32_t kKeyRT = 1u << 17;
  static constexpr uint32_t kKeyLUp = 1u << 18;
  static constexpr uint32_t kKeyLDown = 1u << 19;
  static constexpr uint32_t kKeyLRight = 1u << 20;
  static constexpr uint32_t kKeyLLeft = 1u << 21;
  static constexpr uint32_t kKeyRUp = 1u << 22;
  static constexpr uint32_t kKeyRDown = 1u << 23;
  static constexpr uint32_t kKeyRRight = 1u << 24;
  static constexpr uint32_t kKeyRLeft = 1u << 25;
  static constexpr uint32_t kKeyCount = 26;

  uint32_t KeysNow();

  std::vector<Press> presses_;
  std::mutex mutex_;
  uint32_t last_keys_ = 0;           // state reported by GetState
  uint32_t last_keystroke_keys_ = 0;  // state reported by GetKeystroke
  uint32_t packet_number_ = 1;
};

std::unique_ptr<InputDriver> CreateReplay(xe::ui::Window* window,
                                          size_t window_z_order,
                                          std::string_view text);

}  // namespace nop
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_NOP_REPLAY_INPUT_DRIVER_H_
