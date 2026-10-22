/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/nop/replay_input_driver.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/redux.h"
#include "xenia/base/utf8.h"
#include "xenia/ui/virtual_key.h"

namespace xe {
namespace hid {
namespace nop {

namespace {

// Vblanks a press without +LENGTH lasts (DP's lockstep PRESS).
constexpr uint64_t kDefaultPressLength = 10;

struct KeyName {
  const char* name;
  uint32_t bits;
};

constexpr KeyName kKeyNames[] = {
    {"UP", X_INPUT_GAMEPAD_DPAD_UP},
    {"DOWN", X_INPUT_GAMEPAD_DPAD_DOWN},
    {"LEFT", X_INPUT_GAMEPAD_DPAD_LEFT},
    {"RIGHT", X_INPUT_GAMEPAD_DPAD_RIGHT},
    {"START", X_INPUT_GAMEPAD_START},
    {"BACK", X_INPUT_GAMEPAD_BACK},
    {"LS", X_INPUT_GAMEPAD_LEFT_THUMB},
    {"RS", X_INPUT_GAMEPAD_RIGHT_THUMB},
    {"LB", X_INPUT_GAMEPAD_LEFT_SHOULDER},
    {"RB", X_INPUT_GAMEPAD_RIGHT_SHOULDER},
    {"GUIDE", X_INPUT_GAMEPAD_GUIDE},
    {"A", X_INPUT_GAMEPAD_A},
    {"B", X_INPUT_GAMEPAD_B},
    {"X", X_INPUT_GAMEPAD_X},
    {"Y", X_INPUT_GAMEPAD_Y},
    {"LT", 1u << 16},
    {"RT", 1u << 17},
    {"LUP", 1u << 18},
    {"LDOWN", 1u << 19},
    {"LRIGHT", 1u << 20},
    {"LLEFT", 1u << 21},
    {"RUP", 1u << 22},
    {"RDOWN", 1u << 23},
    {"RRIGHT", 1u << 24},
    {"RLEFT", 1u << 25},
};

// Keystroke virtual key per key bit, in the order events are sent when
// several keys change at once (as the SDL driver does).
constexpr std::array<ui::VirtualKey, 26> kKeyVk = {
    ui::VirtualKey::kXInputPadDpadUp,      ui::VirtualKey::kXInputPadDpadDown,
    ui::VirtualKey::kXInputPadDpadLeft,    ui::VirtualKey::kXInputPadDpadRight,
    ui::VirtualKey::kXInputPadStart,       ui::VirtualKey::kXInputPadBack,
    ui::VirtualKey::kXInputPadLThumbPress, ui::VirtualKey::kXInputPadRThumbPress,
    ui::VirtualKey::kXInputPadLShoulder,   ui::VirtualKey::kXInputPadRShoulder,
    ui::VirtualKey::kXInputPadGuide,       ui::VirtualKey::kNone,
    ui::VirtualKey::kXInputPadA,           ui::VirtualKey::kXInputPadB,
    ui::VirtualKey::kXInputPadX,           ui::VirtualKey::kXInputPadY,
    ui::VirtualKey::kXInputPadLTrigger,    ui::VirtualKey::kXInputPadRTrigger,
    ui::VirtualKey::kXInputPadLThumbUp,    ui::VirtualKey::kXInputPadLThumbDown,
    ui::VirtualKey::kXInputPadLThumbRight, ui::VirtualKey::kXInputPadLThumbLeft,
    ui::VirtualKey::kXInputPadRThumbUp,    ui::VirtualKey::kXInputPadRThumbDown,
    ui::VirtualKey::kXInputPadRThumbRight, ui::VirtualKey::kXInputPadRThumbLeft,
};

bool ParseNumber(std::string_view text, uint64_t* out) {
  if (text.empty()) {
    return false;
  }
  auto result = std::from_chars(text.data(), text.data() + text.size(), *out);
  return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() && std::isspace(uint8_t(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(uint8_t(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

}  // namespace

ReplayInputDriver::ReplayInputDriver(xe::ui::Window* window,
                                     size_t window_z_order,
                                     std::vector<Press> presses)
    : InputDriver(window, window_z_order), presses_(std::move(presses)) {}

ReplayInputDriver::~ReplayInputDriver() = default;

bool ReplayInputDriver::Parse(std::string_view text, std::vector<Press>* out,
                              std::string* error) {
  std::string list(text);
  if (!list.empty() && list.front() == '@') {
    std::ifstream file(xe::to_path(list.substr(1)));
    if (!file) {
      *error = "cannot read " + list.substr(1);
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
  for (std::string_view part : xe::utf8::split(list, ",\r\n")) {
    part = Trim(part);
    if (part.empty()) {
      continue;
    }
    size_t at_pos = part.find('@');
    if (at_pos == std::string_view::npos) {
      *error = "missing @ in " + std::string(part);
      return false;
    }
    std::string name = xe::utf8::upper_ascii(part.substr(0, at_pos));
    std::string_view rest = part.substr(at_pos + 1);
    size_t plus_pos = rest.find('+');
    Press press = {0, kDefaultPressLength, 0};
    if (!ParseNumber(rest.substr(0, plus_pos), &press.at) ||
        (plus_pos != std::string_view::npos &&
         !ParseNumber(rest.substr(plus_pos + 1), &press.length))) {
      *error = "bad vblank numbers in " + std::string(part);
      return false;
    }
    for (const KeyName& key : kKeyNames) {
      if (name == key.name) {
        press.keys = key.bits;
      }
    }
    if (!press.keys) {
      *error = "unknown button " + name;
      return false;
    }
    out->push_back(press);
  }
  return true;
}

X_STATUS ReplayInputDriver::Setup() {
  for (const Press& press : presses_) {
    XELOGI("Input replay: keys {:07X} at vblank {} for {}", press.keys,
           press.at, press.length);
  }
  return X_STATUS_SUCCESS;
}

uint32_t ReplayInputDriver::KeysNow() {
  const uint64_t vblank = redux::vblank_count.load(std::memory_order_relaxed);
  uint32_t keys = 0;
  for (const Press& press : presses_) {
    if (vblank >= press.at && vblank < press.at + press.length) {
      keys |= press.keys;
    }
  }
  return keys;
}

X_RESULT ReplayInputDriver::GetCapabilities(uint32_t user_index,
                                            uint32_t flags,
                                            X_INPUT_CAPABILITIES* out_caps) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (!out_caps) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  std::memset(out_caps, 0, sizeof(*out_caps));
  out_caps->type = XINPUT_DEVTYPE_GAMEPAD;
  out_caps->sub_type = XINPUT_DEVSUBTYPE_GAMEPAD;
  out_caps->gamepad.buttons = 0xF7FF;
  out_caps->gamepad.left_trigger = 0xFF;
  out_caps->gamepad.right_trigger = 0xFF;
  out_caps->gamepad.thumb_lx = int16_t(0x7FFF);
  out_caps->gamepad.thumb_ly = int16_t(0x7FFF);
  out_caps->gamepad.thumb_rx = int16_t(0x7FFF);
  out_caps->gamepad.thumb_ry = int16_t(0x7FFF);
  return X_ERROR_SUCCESS;
}

X_RESULT ReplayInputDriver::GetState(uint32_t user_index,
                                     X_INPUT_STATE* out_state) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  const uint32_t keys = KeysNow();
  uint32_t packet_number;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (keys != last_keys_) {
      XELOGI("Input replay: vblank {} keys {:07X}",
             redux::vblank_count.load(std::memory_order_relaxed), keys);
      last_keys_ = keys;
      ++packet_number_;
    }
    packet_number = packet_number_;
  }
  if (!out_state) {
    return X_ERROR_SUCCESS;
  }
  auto axis = [keys](uint32_t plus, uint32_t minus) -> int16_t {
    return (keys & plus) ? int16_t(32767) : (keys & minus) ? int16_t(-32768) : 0;
  };
  out_state->packet_number = packet_number;
  out_state->gamepad.buttons = uint16_t(keys & 0xFFFF);
  out_state->gamepad.left_trigger = (keys & kKeyLT) ? 0xFF : 0;
  out_state->gamepad.right_trigger = (keys & kKeyRT) ? 0xFF : 0;
  out_state->gamepad.thumb_lx = axis(kKeyLRight, kKeyLLeft);
  out_state->gamepad.thumb_ly = axis(kKeyLUp, kKeyLDown);
  out_state->gamepad.thumb_rx = axis(kKeyRRight, kKeyRLeft);
  out_state->gamepad.thumb_ry = axis(kKeyRUp, kKeyRDown);
  return X_ERROR_SUCCESS;
}

X_RESULT ReplayInputDriver::SetState(uint32_t user_index,
                                     X_INPUT_VIBRATION* vibration) {
  return user_index == 0 ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT ReplayInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  if (user_index != 0 && user_index != XUserIndexAny) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (!out_keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  const uint32_t keys = KeysNow();
  std::lock_guard<std::mutex> lock(mutex_);
  const uint32_t changed = keys ^ last_keystroke_keys_;
  for (uint32_t i = 0; i < kKeyCount; ++i) {
    const uint32_t bit = 1u << i;
    if (!(changed & bit) || kKeyVk[i] == ui::VirtualKey::kNone) {
      continue;
    }
    last_keystroke_keys_ ^= bit;
    out_keystroke->virtual_key = uint16_t(kKeyVk[i]);
    out_keystroke->unicode = 0;
    out_keystroke->flags =
        (keys & bit) ? X_INPUT_KEYSTROKE_KEYDOWN : X_INPUT_KEYSTROKE_KEYUP;
    out_keystroke->user_index = 0;
    out_keystroke->hid_code = 0;
    return X_ERROR_SUCCESS;
  }
  return X_ERROR_EMPTY;
}

InputType ReplayInputDriver::GetInputType() const {
  return InputType::Controller;
}

std::unique_ptr<InputDriver> CreateReplay(xe::ui::Window* window,
                                          size_t window_z_order,
                                          std::string_view text) {
  std::vector<ReplayInputDriver::Press> presses;
  std::string error;
  if (!ReplayInputDriver::Parse(text, &presses, &error)) {
    XELOGE("Input replay: {}", error);
    return nullptr;
  }
  return std::make_unique<ReplayInputDriver>(window, window_z_order,
                                             std::move(presses));
}

}  // namespace nop
}  // namespace hid
}  // namespace xe
