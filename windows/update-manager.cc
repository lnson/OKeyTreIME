// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/update-manager.h"

#include <shellapi.h>
#include <winhttp.h>

#include <chrono>
#include <thread>
#include <vector>

#include "windows/localization.h"

namespace okeytre::win {

namespace {

constexpr wchar_t kGitHubApiHost[] = L"api.github.com";
constexpr wchar_t kGitHubLatestReleasePath[] =
    L"/repos/lnson/OKeyTreIME/releases/latest";

class WinHttpHandle {
 public:
  explicit WinHttpHandle(HINTERNET h = nullptr) : handle_(h) {}
  ~WinHttpHandle() {
    if (handle_ != nullptr) {
      WinHttpCloseHandle(handle_);
    }
  }
  WinHttpHandle(const WinHttpHandle&) = delete;
  WinHttpHandle& operator=(const WinHttpHandle&) = delete;
  HINTERNET Get() const noexcept { return handle_; }
  explicit operator bool() const noexcept { return handle_ != nullptr; }

 private:
  HINTERNET handle_ = nullptr;
};

struct HttpResponse {
  bool ok = false;
  DWORD status_code = 0;
  std::string body;
};

HttpResponse FetchLatestReleaseJson(const std::wstring& user_agent) {
  HttpResponse resp;

  WinHttpHandle session(
      WinHttpOpen(user_agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    return resp;
  }

  WinHttpSetTimeouts(session.Get(), 10000, 10000, 15000, 15000);

  WinHttpHandle connect(WinHttpConnect(session.Get(), kGitHubApiHost,
                                       INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connect) {
    return resp;
  }

  WinHttpHandle request(WinHttpOpenRequest(
      connect.Get(), L"GET", kGitHubLatestReleasePath, nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request) {
    return resp;
  }

  const wchar_t kHeaders[] =
      L"Accept: application/vnd.github+json\r\n"
      L"X-GitHub-Api-Version: 2022-11-28\r\n";

  if (!WinHttpSendRequest(request.Get(), kHeaders, static_cast<DWORD>(-1),
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    return resp;
  }

  if (!WinHttpReceiveResponse(request.Get(), nullptr)) {
    return resp;
  }

  DWORD status_code = 0;
  DWORD status_size = sizeof(status_code);
  if (!WinHttpQueryHeaders(
          request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
          WINHTTP_NO_HEADER_INDEX)) {
    return resp;
  }

  resp.status_code = status_code;
  resp.ok = true;

  while (true) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.Get(), &available) ||
        available == 0) {
      break;
    }
    const size_t old_size = resp.body.size();
    resp.body.resize(old_size + available);
    DWORD read = 0;
    if (!WinHttpReadData(request.Get(), resp.body.data() + old_size, available,
                         &read)) {
      resp.ok = false;
      break;
    }
    resp.body.resize(old_size + read);
    if (read == 0) break;
    if (resp.body.size() > 2 * 1024 * 1024) {
      resp.ok = false;
      break;
    }
  }

  return resp;
}

}  // namespace

UpdateManager::UpdateManager(SettingsStore& settings, HWND notify_hwnd,
                             UINT notify_msg)
    : settings_(settings),
      notify_hwnd_(notify_hwnd),
      current_version_(AppVersion::Parse(kCurrentAppVersionString).value()),
      skipped_version_(settings.GetSkippedUpdateVersion()),
      notify_msg_(notify_msg),
      is_auto_check_enabled_(settings.GetAutoCheckUpdates()) {}

UpdateManager::~UpdateManager() { shutting_down_.store(true); }

int64_t UpdateManager::NowUnixSeconds() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

void UpdateManager::SetAutoCheckEnabled(bool enabled) {
  if (is_auto_check_enabled_ == enabled) return;
  is_auto_check_enabled_ = enabled;
  settings_.SetAutoCheckUpdates(enabled);
  if (on_state_changed_) on_state_changed_();
  if (enabled) CheckIfDueInBackground();
}

std::optional<UpdateRelease> UpdateManager::visible_available_release() const {
  if (!available_release_.has_value()) return std::nullopt;
  if (skipped_version_.has_value()) {
    if (const auto skipped = AppVersion::Parse(*skipped_version_);
        skipped.has_value() && available_release_->version <= *skipped) {
      return std::nullopt;
    }
  }
  return available_release_;
}

void UpdateManager::CheckIfDueInBackground() {
  if (!is_auto_check_enabled_ || is_checking_) return;
  const int64_t last_check = settings_.GetLastUpdateCheckTimestamp();
  const int64_t now = NowUnixSeconds();
  if (last_check > 0 && (now - last_check) < kCheckIntervalSeconds) return;
  StartCheckThread(/*user_initiated=*/false);
}

void UpdateManager::CheckForUpdatesNow() {
  if (is_checking_) return;
  did_confirm_up_to_date_ = false;
  StartCheckThread(/*user_initiated=*/true);
}

void UpdateManager::StartCheckThread(bool user_initiated) {
  is_checking_ = true;
  if (on_state_changed_) on_state_changed_();

  const AppVersion current = current_version_;
  const std::wstring ua = L"OKeyTre/" + Utf8ToWide(current.description());

  std::thread([this, current, ua, user_initiated]() {
    PendingResult result;
    result.user_initiated = user_initiated;

    const HttpResponse http = FetchLatestReleaseJson(ua);
    if (http.ok) {
      if (http.status_code == 404) {
        result.succeeded = true;
        result.release = std::nullopt;
      } else if (http.status_code >= 200 && http.status_code <= 299) {
        result.succeeded = true;
        result.release = ParseLatestGitHubRelease(http.body, current);
      }
    }

    if (shutting_down_.load()) return;

    {
      absl::MutexLock lock(result_mutex_);
      pending_result_ = std::move(result);
    }

    if (notify_hwnd_ != nullptr && IsWindow(notify_hwnd_)) {
      PostMessageW(notify_hwnd_, notify_msg_, 0, 0);
    }
  }).detach();
}

void UpdateManager::HandleAsyncCheckFinished() {
  std::optional<PendingResult> result;
  {
    absl::MutexLock lock(result_mutex_);
    result = std::move(pending_result_);
    pending_result_.reset();
  }
  is_checking_ = false;

  if (result.has_value() && result->succeeded) {
    settings_.SetLastUpdateCheckTimestamp(NowUnixSeconds());
    available_release_ = result->release;
    if (result->user_initiated) {
      if (available_release_.has_value()) {
        skipped_version_.reset();
        settings_.ClearSkippedUpdateVersion();
      } else {
        did_confirm_up_to_date_ = true;
      }
    }
  }

  if (on_state_changed_) on_state_changed_();
}

void UpdateManager::ClearUpToDateBanner() {
  if (!did_confirm_up_to_date_) {
    return;
  }
  did_confirm_up_to_date_ = false;
  if (on_state_changed_) on_state_changed_();
}

void UpdateManager::SkipRelease(const UpdateRelease& release) {
  skipped_version_ = release.display_version();
  settings_.SetSkippedUpdateVersion(*skipped_version_);
  if (on_state_changed_) on_state_changed_();
}

void UpdateManager::OpenReleasePage(
    const std::optional<UpdateRelease>& release) {
  std::string url = std::string(kReleasesWebUrl);
  const auto& target = release.has_value() ? release : available_release_;
  if (target.has_value() && IsValidatedGitHubUrl(target->release_page_url)) {
    url = target->release_page_url;
  }
  const std::wstring wurl = Utf8ToWide(url);
  ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);
}

void UpdateManager::OpenReleasesWebPage() {
  const std::wstring wurl = Utf8ToWide(kReleasesWebUrl);
  ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);
}

}  // namespace okeytre::win
