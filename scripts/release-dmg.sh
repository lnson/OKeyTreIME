#!/bin/zsh
# Copyright (C) 2026 lnson
# SPDX-License-Identifier: GPL-3.0-only
#
# Builds, signs, optionally notarizes + staples, and packages ÔKêyTre as a
# Universal macOS release disk image (`build/OKeyTre-v<version>.dmg`).
#
# Usage:
#   ./scripts/release-dmg.sh [version] [options]
#
# Examples:
#   ./scripts/release-dmg.sh                  # Auto-detect Developer ID + notarize (or ad-hoc if --adhoc)
#   ./scripts/release-dmg.sh 0.2 --open       # Bump version to 0.2, build + sign + notarize DMG, reveal in Finder
#   ./scripts/release-dmg.sh 0.2 --publish    # Also create & upload GitHub Release

set -euo pipefail

export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

SCRIPT_DIR="${0:A:h}"
REPO_ROOT="${SCRIPT_DIR:h}"
cd "$REPO_ROOT"

PLIST_PATH="$REPO_ROOT/BazelInfo.plist"
PBXPROJ_PATH="$REPO_ROOT/OKeyTre.xcodeproj/project.pbxproj"
PLIST_BUDDY="/usr/libexec/PlistBuddy"
GITHUB_REPO="lnson/OKeyTreIME"
DEFAULT_NOTARY_PROFILE="okeytre-notary"

usage() {
  cat <<'EOF'
Usage: ./scripts/release-dmg.sh [version] [options]

Arguments:
  version                    Optional target version (e.g. 0.2, v0.2.0). If provided,
                             updates BazelInfo.plist and OKeyTre.xcodeproj automatically.

Options:
  --adhoc                    Force anonymous ad-hoc signing instead of Developer ID signing
  --skip-notarize            Sign with Developer ID (if available) without Apple notarization
  --notary-profile <name>    Keychain profile name for `xcrun notarytool` (default: okeytre-notary)
  --sign-identity <id>       Explicit codesign identity (default: auto-detect Developer ID Application)
  --arm64-only               Build for Apple Silicon (arm64) only instead of Universal (arm64 + x86_64)
  --skip-tests               Skip running C++ & imec unit tests before building
  --open                     Reveal the generated .dmg in Finder when finished
  --publish                  Publish tag + .dmg to GitHub Releases (uses `gh` CLI or GITHUB_TOKEN)
  --publish-only             Publish existing build/OKeyTre-v<version>.dmg without rebuilding/notarizing
  --prerelease               Mark the GitHub Release as a pre-release (used with --publish)
  -h, --help                 Show this help message
EOF
}

TARGET_VERSION=""
CPUS="arm64,x86_64"
FORCE_ADHOC=0
SKIP_NOTARIZE=0
NOTARY_PROFILE="$DEFAULT_NOTARY_PROFILE"
SIGN_IDENTITY=""
RUN_TESTS=1
REVEAL_IN_FINDER=0
PUBLISH_RELEASE=0
PUBLISH_ONLY=0
PRERELEASE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --adhoc)
      FORCE_ADHOC=1
      shift
      ;;
    --skip-notarize)
      SKIP_NOTARIZE=1
      shift
      ;;
    --notary-profile)
      NOTARY_PROFILE="${2:?missing value for --notary-profile}"
      shift 2
      ;;
    --sign-identity)
      SIGN_IDENTITY="${2:?missing value for --sign-identity}"
      shift 2
      ;;
    --arm64-only)
      CPUS="arm64"
      shift
      ;;
    --skip-tests)
      RUN_TESTS=0
      shift
      ;;
    --open)
      REVEAL_IN_FINDER=1
      shift
      ;;
    --publish)
      PUBLISH_RELEASE=1
      shift
      ;;
    --publish-only)
      PUBLISH_RELEASE=1
      PUBLISH_ONLY=1
      shift
      ;;
    --prerelease)
      PRERELEASE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      echo "error: unknown option '$1'" >&2
      usage >&2
      exit 1
      ;;
    *)
      if [[ -n "$TARGET_VERSION" ]]; then
        echo "error: unexpected extra argument '$1'" >&2
        usage >&2
        exit 1
      fi
      TARGET_VERSION="${1#v}"
      TARGET_VERSION="${TARGET_VERSION#V}"
      shift
      ;;
  esac
