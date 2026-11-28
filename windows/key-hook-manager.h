// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_KEY_HOOK_MANAGER_H_
#define OKEYTRE_WINDOWS_KEY_HOOK_MANAGER_H_

#include <windows.h>

#include <cstdint>
#include <functional>

#include "windows/caret-text-reader.h"
#include "windows/ime-controller.h"
#include "windows/shortcut-detectors.h"

namespace okeytre::win {

// Installs system-wide Windows low-level keyboard & mouse hooks
// (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`) and foreground-window observer
// (`EVENT_SYSTEM_FOREGROUND`), forwarding typed characters to `ImeController`
// and injecting minimal Backspace + UTF-16 edits via `SendInput`.
class KeyHookManager {
 public:
  static constexpr ULONG_PTR kMagicExtraInfo = 0xCAFEBABE;

  using AsyncCallbackFn = std::function<void()>;

  KeyHookManager(ImeController& controller, CaretTextReader& caret_reader);
  ~KeyHookManager();

  KeyHookManager(const KeyHookManager&) = delete;
  KeyHookManager& operator=(const KeyHookManager&) = delete;

  // Invoked asynchronously (safe to post a message) when Left Ctrl + Right Ctrl
  // toggles Vietnamese input, or when the foreground window changes.
  void SetOnToggleRequested(AsyncCallbackFn cb) {
    on_toggle_requested_ = std::move(cb);
  }
  void SetOnForegroundChanged(AsyncCallbackFn cb) {
    on_foreground_changed_ = std::move(cb);
  }

  bool Start();
  void Stop();
  bool is_running() const noexcept { return keyboard_hook_ != nullptr; }

  void ResetTransientState() noexcept;

 private:
  static LRESULT CALLBACK KeyboardProc(int code, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK MouseProc(int code, WPARAM wparam, LPARAM lparam);
  static void CALLBACK ForegroundEventProc(HWINEVENTHOOK hook, DWORD event,
                                           HWND hwnd, LONG id_object,
                                           LONG id_child, DWORD event_thread,
                                           DWORD event_time);

  LRESULT HandleKeyboardEvent(WPARAM wparam, const KBDLLHOOKSTRUCT* info);
  LRESULT HandleMouseEvent(WPARAM wparam);
  void HandleForegroundChanged();

  void SendEditAction(const ImeEditAction& action);

  static KeyHookManager* active_instance_;

  ImeController& controller_;
  CaretTextReader& caret_reader_;
  HHOOK keyboard_hook_ = nullptr;
  HHOOK mouse_hook_ = nullptr;
  HWINEVENTHOOK foreground_hook_ = nullptr;

  AsyncCallbackFn on_toggle_requested_;
  AsyncCallbackFn on_foreground_changed_;
  DualShiftDetector dual_shift_;
  DualControlDetector dual_control_;
  bool in_keyboard_event_ = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_KEY_HOOK_MANAGER_H_
