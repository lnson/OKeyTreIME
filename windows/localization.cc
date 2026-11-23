// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/localization.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace okeytre::win {

namespace {

std::string ToLowerAscii(std::string_view s) {
  std::string out(s.size(), '\0');
  for (size_t i = 0; i < s.size(); ++i) {
    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  }
  return out;
}

std::vector<std::string> GetSystemPreferredLanguages() {
  std::vector<std::string> result;
#ifdef _WIN32
  ULONG num_languages = 0;
  ULONG buffer_size = 0;
  if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num_languages, nullptr,
                                  &buffer_size) &&
      buffer_size > 1) {
    std::wstring buf(buffer_size, L'\0');
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num_languages,
                                    buf.data(), &buffer_size)) {
      const wchar_t* p = buf.c_str();
      while (*p != L'\0') {
        std::wstring_view lang(p);
        result.push_back(WideToUtf8(lang));
        p += lang.size() + 1;
      }
    }
  }
#endif
  return result;
}

const std::unordered_map<std::string_view, std::wstring_view>& EnglishStrings() {
  static const std::unordered_map<std::string_view, std::wstring_view> kMap = {
      // System Tray Menu
      {"menu.input_source_warning.title", L"⚠️ Keyboard: %@"},
      {"menu.input_source_warning.subtitle",
       L"Recommended: English (U.S.) keyboard"},
      {"menu.input_style", L"Input Method"},
      {"menu.input_style.off", L"Off"},
      {"menu.tooltip.mode", L"ÔKêyTre: %@"},
      {"menu.restore_invalid_words", L"Auto-Restore Non-Vietnamese Words"},
      {"menu.launch_at_login", L"Launch at Login"},
      {"menu.auto_check_updates", L"Auto-Check for Updates"},
      {"menu.update_available.title", L"✨ New Version Available: %@"},
      {"menu.language", L"Language"},
      {"menu.showcase", L"Feature Showcase..."},
      {"menu.settings", L"Settings..."},
      {"menu.quit", L"Quit"},

      // Shared Actions
      {"action.switch_to_us", L"Switch to U.S."},
      {"action.download_update", L"Download Update..."},
      {"action.skip_version", L"Skip This Version"},

      // Language Options
      {"language.system", L"System Default"},

      // Settings Window & Sidebar
      {"app.name", L"ÔKêyTre"},
      {"settings.window_title", L"ÔKêyTre Settings"},
      {"settings.sidebar.version", L"Version %@"},
      {"settings.sidebar.check_updates", L"Check for Updates"},
      {"settings.sidebar.checking_updates", L"Checking..."},
      {"settings.sidebar.up_to_date", L"Up to date"},
      {"settings.sidebar.update_available", L"Update to %@"},
      {"settings.section.general", L"General"},
      {"settings.section.correction", L"Tones & Spelling"},
      {"settings.section.advanced", L"Advanced"},

      // General & Update Settings
      {"settings.general.system.header", L"System"},
      {"settings.general.input_style.description",
       L"Press Left Ctrl + Right Ctrl simultaneously to quickly toggle between "
       L"Off and your active input method."},
      {"settings.general.launch_at_login.description",
       L"Automatically start ÔKêyTre in the system tray when you sign in to "
       L"Windows."},
      {"settings.general.language.description",
       L"Choose the display language for menus and settings."},
      {"settings.updates.header", L"Updates"},
      {"settings.updates.auto_check.title",
       L"Automatically check for updates"},
      {"settings.updates.auto_check.description",
       L"Check GitHub Releases once a day in the background and show a notice "
       L"when a new version is available."},
      {"settings.updates.current_version", L"Current version: %@"},
      {"settings.updates.available_status", L"New version available: %@"},
      {"settings.updates.manual_download.title", L"Manual downloads"},
      {"settings.updates.manual_download.description",
       L"Open the releases page to view release notes or download the latest "
       L"Windows release manually."},
      {"settings.updates.manual_download.link", L"GitHub Releases"},

      // Tone Placement Settings
      {"settings.orthography.header", L"Tone Placement"},
      {"settings.orthography.modern_tone.title",
       L"Use modern Vietnamese orthography (\"oà, oé, uý\")"},
      {"settings.orthography.modern_tone.description",
       L"Place tone marks on the second vowel in open \"oa\", \"oe\", and "
       L"\"uy\" syllables (e.g., hoà, khoẻ, thuý instead of hòa, khỏe, thúy)."},

      // Non-Vietnamese Word Restoration
      {"settings.english.header", L"Non-Vietnamese Words"},
      {"settings.english.restore_invalid.title",
       L"Auto-restore non-Vietnamese words"},
      {"settings.english.restore_invalid.description",
       L"Automatically restore the raw typed characters when ending a word "
       L"(with Space or punctuation) that contains combinations impossible in "
       L"Vietnamese (e.g., window, user, text, lexi)."},

      // Auto-Correction Settings
      {"settings.correction.header", L"Spelling & Diphthongs"},
      {"settings.correction.uo_horn.title",
       L"Auto-correct \"ưo\" to \"ươ\""},
      {"settings.correction.uo_horn.description",
       L"Automatically convert 'o' right after 'ư' into 'ơ' (e.g., tư + o → "
       L"tươ, như + o → nhươ)."},
      {"settings.correction.ng_to_ngh.title",
       L"Auto-correct \"ng + i/e\" to \"nghi/nghe\""},
      {"settings.correction.ng_to_ngh.description",
       L"Automatically insert 'h' when typing 'i' or 'e' right after initial "
       L"consonant 'ng' (e.g., ng + i → nghi, ng + e → nghe)."},
      {"settings.correction.ngh_to_ng.title",
       L"Auto-correct \"ngh + a/o/u\" to \"nga/ngo/ngu\""},
      {"settings.correction.ngh_to_ng.description",
       L"Automatically drop 'h' when typing 'a', 'o', or 'u' right after "
       L"initial consonant 'ngh' (e.g., ngh + a → nga, ngh + o → ngo, ngh + u "
       L"→ ngu)."},

      // Advanced Settings
      {"settings.advanced.header", L"Keystroke Buffer"},
      {"settings.advanced.buffer_size.title", L"Character buffer size"},
      {"settings.advanced.buffer_size.description",
       L"Maximum number of characters retained in the buffer to support "
       L"backspacing into previous words in apps that don't expose text around "
       L"the caret (such as Windows Terminal)."},

      // Alerts & System Input Sources
      {"input_source.unknown", L"Unknown"},
      {"alert.non_english_keyboard.title", L"Non-English System Keyboard"},
      {"alert.non_english_keyboard.message",
       L"The current keyboard is \"%@\". For ÔKêyTre to work accurately "
       L"without typing conflicts, you should switch to the English (U.S.) "
       L"keyboard."},
      {"alert.non_english_keyboard.open_keyboard_settings",
       L"Open Keyboard Settings"},
      {"alert.non_english_keyboard.ignore", L"Ignore"},

      // First-Time & Feature Update Showcase
      {"showcase.window_title", L"Hey there!"},
      {"showcase.title", L"Hey there!"},
      {"showcase.whats_new.title", L"What's New in ÔKêyTre %@"},
      {"showcase.badge.new", L"NEW"},
      {"showcase.subtitle",
       L"ÔKêyTre lives quietly in your Windows system tray with smart "
       L"shortcuts for effortless bilingual Vietnamese and English typing."},
      {"showcase.status.ready",
       L"Ready to type! Try the shortcuts in the box below."},
      {"showcase.card.diacritics.title", L"Free-order diacritics"},
      {"showcase.card.diacritics.desc",
       L"Type tone marks and vowel modifiers anywhere in a syllable, or press "
       L"z to strip marks."},
      {"showcase.card.auto_restore.title", L"Auto-restore non-Vietnamese"},
      {"showcase.card.auto_restore.desc",
       L"Words impossible in Vietnamese revert to raw characters automatically "
       L"on Space or punctuation."},
      {"showcase.card.dual_shift.title", L"Instant raw-word restore"},
      {"showcase.card.dual_shift.desc",
       L"Press both Shift keys together (mid-word or right after Space) to "
       L"restore raw characters."},
      {"showcase.card.toggle.title", L"Quick on/off toggle"},
      {"showcase.card.toggle.desc",
       L"Press both Ctrl keys together to switch between Off and your active "
       L"input style."},
      {"showcase.card.toggle.result", L"Off / On"},
      {"showcase.practice.label", L"Try typing right here"},
      {"showcase.practice.placeholder",
       L"Try typing: tieengs vieetj cho user..."},
      {"showcase.action.get_started", L"Get Started"},
  };
  return kMap;
}

