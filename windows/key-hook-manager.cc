// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/key-hook-manager.h"

#include <array>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "windows/input-source-manager.h"

namespace okeytre::win {

namespace {

// Navigation / editing code units matching `okeytre::UnicodeKey` in
// `OKeyTre/ime/core/input-method-buffer.h`.
constexpr uint16_t kKeyBackspaceCtrl = 0x0008;
constexpr uint16_t kKeyTab = 0x0009;
constexpr uint16_t kKeyCR = 0x000D;
constexpr uint16_t kKeyEscape = 0x001B;
constexpr uint16_t kKeyUp = 0xF700;
constexpr uint16_t kKeyDown = 0xF701;
constexpr uint16_t kKeyLeft = 0xF702;
constexpr uint16_t kKeyRight = 0xF703;
constexpr uint16_t kKeyForwardDelete = 0xF728;
constexpr uint16_t kKeyHome = 0xF729;
constexpr uint16_t kKeyEnd = 0xF72B;
constexpr uint16_t kKeyPageUp = 0xF72C;
constexpr uint16_t kKeyPageDown = 0xF72D;

bool IsKeyDownAsync(int vk) noexcept {
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool IsKeyToggled(int vk) noexcept { return (GetKeyState(vk) & 0x0001) != 0; }

bool IsModifierVk(DWORD vk) noexcept {
  switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
      return true;
    default:
      return false;
  }
}

ModifierSnapshot CurrentModifierSnapshot(DWORD event_vk,
                                         bool is_down) noexcept {
  auto pressed = [event_vk, is_down](int vk) {
    return static_cast<DWORD>(vk) == event_vk ? is_down : IsKeyDownAsync(vk);
  };
  ModifierSnapshot snap;
  snap.shift = pressed(VK_LSHIFT) || pressed(VK_RSHIFT);
  snap.control = pressed(VK_LCONTROL) || pressed(VK_RCONTROL);
  snap.alt = pressed(VK_LMENU) || pressed(VK_RMENU);
  snap.win = pressed(VK_LWIN) || pressed(VK_RWIN);
  return snap;
}

uint32_t ToOktModifiers(const ModifierSnapshot& mods) noexcept {
  uint32_t mask = OKT_MOD_NONE;
  if (mods.shift) mask |= OKT_MOD_SHIFT;
  if (mods.control) mask |= OKT_MOD_CONTROL;
  if (mods.alt) mask |= OKT_MOD_OPTION;
  if (mods.win) mask |= OKT_MOD_COMMAND;
  return mask;
}

uint16_t MapNavigationOrControlVk(DWORD vk) noexcept {
  switch (vk) {
    case VK_BACK:
      return kKeyBackspaceCtrl;
    case VK_TAB:
      return kKeyTab;
    case VK_RETURN:
      return kKeyCR;
    case VK_ESCAPE:
      return kKeyEscape;
    case VK_LEFT:
      return kKeyLeft;
    case VK_RIGHT:
      return kKeyRight;
    case VK_UP:
      return kKeyUp;
    case VK_DOWN:
      return kKeyDown;
    case VK_DELETE:
      return kKeyForwardDelete;
    case VK_HOME:
      return kKeyHome;
    case VK_END:
      return kKeyEnd;
    case VK_PRIOR:
      return kKeyPageUp;
    case VK_NEXT:
      return kKeyPageDown;
    default:
      return 0;
  }
}

int TranslateKeyToUtf16(DWORD vk, DWORD scan_code, const ModifierSnapshot& mods,
                        uint16_t* out_units, int out_capacity) {
  BYTE key_state[256] = {};
  if (mods.shift) {
    key_state[VK_SHIFT] = 0x80;
    if (IsKeyDownAsync(VK_LSHIFT)) key_state[VK_LSHIFT] = 0x80;
    if (IsKeyDownAsync(VK_RSHIFT)) key_state[VK_RSHIFT] = 0x80;
  }
  if (mods.control) key_state[VK_CONTROL] = 0x80;
  if (mods.alt) key_state[VK_MENU] = 0x80;
  if (IsKeyToggled(VK_CAPITAL)) key_state[VK_CAPITAL] = 0x01;
  if (IsKeyToggled(VK_NUMLOCK)) key_state[VK_NUMLOCK] = 0x01;

  HWND fg = GetForegroundWindow();
  DWORD fg_thread = fg != nullptr ? GetWindowThreadProcessId(fg, nullptr) : 0;
  HKL layout = GetKeyboardLayout(fg_thread);
  if (layout == nullptr) layout = GetKeyboardLayout(0);

  // Flag bit 2 (0x0004, Win10 1607+): do not modify keyboard dead-key state.
  constexpr UINT kDoNotChangeKeyboardState = 0x0004;
  std::array<wchar_t, 8> wbuf{};
  const int count = ToUnicodeEx(vk, scan_code, key_state, wbuf.data(),
                                static_cast<int>(wbuf.size()),
                                kDoNotChangeKeyboardState, layout);
  if (count <= 0) return 0;

  const int safe_count = count < out_capacity ? count : out_capacity;
  for (int i = 0; i < safe_count; ++i) {
    out_units[i] = static_cast<uint16_t>(wbuf[static_cast<size_t>(i)]);
  }
  return safe_count;
}

}  // namespace

KeyHookManager* KeyHookManager::active_instance_ = nullptr;

KeyHookManager::KeyHookManager(ImeController& controller,
                               CaretTextReader& caret_reader)
    : controller_(controller), caret_reader_(caret_reader) {}

KeyHookManager::~KeyHookManager() { Stop(); }

bool KeyHookManager::Start() {
  if (keyboard_hook_ != nullptr) return true;

  active_instance_ = this;
  HINSTANCE hinst = GetModuleHandleW(nullptr);
  keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL,
                                     &KeyHookManager::KeyboardProc, hinst, 0);
  if (keyboard_hook_ == nullptr) {
    active_instance_ = nullptr;
    return false;
  }

