# ÔKêyTre

**[Tiếng Việt](#tiếng-việt) · [English](#english)**

---

## Tiếng Việt

**ÔKêyTre** ra đời nhằm cải thiện trải nghiệm gõ tiếng Việt trên **macOS** và **Windows**. Các tính năng chính bao gồm:

### 1. Kiểu gõ & Gõ xen kẽ tiếng Anh
- **Hỗ trợ nhiều kiểu gõ**, gồm có **Telex**, **VNI** và **VIQR**.
- **Bỏ dấu tự do**: Cho phép gõ dấu thanh và dấu mũ/móc ở bất kỳ vị trí nào trong âm tiết.
- **Tự động khôi phục từ không phải tiếng Việt**: Ngay khi kết thúc từ bằng dấu cách hoặc dấu câu, các từ chứa tổ hợp âm không thể có trong tiếng Việt (như `window`, `user`, `text`, `source`, `lexi`) sẽ tự động được trả về chuỗi phím gốc mà không cần chuyển bàn phím. Có thể bật/tắt nhanh ngay trên thanh menu / khay hệ thống hoặc trong **Cài đặt → Bỏ dấu & Chính tả**.
- **Khôi phục thủ công bằng hai phím Shift**: Nhấn đồng thời **⇧ Trái + ⇧ Phải** (ngay trong lúc gõ hoặc ngay cả khi đã nhấn dấu cách/dấu câu kết thúc từ, ví dụ `roof ` → `rồ `) để trả từ vừa gõ về ký tự gốc.
- **Bật/tắt nhanh bộ gõ**: Chọn **Tắt** ngay trên menu thanh trạng thái (hoặc trong **Cài đặt → Thường gặp**), hay nhấn đồng thời **⌘ Trái + ⌘ Phải** (macOS) / **Ctrl Trái + Ctrl Phải** (Windows) để chuyển nhanh giữa **Tắt** và kiểu gõ tiếng Việt đang dùng.
- **Tối ưu hoá việc sửa từ tại con trỏ**: Quay lại sửa chữ trên cùng một dòng dễ dàng (kể cả trong Terminal). Với các ứng dụng hỗ trợ Trợ năng (Accessibility / Windows UI Automation), chỉ cần đặt con trỏ vào từ bất kỳ để sửa dấu.

### 2. Bỏ dấu & Chính tả
Có thể bật/tắt riêng từng quy tắc trong mục **Bỏ dấu & Chính tả**:
- **Đặt dấu theo chính tả kiểu mới**: `hoà`, `khoẻ`, `thuý` thay vì `hòa`, `khỏe`, `thúy`.
- **Tự động khôi phục từ không phải tiếng Việt**: Mặc định bật, giúp gõ tiếng Anh xen kẽ tự nhiên.
- **Tự động sửa chính tả & nguyên âm đôi**:
  - `"ưo"` thành `"ươ"`
  - `"ng + i/e"` thành `"nghi/nghe"`
  - `"ngh + a/o/u"` thành `"nga/ngo/ngu"`

### 3. Tích hợp hệ thống
- **macOS**: Chạy hoàn toàn trên menu bar, tích hợp với bàn phím macOS (cảnh báo và tránh xung đột khi không dùng U.S./ABC).
- **Windows**: Ứng dụng Win32 C++23 thuần (không phụ thuộc .NET runtime), chạy gọn trên khay hệ thống với giao diện Fluent hỗ trợ chế độ Sáng/Tối, tự động phát hiện bố cục bàn phím hệ thống, đọc văn bản quanh con trỏ qua UI Automation và tự kiểm tra cập nhật.

### 4. Giấy phép
Bản quyền (C) 2026 `lnson`. Phân phối theo giấy phép [GNU General Public License v3.0](LICENSE).

---

## English

**ÔKêyTre** is an Input Method Engine (IME) designed to make Vietnamese typing on **macOS** and **Windows** fast, accurate, and effortless. Key features include:

### 1. Input Methods & Seamless English Typing
- **Multiple input styles**, including **Telex**, **VNI**, and **VIQR**.
- **Free-order diacritics**: Type tone marks and vowel modifiers at any position within a syllable.
- **Auto-restore non-Vietnamese words**: As soon as you end a word with Space or punctuation, words containing combinations impossible in Vietnamese (such as `window`, `user`, `text`, `source`, `lexi`) are automatically restored to their raw typed characters without switching keyboards. Toggleable directly from the menu bar / system tray or **Settings → Tones & Spelling**.
- **Ad-hoc restore via dual Shift**: Press **Left ⇧ + Right ⇧** simultaneously—either mid-word or even right after ending a word with Space/punctuation (e.g., `roof ` → `rồ `)—to restore the raw typed word on demand.
- **Quickly turn Vietnamese input off/on**: Choose **Off** from the menu bar / system tray menu (or **Settings → General**), or press **Left ⌘ + Right ⌘** (macOS) / **Left Ctrl + Right Ctrl** (Windows) simultaneously to toggle between **Off** and your active Vietnamese input style.
- **Streamlined cursor word editing**: Backspace into earlier words on the same line (even in Terminal). In Accessibility / Windows UI Automation enabled apps, click directly inside any word to edit its diacritics.

### 2. Tones & Spelling
Each rule can be toggled individually under **Tones & Spelling**:
- **Modern Vietnamese orthography**: `hoà`, `khoẻ`, `thuý` instead of `hòa`, `khỏe`, `thúy`.
- **Auto-restore non-Vietnamese words**: Enabled by default for seamless bilingual Vietnamese/English typing.
- **Spelling & diphthong auto-correction**:
  - `"ưo"` to `"ươ"`
  - `"ng + i/e"` to `"nghi/nghe"`
  - `"ngh + a/o/u"` to `"nga/ngo/ngu"`

### 3. System Integration
- **macOS**: Lives entirely in the menu bar and detects non-U.S./ABC keyboard layouts to prevent input conflicts.
- **Windows**: Native pure C++23 Win32 desktop application (zero .NET runtime dependency) with a system tray icon, Fluent Light/Dark Settings & Feature Showcase windows, Windows UI Automation caret reader, keyboard layout detection, and background update checks.

### 4. Building from Source
- **macOS**:
  ```zsh
  bazel test //OKeyTre/ime/core/... //imec/... //invc/... //windows/...
  bazel build --config=release //:OKeyTreApp
  ```
- **Windows** (requires Visual Studio 2022 C++ Build Tools + Windows 10/11 SDK, Bazel/Bazelisk, and `win_flex`/`win_bison` on `PATH`):
  ```powershell
  .\scripts\build-windows.ps1 -Release -Package
  ```
  Or to build, verify static CRT, package, and publish to GitHub Releases:
  ```powershell
  .\scripts\release-windows.ps1 -Publish
  ```
  Or directly via Bazel:
  ```powershell
  bazel test //OKeyTre/ime/core/... //imec/... //invc/... //windows/...
  bazel build --config=release --features=static_link_msvcrt //:OKeyTreWindows
  ```

### 5. License
Copyright (C) 2026 `lnson`. Licensed under the [GNU General Public License v3.0](LICENSE).
