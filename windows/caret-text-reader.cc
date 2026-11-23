// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/caret-text-reader.h"

#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>

#include <algorithm>
#include <string>
#include <vector>

namespace okeytre::win {

namespace {

constexpr UINT kSendMsgTimeoutMs = 50;
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

}  // namespace

CaretTextReader::CaretTextReader() = default;

CaretTextReader::~CaretTextReader() {
  if (automation_ != nullptr) {
    automation_->Release();
    automation_ = nullptr;
  }
}

void CaretTextReader::EnsureAutomation() {
  if (automation_ != nullptr || automation_init_attempted_) {
    return;
  }
  automation_init_attempted_ = true;
  CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                   IID_PPV_ARGS(&automation_));
}

std::optional<SurroundingWord> CaretTextReader::GetSurroundingText() {
  if (auto uia = ReadViaUIAutomation(); uia.has_value()) {
    return uia;
  }
  return ReadViaWin32Edit();
}

int32_t CaretTextReader::GetSelectedRangeLength() {
  const int32_t uia_len = SelectionLengthViaUIAutomation();
  if (uia_len > 0) {
    return uia_len;
  }
  return SelectionLengthViaWin32Edit();
}

std::optional<SurroundingWord> CaretTextReader::ReadViaUIAutomation() {
  EnsureAutomation();
  if (automation_ == nullptr) {
    return std::nullopt;
  }

  ComPtr<IUIAutomationElement> focused;
  if (FAILED(automation_->GetFocusedElement(focused.Put())) || !focused) {
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

  // Collapse a copy to the start endpoint (the caret position or selection
  // start), then expand backwards and forwards by up to kWindowRadiusChars.
  ComPtr<IUIAutomationTextRange> before_range;
  ComPtr<IUIAutomationTextRange> after_range;
  if (FAILED(caret_range->Clone(before_range.Put())) || !before_range ||
      FAILED(caret_range->Clone(after_range.Put())) || !after_range) {
    return std::nullopt;
  }

  before_range->MoveEndpointByRange(TextPatternRangeEndpoint_End,
                                    caret_range.Get(),
                                    TextPatternRangeEndpoint_Start);
  int moved_before = 0;
  before_range->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,
                                   TextUnit_Character, -kWindowRadiusChars,
                                   &moved_before);

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

  BSTR before_bstr = nullptr;
  BSTR after_bstr = nullptr;
  std::wstring before_str;
  std::wstring after_str;
  if (SUCCEEDED(before_range->GetText(kWindowRadiusChars, &before_bstr)) &&
      before_bstr != nullptr) {
    before_str = BstrToWString(before_bstr);
    SysFreeString(before_bstr);
  }
  if (SUCCEEDED(after_range->GetText(kWindowRadiusChars, &after_bstr)) &&
      after_bstr != nullptr) {
    after_str = BstrToWString(after_bstr);
    SysFreeString(after_bstr);
  }

  if (before_str.empty()) {
    return std::nullopt;
  }

  std::wstring combined = before_str + after_str;
  const int32_t caret_loc = static_cast<int32_t>(before_str.size());
  SurroundingWord word = ExtractSurroundingWord(
      reinterpret_cast<const uint16_t*>(combined.data()),
      static_cast<int32_t>(combined.size()), 0, caret_loc);
  if (word.word_utf16.empty()) {
    return std::nullopt;
  }
  return word;
}

int32_t CaretTextReader::SelectionLengthViaUIAutomation() {
  EnsureAutomation();
  if (automation_ == nullptr) {
    return 0;
  }

  ComPtr<IUIAutomationElement> focused;
  if (FAILED(automation_->GetFocusedElement(focused.Put())) || !focused) {
    return 0;
  }

  ComPtr<IUIAutomationTextPattern> text_pattern;
  if (FAILED(focused->GetCurrentPatternAs(
          UIA_TextPatternId, IID_PPV_ARGS(text_pattern.Put()))) ||
      !text_pattern) {
    return 0;
  }

  ComPtr<IUIAutomationTextRangeArray> selection;
  if (FAILED(text_pattern->GetSelection(selection.Put())) || !selection) {
    return 0;
  }

  int count = 0;
  if (FAILED(selection->get_Length(&count)) || count <= 0) {
    return 0;
  }

  ComPtr<IUIAutomationTextRange> range;
  if (FAILED(selection->GetElement(0, range.Put())) || !range) {
    return 0;
  }

  int cmp = 0;
  if (SUCCEEDED(range->CompareEndpoints(TextPatternRangeEndpoint_Start,
                                        range.Get(),
                                        TextPatternRangeEndpoint_End, &cmp)) &&
      cmp == 0) {
    return 0;
  }

  BSTR selected_bstr = nullptr;
  if (SUCCEEDED(range->GetText(256, &selected_bstr)) &&
      selected_bstr != nullptr) {
    const int32_t len = static_cast<int32_t>(SysStringLen(selected_bstr));
    SysFreeString(selected_bstr);
    return len;
  }
  return 0;
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
  if (sel_start == 0) {
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
  if (word.word_utf16.empty()) {
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
