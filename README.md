# ÔKêyTre

**[Tiếng Việt](#tiếng-việt) · [English](#english)**

---

## Tiếng Việt

**ÔKêyTre** ra đời nhằm cải thiện trải nghiệm gõ tiếng Việt trên Mac. Các tính năng chính bao gồm:

### 1. Kiểu gõ & Gõ xen kẽ tiếng Anh
- **Hỗ trợ nhiều kiểu gõ**, gồm có **Telex**, **VNI** và **VIQR**.
- **Bỏ dấu tự do**: Cho phép gõ dấu thanh và dấu mũ/móc ở bất kỳ vị trí nào trong âm tiết.
- **Tự động khôi phục từ không phải tiếng Việt**: Ngay khi kết thúc từ bằng dấu cách hoặc dấu câu, các từ chứa tổ hợp âm không thể có trong tiếng Việt (như `window`, `user`, `text`, `source`, `lexi`) sẽ tự động được trả về chuỗi phím gốc mà không cần chuyển bàn phím. Có thể bật/tắt nhanh ngay trên thanh menu hoặc trong **Cài đặt → Bỏ dấu & Chính tả**.
- **Khôi phục thủ công bằng hai phím Shift**: Nhấn đồng thời **⇧ Trái + ⇧ Phải** (ngay trong lúc gõ hoặc ngay cả khi đã nhấn dấu cách/dấu câu kết thúc từ, ví dụ `roof ` → `rồ `) để trả từ vừa gõ về ký tự gốc.
- **Bật/tắt nhanh bộ gõ**: Chọn **Tắt** ngay trên menu thanh trạng thái (hoặc trong **Cài đặt → Thường gặp**), hay nhấn đồng thời **⌘ Trái + ⌘ Phải** để chuyển nhanh giữa **Tắt** và kiểu gõ tiếng Việt đang dùng.
- **Tối ưu hoá việc sửa từ tại con trỏ**: Quay lại sửa chữ trên cùng một dòng dễ dàng (kể cả trong Terminal). Với các ứng dụng hỗ trợ Trợ năng, chỉ cần đặt con trỏ vào từ bất kỳ để sửa dấu.

### 2. Bỏ dấu & Chính tả
Có thể bật/tắt riêng từng quy tắc trong mục **Bỏ dấu & Chính tả**:
- **Đặt dấu theo chính tả kiểu mới**: `hoà`, `khoẻ`, `thuý` thay vì `hòa`, `khỏe`, `thúy`.
- **Tự động khôi phục từ không phải tiếng Việt**: Mặc định bật, giúp gõ tiếng Anh xen kẽ tự nhiên.
- **Tự động sửa chính tả & nguyên âm đôi**:
  - `"ưo"` thành `"ươ"`
  - `"ng + i/e"` thành `"nghi/nghe"`
  - `"ngh + a/o/u"` thành `"nga/ngo/ngu"`

### 3. Tích hợp hệ thống
- **Chạy hoàn toàn trên menu bar**: Giống hoàn toàn với trải nghiệm bàn phím gốc của macOS.
- **Tích hợp với bàn phím của Mac**: Cảnh báo và tránh xung đột khi bàn phím hệ thống không phải U.S./ABC.

### 4. Giấy phép
Bản quyền (C) 2026 `lnson`. Phân phối theo giấy phép [GNU General Public License v3.0](LICENSE).

---

## English

**ÔKêyTre** is an Input Method Engine (IME) designed to make Vietnamese typing on macOS fast, accurate, and effortless. Key features include:

### 1. Input Methods & Seamless English Typing
- **Multiple input styles**, including **Telex**, **VNI**, and **VIQR**.
- **Free-order diacritics**: Type tone marks and vowel modifiers at any position within a syllable.
- **Auto-restore non-Vietnamese words**: As soon as you end a word with Space or punctuation, words containing combinations impossible in Vietnamese (such as `window`, `user`, `text`, `source`, `lexi`) are automatically restored to their raw typed characters without switching keyboards. Toggleable directly from the menu bar or **Settings → Tones & Spelling**.
- **Ad-hoc restore via dual Shift**: Press **Left ⇧ + Right ⇧** simultaneously—either mid-word or even right after ending a word with Space/punctuation (e.g., `roof ` → `rồ `)—to restore the raw typed word on demand.
- **Quickly turn Vietnamese input off/on**: Choose **Off** from the menu bar menu (or **Settings → General**), or press **Left ⌘ + Right ⌘** simultaneously to toggle between **Off** and your active Vietnamese input style.
- **Streamlined cursor word editing**: Backspace into earlier words on the same line (even in Terminal). In Accessibility-enabled apps, click directly inside any word to edit its diacritics.

### 2. Tones & Spelling
Each rule can be toggled individually under **Tones & Spelling**:
- **Modern Vietnamese orthography**: `hoà`, `khoẻ`, `thuý` instead of `hòa`, `khỏe`, `thúy`.
- **Auto-restore non-Vietnamese words**: Enabled by default for seamless bilingual Vietnamese/English typing.
- **Spelling & diphthong auto-correction**:
  - `"ưo"` to `"ươ"`
  - `"ng + i/e"` to `"nghi/nghe"`
  - `"ngh + a/o/u"` to `"nga/ngo/ngu"`

### 3. System Integration
- **Lives entirely in the menu bar**: Matches the native macOS input source experience.
- **Integrates with macOS keyboard layouts**: Detects non-U.S./ABC layouts to prevent input conflicts.

### 4. License
Copyright (C) 2026 `lnson`. Licensed under the [GNU General Public License v3.0](LICENSE).
