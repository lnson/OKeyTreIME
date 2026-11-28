// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/caret-text-reader.h"

// clang-format off
#include <windows.h>
// clang-format on

#include <oleauto.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "absl/cleanup/cleanup.h"

namespace okeytre::win {

namespace {

constexpr UINT kSendMsgTimeoutMs = 50;
constexpr DWORD kUiaRpcTimeoutMs = 50;
constexpr absl::Duration kSurroundingWaitBudget = absl::Milliseconds(35);
constexpr absl::Duration kSelectionWaitBudget = absl::Milliseconds(25);
constexpr int32_t kWindowRadiusChars = 64;
constexpr int32_t kMaxClassicEditLen = 65536;

using Microsoft::WRL::ComPtr;

bool IsClassicEditClass(HWND hwnd) {
  if (hwnd == nullptr) return false;
  wchar_t cls[64] = {};
  if (GetClassNameW(hwnd, cls, 64) == 0) {
    return false;
  }
  // Matches RichEdit, RichEdit20W, RICHEDIT50W, RICHEDIT60W, etc.
  constexpr std::wstring_view kRichEditPrefix = L"RichEdit";
  return _wcsicmp(cls, L"Edit") == 0 ||
         _wcsnicmp(cls, kRichEditPrefix.data(), kRichEditPrefix.size()) == 0;
}

HWND GetFocusedControlHwnd() {
  GUITHREADINFO gti{};
  gti.cbSize = sizeof(gti);
  return GetGUIThreadInfo(0, &gti) ? gti.hwndFocus : nullptr;
}

std::wstring_view BstrView(BSTR bstr) {
  return bstr == nullptr ? std::wstring_view{}
                         : std::wstring_view(bstr, SysStringLen(bstr));
}

std::wstring_view TrimBeforeToCurrentLine(std::wstring_view text) {
  const size_t pos = text.find_last_of(L"\r\n");
  return pos == std::wstring_view::npos ? text : text.substr(pos + 1);
}

std::wstring_view TrimAfterToCurrentLine(std::wstring_view text) {
  const size_t pos = text.find_first_of(L"\r\n");
  return pos == std::wstring_view::npos ? text : text.substr(0, pos);
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

CaretTextReader::~CaretTextReader() {
  {
    absl::MutexLock lock(mu_);
    stop_ = true;
  }
  if (worker_.joinable()) worker_.join();
}

void CaretTextReader::EnsureWorkerStarted() {
  if (worker_started_) return;

  worker_started_ = true;
  worker_ = std::thread(&CaretTextReader::WorkerLoop, this);
}

bool CaretTextReader::RunWorkerRequest(WorkerRequestKind kind,
                                       absl::Duration budget) {
  EnsureWorkerStarted();

  // If a previous cross-process UIA query is still unwinding, skip rather than
  // blocking the low-level keyboard hook.
  if (in_flight_) return false;

  in_flight_ = true;
  request_kind_ = kind;
  const uint64_t my_id = ++request_id_;
  auto done = [this, my_id]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return completed_id_ >= my_id || stop_;
  };
  return mu_.AwaitWithTimeout(absl::Condition(&done), budget) && !stop_;
}

void CaretTextReader::WorkerLoop() {
  const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  ComPtr<IUIAutomation> automation;
  ComPtr<IUIAutomation2> automation2;
  if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8, nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&automation2))) &&
      automation2 != nullptr) {
    automation2->put_ConnectionTimeout(kUiaRpcTimeoutMs);
    automation2->put_TransactionTimeout(kUiaRpcTimeoutMs);
    automation2.As(&automation);
  }
  if (automation == nullptr) {
    CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&automation));
  }

  for (;;) {
    uint64_t target_id = 0;
    WorkerRequestKind kind = WorkerRequestKind::kNone;
    {
      absl::MutexLock lock(mu_);
      auto has_work = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
        return stop_ || request_id_ != completed_id_;
      };
      mu_.Await(absl::Condition(&has_work));
      if (stop_) break;

      target_id = request_id_;
      kind = request_kind_;
    }

    std::optional<SurroundingWord> surrounding;
    bool has_selection = false;
    if (automation != nullptr) {
      if (kind == WorkerRequestKind::kSurroundingText) {
        surrounding = ReadViaUIAutomationOnWorker(automation.Get());
      } else if (kind == WorkerRequestKind::kHasSelection) {
        has_selection = HasSelectionViaUIAutomationOnWorker(automation.Get());
      }
    }

    {
      absl::MutexLock lock(mu_);
      worker_surrounding_result_ = std::move(surrounding);
      worker_has_selection_result_ = has_selection;
      completed_id_ = target_id;
      in_flight_ = false;
    }
  }

  automation.Reset();
  automation2.Reset();
  if (SUCCEEDED(com_hr)) {
    CoUninitialize();
  }
}

