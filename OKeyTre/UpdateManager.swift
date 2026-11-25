// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

import AppKit
import Combine
import Foundation
import SwiftUI

nonisolated struct AppVersion: Comparable, Equatable, Hashable, CustomStringConvertible, Sendable {
    enum PrereleaseIdentifier: Comparable, Equatable, Hashable, Sendable {
        case numeric(Int)
        case alphanumeric(String)

        static func < (lhs: PrereleaseIdentifier, rhs: PrereleaseIdentifier) -> Bool {
            switch (lhs, rhs) {
            case let (.numeric(a), .numeric(b)):
                return a < b
            case (.numeric, .alphanumeric):
                // Per SemVer 2.0.0, numeric identifiers have lower precedence than non-numeric.
                return true
            case (.alphanumeric, .numeric):
                return false
            case let (.alphanumeric(a), .alphanumeric(b)):
                return a < b
            }
        }
    }

    let numbers: [Int]
    let prerelease: [PrereleaseIdentifier]
    let description: String

    init?(_ raw: String) {
        var trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.hasPrefix("v") || trimmed.hasPrefix("V") {
            trimmed.removeFirst()
        }
        guard !trimmed.isEmpty else { return nil }

        // Strip optional +build metadata.
        let withoutBuild = trimmed.split(separator: "+", maxSplits: 1, omittingEmptySubsequences: false)[0]
        guard !withoutBuild.isEmpty else { return nil }

        let dashParts = withoutBuild.split(separator: "-", maxSplits: 1, omittingEmptySubsequences: false)
        let corePart = dashParts[0]
        guard !corePart.isEmpty else { return nil }

        let numberStrings = corePart.split(separator: ".", omittingEmptySubsequences: false)
        var parsedNumbers: [Int] = []
        parsedNumbers.reserveCapacity(numberStrings.count)
        for token in numberStrings {
            guard !token.isEmpty,
                  token.allSatisfy(\.isASCII),
                  let value = Int(token),
                  value >= 0 else {
                return nil
            }
            parsedNumbers.append(value)
        }

        var parsedPrerelease: [PrereleaseIdentifier] = []
        var prereleaseText = ""
        if dashParts.count == 2 {
            let prePart = dashParts[1]
            guard !prePart.isEmpty else { return nil }
            let preTokens = prePart.split(separator: ".", omittingEmptySubsequences: false)
            parsedPrerelease.reserveCapacity(preTokens.count)
            for token in preTokens {
                guard !token.isEmpty else { return nil }
                let str = String(token)
                if token.allSatisfy(\.isNumber), let num = Int(str) {
                    parsedPrerelease.append(.numeric(num))
                } else {
                    parsedPrerelease.append(.alphanumeric(str))
                }
            }
            prereleaseText = "-\(prePart)"
        }

        self.numbers = parsedNumbers
        self.prerelease = parsedPrerelease
        self.description = parsedNumbers.map(String.init).joined(separator: ".") + prereleaseText
    }

    private var normalizedCore: [Int] {
        var trimmed = numbers
        while trimmed.count > 1 && trimmed.last == 0 {
            trimmed.removeLast()
        }
        return trimmed
    }

    static func == (lhs: AppVersion, rhs: AppVersion) -> Bool {
        lhs.normalizedCore == rhs.normalizedCore && lhs.prerelease == rhs.prerelease
    }

    func hash(into hasher: inout Hasher) {
        hasher.combine(normalizedCore)
        hasher.combine(prerelease)
    }

    static func < (lhs: AppVersion, rhs: AppVersion) -> Bool {
        let count = max(lhs.numbers.count, rhs.numbers.count)
        for index in 0..<count {
            let left = index < lhs.numbers.count ? lhs.numbers[index] : 0
            let right = index < rhs.numbers.count ? rhs.numbers[index] : 0
            if left != right {
                return left < right
            }
        }

        if lhs.prerelease.isEmpty && rhs.prerelease.isEmpty {
            return false
        }
        if lhs.prerelease.isEmpty {
            // Normal release has higher precedence than a prerelease of the same core version.
            return false
        }
        if rhs.prerelease.isEmpty {
            return true
        }

        let preCount = min(lhs.prerelease.count, rhs.prerelease.count)
        for index in 0..<preCount where lhs.prerelease[index] != rhs.prerelease[index] {
            return lhs.prerelease[index] < rhs.prerelease[index]
        }
        return lhs.prerelease.count < rhs.prerelease.count
    }
}

