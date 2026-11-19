#!/usr/bin/env bash
set -euo pipefail

# Build, package, zipalign, sign (v1+v2+v3), and verify Open City Android APK.
# Works cleanly in GitHub Actions (ubuntu-latest) or any Linux machine with Android SDK & NDK.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${OUT_DIR:-${ROOT_DIR}/build-android}"
ANDROID_API="${ANDROID_API:-24}"
ANDROID_TARGET_SDK="${ANDROID_TARGET_SDK:-34}"
ANDROID_ABIS="${ANDROID_ABIS:-arm64-v8a armeabi-v7a}"
ANDROID_VERSION_NAME="${ANDROID_VERSION_NAME:-1.1.0}"
ANDROID_VERSION_CODE="${ANDROID_VERSION_CODE:-110}"
APK_OUTPUT="${APK_OUTPUT:-${OUT_DIR}/OpenCity-Android-v${ANDROID_VERSION_NAME}.apk}"

ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/usr/local/lib/android/sdk}}"
if [[ ! -d "${ANDROID_SDK_ROOT}" ]]; then
    echo "ERROR: ANDROID_SDK_ROOT not found: ${ANDROID_SDK_ROOT}" >&2
    exit 1
fi

# Locate latest installed Android NDK
if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
    if [[ -d "${ANDROID_SDK_ROOT}/ndk" ]]; then
        ANDROID_NDK_HOME="$(ls -1d "${ANDROID_SDK_ROOT}"/ndk/* | sort -V | tail -n 1)"
    elif [[ -d "${ANDROID_SDK_ROOT}/ndk-bundle" ]]; then
        ANDROID_NDK_HOME="${ANDROID_SDK_ROOT}/ndk-bundle"
    fi
fi
if [[ -z "${ANDROID_NDK_HOME:-}" || ! -f "${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" ]]; then
    echo "ERROR: Android NDK with android.toolchain.cmake not found under ${ANDROID_SDK_ROOT}" >&2
    exit 1
fi

# Locate latest build-tools & target platform android.jar
BUILD_TOOLS_DIR="$(ls -1d "${ANDROID_SDK_ROOT}"/build-tools/* | sort -V | tail -n 1)"
ANDROID_JAR="${ANDROID_SDK_ROOT}/platforms/android-${ANDROID_TARGET_SDK}/android.jar"
if [[ ! -f "${ANDROID_JAR}" ]]; then
    ANDROID_JAR="$(ls -1 "${ANDROID_SDK_ROOT}"/platforms/android-*/android.jar | sort -V | tail -n 1)"
fi

echo "=== Open City Android APK Build ==="
echo "Version     : ${ANDROID_VERSION_NAME} (code ${ANDROID_VERSION_CODE})"
echo "SDK         : ${ANDROID_SDK_ROOT}"
echo "NDK         : ${ANDROID_NDK_HOME}"
echo "Build-Tools : ${BUILD_TOOLS_DIR}"
echo "Android JAR : ${ANDROID_JAR}"
echo "ABIs        : ${ANDROID_ABIS}"

mkdir -p "${OUT_DIR}/staging/assets/fonts" "${OUT_DIR}/staging/lib" "${OUT_DIR}/compiled_res" "${OUT_DIR}/java_gen" "${OUT_DIR}/java_obj" "${OUT_DIR}/dex"
rm -f "${OUT_DIR}/base-unaligned.apk" "${OUT_DIR}/base-aligned.apk"

# 1) Generate launcher icons & sync compact Korean glyph table for zero missing Hangul on mobile
python3 "${ROOT_DIR}/tools/gen_android_icons.py"
python3 "${ROOT_DIR}/tools/gen_korean_glyphs.py"

# 2) Bundle crisp Korean TrueType font into APK assets/fonts/korean.ttf
FONT_SRC="${OPEN_CITY_ANDROID_FONT:-}"
if [[ -z "${FONT_SRC}" ]]; then
    for cand in \
        "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf" \
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf" \
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc" \
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"; do
        if [[ -f "${cand}" ]]; then
            FONT_SRC="${cand}"
            break
        fi
    done
fi
if [[ -n "${FONT_SRC}" && -f "${FONT_SRC}" ]]; then
    cp -f "${FONT_SRC}" "${OUT_DIR}/staging/assets/fonts/korean.ttf"
    echo "Bundled Korean font: ${FONT_SRC} -> assets/fonts/korean.ttf ($(du -h "${OUT_DIR}/staging/assets/fonts/korean.ttf" | cut -f1))"
else
    echo "WARNING: No Korean font file found to bundle; APK will fall back to /system/fonts on device."
fi

# 3) Build libopen_city.so for each requested Android ABI via CMake + NDK toolchain
for ABI in ${ANDROID_ABIS}; do
    echo "--- Building native shared library for ${ABI} ---"
    ABI_BUILD="${OUT_DIR}/cmake-${ABI}"
    cmake -S "${ROOT_DIR}" -B "${ABI_BUILD}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="${ABI}" \
        -DANDROID_PLATFORM="android-${ANDROID_API}" \
        -DANDROID_STL=c++_static \
        -DOPEN_CITY_BUILD_APP=ON \
        -DOPEN_CITY_BUILD_TESTS=OFF
    cmake --build "${ABI_BUILD}" --target open_city --parallel

    mkdir -p "${OUT_DIR}/staging/lib/${ABI}"
    cp -f "${ABI_BUILD}/libopen_city.so" "${OUT_DIR}/staging/lib/${ABI}/libopen_city.so"
    # Strip debug symbols for lean APK size
    LLVM_STRIP="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
    if [[ -x "${LLVM_STRIP}" ]]; then
        "${LLVM_STRIP}" --strip-unneeded "${OUT_DIR}/staging/lib/${ABI}/libopen_city.so"
    fi
    ls -lh "${OUT_DIR}/staging/lib/${ABI}/libopen_city.so"