done

# Auto-detect Developer ID Application certificate unless --adhoc was requested.
if [[ "$FORCE_ADHOC" -eq 0 && -z "$SIGN_IDENTITY" ]]; then
  SIGN_IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null \
    | sed -n 's/.*"\(Developer ID Application:[^"]*\)".*/\1/p' \
    | head -n 1)"
fi

USE_DEVELOPER_ID=0
if [[ "$FORCE_ADHOC" -eq 0 && -n "$SIGN_IDENTITY" ]]; then
  USE_DEVELOPER_ID=1
fi

CURRENT_VERSION="$("$PLIST_BUDDY" -c "Print :CFBundleShortVersionString" "$PLIST_PATH")"
CURRENT_BUILD="$("$PLIST_BUDDY" -c "Print :CFBundleVersion" "$PLIST_PATH")"

if [[ -n "$TARGET_VERSION" ]]; then
  if [[ ! "$TARGET_VERSION" =~ ^[0-9]+(\.[0-9]+)*(-[0-9A-Za-z.-]+)?$ ]]; then
    echo "error: invalid version '$TARGET_VERSION' (expected e.g. 0.2 or 0.2.1)" >&2
    exit 1
  fi

  if [[ "$TARGET_VERSION" != "$CURRENT_VERSION" ]]; then
    NEXT_BUILD=$(( CURRENT_BUILD + 1 ))
    echo "==> Updating version: $CURRENT_VERSION ($CURRENT_BUILD) -> $TARGET_VERSION ($NEXT_BUILD)"
    "$PLIST_BUDDY" -c "Set :CFBundleShortVersionString $TARGET_VERSION" "$PLIST_PATH"
    "$PLIST_BUDDY" -c "Set :CFBundleVersion $NEXT_BUILD" "$PLIST_PATH"

    if [[ -f "$PBXPROJ_PATH" ]]; then
      sed -i '' -E "s/MARKETING_VERSION = [^;]+;/MARKETING_VERSION = ${TARGET_VERSION};/g" "$PBXPROJ_PATH"
      sed -i '' -E "s/CURRENT_PROJECT_VERSION = [0-9]+;/CURRENT_PROJECT_VERSION = ${NEXT_BUILD};/g" "$PBXPROJ_PATH"
    fi
    VERSION="$TARGET_VERSION"
  else
    VERSION="$CURRENT_VERSION"
  fi
else
  VERSION="$CURRENT_VERSION"
fi

TAG="v${VERSION}"
STAGING_DIR="$REPO_ROOT/build/release"
DMG_PATH="$REPO_ROOT/build/OKeyTre-${TAG}.dmg"
APP_BUNDLE="$STAGING_DIR/OKeyTre.app"
APP_BIN="$APP_BUNDLE/Contents/MacOS/OKeyTre"
APP_PLIST="$APP_BUNDLE/Contents/Info.plist"

NOTARIZED=0
if [[ "$PUBLISH_ONLY" -eq 1 ]]; then
  if [[ ! -f "$DMG_PATH" ]]; then
    echo "error: expected existing disk image at '${DMG_PATH#$REPO_ROOT/}', but it was not found." >&2
    exit 1
  fi
  echo "==> Reusing existing disk image: ${DMG_PATH#$REPO_ROOT/}"
  if xcrun stapler validate "$DMG_PATH" >/dev/null 2>&1; then
    NOTARIZED=1
  fi