nonisolated struct UpdateRelease: Equatable, Sendable {
    let tagName: String
    let version: AppVersion
    let releasePageURL: URL
    let dmgDownloadURL: URL?

    var displayVersion: String {
        version.description
    }
}

nonisolated enum UpdateCheckError: Error, Equatable, Sendable {
    case invalidResponse
    case unexpectedStatusCode(Int)
}

private nonisolated struct GitHubReleasePayload: Decodable {
    struct Asset: Decodable {
        let name: String
        let browserDownloadURL: String

        enum CodingKeys: String, CodingKey {
            case name
            case browserDownloadURL = "browser_download_url"
        }
    }

    let tagName: String
    let htmlURL: String
    let draft: Bool?
    let prerelease: Bool?
    let assets: [Asset]?

    enum CodingKeys: String, CodingKey {
        case tagName = "tag_name"
        case htmlURL = "html_url"
        case draft
        case prerelease
        case assets
    }
}

final class UpdateManager: ObservableObject {
    typealias DataLoader = @Sendable (URLRequest) async throws -> (Data, URLResponse)

    static let shared = UpdateManager()

    nonisolated static let autoCheckDefaultsKey = "automaticallyCheckForUpdates"
    nonisolated static let defaultAutoCheck = true
    nonisolated static let lastCheckTimestampDefaultsKey = "lastUpdateCheckTimestamp"
    nonisolated static let skippedVersionDefaultsKey = "skippedUpdateVersion"

    nonisolated static let checkInterval: TimeInterval = 24 * 60 * 60
    nonisolated static let retryInterval: TimeInterval = 60 * 60
    nonisolated static let latestReleaseAPIURL = URL(
        string: "https://api.github.com/repos/lnson/OKeyTreIME/releases/latest"
    )!
    nonisolated static let releasesWebURL = URL(
        string: "https://github.com/lnson/OKeyTreIME/releases/latest"
    )!

    static let currentAppVersionString: String = {
        if let version = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String,
           !version.isEmpty,
           !version.hasPrefix("$(") {
            return version
        }
        return "0.3.2"
    }()

    @Published private(set) var availableRelease: UpdateRelease?
    @Published private(set) var isChecking: Bool = false
    @Published private(set) var isAutoCheckEnabled: Bool
    @Published private(set) var skippedVersion: String?
    @Published private(set) var didConfirmUpToDate: Bool = false

    let currentVersion: AppVersion

    private let userDefaults: UserDefaults
    private let isAutomatedEnvironment: Bool
    private let dataLoader: DataLoader
    private let nowProvider: () -> Date
    private let openURLHandler: (URL) -> Void
    private var scheduleTask: Task<Void, Never>?
    private var upToDateResetTask: Task<Void, Never>?
    private var wakeObserver: NSObjectProtocol?

    init(
        userDefaults: UserDefaults = .standard,
        currentVersion: AppVersion? = nil,
        isAutomatedEnvironment: Bool? = nil,
        dataLoader: @escaping DataLoader = { request in
            try await URLSession.shared.data(for: request)
        },
        nowProvider: @escaping () -> Date = Date.init,
        openURLHandler: @escaping (URL) -> Void = { url in
            NSWorkspace.shared.open(url)
        }
    ) {
        self.isAutomatedEnvironment = isAutomatedEnvironment ?? ProcessInfo.isAutomatedEnvironment
        self.userDefaults = userDefaults
        self.currentVersion = currentVersion
            ?? AppVersion(Self.currentAppVersionString)
            ?? AppVersion("0.3.2")!
        self.dataLoader = dataLoader
        self.nowProvider = nowProvider
        self.openURLHandler = openURLHandler

        self.isAutoCheckEnabled = Self.resolvedAutoCheckEnabled(from: userDefaults)
        self.skippedVersion = userDefaults.string(forKey: Self.skippedVersionDefaultsKey)

        if !self.isAutomatedEnvironment {
            observeSystemWake()
        }
    }

