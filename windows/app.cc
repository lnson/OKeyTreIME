// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/app.h"

#include <commctrl.h>
#include <objidl.h>

#include <gdiplus.h>

#include "okeytre-engine.h"
#include "windows/app-version.h"
#include "windows/localization.h"
#include "windows/resources/resource.h"

namespace okeytre::win {

namespace {

constexpr wchar_t kHiddenClassName[] = L"OKeyTreHiddenControllerWindow";
constexpr wchar_t kSingleInstanceMutexName[] =
    L"Local\\com.okeytre.OKeyTre.SingleInstance";
constexpr wchar_t kTerminateMsgName[] =
    L"com.okeytre.OKeyTre.TerminateExistingInstance";

constexpr UINT kMsgTrayCallback = WM_APP + 1;
constexpr UINT kMsgUpdateFinished = WM_APP + 2;
constexpr UINT kMsgToggleVietnamese = WM_APP + 3;
constexpr UINT kMsgForegroundChanged = WM_APP + 4;

constexpr UINT_PTR kTimerPollLayout = 1;
constexpr UINT_PTR kTimerInitialUpdateCheck = 2;
constexpr UINT_PTR kTimerPeriodicUpdateCheck = 3;
constexpr UINT_PTR kTimerClearUpToDate = 4;

constexpr UINT kLayoutPollIntervalMs = 750;
constexpr UINT kInitialUpdateDelayMs = 3000;
constexpr UINT kPeriodicUpdateIntervalMs = 60 * 60 * 1000;  // 1 hour
constexpr UINT kUpToDateBannerDurationMs = 4000;

}  // namespace

App::~App() {
  if (key_hook_) key_hook_->Stop();
  if (settings_window_) settings_window_->Close();
  if (showcase_window_) showcase_window_->Close();
  if (tray_icon_) tray_icon_->Destroy();
  if (hidden_hwnd_ != nullptr) {
    DestroyWindow(hidden_hwnd_);
    hidden_hwnd_ = nullptr;
  }
  if (instance_mutex_ != nullptr) {
    CloseHandle(instance_mutex_);
    instance_mutex_ = nullptr;
  }
}

void App::TerminateOlderInstances() {
  msg_terminate_existing_ = RegisterWindowMessageW(kTerminateMsgName);
  const DWORD current_pid = GetCurrentProcessId();

  // Notify any existing OKeyTre hidden controller window to exit cleanly
  // (matching macOS `OKeyTreMain.terminateExistingInstances()`).
  HWND existing = FindWindowW(kHiddenClassName, nullptr);
  while (existing != nullptr) {
    DWORD pid = 0;
    GetWindowThreadProcessId(existing, &pid);
    if (pid != 0 && pid != current_pid) {
      PostMessageW(existing, msg_terminate_existing_,
                   static_cast<WPARAM>(current_pid), 0);
    }
    existing = FindWindowExW(nullptr, existing, kHiddenClassName, nullptr);
  }

  // Brief wait for previous instance to release hooks and tray icon.
  for (int i = 0; i < 20; ++i) {
    instance_mutex_ = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
    if (instance_mutex_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS) {
      break;
    }
    if (instance_mutex_ != nullptr) {
      CloseHandle(instance_mutex_);
      instance_mutex_ = nullptr;
    }
    Sleep(50);
  }
}

bool App::CreateHiddenWindow() {
  HINSTANCE hinst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &App::HiddenWndProc;
  wc.hInstance = hinst;
  wc.lpszClassName = kHiddenClassName;
  RegisterClassExW(&wc);

  msg_taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");

  // Use a hidden top-level window (not HWND_MESSAGE) so we receive
  // WM_SETTINGCHANGE, WM_POWERBROADCAST, TaskbarCreated, and FindWindowW.
  hidden_hwnd_ = CreateWindowExW(0, kHiddenClassName, L"ÔKêyTre", WS_OVERLAPPED,
                                 0, 0, 0, 0, nullptr, nullptr, hinst, this);
  return hidden_hwnd_ != nullptr;
}

void App::WireSubsystems() {
  settings_ = std::make_unique<SettingsStore>(CreateRegistrySettingsBackend());
  caret_reader_ = std::make_unique<CaretTextReader>();

  ime_controller_ = std::make_unique<ImeController>(
      *settings_, [this]() { return caret_reader_->GetSurroundingText(); });
  ime_controller_->SetOnStateChanged([this]() { RefreshAllUI(); });

  input_source_manager_ = std::make_unique<InputSourceManager>();
  ime_controller_->SetSystemKeyboardCompatible(
      input_source_manager_->is_english());
  input_source_manager_->SetOnChanged([this]() {
    if (ime_controller_ != nullptr && input_source_manager_ != nullptr) {
      ime_controller_->SetSystemKeyboardCompatible(
          input_source_manager_->is_english());
    }
    if (key_hook_ != nullptr && input_source_manager_ != nullptr &&
        !input_source_manager_->is_english()) {
      key_hook_->ResetTransientState();
    }
    RefreshAllUI();
  });

  launch_at_login_ = std::make_unique<LaunchAtLoginManager>(*settings_);

  update_manager_ = std::make_unique<UpdateManager>(*settings_, hidden_hwnd_,
                                                    kMsgUpdateFinished);
  update_manager_->SetOnStateChanged([this]() {
    if (update_manager_->did_confirm_up_to_date()) {
      SetTimer(hidden_hwnd_, kTimerClearUpToDate, kUpToDateBannerDurationMs,
               nullptr);
    }
    RefreshAllUI();
  });

  key_hook_ =
      std::make_unique<KeyHookManager>(*ime_controller_, *caret_reader_);
  key_hook_->SetOnToggleRequested([this]() {
    if (hidden_hwnd_ != nullptr) {
      PostMessageW(hidden_hwnd_, kMsgToggleVietnamese, 0, 0);
    }
  });
  key_hook_->SetOnForegroundChanged([this]() {
    if (hidden_hwnd_ != nullptr) {
      PostMessageW(hidden_hwnd_, kMsgForegroundChanged, 0, 0);
    }
  });

  TrayCallbacks tray_cbs;
  tray_cbs.on_select_style = [this](InputStyle style) {
    ime_controller_->SetInputStyle(style);
  };
  tray_cbs.on_toggle_restore_invalid = [this]() {
    ime_controller_->SetRestoreInvalidWords(
        !ime_controller_->restore_invalid_words());
  };
  tray_cbs.on_toggle_launch_at_login = [this]() {
    launch_at_login_->SetEnabled(!launch_at_login_->IsEnabled());
    RefreshAllUI();
  };
  tray_cbs.on_select_language = [this](AppLanguage lang) {
    settings_->SetAppLanguage(lang);
    input_source_manager_->Refresh(lang);
    RefreshAllUI();
  };
  tray_cbs.on_switch_to_us = [this]() {
    input_source_manager_->SwitchToEnglishKeyboard(settings_->GetAppLanguage());
  };
  tray_cbs.on_download_update = [this]() {
    update_manager_->OpenReleasePage();
  };
  tray_cbs.on_skip_update = [this]() {
    if (const auto rel = update_manager_->visible_available_release();
        rel.has_value()) {
      update_manager_->SkipRelease(*rel);
    }
  };
  tray_cbs.on_open_showcase = [this]() {
    ShowShowcase(ShowcaseMode::kFirstInstall);
  };
  tray_cbs.on_open_settings = [this]() { ShowSettings(); };
  tray_cbs.on_quit = [this]() { PostQuitMessage(0); };

  tray_icon_ = std::make_unique<TrayIcon>(hidden_hwnd_, kMsgTrayCallback,
                                          std::move(tray_cbs));

  SettingsCallbacks settings_cbs;
  settings_cbs.on_select_language = [this](AppLanguage lang) {
    settings_->SetAppLanguage(lang);
    input_source_manager_->Refresh(lang);
    RefreshAllUI();
  };
  settings_cbs.on_select_style = [this](InputStyle style) {
    ime_controller_->SetInputStyle(style);
  };
  settings_cbs.on_set_launch_at_login = [this](bool enabled) {
    launch_at_login_->SetEnabled(enabled);
    RefreshAllUI();
  };
  settings_cbs.on_set_auto_check_updates = [this](bool enabled) {
    update_manager_->SetAutoCheckEnabled(enabled);
  };
  settings_cbs.on_check_updates_now = [this]() {
    update_manager_->CheckForUpdatesNow();
  };
  settings_cbs.on_download_update = [this]() {
    update_manager_->OpenReleasePage();
  };
  settings_cbs.on_open_releases_page = [this]() {
    update_manager_->OpenReleasesWebPage();
  };
  settings_cbs.on_set_modern_tone = [this](bool enabled) {
    ime_controller_->SetModernTone(enabled);
  };
  settings_cbs.on_set_restore_invalid = [this](bool enabled) {
    ime_controller_->SetRestoreInvalidWords(enabled);
  };
  settings_cbs.on_set_uo_horn = [this](bool enabled) {
    ime_controller_->SetUoHorn(enabled);
  };
  settings_cbs.on_set_ng_to_ngh = [this](bool enabled) {
    ime_controller_->SetNgToNgh(enabled);
  };
  settings_cbs.on_set_ngh_to_ng = [this](bool enabled) {
    ime_controller_->SetNghToNg(enabled);
  };
  settings_cbs.on_set_max_buffer_size = [this](int32_t size) {
    ime_controller_->SetMaxBufferSize(size);
  };
  settings_window_ = std::make_unique<SettingsWindow>(std::move(settings_cbs));

  ShowcaseCallbacks showcase_cbs;
  showcase_cbs.on_select_language = [this](AppLanguage lang) {
    settings_->SetAppLanguage(lang);
    input_source_manager_->Refresh(lang);
    RefreshAllUI();
  };
  showcase_cbs.on_select_style = [this](InputStyle style) {
    ime_controller_->SetInputStyle(style);
  };
  showcase_cbs.on_switch_to_us = [this]() {
    input_source_manager_->SwitchToEnglishKeyboard(settings_->GetAppLanguage());
  };
  showcase_window_ = std::make_unique<ShowcaseWindow>(std::move(showcase_cbs));
}

TrayState App::BuildTrayState() const {
  return TrayState{
      .keyboard_name = input_source_manager_->current_source_name(),
      .visible_update = update_manager_->visible_available_release(),
      .style = ime_controller_->input_style(),
      .language = settings_->GetAppLanguage(),
      .restore_invalid_words = ime_controller_->restore_invalid_words(),
      .launch_at_login = launch_at_login_->IsEnabled(),
      .is_english_keyboard = input_source_manager_->is_english(),
  };
}

SettingsViewModel App::BuildSettingsViewModel() const {
  return SettingsViewModel{
      .available_release = update_manager_->available_release(),
      .current_version =
          Utf8ToWide(update_manager_->current_version().description()),
      .max_buffer_size = ime_controller_->max_buffer_size(),
      .min_buffer_size = OKT_BUFFER_MIN_SIZE,
      .max_allowed_buffer_size = OKT_BUFFER_MAX_SIZE,
      .language = settings_->GetAppLanguage(),
      .style = ime_controller_->input_style(),
      .launch_at_login = launch_at_login_->IsEnabled(),
      .auto_check_updates = update_manager_->is_auto_check_enabled(),
      .is_checking_updates = update_manager_->is_checking(),
      .did_confirm_up_to_date = update_manager_->did_confirm_up_to_date(),
      .modern_tone = ime_controller_->modern_tone(),
      .restore_invalid_words = ime_controller_->restore_invalid_words(),
      .uo_horn = ime_controller_->uo_horn(),
      .ng_to_ngh = ime_controller_->ng_to_ngh(),
      .ngh_to_ng = ime_controller_->ngh_to_ng(),
  };
}

ShowcaseViewModel App::BuildShowcaseViewModel(ShowcaseMode mode) const {
  return ShowcaseViewModel{
      .keyboard_name = input_source_manager_->current_source_name(),
      .milestone_version = Utf8ToWide(kCurrentFeatureMilestoneVersionString),
      .mode = mode,
      .language = settings_->GetAppLanguage(),
      .style = ime_controller_->input_style(),
      .is_english_keyboard = input_source_manager_->is_english(),
  };
}

void App::RefreshAllUI() {
  if (tray_icon_) {
    tray_icon_->Update(BuildTrayState());
  }
  if (settings_window_ && settings_window_->IsVisible()) {
    settings_window_->Update(BuildSettingsViewModel());
  }
  if (showcase_window_ && showcase_window_->IsVisible()) {
    showcase_window_->Update(BuildShowcaseViewModel(active_showcase_mode_));
  }
}

void App::ShowSettings() {
  if (settings_window_) {
    settings_window_->Show(BuildSettingsViewModel());
  }
}

void App::ShowShowcase(ShowcaseMode mode) {
  active_showcase_mode_ = mode;
  settings_->MarkShowcaseSeen();
  if (showcase_window_) {
    showcase_window_->Show(BuildShowcaseViewModel(mode));
  }
}

int App::Run() {
  // Initialize Common Controls v6, COM (STA for UI Automation), and GDI+.
  INITCOMMONCONTROLSEX icc{sizeof(icc),
                           ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS};
  InitCommonControlsEx(&icc);

  const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  Gdiplus::GdiplusStartupInput gdip_input;
  ULONG_PTR gdip_token = 0;
  Gdiplus::GdiplusStartup(&gdip_token, &gdip_input, nullptr);

  TerminateOlderInstances();
  if (!CreateHiddenWindow()) {
    if (gdip_token != 0) Gdiplus::GdiplusShutdown(gdip_token);
    if (SUCCEEDED(com_hr)) CoUninitialize();
    return 1;
  }

  WireSubsystems();

  // Resolve showcase mode BEFORE EnableByDefaultIfNeeded sets registry state.
  const ShowcaseMode launch_showcase_mode =
      settings_->ResolveLaunchShowcaseMode();
  launch_at_login_->EnableByDefaultIfNeeded();

  input_source_manager_->Refresh(settings_->GetAppLanguage());
  tray_icon_->Create(BuildTrayState());
  key_hook_->Start();

  SetTimer(hidden_hwnd_, kTimerPollLayout, kLayoutPollIntervalMs, nullptr);
  SetTimer(hidden_hwnd_, kTimerInitialUpdateCheck, kInitialUpdateDelayMs,
           nullptr);
  SetTimer(hidden_hwnd_, kTimerPeriodicUpdateCheck, kPeriodicUpdateIntervalMs,
           nullptr);

  if (launch_showcase_mode != ShowcaseMode::kNone) {
    ShowShowcase(launch_showcase_mode);
  } else if (ime_controller_->is_vietnamese_enabled()) {
    input_source_manager_->CheckAndWarnOnStartup(settings_->GetAppLanguage());
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  KillTimer(hidden_hwnd_, kTimerPollLayout);
  KillTimer(hidden_hwnd_, kTimerInitialUpdateCheck);
  KillTimer(hidden_hwnd_, kTimerPeriodicUpdateCheck);
  KillTimer(hidden_hwnd_, kTimerClearUpToDate);

  key_hook_->Stop();
  settings_window_->Close();
  showcase_window_->Close();
  tray_icon_->Destroy();

  if (gdip_token != 0) {
    Gdiplus::GdiplusShutdown(gdip_token);
  }
  if (SUCCEEDED(com_hr)) {
    CoUninitialize();
  }
  return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam) {
  App* self = nullptr;
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<App*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hidden_hwnd_ = hwnd;
  } else {
    self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self != nullptr) {
    return self->HandleHiddenMessage(msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT App::HandleHiddenMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg_terminate_existing_ != 0 && msg == msg_terminate_existing_) {
    if (static_cast<DWORD>(wparam) != GetCurrentProcessId()) {
      PostQuitMessage(0);
    }
    return 0;
  }

  if (msg_taskbar_created_ != 0 && msg == msg_taskbar_created_) {
    if (tray_icon_) {
      tray_icon_->RecreateAfterExplorerRestart();
    }
    return 0;
  }

  switch (msg) {
    case kMsgTrayCallback:
      if (tray_icon_) {
        tray_icon_->HandleTrayMessage(wparam, lparam);
      }
      return 0;

    case kMsgUpdateFinished:
      if (update_manager_) {
        update_manager_->HandleAsyncCheckFinished();
      }
      return 0;

    case kMsgToggleVietnamese:
      if (ime_controller_) {
        ime_controller_->ToggleVietnameseEnabled();
      }
      return 0;

    case kMsgForegroundChanged:
      if (input_source_manager_ && settings_) {
        input_source_manager_->Refresh(settings_->GetAppLanguage());
      }
      return 0;

    case WM_TIMER:
      switch (wparam) {
        case kTimerPollLayout:
          if (input_source_manager_ && settings_) {
            input_source_manager_->Refresh(settings_->GetAppLanguage());
          }
          break;
        case kTimerInitialUpdateCheck:
          KillTimer(hidden_hwnd_, kTimerInitialUpdateCheck);
          if (update_manager_) {
            update_manager_->CheckIfDueInBackground();
          }
          break;
        case kTimerPeriodicUpdateCheck:
          if (update_manager_) {
            update_manager_->CheckIfDueInBackground();
          }
          break;
        case kTimerClearUpToDate:
          KillTimer(hidden_hwnd_, kTimerClearUpToDate);
          if (update_manager_) {
            update_manager_->ClearUpToDateBanner();
          }
          break;
        default:
          break;
      }
      return 0;

    case WM_SETTINGCHANGE:
      if (lparam != 0 && _wcsicmp(reinterpret_cast<const wchar_t*>(lparam),
                                  L"ImmersiveColorSet") == 0) {
        if (tray_icon_) tray_icon_->Update(BuildTrayState());
        if (settings_window_) settings_window_->RefreshTheme();
        if (showcase_window_) showcase_window_->RefreshTheme();
      }
      return 0;

    case WM_POWERBROADCAST:
      if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) {
        if (update_manager_) {
          update_manager_->CheckIfDueInBackground();
        }
      }
      return TRUE;

    case WM_QUERYENDSESSION:
      return TRUE;

    case WM_ENDSESSION:
      if (wparam != FALSE) {
        PostQuitMessage(0);
      }
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }

  return DefWindowProcW(hidden_hwnd_, msg, wparam, lparam);
}

}  // namespace okeytre::win