done

# 4) Compile Android resources & link base APK with aapt2
"${BUILD_TOOLS_DIR}/aapt2" compile --dir "${ROOT_DIR}/android/res" -o "${OUT_DIR}/compiled_res/res.zip"
"${BUILD_TOOLS_DIR}/aapt2" link \
    -o "${OUT_DIR}/base-unaligned.apk" \
    -I "${ANDROID_JAR}" \
    --manifest "${ROOT_DIR}/android/AndroidManifest.xml" \
    --version-code "${ANDROID_VERSION_CODE}" \
    --version-name "${ANDROID_VERSION_NAME}" \
    -A "${OUT_DIR}/staging/assets" \
    --java "${OUT_DIR}/java_gen" \
    "${OUT_DIR}/compiled_res/res.zip" \
    --auto-add-overlay

# 5) Compile NativeLoader.java -> classes.dex (Java 8 bytecode -> DEX via d8)
javac -source 1.8 -target 1.8 -Xlint:-options \
    -classpath "${ANDROID_JAR}" \
    -d "${OUT_DIR}/java_obj" \
    "${ROOT_DIR}/android/src/com/stickmongus/opencity/NativeLoader.java" \
    $(find "${OUT_DIR}/java_gen" -name "*.java")

"${BUILD_TOOLS_DIR}/d8" \
    --min-api "${ANDROID_API}" \
    --lib "${ANDROID_JAR}" \
    --output "${OUT_DIR}/dex" \
    $(find "${OUT_DIR}/java_obj" -name "*.class")

# 6) Inject classes.dex and native lib/<abi>/libopen_city.so into APK
cp -f "${OUT_DIR}/dex/classes.dex" "${OUT_DIR}/staging/classes.dex"
(
    cd "${OUT_DIR}/staging"
    zip -q -u "${OUT_DIR}/base-unaligned.apk" classes.dex
    zip -q -r -u "${OUT_DIR}/base-unaligned.apk" lib
)

# 7) Align APK on 4-byte (and 4KB page for shared libs) boundaries
"${BUILD_TOOLS_DIR}/zipalign" -p -f -v 4 "${OUT_DIR}/base-unaligned.apk" "${OUT_DIR}/base-aligned.apk" >/dev/null

# 8) Sign APK with Release Keystore (from env/secrets) or ephemeral self-signed fallback for local PR tests
KS_PATH="${ANDROID_KEYSTORE_PATH:-}"
KS_ALIAS="${ANDROID_KEY_ALIAS:-opencity}"
KS_STORE_PASS="${ANDROID_STORE_PASSWORD:-}"
KS_KEY_PASS="${ANDROID_KEY_PASSWORD:-${KS_STORE_PASS}}"

if [[ -z "${KS_PATH}" && -n "${ANDROID_KEYSTORE_BASE64:-}" ]]; then
    KS_PATH="${OUT_DIR}/release-signing.jks"
    printf '%s' "${ANDROID_KEYSTORE_BASE64}" | base64 -d > "${KS_PATH}"
fi

if [[ -z "${KS_PATH}" || ! -f "${KS_PATH}" ]]; then
    echo "INFO: No release keystore provided; generating temporary debug signing key for verification."
    KS_PATH="${OUT_DIR}/debug-temp.jks"
    KS_ALIAS="opencitydebug"
    KS_STORE_PASS="opencitydebug123"
    KS_KEY_PASS="${KS_STORE_PASS}"
    rm -f "${KS_PATH}"
    keytool -genkeypair -noprompt \
        -keystore "${KS_PATH}" \
        -storetype PKCS12 \
        -alias "${KS_ALIAS}" \
        -keyalg RSA -keysize 2048 \
        -validity 365 \
        -storepass "${KS_STORE_PASS}" \
        -keypass "${KS_KEY_PASS}" \
        -dname "CN=Open City Debug, O=Stickmongus, C=KR"
fi

"${BUILD_TOOLS_DIR}/apksigner" sign \
    --ks "${KS_PATH}" \
    --ks-key-alias "${KS_ALIAS}" \
    --ks-pass "pass:${KS_STORE_PASS}" \
    --key-pass "pass:${KS_KEY_PASS}" \
    --v1-signing-enabled true \
    --v2-signing-enabled true \
    --v3-signing-enabled true \
    --out "${APK_OUTPUT}" \
    "${OUT_DIR}/base-aligned.apk"

# Remove decoded temporary keystore if created from base64
if [[ "${KS_PATH}" == "${OUT_DIR}/release-signing.jks" ]]; then
    rm -f "${KS_PATH}"
fi

echo "=== Verifying APK Signature ==="
"${BUILD_TOOLS_DIR}/apksigner" verify --verbose --print-certs "${APK_OUTPUT}"
ls -lh "${APK_OUTPUT}"
echo "SUCCESS: Signed APK ready at ${APK_OUTPUT}"
