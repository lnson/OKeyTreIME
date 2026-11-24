// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/caret-text-reader.h"

#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace okeytre::win {

namespace {

constexpr UINT kSendMsgTimeoutMs = 50;
constexpr DWORD kUiaRpcTimeoutMs = 50;
constexpr auto kSurroundingWaitBudget = std::chrono::milliseconds(35);
constexpr auto kSelectionWaitBudget = std::chrono::milliseconds(25);
constexpr int32_t kWindowRadiusChars = 64;
constexpr int32_t kMaxClassicEditLen = 65536;

template <typename T>
class ComPtr {
 public:
  ComPtr() = default;
  ~ComPtr() { Reset(); }

  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;

  T** Put() noexcept {
    Reset();
    return &ptr_;
  }
  T* Get() const noexcept { return ptr_; }
  T* operator->() const noexcept { return ptr_; }
  explicit operator bool() const noexcept { return ptr_ != nullptr; }

  void Reset() noexcept {
    if (ptr_ != nullptr) {
      ptr_->Release();
      ptr_ = nullptr;
    }
  }

 private:
  T* ptr_ = nullptr;
};

bool IsClassicEditClass(HWND hwnd) {
  wchar_t cls[64] = {};
  if (GetClassNameW(hwnd, cls, 64) == 0) {
    return false;
  }
  return _wcsicmp(cls, L"Edit") == 0 ||
         _wcsnicmp(cls, L"RichEdit", 8) == 0 ||
         _wcsnicmp(cls, L"RICHEDIT", 8) == 0;
}

HWND GetFocusedControlHwnd() {
  GUITHREADINFO gti{};
  gti.cbSize = sizeof(gti);
  if (!GetGUIThreadInfo(0, &gti)) {
    return nullptr;
  }
  return gti.hwndFocus;
}

std::wstring BstrToWString(BSTR bstr) {
  if (bstr == nullptr) {
    return {};
  }
  return std::wstring(bstr, SysStringLen(bstr));
}

std::wstring TrimBeforeToCurrentLine(std::wstring_view text) {
  const size_t pos = text.find_last_of(L"\r\n");
  if (pos != std::wstring_view::npos) {
    text.remove_prefix(pos + 1);
  }
  return std::wstring(text);
}

std::wstring TrimAfterToCurrentLine(std::wstring_view text) {
  const size_t pos = text.find_first_of(L"\r\n");
  if (pos != std::wstring_view::npos) {
    text = text.substr(0, pos);
  }
  return std::wstring(text);
}

bool IsElementReadOnly(IUIAutomationElement* element) {
  VARIANT val;
  VariantInit(&val);
  if (SUCCEEDED(element->GetCurrentPropertyValue(UIA_ValueIsReadOnlyPropertyId,
                                                 &val))) {
    const bool read_only = (val.vt == VT_BOOL && val.boolVal == VARIANT_TRUE);
    VariantClear(&val);
    return read_only;
  }
  return false;
}

}  // namespace

CaretTextReader::CaretTextReader() = default;

CaretTextReader::~CaretTextReader() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
  }
  cv_request_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void CaretTextReader::EnsureWorkerStarted() {
  if (worker_started_) {
    return;
  }
  worker_started_ = true;
  worker_ = std::thread(&CaretTextReader::WorkerLoop, this);
}

