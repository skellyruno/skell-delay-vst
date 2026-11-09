#!/bin/bash
# usage: build_pkg.sh <version>   (needs the APPLE_* environment variables from installer/README.md)
set -euo pipefail
VERSION="${1:?version}"
KC=build.keychain; PW=$(uuidgen)
security create-keychain -p "$PW" $KC; security default-keychain -s $KC; security unlock-keychain -p "$PW" $KC
echo "$APPLE_CERT_P12" | base64 --decode > cert.p12
security import cert.p12 -k $KC -P "$APPLE_CERT_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productbuild
security set-key-partition-list -S apple-tool:,apple: -s -k "$PW" $KC >/dev/null

VST3=$(find build -name "Skell Delay.vst3" -maxdepth 6 | head -1)
AU=$(find build -name "Skell Delay.component" -maxdepth 6 | head -1)
for b in "$VST3" "$AU"; do codesign --force --deep --options runtime --timestamp --sign "$APPLE_APP_IDENTITY" "$b"; done

mkdir -p pkgroot/Library/Audio/Plug-Ins/VST3 pkgroot/Library/Audio/Plug-Ins/Components dist
cp -R "$VST3" pkgroot/Library/Audio/Plug-Ins/VST3/; cp -R "$AU" pkgroot/Library/Audio/Plug-Ins/Components/
pkgbuild --root pkgroot --identifier com.myaudiodev.skelldelay --version "$VERSION" --install-location / \
         --sign "$APPLE_INSTALLER_IDENTITY" "dist/SkellDelay-$VERSION-macOS.pkg"
xcrun notarytool submit "dist/SkellDelay-$VERSION-macOS.pkg" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait
xcrun stapler staple "dist/SkellDelay-$VERSION-macOS.pkg"