const std::unordered_map<std::string_view, std::wstring_view>&
VietnameseStrings() {
  static const std::unordered_map<std::string_view, std::wstring_view> kMap = {
      // System Tray Menu
      {"menu.input_source_warning.title", L"⚠️ Bàn phím: %@"},
      {"menu.input_source_warning.subtitle",
       L"Nên dùng bàn phím tiếng Anh (U.S.)"},
      {"menu.input_style", L"Kiểu gõ"},
      {"menu.input_style.off", L"Tắt"},
      {"menu.tooltip.mode", L"ÔKêyTre: %@"},
      {"menu.restore_invalid_words",
       L"Tự động khôi phục từ không phải tiếng Việt"},
      {"menu.launch_at_login", L"Khởi động cùng hệ thống"},
      {"menu.auto_check_updates", L"Tự động kiểm tra cập nhật"},
      {"menu.update_available.title", L"✨ Đã có phiên bản mới: %@"},
      {"menu.language", L"Ngôn ngữ"},
      {"menu.showcase", L"Giới thiệu tính năng..."},
      {"menu.settings", L"Cài đặt..."},
      {"menu.quit", L"Thoát"},

      // Shared Actions
      {"action.switch_to_us", L"Chuyển sang U.S."},
      {"action.download_update", L"Tải bản cập nhật..."},
      {"action.skip_version", L"Bỏ qua phiên bản này"},

      // Language Options
      {"language.system", L"Theo hệ thống"},

      // Settings Window & Sidebar
      {"app.name", L"ÔKêyTre"},
      {"settings.window_title", L"Cài đặt ÔKêyTre"},
      {"settings.sidebar.version", L"Phiên bản %@"},
      {"settings.sidebar.check_updates", L"Kiểm tra cập nhật"},
      {"settings.sidebar.checking_updates", L"Đang kiểm tra..."},
      {"settings.sidebar.up_to_date", L"Đã là bản mới nhất"},
      {"settings.sidebar.update_available", L"Cập nhật bản %@"},
      {"settings.section.general", L"Thường gặp"},
      {"settings.section.correction", L"Bỏ dấu & Chính tả"},
      {"settings.section.advanced", L"Dành cho chuyên gia"},

      // General & Update Settings
      {"settings.general.system.header", L"Hệ thống"},
      {"settings.general.input_style.description",
       L"Nhấn đồng thời Ctrl Trái + Ctrl Phải để chuyển nhanh giữa Tắt và kiểu "
       L"gõ đang dùng."},
      {"settings.general.launch_at_login.description",
       L"Tự động mở ÔKêyTre trên khay hệ thống khi đăng nhập vào Windows."},
      {"settings.general.language.description",
       L"Chọn ngôn ngữ hiển thị cho menu và cửa sổ cài đặt."},
      {"settings.updates.header", L"Cập nhật"},
      {"settings.updates.auto_check.title", L"Tự động kiểm tra cập nhật"},
      {"settings.updates.auto_check.description",
       L"Kiểm tra phiên bản mới trên GitHub Releases mỗi ngày một lần và hiển "
       L"thị thông báo khi có bản mới."},
      {"settings.updates.current_version", L"Phiên bản hiện tại: %@"},
      {"settings.updates.available_status", L"Đã có phiên bản mới: %@"},
      {"settings.updates.manual_download.title", L"Tải cập nhật thủ công"},
      {"settings.updates.manual_download.description",
       L"Mở trang phát hành để xem ghi chú thay đổi hoặc tải trực tiếp bản "
       L"Windows mới nhất."},
      {"settings.updates.manual_download.link", L"GitHub Releases"},

      // Tone Placement Settings
      {"settings.orthography.header", L"Quy tắc đặt dấu"},
      {"settings.orthography.modern_tone.title",
       L"Đặt dấu theo chính tả kiểu mới (\"oà, oé, uý\")"},
      {"settings.orthography.modern_tone.description",
       L"Đặt dấu thanh lên nguyên âm thứ hai trong các vần mở \"oa\", \"oe\", "
       L"\"uy\" (ví dụ: hoà, khoẻ, thuý thay vì hòa, khỏe, thúy)."},

      // Non-Vietnamese Word Restoration
      {"settings.english.header", L"Từ không phải tiếng Việt"},
      {"settings.english.restore_invalid.title",
       L"Tự động khôi phục từ không phải tiếng Việt"},
      {"settings.english.restore_invalid.description",
       L"Tự động trả về chuỗi phím gốc ngay khi kết thúc từ (bằng dấu cách "
       L"hoặc dấu câu) nếu từ đó chứa tổ hợp âm không thể có trong tiếng Việt "
       L"(ví dụ: window, user, text, lexi)."},

      // Auto-Correction Settings
      {"settings.correction.header", L"Chính tả & Nguyên âm đôi"},
      {"settings.correction.uo_horn.title",
       L"Tự động sửa \"ưo\" thành \"ươ\""},
      {"settings.correction.uo_horn.description",
       L"Tự động chuyển chữ 'o' ngay sau 'ư' thành 'ơ' (ví dụ: tư + o → tươ, "
       L"như + o → nhươ)."},
      {"settings.correction.ng_to_ngh.title",
       L"Tự động sửa \"ng + i/e\" thành \"nghi/nghe\""},
      {"settings.correction.ng_to_ngh.description",
       L"Tự động thêm 'h' khi gõ 'i' hoặc 'e' ngay sau phụ âm đầu 'ng' (ví dụ: "
       L"ng + i → nghi, ng + e → nghe)."},
      {"settings.correction.ngh_to_ng.title",
       L"Tự động sửa \"ngh + a/o/u\" thành \"nga/ngo/ngu\""},
      {"settings.correction.ngh_to_ng.description",
       L"Tự động bỏ 'h' khi gõ 'a', 'o', hoặc 'u' ngay sau phụ âm đầu 'ngh' "
       L"(ví dụ: ngh + a → nga, ngh + o → ngo, ngh + u → ngu)."},

      // Advanced Settings
      {"settings.advanced.header", L"Bộ đệm gõ phím"},
      {"settings.advanced.buffer_size.title", L"Kích thước bộ đệm ký tự"},
      {"settings.advanced.buffer_size.description",
       L"Số ký tự tối đa được lưu trong bộ đệm để hỗ trợ xóa lùi (Backspace) "
       L"về các từ trước đó trong những ứng dụng không hỗ trợ đọc văn bản "
       L"quanh con trỏ (như Windows Terminal)."},

      // Alerts & System Input Sources
      {"input_source.unknown", L"Không rõ"},
      {"alert.non_english_keyboard.title",
       L"Bàn phím hệ thống không phải tiếng Anh"},
      {"alert.non_english_keyboard.message",
       L"Bàn phím hiện tại là \"%@\". Để ÔKêyTre hoạt động chính xác và không "
       L"bị xung đột gõ, bạn nên chuyển sang bàn phím tiếng Anh (U.S.)."},
      {"alert.non_english_keyboard.open_keyboard_settings",
       L"Mở Cài đặt Bàn phím"},
      {"alert.non_english_keyboard.ignore", L"Bỏ qua"},

      // First-Time & Feature Update Showcase
      {"showcase.window_title", L"ÔKêyTre chào bạn"},
      {"showcase.title", L"ÔKêyTre chào bạn"},
      {"showcase.whats_new.title", L"Có gì mới trong ÔKêyTre %@"},
      {"showcase.badge.new", L"MỚI"},
      {"showcase.subtitle",
       L"Bộ gõ tiếng Việt nằm gọn trên khay hệ thống Windows với các phím tắt "
       L"thông minh giúp gõ xen kẽ tiếng Việt và tiếng Anh tự nhiên."},
      {"showcase.status.ready",
       L"Sẵn sàng gõ tiếng Việt! Hãy thử ngay tại ô bên dưới."},
      {"showcase.card.diacritics.title", L"Bỏ dấu tự do"},
      {"showcase.card.diacritics.desc",
       L"Gõ dấu thanh và dấu mũ/móc ở bất kỳ vị trí nào trong âm tiết, nhấn z "
       L"để xoá dấu."},
      {"showcase.card.auto_restore.title", L"Tự động khôi phục từ ngoại ngữ"},
      {"showcase.card.auto_restore.desc",
       L"Tự động trả từ không phải tiếng Việt về ký tự gốc ngay khi nhấn dấu "
       L"cách hoặc dấu câu."},
      {"showcase.card.dual_shift.title", L"Khôi phục từ gốc tức thì"},
      {"showcase.card.dual_shift.desc",
       L"Nhấn đồng thời hai phím Shift (trong lúc gõ hoặc ngay sau dấu cách) "
       L"để trả về từ gốc."},
      {"showcase.card.toggle.title", L"Bật / tắt nhanh bộ gõ"},
      {"showcase.card.toggle.desc",
       L"Nhấn đồng thời hai phím Ctrl để chuyển nhanh giữa Tắt và kiểu gõ đang "
       L"chọn."},
      {"showcase.card.toggle.result", L"Tắt / Bật"},
      {"showcase.practice.label", L"Gõ thử ngay tại đây"},
      {"showcase.practice.placeholder",
       L"Thử gõ: tieengs vieetj cho user..."},
      {"showcase.action.get_started", L"Bắt đầu sử dụng"},
  };
  return kMap;
}

}  // namespace

