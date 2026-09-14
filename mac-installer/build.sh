#!/bin/zsh

set -euo pipefail

ROOT="${0:A:h:h}"
BUILD="$ROOT/build/mac-installer"
APP_NAME="FoloOS 编程伴侣"
APP="$BUILD/$APP_NAME.app"
CONTENTS="$APP/Contents"
MACOS="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"
STAGE="$BUILD/dmg"
RELEASE="$ROOT/releases/community-20260825"
DMG="$RELEASE/FoloOS-Codex-Mac-GUI-Installer-community-20260830-v1.dmg"

rm -rf "$BUILD"
mkdir -p "$MACOS" "$RESOURCES/tools" "$STAGE" "$RELEASE"

for ARCH in arm64 x86_64; do
    xcrun --sdk macosx clang \
        -O \
        -fobjc-arc \
        -target "$ARCH-apple-macos11.0" \
        -framework Cocoa \
        "$ROOT/mac-installer/FoloOSCompanion.m" \
        -o "$BUILD/FoloOSCompanion-$ARCH"
done

lipo -create \
    "$BUILD/FoloOSCompanion-arm64" \
    "$BUILD/FoloOSCompanion-x86_64" \
    -output "$MACOS/FoloOSCompanion"

cp -p "$ROOT/mac-installer/Info.plist" "$CONTENTS/Info.plist"
cp -p "$ROOT/mac-installer/README.md" "$RESOURCES/README.md"
for FILE in \
    mac_bridge.py \
    codex_backend.py \
    install_autostart.py \
    mac_speech_helper.m \
    mac_speech_helper-Info.plist
do
    cp -p "$ROOT/tools/$FILE" "$RESOURCES/tools/$FILE"
done

chmod 755 "$MACOS/FoloOSCompanion"
codesign --force --deep --sign - "$APP"

cp -R "$APP" "$STAGE/$APP_NAME.app"
ln -s /Applications "$STAGE/应用程序"
rm -f "$DMG" "$DMG.sha256"
hdiutil create \
    -volname "FoloOS 编程伴侣" \
    -srcfolder "$STAGE" \
    -ov \
    -format UDZO \
    "$DMG"
shasum -a 256 "$DMG" > "$DMG.sha256"

echo "已生成：$DMG"
echo "SHA-256：$(cut -d ' ' -f 1 "$DMG.sha256")"