else
  echo "==> Preparing ÔKêyTre ${TAG} (architectures: ${CPUS})"

  if [[ "$RUN_TESTS" -eq 1 ]]; then
    echo "==> Running C++ engine & compiler tests..."
    bazel test //OKeyTre/ime/core/... //imec/...
  fi

  echo "==> Building optimized stripped release bundle..."
  bazel build --config=release --macos_cpus="$CPUS" //:OKeyTreApp

  echo "==> Extracting and staging OKeyTre.app..."
  rm -rf "$STAGING_DIR"
  mkdir -p "$STAGING_DIR"
  ditto -x -k "$REPO_ROOT/bazel-bin/OKeyTreApp.zip" "$STAGING_DIR/"

  # Normalize Bazel's 1980-01-01 zip timestamps before signing.
  find "$APP_BUNDLE" -exec touch {} +

  if [[ "$USE_DEVELOPER_ID" -eq 1 ]]; then
    echo "==> Signing OKeyTre.app with Hardened Runtime (${SIGN_IDENTITY})..."
    codesign --force --deep --options runtime --timestamp --sign "$SIGN_IDENTITY" "$APP_BUNDLE"
  else
    echo "==> Applying clean ad-hoc code signature..."
    codesign --force --deep --sign - "$APP_BUNDLE"
  fi
  codesign --verify --deep --strict "$APP_BUNDLE"

  echo "==> Running binary & bundle checks..."
  BUILT_VERSION="$("$PLIST_BUDDY" -c "Print :CFBundleShortVersionString" "$APP_PLIST")"
  if [[ "$BUILT_VERSION" != "$VERSION" ]]; then
    echo "error: built Info.plist version '$BUILT_VERSION' does not match expected '$VERSION'" >&2
    exit 1
  fi

  CODESIGN_INFO="$(codesign -dvvv "$APP_BUNDLE" 2>&1)"
  if [[ "$USE_DEVELOPER_ID" -eq 0 ]]; then
    if ! grep -q "^Signature=adhoc$" <<<"$CODESIGN_INFO"; then
      echo "error: expected ad-hoc signature, got:" >&2
      echo "$CODESIGN_INFO" >&2
      exit 1
    fi
    if ! grep -q "^TeamIdentifier=not set$" <<<"$CODESIGN_INFO"; then
      echo "error: TeamIdentifier is set in ad-hoc binary signature:" >&2
      echo "$CODESIGN_INFO" >&2
      exit 1
    fi
  fi

  if strings "$APP_BIN" | grep -q "/Users/"; then
    echo "error: local '/Users/...' path found in stripped binary '$APP_BIN'!" >&2
    exit 1
  fi

  ARCHS="$(lipo -archs "$APP_BIN")"
  echo "    Verified architectures : $ARCHS"
  echo "    Verified bundle version: $BUILT_VERSION"
  if [[ "$USE_DEVELOPER_ID" -eq 1 ]]; then
    echo "    Verified code signature: $SIGN_IDENTITY"
  else
    echo "    Verified code signature: adhoc"
  fi

  echo "==> Packaging compressed disk image: ${DMG_PATH#$REPO_ROOT/}"
  ln -sf /Applications "$STAGING_DIR/Applications"
  rm -f "$DMG_PATH"
  hdiutil create -volname "ÔKêyTre" -srcfolder "$STAGING_DIR" -ov -format UDZO "$DMG_PATH" >/dev/null

  if [[ "$USE_DEVELOPER_ID" -eq 1 ]]; then
    echo "==> Signing disk image..."
    codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG_PATH"

    if [[ "$SKIP_NOTARIZE" -eq 0 ]]; then
      echo "==> Submitting $(basename "$DMG_PATH") to Apple Notary Service (profile: ${NOTARY_PROFILE})..."
      echo "    (If this fails with missing profile, run once:"
      echo "     xcrun notarytool store-credentials \"${NOTARY_PROFILE}\" --apple-id YOUR_EMAIL --team-id YOUR_TEAM_ID --password APP_SPECIFIC_PASSWORD)"
      xcrun notarytool submit "$DMG_PATH" --keychain-profile "$NOTARY_PROFILE" --wait

      echo "==> Stapling notarization ticket to $(basename "$DMG_PATH")..."
      xcrun stapler staple "$DMG_PATH"
      xcrun stapler validate "$DMG_PATH"
      spctl -a -vvv -t open --context context:primary-signature "$DMG_PATH"
      NOTARIZED=1
    fi
  fi
fi

DMG_SIZE="$(du -h "$DMG_PATH" | awk '{print $1}')"
DMG_SHA256="$(shasum -a 256 "$DMG_PATH" | awk '{print $1}')"