void CaretTextReader::WorkerLoop() {
  const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  ComPtr<IUIAutomation> automation;
  ComPtr<IUIAutomation2> automation2;
  if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8, nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(automation2.Put()))) &&
      automation2) {
    automation2->put_ConnectionTimeout(kUiaRpcTimeoutMs);
    automation2->put_TransactionTimeout(kUiaRpcTimeoutMs);
    automation2->QueryInterface(IID_PPV_ARGS(automation.Put()));
  }
  if (!automation) {
    CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(automation.Put()));
  }

  for (;;) {
    uint64_t target_id = 0;
    WorkerRequestKind kind = WorkerRequestKind::kNone;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_request_.wait(lock, [this]() {
        return stop_ || request_id_ != completed_id_;
      });
      if (stop_) {
        break;
      }
      target_id = request_id_;
      kind = request_kind_;
    }

    std::optional<SurroundingWord> surrounding;
    bool has_selection = false;
    if (automation) {
      if (kind == WorkerRequestKind::kSurroundingText) {
        surrounding = ReadViaUIAutomationOnWorker(automation.Get());
      } else if (kind == WorkerRequestKind::kHasSelection) {
        has_selection = HasSelectionViaUIAutomationOnWorker(automation.Get());
      }
    }

    {
      std::lock_guard<std::mutex> lock(mu_);
      worker_surrounding_result_ = std::move(surrounding);
      worker_has_selection_result_ = has_selection;
      completed_id_ = target_id;
      in_flight_ = false;
    }
    cv_response_.notify_one();
  }

  automation.Reset();
  automation2.Reset();
  if (SUCCEEDED(com_hr)) {
    CoUninitialize();
  }
}

std::optional<SurroundingWord> CaretTextReader::GetSurroundingText() {
  // Fast non-blocking path for classic Win32 Edit/RichEdit controls.
  if (HWND focused = GetFocusedControlHwnd();
      focused != nullptr && IsClassicEditClass(focused)) {
    return ReadViaWin32Edit();
  }

  std::unique_lock<std::mutex> lock(mu_);
  EnsureWorkerStarted();

  // If a previous cross-process UIA query is still unwinding, skip rather than
  // blocking the low-level keyboard hook.
  if (in_flight_) {
    return std::nullopt;
  }

  in_flight_ = true;
  request_kind_ = WorkerRequestKind::kSurroundingText;
  const uint64_t my_id = ++request_id_;
  lock.unlock();
  cv_request_.notify_one();

  lock.lock();
  const bool finished = cv_response_.wait_for(
      lock, kSurroundingWaitBudget, [this, my_id]() {
        return completed_id_ >= my_id || stop_;
      });
  if (!finished || stop_) {
    return std::nullopt;
  }
  return std::move(worker_surrounding_result_);
}

int32_t CaretTextReader::GetSelectedRangeLength() {
  // Fast non-blocking path for classic Win32 Edit/RichEdit controls.
  if (HWND focused = GetFocusedControlHwnd();
      focused != nullptr && IsClassicEditClass(focused)) {
    return SelectionLengthViaWin32Edit();
  }

  // Query UI Automation on the MTA worker thread (no STA message pumping) with
  // a tight timeout so browser Omnibox inline autocomplete selections (e.g.
  // "a[ccount.apple.com]") are cleared cleanly before rewriting.
  std::unique_lock<std::mutex> lock(mu_);
  EnsureWorkerStarted();

  if (in_flight_) {
    return 0;
  }

  in_flight_ = true;
  request_kind_ = WorkerRequestKind::kHasSelection;
  const uint64_t my_id = ++request_id_;
  lock.unlock();
  cv_request_.notify_one();

  lock.lock();
  const bool finished = cv_response_.wait_for(
      lock, kSelectionWaitBudget, [this, my_id]() {
        return completed_id_ >= my_id || stop_;
      });
  if (!finished || stop_) {
    return 0;
  }
  return worker_has_selection_result_ ? 1 : 0;
}

bool CaretTextReader::HasSelectionViaUIAutomationOnWorker(
    IUIAutomation* automation) {
  if (automation == nullptr) {
    return false;
  }

  ComPtr<IUIAutomationElement> focused;
  if (FAILED(automation->GetFocusedElement(focused.Put())) || !focused) {
    return false;
  }

  ComPtr<IUIAutomationTextPattern> text_pattern;
  if (FAILED(focused->GetCurrentPatternAs(
          UIA_TextPatternId, IID_PPV_ARGS(text_pattern.Put()))) ||
      !text_pattern) {
    return false;
  }

  ComPtr<IUIAutomationTextRangeArray> selection;
  if (FAILED(text_pattern->GetSelection(selection.Put())) || !selection) {
    return false;
  }

  int count = 0;
  if (FAILED(selection->get_Length(&count)) || count <= 0) {
    return false;
  }

  ComPtr<IUIAutomationTextRange> range;
  if (FAILED(selection->GetElement(0, range.Put())) || !range) {
    return false;
  }

  int cmp = 0;
  if (SUCCEEDED(range->CompareEndpoints(TextPatternRangeEndpoint_Start,
                                        range.Get(),
                                        TextPatternRangeEndpoint_End, &cmp))) {
    return cmp != 0;
  }

  BSTR text = nullptr;
  if (SUCCEEDED(range->GetText(1, &text)) && text != nullptr) {
    const bool has_text = SysStringLen(text) > 0;
    SysFreeString(text);
    return has_text;
  }
  return false;
}