  mouse_hook_ =
      SetWindowsHookExW(WH_MOUSE_LL, &KeyHookManager::MouseProc, hinst, 0);
  foreground_hook_ =
      SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                      &KeyHookManager::ForegroundEventProc, 0, 0,
                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  return true;
}

void KeyHookManager::Stop() {
  if (keyboard_hook_ != nullptr) {
    UnhookWindowsHookEx(keyboard_hook_);
    keyboard_hook_ = nullptr;
  }
  if (mouse_hook_ != nullptr) {
    UnhookWindowsHookEx(mouse_hook_);
    mouse_hook_ = nullptr;
  }
  if (foreground_hook_ != nullptr) {
    UnhookWinEvent(foreground_hook_);
    foreground_hook_ = nullptr;
  }
  if (active_instance_ == this) {
    active_instance_ = nullptr;
  }
  ResetTransientState();
}

void KeyHookManager::ResetTransientState() noexcept {
  dual_shift_.Reset();
  dual_control_.Reset();
  controller_.ResetBuffer();
}

LRESULT CALLBACK KeyHookManager::KeyboardProc(int code, WPARAM wparam,
                                              LPARAM lparam) {
  if (code == HC_ACTION && active_instance_ != nullptr) {
    const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
    if (info != nullptr) {
      const LRESULT res = active_instance_->HandleKeyboardEvent(wparam, info);
      if (res != 0) return res;
    }
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK KeyHookManager::MouseProc(int code, WPARAM wparam,
                                           LPARAM lparam) {
  if (code == HC_ACTION && active_instance_ != nullptr) {
    active_instance_->HandleMouseEvent(wparam);
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}

void CALLBACK KeyHookManager::ForegroundEventProc(
    HWINEVENTHOOK /*hook*/, DWORD /*event*/, HWND /*hwnd*/, LONG /*id_object*/,
    LONG /*id_child*/, DWORD /*event_thread*/, DWORD /*event_time*/) {
  if (active_instance_ != nullptr) {
    active_instance_->HandleForegroundChanged();
  }
}

LRESULT KeyHookManager::HandleMouseEvent(WPARAM wparam) {
  switch (wparam) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
      dual_shift_.Reset();
      controller_.ResetBuffer();
      break;
    default:
      break;
  }
  return 0;
}

void KeyHookManager::HandleForegroundChanged() {
  dual_shift_.Reset();
  dual_control_.Reset();
  controller_.ResetBuffer();
  if (on_foreground_changed_) on_foreground_changed_();
}

LRESULT KeyHookManager::HandleKeyboardEvent(WPARAM wparam,
                                            const KBDLLHOOKSTRUCT* info) {
  // Never re-process our own injected SendInput events.
  if (info->dwExtraInfo == kMagicExtraInfo) return 0;

  // Guard against unexpected hook re-entrancy.
  if (in_keyboard_event_) return 0;

  in_keyboard_event_ = true;
  const auto reset_guard =
      absl::MakeCleanup([this]() { in_keyboard_event_ = false; });

  const bool is_key_down = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN);
  const bool is_key_up = (wparam == WM_KEYUP || wparam == WM_SYSKEYUP);
  if (!is_key_down && !is_key_up) {
    return 0;
  }

  const DWORD vk = info->vkCode;
  const ModifierSnapshot mods = CurrentModifierSnapshot(vk, is_key_down);

  // 1. Handle Left Ctrl + Right Ctrl quick Vietnamese On/Off toggle.
  if (vk == VK_LCONTROL || vk == VK_RCONTROL) {
    if (dual_control_.OnKeyEvent(vk, is_key_down, mods)) {
      if (on_toggle_requested_) {
        on_toggle_requested_();
      } else {
        controller_.ToggleVietnameseEnabled();
      }
    }
    return 0;
  }

  // 2. Handle Left Shift + Right Shift instant raw-word restoration.
  if (vk == VK_LSHIFT || vk == VK_RSHIFT) {
    if (is_key_down) dual_control_.OnKeyEvent(vk, true, mods);

    const bool english_layout =
        InputSourceManager::IsForegroundKeyboardEnglish();
    if (english_layout != controller_.is_system_keyboard_compatible()) {
      controller_.SetSystemKeyboardCompatible(english_layout);
      if (on_foreground_changed_) on_foreground_changed_();
    }
    if (controller_.is_effectively_active() &&
        dual_shift_.OnKeyEvent(vk, is_key_down, mods)) {
      if (const std::optional<ImeEditAction> action =
              controller_.RestoreCurrentWord();
          action.has_value()) {
        SendEditAction(*action);
      }
    } else if (!controller_.is_effectively_active()) {
      dual_shift_.Reset();
    }
    return 0;
  }

  if (IsModifierVk(vk)) {
    if (is_key_down) dual_control_.OnKeyEvent(vk, true, mods);
    return 0;
  }

  if (!is_key_down) return 0;

  // Any non-modifier key down disrupts an active Ctrl chord.
  dual_control_.NoteNonModifierKeyDown();

  const bool english_layout = InputSourceManager::IsForegroundKeyboardEnglish();
  if (english_layout != controller_.is_system_keyboard_compatible()) {
    controller_.SetSystemKeyboardCompatible(english_layout);
    if (on_foreground_changed_) on_foreground_changed_();
  }

  if (!controller_.is_effectively_active()) {
    dual_shift_.Reset();
    controller_.ResetBuffer();
    return 0;
  }

  if (!mods.shift) dual_shift_.Reset();

  // Ctrl / Alt / Win shortcuts reset the buffer and pass through untouched.
  if (mods.control || mods.alt || mods.win) {
    dual_shift_.Reset();
    controller_.ResetBuffer();
    return 0;
  }

  const uint32_t okt_mods = ToOktModifiers(mods);

  // Navigation / editing keys (Backspace, Delete, Arrows, Enter, Tab, Escape).
  if (const uint16_t nav_unit = MapNavigationOrControlVk(vk); nav_unit != 0) {
    controller_.HandleInput(&nav_unit, 1, okt_mods);
    return 0;
  }

  // Function keys (F1..F24) reset buffer and pass through.
  if (vk >= VK_F1 && vk <= VK_F24) {
    controller_.ResetBuffer();
    return 0;
  }

  std::array<uint16_t, 8> utf16{};
  const int count = TranslateKeyToUtf16(vk, info->scanCode, mods, utf16.data(),
                                        static_cast<int>(utf16.size()));
  if (count <= 0) {
    return 0;
  }

  const auto action = controller_.HandleInput(utf16.data(), count, okt_mods);
  if (action.has_value()) {
    SendEditAction(*action);
    return 1;  // Swallow original keystroke.
  }

  return 0;
}

void KeyHookManager::SendEditAction(const ImeEditAction& action) {
  const int32_t extra_bs =
      (action.backspace_count > 0 && caret_reader_.GetSelectedRangeLength() > 0)
          ? 1
          : 0;
  const int32_t total_backspaces = action.backspace_count + extra_bs;

  // If either Shift key is physically held (e.g. typing '?' via Shift+/ or
  // pressing Left Shift + Right Shift to restore), temporarily release it
  // around VK_BACK so target apps receive plain Backspace rather than
  // Shift+Backspace, then restore the held Shift state afterwards.
  const bool lshift_held = total_backspaces > 0 && IsKeyDownAsync(VK_LSHIFT);
  const bool rshift_held = total_backspaces > 0 && IsKeyDownAsync(VK_RSHIFT);

  std::vector<INPUT> inputs;
  inputs.reserve(static_cast<size_t>(total_backspaces) * 2 +
                 action.text.size() * 2 + 4);

  auto push_vk = [&inputs](WORD vk, bool key_up) {
    INPUT ev{};
    ev.type = INPUT_KEYBOARD;
    ev.ki.wVk = vk;
    ev.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    ev.ki.dwFlags = key_up ? KEYEVENTF_KEYUP : 0;
    ev.ki.dwExtraInfo = kMagicExtraInfo;
    inputs.push_back(ev);
  };

  if (lshift_held) push_vk(VK_LSHIFT, /*key_up=*/true);
  if (rshift_held) push_vk(VK_RSHIFT, /*key_up=*/true);

  for (int32_t i = 0; i < total_backspaces; ++i) {
    push_vk(VK_BACK, /*key_up=*/false);
    push_vk(VK_BACK, /*key_up=*/true);
  }

  for (char16_t ch : action.text) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wScan = static_cast<WORD>(ch);
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    down.ki.dwExtraInfo = kMagicExtraInfo;
    inputs.push_back(down);

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    inputs.push_back(up);
  }

  if (lshift_held) push_vk(VK_LSHIFT, /*key_up=*/false);
  if (rshift_held) push_vk(VK_RSHIFT, /*key_up=*/false);

  if (!inputs.empty()) {
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  }
}

}  // namespace okeytre::win