if [[ "$NOTARIZED" -eq 1 ]]; then
  RELEASE_NOTES=$(cat <<EOF
### Installation (macOS 14.0+, Apple Silicon & Intel)
1. Open \`$(basename "$DMG_PATH")\` and drag **OKeyTre** into **Applications**.
2. Launch **ÔKêyTre** and grant **Accessibility** permission when prompted.
EOF
)
else
  RELEASE_NOTES=$(cat <<EOF
### Installation (macOS 14.0+, Apple Silicon & Intel)
1. Open \`$(basename "$DMG_PATH")\` and drag **OKeyTre** into **Applications**.
2. Allow first launch via **System Settings → Privacy & Security → Open Anyway**, or run:
   \`\`\`zsh
   xattr -cr /Applications/OKeyTre.app
   \`\`\`
3. Grant **Accessibility** permission when prompted so ÔKêyTre can process keystrokes.
EOF
)
fi

echo ""
echo "✅ Built release DMG successfully!"
echo "   File     : ${DMG_PATH#$REPO_ROOT/} (${DMG_SIZE})"
echo "   Tag      : ${TAG}"
echo "   Signed   : $([[ "$USE_DEVELOPER_ID" -eq 1 ]] && echo "$SIGN_IDENTITY" || echo "adhoc")"
echo "   Notarized: $([[ "$NOTARIZED" -eq 1 ]] && echo "yes (stapled)" || echo "no")"
echo "   SHA-256  : ${DMG_SHA256}"

if [[ "$PUBLISH_RELEASE" -eq 1 ]]; then
  echo ""
  if [[ -n "$(git status --porcelain)" ]]; then
    echo "error: working tree has uncommitted changes (e.g. version bump in BazelInfo.plist)." >&2
    echo "       Commit and 'git push' first, then re-run with '--skip-tests --publish' (or re-run full script)." >&2
    exit 1
  fi

  HEAD_SHA="$(git rev-parse HEAD)"
  if ! git branch -r --contains "$HEAD_SHA" | grep -q "origin/"; then
    echo "error: current commit ($HEAD_SHA) has not been pushed to 'origin' yet." >&2
    echo "       Run 'git push' first so the release tag points to your pushed commit." >&2
    exit 1
  fi

  echo "==> Publishing ${TAG} (${HEAD_SHA:0:7}) to GitHub (${GITHUB_REPO})..."
  if command -v gh >/dev/null 2>&1; then
    GH_FLAGS=(--repo "$GITHUB_REPO" --target "$HEAD_SHA" --title "ÔKêyTre ${TAG}" --notes "$RELEASE_NOTES")
    if [[ "$PRERELEASE" -eq 1 ]]; then
      GH_FLAGS+=(--prerelease)
    else
      GH_FLAGS+=(--latest)
    fi
    gh release create "$TAG" "$DMG_PATH" "${GH_FLAGS[@]}"
  elif [[ -n "${GITHUB_TOKEN:-}" ]]; then
    IS_PRERELEASE=$([[ "$PRERELEASE" -eq 1 ]] && echo "true" || echo "false")
    CREATE_PAYLOAD="$(python3 -c '
import json, sys
tag, sha, notes, pre = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == "true"
print(json.dumps({
    "tag_name": tag,
    "target_commitish": sha,
    "name": f"ÔKêyTre {tag}",
    "body": notes,
    "draft": False,
    "prerelease": pre,
}))
' "$TAG" "$HEAD_SHA" "$RELEASE_NOTES" "$IS_PRERELEASE")"

    RESPONSE="$(curl -fsSL -X POST \
      -H "Accept: application/vnd.github+json" \
      -H "Authorization: Bearer ${GITHUB_TOKEN}" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      "https://api.github.com/repos/${GITHUB_REPO}/releases" \
      -d "$CREATE_PAYLOAD")"

    UPLOAD_URL="$(python3 -c 'import json, sys; print(json.loads(sys.stdin.read())["upload_url"].split("{")[0])' <<<"$RESPONSE")"
    HTML_URL="$(python3 -c 'import json, sys; print(json.loads(sys.stdin.read())["html_url"])' <<<"$RESPONSE")"

    curl -fsSL -X POST \
      -H "Accept: application/vnd.github+json" \
      -H "Authorization: Bearer ${GITHUB_TOKEN}" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      -H "Content-Type: application/x-apple-diskimage" \
      --data-binary @"$DMG_PATH" \
      "${UPLOAD_URL}?name=$(basename "$DMG_PATH")" >/dev/null

    echo "✅ Published release: ${HTML_URL}"
  else
    echo "error: --publish requires either GitHub CLI ('brew install gh && gh auth login') or GITHUB_TOKEN env var." >&2
    exit 1
  fi
fi

if [[ "$REVEAL_IN_FINDER" -eq 1 ]]; then
  open -R "$DMG_PATH"
fi