std::optional<SurroundingWord> CaretTextReader::ReadViaUIAutomationOnWorker(
    IUIAutomation* automation) {
  if (automation == nullptr) {
    return std::nullopt;
  }

  ComPtr<IUIAutomationElement> focused;
  if (FAILED(automation->GetFocusedElement(focused.Put())) || !focused) {
    return std::nullopt;
  }

  if (IsElementReadOnly(focused.Get())) {
    return std::nullopt;
  }

  // Prefer TextPattern2::GetCaretRange (supported by Chromium, Edge, Electron,
  // VS Code, Word, Win11 Notepad, WinUI, WPF).
  ComPtr<IUIAutomationTextRange> caret_range;
  ComPtr<IUIAutomationTextPattern2> text_pattern2;
  if (SUCCEEDED(focused->GetCurrentPatternAs(UIA_TextPattern2Id,
                                             IID_PPV_ARGS(text_pattern2.Put()))) &&
      text_pattern2) {
    BOOL is_active = FALSE;
    if (SUCCEEDED(text_pattern2->GetCaretRange(&is_active, caret_range.Put())) &&
        !is_active) {
      caret_range.Reset();
    }
  }

  if (!caret_range) {
    ComPtr<IUIAutomationTextPattern> text_pattern;
    if (SUCCEEDED(focused->GetCurrentPatternAs(
            UIA_TextPatternId, IID_PPV_ARGS(text_pattern.Put()))) &&
        text_pattern) {
      ComPtr<IUIAutomationTextRangeArray> selection;
      if (SUCCEEDED(text_pattern->GetSelection(selection.Put())) && selection) {
        int length = 0;
        if (SUCCEEDED(selection->get_Length(&length)) && length > 0) {
          selection->GetElement(0, caret_range.Put());
        }
      }
    }
  }

  if (!caret_range) {
    return std::nullopt;
  }

  // Never seed when text is currently selected (the incoming keystroke will
  // replace the selection rather than editing within it).
  int endpoint_cmp = 0;
  if (SUCCEEDED(caret_range->CompareEndpoints(
          TextPatternRangeEndpoint_Start, caret_range.Get(),
          TextPatternRangeEndpoint_End, &endpoint_cmp)) &&
      endpoint_cmp != 0) {
    return std::nullopt;
  }

  // Expand backwards from the caret first. If there is no word character
  // immediately preceding the caret (empty webview, after space/newline, or
  // after a rich-text sentinel like U+200B / U+FFFC), stop immediately so we
  // never seed placeholder text or trailing DOM sentinels.
  ComPtr<IUIAutomationTextRange> before_range;
  if (FAILED(caret_range->Clone(before_range.Put())) || !before_range) {
    return std::nullopt;
  }

  before_range->MoveEndpointByRange(TextPatternRangeEndpoint_End,
                                    caret_range.Get(),
                                    TextPatternRangeEndpoint_Start);
  int moved_before = 0;
  before_range->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,
                                   TextUnit_Character, -kWindowRadiusChars,
                                   &moved_before);

  BSTR before_bstr = nullptr;
  std::wstring before_str;
  if (SUCCEEDED(before_range->GetText(kWindowRadiusChars, &before_bstr)) &&
      before_bstr != nullptr) {
    before_str = BstrToWString(before_bstr);
    SysFreeString(before_bstr);
  }
  before_str = TrimBeforeToCurrentLine(before_str);
  if (before_str.empty() ||
      IsCaretWordBoundary(static_cast<uint16_t>(before_str.back()))) {
    return std::nullopt;
  }

  ComPtr<IUIAutomationTextRange> after_range;
  std::wstring after_str;
  if (SUCCEEDED(caret_range->Clone(after_range.Put())) && after_range) {
    after_range->MoveEndpointByRange(TextPatternRangeEndpoint_Start,
                                     caret_range.Get(),
                                     TextPatternRangeEndpoint_Start);
    after_range->MoveEndpointByRange(TextPatternRangeEndpoint_End,
                                     caret_range.Get(),
                                     TextPatternRangeEndpoint_Start);
    int moved_after = 0;
    after_range->MoveEndpointByUnit(TextPatternRangeEndpoint_End,
                                    TextUnit_Character, kWindowRadiusChars,
                                    &moved_after);

    BSTR after_bstr = nullptr;
    if (SUCCEEDED(after_range->GetText(kWindowRadiusChars, &after_bstr)) &&
        after_bstr != nullptr) {
      after_str = BstrToWString(after_bstr);
      SysFreeString(after_bstr);
    }
    after_str = TrimAfterToCurrentLine(after_str);
  }

  std::wstring combined = before_str + after_str;
  const int32_t caret_loc = static_cast<int32_t>(before_str.size());
  SurroundingWord word = ExtractSurroundingWord(
      reinterpret_cast<const uint16_t*>(combined.data()),
      static_cast<int32_t>(combined.size()), 0, caret_loc);
  if (word.word_utf16.empty() || word.caret_offset <= 0) {
    return std::nullopt;
  }
  return word;
}