std::string_view AppLanguageToRaw(AppLanguage language) noexcept {
  switch (language) {
    case AppLanguage::kSystem:
      return "system";
    case AppLanguage::kVietnamese:
      return "vi";
    case AppLanguage::kEnglish:
      return "en";
  }
  return "system";
}

AppLanguage AppLanguageFromRaw(std::string_view raw) noexcept {
  if (raw == "vi") return AppLanguage::kVietnamese;
  if (raw == "en") return AppLanguage::kEnglish;
  return AppLanguage::kSystem;
}

std::string_view ResolveLanguageCode(
    AppLanguage language,
    const std::vector<std::string>& preferred_languages) {
  switch (language) {
    case AppLanguage::kVietnamese:
      return "vi";
    case AppLanguage::kEnglish:
      return "en";
    case AppLanguage::kSystem:
      for (const std::string& identifier : preferred_languages) {
        const std::string lower = ToLowerAscii(identifier);
        if (lower == "en" || lower.starts_with("en-") ||
            lower.starts_with("en_")) {
          return "en";
        }
        if (lower == "vi" || lower.starts_with("vi-") ||
            lower.starts_with("vi_")) {
          return "vi";
        }
      }
      return "vi";
  }
  return "vi";
}

std::string_view ResolveSystemLanguageCode(AppLanguage language) {
  if (language == AppLanguage::kVietnamese) return "vi";
  if (language == AppLanguage::kEnglish) return "en";
  return ResolveLanguageCode(language, GetSystemPreferredLanguages());
}

