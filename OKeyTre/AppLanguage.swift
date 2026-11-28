// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import Foundation

enum AppLanguage: String, CaseIterable, Identifiable {
    case system = "system"
    case vietnamese = "vi"
    case english = "en"

    static let defaultsKey = "appLanguage"

    var id: String { rawValue }

    /// Resolves `.system` against `preferredLanguages`, matching the first English or Vietnamese
    /// entry and falling back to Vietnamese (`"vi"`) when neither is present.
    func resolvedLanguageCode(
        preferredLanguages: [String] = Locale.preferredLanguages
    ) -> String {
        switch self {
        case .vietnamese:
            return "vi"
        case .english:
            return "en"
        case .system:
            for identifier in preferredLanguages {
                let lower = identifier.lowercased()
                if lower == "en" || lower.hasPrefix("en-") || lower.hasPrefix("en_") {
                    return "en"
                }
                if lower == "vi" || lower.hasPrefix("vi-") || lower.hasPrefix("vi_") {
                    return "vi"
                }
            }
            return "vi"
        }
    }

    var locale: Locale {
        Locale(identifier: resolvedLanguageCode())
    }

    func displayName(in activeLanguage: AppLanguage) -> String {
        switch self {
        case .system:
            return activeLanguage.localized("language.system")
        case .vietnamese:
            return "Tiếng Việt"
        case .english:
            return "English"
        }
    }

    private static let bundles: [String: Bundle] = {
        var map: [String: Bundle] = [:]
        for code in ["en", "vi"] {
            if let path = Bundle.main.path(forResource: code, ofType: "lproj"),
               let bundle = Bundle(path: path) {
                map[code] = bundle
            }
        }
        return map
    }()

    func localized(_ key: String) -> String {
        let code = resolvedLanguageCode()
        guard let bundle = Self.bundles[code] else {
            return Bundle.main.localizedString(forKey: key, value: nil, table: "Localizable")
        }
        return bundle.localizedString(forKey: key, value: nil, table: "Localizable")
    }

    func localized(_ key: String, _ args: CVarArg...) -> String {
        let format = localized(key)
        return String(format: format, locale: locale, arguments: args)
    }

    static var current: AppLanguage {
        UserDefaults.standard.string(forKey: defaultsKey)
            .flatMap(AppLanguage.init(rawValue:)) ?? .system
    }
}

extension ProcessInfo {
    nonisolated static let isAutomatedEnvironment: Bool = {
        let env = processInfo.environment
        return env["XCODE_RUNNING_FOR_PREVIEWS"] == "1"
            || env["XCTestConfigurationFilePath"] != nil
            || env["XCTestBundlePath"] != nil
            || env["XCTestSessionIdentifier"] != nil
    }()
}