    static func resolvedAutoCheckEnabled(from defaults: UserDefaults = .standard) -> Bool {
        defaults.object(forKey: autoCheckDefaultsKey) as? Bool ?? defaultAutoCheck
    }

    deinit {
        scheduleTask?.cancel()
        upToDateResetTask?.cancel()
        if let wakeObserver {
            NSWorkspace.shared.notificationCenter.removeObserver(wakeObserver)
        }
    }

    var visibleAvailableRelease: UpdateRelease? {
        guard let release = availableRelease else { return nil }
        if let skippedVersion,
           let skipped = AppVersion(skippedVersion),
           release.version <= skipped {
            return nil
        }
        return release
    }

    var autoCheckBinding: Binding<Bool> {
        Binding(
            get: { self.isAutoCheckEnabled },
            set: { self.setAutoCheckEnabled($0) }
        )
    }

    func setAutoCheckEnabled(_ enabled: Bool) {
        guard isAutoCheckEnabled != enabled else { return }
        isAutoCheckEnabled = enabled
        userDefaults.set(enabled, forKey: Self.autoCheckDefaultsKey)
        if enabled {
            startAutomaticChecksIfNeeded(initialDelay: 0)
        } else {
            scheduleTask?.cancel()
            scheduleTask = nil
        }
    }

    func startAutomaticChecksIfNeeded(initialDelay: TimeInterval = 3.0) {
        guard !isAutomatedEnvironment, isAutoCheckEnabled else { return }
        scheduleTask?.cancel()
        scheduleTask = Task { [weak self] in
            if initialDelay > 0 {
                try? await Task.sleep(for: .seconds(initialDelay))
            }
            while !Task.isCancelled {
                guard let self, self.isAutoCheckEnabled else { return }
                let waitSeconds = self.secondsUntilNextAutomaticCheck()
                if waitSeconds > 0 {
                    try? await Task.sleep(for: .seconds(waitSeconds))
                    if Task.isCancelled { return }
                }
                guard self.isAutoCheckEnabled else { return }
                let succeeded = await self.performCheck(userInitiated: false)
                if !succeeded {
                    try? await Task.sleep(for: .seconds(Self.retryInterval))
                }
            }
        }
    }

    func secondsUntilNextAutomaticCheck() -> TimeInterval {
        let lastCheck = userDefaults.double(forKey: Self.lastCheckTimestampDefaultsKey)
        guard lastCheck > 0 else { return 0 }
        let elapsed = nowProvider().timeIntervalSince1970 - lastCheck
        return max(0, Self.checkInterval - elapsed)
    }

    @discardableResult
    func checkIfDueInBackground() async -> Bool {
        guard isAutoCheckEnabled, secondsUntilNextAutomaticCheck() <= 0 else {
            return false
        }
        return await performCheck(userInitiated: false)
    }

    func checkForUpdatesNow() {
        Task {
            _ = await performCheck(userInitiated: true)
        }
    }