std::wstring Localized(AppLanguage language, std::string_view key) {
  const std::string_view code = ResolveSystemLanguageCode(language);
  const auto& table = (code == "en") ? EnglishStrings() : VietnameseStrings();
  if (const auto it = table.find(key); it != table.end()) {
    return std::wstring(it->second);
  }
  const auto& fallback = EnglishStrings();
  if (const auto it = fallback.find(key); it != fallback.end()) {
    return std::wstring(it->second);
  }
  return Utf8ToWide(key);
}

std::wstring LocalizedFormat(AppLanguage language, std::string_view key,
                             std::wstring_view arg) {
  std::wstring result = Localized(language, key);
  const size_t pos = result.find(L"%@");
  if (pos != std::wstring::npos) {
    result.replace(pos, 2, arg);
  }
  return result;
}

std::wstring LanguageDisplayName(AppLanguage option,
                                 AppLanguage active_language) {
  switch (option) {
    case AppLanguage::kSystem:
      return Localized(active_language, "language.system");
    case AppLanguage::kVietnamese:
      return L"Tiếng Việt";
    case AppLanguage::kEnglish:
      return L"English";
  }
  return Localized(active_language, "language.system");
}

std::wstring Utf8ToWide(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
#ifdef _WIN32
  const int needed = MultiByteToWideChar(
      CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      out.data(), needed);
  return out;
#else
  std::wstring out;
  out.reserve(utf8.size());
  size_t i = 0;
  while (i < utf8.size()) {
    const unsigned char b0 = static_cast<unsigned char>(utf8[i]);
    uint32_t cp = 0xFFFD;
    if (b0 < 0x80) {
      cp = b0;
      i += 1;
    } else if ((b0 & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
      cp = ((b0 & 0x1Fu) << 6) |
           (static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
      i += 2;
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
      cp = ((b0 & 0x0Fu) << 12) |
           ((static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu) << 6) |
           (static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu);
      i += 3;
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
      cp = ((b0 & 0x07u) << 18) |
           ((static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu) << 12) |
           ((static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu) << 6) |
           (static_cast<unsigned char>(utf8[i + 3]) & 0x3Fu);
      i += 4;
    } else {
      i += 1;
    }
    out.push_back(static_cast<wchar_t>(cp));
  }
  return out;
#endif
}

std::string WideToUtf8(std::wstring_view wide) {
  if (wide.empty()) {
    return {};
  }
#ifdef _WIN32
  const int needed = WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
      nullptr, nullptr);
  if (needed <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      out.data(), needed, nullptr, nullptr);
  return out;
#else
  std::string out;
  out.reserve(wide.size());
  for (wchar_t wc : wide) {
    const uint32_t cp = static_cast<uint32_t>(wc);
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
#endif
}

}  // namespace okeytre::win
