#!/opt/homebrew/bin/bash
# =============================================================================
# ps5vr/app/scripts/build-native-mac.sh - build on the Mac itself, no Docker.
#
# Uses the Mac's PS5 payload SDK (~/ps5sdk), whose sysroot carries FFmpeg 7.1
# (the Docker volume's is 7.0 - no MV-HEVC), and is several times faster than
# the emulated x86 container. Keeps its helpers in tools/native-mac/:
#   bin/nproc      macOS has no nproc
#   zlib/          static host zlib for the PS5 native-app converter
#
#   ./scripts/build-native-mac.sh              PS5VR (PPSA99177)
# =============================================================================
set -euo pipefail
APP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="${APP}/tools/native-mac"
mkdir -p "${TOOLS}/bin"

if [[ ! -x "${TOOLS}/bin/nproc" ]]; then
    printf '#!/bin/sh\nexec sysctl -n hw.ncpu\n' > "${TOOLS}/bin/nproc"
    chmod +x "${TOOLS}/bin/nproc"
fi

if [[ ! -f "${TOOLS}/zlib/lib/libz.a" ]]; then
    echo "==> building static host zlib"
    tmp="$(mktemp -d)"
    curl -fsSL https://zlib.net/fossils/zlib-1.3.1.tar.gz | tar -xz -C "${tmp}"
    (cd "${tmp}/zlib-1.3.1" && CC=clang ./configure --static --prefix="${TOOLS}/zlib" >/dev/null &&
        make -j"$(sysctl -n hw.ncpu)" >/dev/null && make install >/dev/null)
    rm -rf "${tmp}"
fi

# The converter (engine/tools/native-app) built for macOS arm64; build.sh
# only compiles its own when none is in place, and Homebrew's clang++ cannot.
for d in build-mac; do
    mkdir -p "${APP}/${d}/host"
    [[ -x "${APP}/${d}/host/ps5-native-tool" ]] || cp "${TOOLS}/ps5-native-tool" "${APP}/${d}/host/"
done

export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-$HOME/ps5sdk/opt/ps5-payload-sdk}"
export LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm/bin/llvm-config}"
export ENG_NATIVE_ZLIB="${TOOLS}/zlib"
export APP_NATIVE_BUILD=1
export APP_BUILD_SUFFIX=-mac
export PATH="${TOOLS}/bin:${PS5_PAYLOAD_SDK}/bin:/usr/bin:/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:${PATH}"   # Apple clang++ for the host converter
exec /opt/homebrew/bin/bash "${APP}/scripts/build.sh" "$@"
