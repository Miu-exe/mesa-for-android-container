#!/bin/bash
# Build the KiralyCraft termux-x11 fork (DRI3 + paced Present) debug APK with a private JDK 17 and
# Android SDK/NDK (versions from the fork's CI and lorie/version.gradle). Install with `adb install -r`.
set -e
WORK=${WORK:-$HOME/termux-x11-build}
mkdir -p "$WORK" && cd "$WORK"
if [ ! -d jdk17 ]; then
  curl -sSL -o jdk17.tgz "https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse"
  mkdir jdk17 && tar -xzf jdk17.tgz -C jdk17 --strip-components=1 && rm jdk17.tgz
fi
export JAVA_HOME=$WORK/jdk17 PATH=$WORK/jdk17/bin:$PATH
export ANDROID_HOME=$WORK/android-sdk ANDROID_SDK_ROOT=$WORK/android-sdk
if [ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]; then
  mkdir -p "$ANDROID_HOME/cmdline-tools"
  curl -sSL -o clt.zip https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip
  unzip -q clt.zip -d "$ANDROID_HOME/cmdline-tools" && mv "$ANDROID_HOME/cmdline-tools/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
  rm clt.zip
fi
yes | "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses >/dev/null
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" "platforms;android-34" "build-tools;35.0.0" \
  "ndk;29.0.14206865" "cmake;3.22.1" "platform-tools" >/dev/null
[ -d termux-x11 ] || git clone -b fix/present-vblank-pacing-minimal https://github.com/KiralyCraft/termux-x11.git
cd termux-x11
git submodule update --init --recursive --depth 1 -q
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew --no-daemon assembleDebug
ls -la lorie-app/build/outputs/apk/standalone/debug/*.apk