std::optional<SurroundingWord> CaretTextReader::GetSurroundingText() {
  // Fast non-blocking path for classic Win32 Edit/RichEdit controls.
  if (IsClassicEditClass(GetFocusedControlHwnd())) {
    return ReadViaWin32Edit();
  }

  absl::MutexLock lock(mu_);
  if (!RunWorkerRequest(WorkerRequestKind::kSurroundingText,
                        kSurroundingWaitBudget)) {
    return std::nullopt;
  }
  return std::move(worker_surrounding_result_);
}

int32_t CaretTextReader::GetSelectedRangeLength() {
  // Fast non-blocking path for classic Win32 Edit/RichEdit controls.
  if (IsClassicEditClass(GetFocusedControlHwnd())) {
    return SelectionLengthViaWin32Edit();
  }

  // Query UI Automation on the MTA worker thread (no STA message pumping) with
  // a tight timeout so browser Omnibox inline autocomplete selections (e.g.
  // "a[ccount.apple.com]") are cleared cleanly before rewriting.
  absl::MutexLock lock(mu_);
  if (!RunWorkerRequest(WorkerRequestKind::kHasSelection,
                        kSelectionWaitBudget)) {
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
  if (FAILED(automation->GetFocusedElement(&focused)) || focused == nullptr) {
    return false;
  }

  ComPtr<IUIAutomationTextPattern> text_pattern;
  if (FAILED(focused->GetCurrentPatternAs(UIA_TextPatternId,
                                          IID_PPV_ARGS(&text_pattern))) ||
      text_pattern == nullptr) {
    return false;
  }

  ComPtr<IUIAutomationTextRangeArray> selection;
  if (FAILED(text_pattern->GetSelection(&selection)) || selection == nullptr) {
    return false;
  }

  int count = 0;
  if (FAILED(selection->get_Length(&count)) || count <= 0) {
    return false;
  }

  ComPtr<IUIAutomationTextRange> range;
  if (FAILED(selection->GetElement(0, &range)) || range == nullptr) {
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
  if (FAILED(automation->GetFocusedElement(&focused)) || focused == nullptr) {
    return std::nullopt;
  }

  if (IsElementReadOnly(focused.Get())) return std::nullopt;

  // Prefer TextPattern2::GetCaretRange (supported by Chromium, Edge, Electron,
  // VS Code, Word, Win11 Notepad, WinUI, WPF).
  ComPtr<IUIAutomationTextRange> caret_range;
  ComPtr<IUIAutomationTextPattern2> text_pattern2;
  if (SUCCEEDED(focused->GetCurrentPatternAs(UIA_TextPattern2Id,
                                             IID_PPV_ARGS(&text_pattern2))) &&
      text_pattern2 != nullptr) {
    BOOL is_active = FALSE;
    if (SUCCEEDED(text_pattern2->GetCaretRange(&is_active, &caret_range)) &&
        !is_active) {
      caret_range.Reset();
    }
  }

  if (caret_range == nullptr) {
    ComPtr<IUIAutomationTextPattern> text_pattern;
    if (SUCCEEDED(focused->GetCurrentPatternAs(UIA_TextPatternId,
                                               IID_PPV_ARGS(&text_pattern))) &&
        text_pattern != nullptr) {
      ComPtr<IUIAutomationTextRangeArray> selection;
      if (SUCCEEDED(text_pattern->GetSelection(&selection)) &&
          selection != nullptr) {
        int length = 0;
        if (SUCCEEDED(selection->get_Length(&length)) && length > 0) {
          selection->GetElement(0, &caret_range);
        }
      }
    }
  }

  if (caret_range == nullptr) return std::nullopt;

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
  if (FAILED(caret_range->Clone(&before_range)) || before_range == nullptr) {
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
  const auto free_before =
      absl::MakeCleanup([&before_bstr]() { SysFreeString(before_bstr); });
  std::wstring_view before;
  if (SUCCEEDED(before_range->GetText(kWindowRadiusChars, &before_bstr))) {
    before = TrimBeforeToCurrentLine(BstrView(before_bstr));
  }
  if (before.empty() ||
      IsCaretWordBoundary(static_cast<uint16_t>(before.back()))) {
    return std::nullopt;
  }

  BSTR after_bstr = nullptr;
  const auto free_after =
      absl::MakeCleanup([&after_bstr]() { SysFreeString(after_bstr); });
  std::wstring_view after;
  ComPtr<IUIAutomationTextRange> after_range;
  if (SUCCEEDED(caret_range->Clone(&after_range)) && after_range != nullptr) {
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

    if (SUCCEEDED(after_range->GetText(kWindowRadiusChars, &after_bstr))) {
      after = TrimAfterToCurrentLine(BstrView(after_bstr));
    }
  }

  std::wstring combined_buf;
  std::wstring_view combined = before;
  if (!after.empty()) {
    combined_buf.reserve(before.size() + after.size());
    combined_buf.append(before);
    combined_buf.append(after);
    combined = combined_buf;
  }
  const int32_t caret_loc = static_cast<int32_t>(before.size());
  SurroundingWord word = ExtractSurroundingWord(
      reinterpret_cast<const uint16_t*>(combined.data()),
      static_cast<int32_t>(combined.size()), 0, caret_loc);
  if (word.word_utf16.empty() || word.caret_offset <= 0) return std::nullopt;

  return word;
}

std::optional<SurroundingWord> CaretTextReader::ReadViaWin32Edit() {
  HWND hwnd = GetFocusedControlHwnd();
  if (!IsClassicEditClass(hwnd)) return std::nullopt;

  DWORD sel_start = 0;
  DWORD sel_end = 0;
  DWORD_PTR res = 0;
  if (SendMessageTimeoutW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start),
                          reinterpret_cast<LPARAM>(&sel_end), SMTO_ABORTIFHUNG,
                          kSendMsgTimeoutMs, &res) == 0) {
    return std::nullopt;
  }
  if (sel_start == 0 || sel_end != sel_start) return std::nullopt;

  DWORD_PTR text_len_res = 0;
  if (SendMessageTimeoutW(hwnd, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG,
                          kSendMsgTimeoutMs, &text_len_res) == 0) {
    return std::nullopt;
  }
  const int32_t total_len = static_cast<int32_t>(text_len_res);
  if (total_len <= 0 || total_len > kMaxClassicEditLen) return std::nullopt;

  std::wstring text(static_cast<size_t>(total_len) + 1, L'\0');
  DWORD_PTR copied = 0;
  if (SendMessageTimeoutW(hwnd, WM_GETTEXT, static_cast<WPARAM>(text.size()),
                          reinterpret_cast<LPARAM>(text.data()),
                          SMTO_ABORTIFHUNG, kSendMsgTimeoutMs, &copied) == 0) {
    return std::nullopt;
  }
  text.resize(static_cast<size_t>(copied));
  if (text.empty()) return std::nullopt;

  SurroundingWord word = ExtractSurroundingWord(
      reinterpret_cast<const uint16_t*>(text.data()),
      static_cast<int32_t>(text.size()), 0, static_cast<int32_t>(sel_start));
  if (word.word_utf16.empty() || word.caret_offset <= 0) return std::nullopt;

  return word;
}

int32_t CaretTextReader::SelectionLengthViaWin32Edit() {
  HWND hwnd = GetFocusedControlHwnd();
  if (!IsClassicEditClass(hwnd)) return 0;

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
