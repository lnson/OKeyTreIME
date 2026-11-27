// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_CARET_TEXT_READER_H_
#define OKEYTRE_WINDOWS_CARET_TEXT_READER_H_

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include "windows/ime-controller.h"

struct IUIAutomation;

namespace okeytre::win {

// Reads surrounding word text and selection state around the focused caret on
// Windows. Uses fast Win32 EM_GETSEL / WM_GETTEXT for classic Edit controls,
// and executes UI Automation (`IUIAutomationTextPattern2` / `TextPattern`) on a
// dedicated MTA worker thread with a strict timeout so `WH_KEYBOARD_LL` never
// stalls or suffers STA COM message-pump re-entrancy.
class CaretTextReader {
 public:
  CaretTextReader() = default;
  ~CaretTextReader();

  CaretTextReader(const CaretTextReader&) = delete;
  CaretTextReader& operator=(const CaretTextReader&) = delete;

  // Returns the word surrounding the focused caret and the caret offset within
  // that word, or std::nullopt if unavailable, unsupported, or timed out.
  std::optional<SurroundingWord> GetSurroundingText();

  // Returns the length of any non-empty text selection in the focused control
  // (e.g. browser address bar inline autocomplete), or 0 if no selection.
  int32_t GetSelectedRangeLength();

 private:
  enum class WorkerRequestKind { kNone, kSurroundingText, kHasSelection };

  void EnsureWorkerStarted();
  void WorkerLoop();
  std::optional<SurroundingWord> ReadViaUIAutomationOnWorker(
      IUIAutomation* automation);
  bool HasSelectionViaUIAutomationOnWorker(IUIAutomation* automation);
  std::optional<SurroundingWord> ReadViaWin32Edit();
  int32_t SelectionLengthViaWin32Edit();

  std::mutex mu_;
  std::condition_variable cv_request_;
  std::condition_variable cv_response_;
  std::thread worker_;
  bool worker_started_ = false;
  bool stop_ = false;
  bool in_flight_ = false;
  WorkerRequestKind request_kind_ = WorkerRequestKind::kNone;
  uint64_t request_id_ = 0;
  uint64_t completed_id_ = 0;
  std::optional<SurroundingWord> worker_surrounding_result_;
  bool worker_has_selection_result_ = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_CARET_TEXT_READER_H_