std::optional<SurroundingWord> CaretTextReader::ReadViaWin32Edit() {
  HWND hwnd = GetFocusedControlHwnd();
  if (hwnd == nullptr || !IsClassicEditClass(hwnd)) {
    return std::nullopt;
  }

  DWORD sel_start = 0;
  DWORD sel_end = 0;
  DWORD_PTR res = 0;
  if (SendMessageTimeoutW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start),
                          reinterpret_cast<LPARAM>(&sel_end), SMTO_ABORTIFHUNG,
                          kSendMsgTimeoutMs, &res) == 0) {
    return std::nullopt;
  }
  if (sel_start == 0 || sel_end != sel_start) {
    return std::nullopt;
  }

  DWORD_PTR text_len_res = 0;
  if (SendMessageTimeoutW(hwnd, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG,
                          kSendMsgTimeoutMs, &text_len_res) == 0) {
    return std::nullopt;
  }
  const int32_t total_len = static_cast<int32_t>(text_len_res);
  if (total_len <= 0 || total_len > kMaxClassicEditLen) {
    return std::nullopt;
  }

  std::wstring text(static_cast<size_t>(total_len) + 1, L'\0');
  DWORD_PTR copied = 0;
  if (SendMessageTimeoutW(hwnd, WM_GETTEXT,
                          static_cast<WPARAM>(text.size()),
                          reinterpret_cast<LPARAM>(text.data()),
                          SMTO_ABORTIFHUNG, kSendMsgTimeoutMs, &copied) == 0) {
    return std::nullopt;
  }
  text.resize(static_cast<size_t>(copied));
  if (text.empty()) {
    return std::nullopt;
  }

  SurroundingWord word = ExtractSurroundingWord(
      reinterpret_cast<const uint16_t*>(text.data()),
      static_cast<int32_t>(text.size()), 0, static_cast<int32_t>(sel_start));
  if (word.word_utf16.empty() || word.caret_offset <= 0) {
    return std::nullopt;
  }
  return word;
}

int32_t CaretTextReader::SelectionLengthViaWin32Edit() {
  HWND hwnd = GetFocusedControlHwnd();
  if (hwnd == nullptr || !IsClassicEditClass(hwnd)) {
    return 0;
  }
  DWORD sel_start = 0;
  DWORD sel_end = 0;
  DWORD_PTR res = 0;
  if (SendMessageTimeoutW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start),
                          reinterpret_cast<LPARAM>(&sel_end), SMTO_ABORTIFHUNG,
                          kSendMsgTimeoutMs, &res) == 0) {
    return 0;
  }
  return sel_end > sel_start ? static_cast<int32_t>(sel_end - sel_start) : 0;
}

}  // namespace okeytre::win
