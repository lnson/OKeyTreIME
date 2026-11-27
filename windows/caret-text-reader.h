// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_CARET_TEXT_READER_H_
#define OKEYTRE_WINDOWS_CARET_TEXT_READER_H_

#include <cstdint>
#include <optional>
#include <thread>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
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
  enum class WorkerRequestKind : uint8_t { kNone, kSurroundingText, kHasSelection };

  void EnsureWorkerStarted() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool RunWorkerRequest(WorkerRequestKind kind, absl::Duration budget)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void WorkerLoop();
  std::optional<SurroundingWord> ReadViaUIAutomationOnWorker(
      IUIAutomation* automation);
  bool HasSelectionViaUIAutomationOnWorker(IUIAutomation* automation);
  std::optional<SurroundingWord> ReadViaWin32Edit();
  int32_t SelectionLengthViaWin32Edit();

  absl::Mutex mu_;
  std::thread worker_;
  uint64_t request_id_ ABSL_GUARDED_BY(mu_) = 0;
  uint64_t completed_id_ ABSL_GUARDED_BY(mu_) = 0;
  std::optional<SurroundingWord> worker_surrounding_result_
      ABSL_GUARDED_BY(mu_);
  WorkerRequestKind request_kind_ ABSL_GUARDED_BY(mu_) =
      WorkerRequestKind::kNone;
  bool worker_started_ ABSL_GUARDED_BY(mu_) = false;
  bool stop_ ABSL_GUARDED_BY(mu_) = false;
  bool in_flight_ ABSL_GUARDED_BY(mu_) = false;
  bool worker_has_selection_result_ ABSL_GUARDED_BY(mu_) = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_CARET_TEXT_READER_H_
