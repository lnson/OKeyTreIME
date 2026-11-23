// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_APP_H_
#define OKEYTRE_WINDOWS_APP_H_

#include <windows.h>

#include <memory>

#include "windows/caret-text-reader.h"
#include "windows/ime-controller.h"
#include "windows/input-source-manager.h"
#include "windows/key-hook-manager.h"
#include "windows/launch-at-login.h"
#include "windows/settings-store.h"
#include "windows/settings-window.h"
#include "windows/showcase-window.h"
#include "windows/tray-icon.h"
#include "windows/update-manager.h"

namespace okeytre::win {

// Top-level Windows application controller owning the hidden message-dispatch
// window, single-instance handoff, system tray icon, global hooks, update
// manager, Settings window, and Feature Showcase window.
class App {
 public:
  App();
  ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  int Run();

 private:
  static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam);
  LRESULT HandleHiddenMessage(UINT msg, WPARAM wparam, LPARAM lparam);

  bool CreateHiddenWindow();
  void TerminateOlderInstances();
  void WireSubsystems();

  TrayState BuildTrayState() const;
  SettingsViewModel BuildSettingsViewModel() const;
  ShowcaseViewModel BuildShowcaseViewModel(ShowcaseMode mode) const;

  void RefreshAllUI();
  void ShowSettings();
  void ShowShowcase(ShowcaseMode mode);

  HWND hidden_hwnd_ = nullptr;
  HANDLE instance_mutex_ = nullptr;
  UINT msg_taskbar_created_ = 0;
  UINT msg_terminate_existing_ = 0;

  ShowcaseMode active_showcase_mode_ = ShowcaseMode::kFirstInstall;

  std::unique_ptr<SettingsStore> settings_;
  std::unique_ptr<CaretTextReader> caret_reader_;
  std::unique_ptr<ImeController> ime_controller_;
  std::unique_ptr<InputSourceManager> input_source_manager_;
  std::unique_ptr<LaunchAtLoginManager> launch_at_login_;
  std::unique_ptr<KeyHookManager> key_hook_;
  std::unique_ptr<UpdateManager> update_manager_;
  std::unique_ptr<TrayIcon> tray_icon_;
  std::unique_ptr<SettingsWindow> settings_window_;
  std::unique_ptr<ShowcaseWindow> showcase_window_;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_APP_H_