    @discardableResult
    func performCheck(userInitiated: Bool) async -> Bool {
        guard !isChecking else { return false }
        isChecking = true
        if userInitiated {
            didConfirmUpToDate = false
            upToDateResetTask?.cancel()
        }
        defer { isChecking = false }

        let request = Self.makeReleaseRequest(
            currentVersion: currentVersion.description,
            ignoreCache: userInitiated
        )
        let loader = dataLoader
        let installedVersion = currentVersion

        do {
            let (data, response) = try await loader(request)
            guard let http = response as? HTTPURLResponse else {
                throw UpdateCheckError.invalidResponse
            }

            if http.statusCode == 404 {
                // Repository has no published GitHub Release yet.
                recordSuccessfulCheckTimestamp()
                availableRelease = nil
                if userInitiated {
                    markUpToDateBriefly()
                }
                return true
            }

            guard (200...299).contains(http.statusCode) else {
                throw UpdateCheckError.unexpectedStatusCode(http.statusCode)
            }

            let release = try Self.parseLatestRelease(from: data, currentVersion: installedVersion)
            recordSuccessfulCheckTimestamp()
            availableRelease = release

            if userInitiated {
                if release != nil {
                    // Explicit manual check surfaces the release even if previously skipped.
                    clearSkippedVersion()
                } else {
                    markUpToDateBriefly()
                }
            }
            return true
        } catch {
            return false
        }
    }

    func openReleasePage(_ release: UpdateRelease? = nil) {
        let targetURL = (release ?? availableRelease)?.releasePageURL ?? Self.releasesWebURL
        openURLHandler(targetURL)
    }

    func skipRelease(_ release: UpdateRelease) {
        let versionString = release.version.description
        skippedVersion = versionString
        userDefaults.set(versionString, forKey: Self.skippedVersionDefaultsKey)
    }

    func clearSkippedVersion() {
        skippedVersion = nil
        userDefaults.removeObject(forKey: Self.skippedVersionDefaultsKey)
    }

    nonisolated static func makeReleaseRequest(
        currentVersion: String,
        ignoreCache: Bool
    ) -> URLRequest {
        var request = URLRequest(
            url: latestReleaseAPIURL,
            cachePolicy: ignoreCache ? .reloadIgnoringLocalCacheData : .useProtocolCachePolicy,
            timeoutInterval: 15
        )
        request.httpMethod = "GET"
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("2022-11-28", forHTTPHeaderField: "X-GitHub-Api-Version")
        request.setValue("OKeyTre/\(currentVersion)", forHTTPHeaderField: "User-Agent")
        return request
    }

    nonisolated static func parseLatestRelease(
        from data: Data,
        currentVersion: AppVersion
    ) throws -> UpdateRelease? {
        let payload = try JSONDecoder().decode(GitHubReleasePayload.self, from: data)
        if payload.draft == true || payload.prerelease == true {
            return nil
        }
        guard let latestVersion = AppVersion(payload.tagName),
              latestVersion > currentVersion else {
            return nil
        }

        let releasePageURL = validatedGitHubURL(payload.htmlURL) ?? releasesWebURL
        let dmgURL = payload.assets?
            .first(where: { $0.name.lowercased().hasSuffix(".dmg") })
            .flatMap { validatedGitHubURL($0.browserDownloadURL) }

        return UpdateRelease(
            tagName: payload.tagName,
            version: latestVersion,
            releasePageURL: releasePageURL,
            dmgDownloadURL: dmgURL
        )
    }

    private nonisolated static func validatedGitHubURL(_ raw: String) -> URL? {
        guard let url = URL(string: raw),
              url.scheme?.lowercased() == "https",
              let host = url.host?.lowercased(),
              host == "github.com"
                  || host.hasSuffix(".github.com")
                  || host.hasSuffix(".githubusercontent.com") else {
            return nil
        }
        return url
    }

    private func recordSuccessfulCheckTimestamp() {
        userDefaults.set(nowProvider().timeIntervalSince1970, forKey: Self.lastCheckTimestampDefaultsKey)
    }

    private func markUpToDateBriefly() {
        didConfirmUpToDate = true
        upToDateResetTask?.cancel()
        upToDateResetTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(4))
            guard !Task.isCancelled else { return }
            self?.didConfirmUpToDate = false
        }
    }

    private func observeSystemWake() {
        wakeObserver = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didWakeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            guard let self, self.isAutoCheckEnabled else { return }
            self.startAutomaticChecksIfNeeded(initialDelay: 2.0)
        }
    }
}
