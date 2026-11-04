#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
TOOLCHAIN_DIR="${1:-${OO_PS4_TOOLCHAIN:-}}"
BIN_DIR="${2:-linux}"
UPSTREAM_DIR="$SCRIPT_DIR/upstream"
BUILD_DIR="$SCRIPT_DIR/build"
ARCHIVE="$SCRIPT_DIR/libjbc.a"

if [[ -z "$TOOLCHAIN_DIR" || ! -d "$TOOLCHAIN_DIR/include" ]]; then
    echo "ERROR: OO_PS4_TOOLCHAIN is missing or invalid." >&2
    echo "Set OO_PS4_TOOLCHAIN to your OpenOrbis PS4 toolchain directory." >&2
    exit 2
fi

# Fetch the upstream implementation only on the build machine. The source is
# intentionally not copied into this project archive.
if [[ ! -f "$UPSTREAM_DIR/jailbreak.c" || ! -f "$UPSTREAM_DIR/kernelrw.c" || ! -f "$UPSTREAM_DIR/jailbreak.h" ]]; then
    rm -rf "$UPSTREAM_DIR"
    if ! command -v git >/dev/null 2>&1; then
        echo "ERROR: git is required to fetch ps4-libjbc." >&2
        exit 3
    fi
    if ! git clone --depth 1 https://github.com/sleirsgoevy/ps4-libjbc.git "$UPSTREAM_DIR"; then
        rm -rf "$UPSTREAM_DIR"
        echo "Primary libjbc source fetch failed; trying the bucanero fork..." >&2
        git clone --depth 1 https://github.com/bucanero/ps4-libjbc.git "$UPSTREAM_DIR"
    fi
fi

for f in jailbreak.c kernelrw.c jailbreak.h kernelrw.h defs.h; do
    if [[ ! -f "$UPSTREAM_DIR/$f" ]]; then
        echo "ERROR: ps4-libjbc checkout is missing $f" >&2
        exit 4
    fi
done

CLANG="$TOOLCHAIN_DIR/bin/$BIN_DIR/clang"
if [[ ! -x "$CLANG" && -x "$TOOLCHAIN_DIR/bin/$BIN_DIR/clang.exe" ]]; then
    CLANG="$TOOLCHAIN_DIR/bin/$BIN_DIR/clang.exe"
fi
if [[ ! -x "$CLANG" ]]; then
    if command -v clang >/dev/null 2>&1; then
        CLANG="$(command -v clang)"
    else
        echo "ERROR: clang was not found at $CLANG or in PATH." >&2
        exit 5
    fi
fi

if [[ -x "$TOOLCHAIN_DIR/bin/$BIN_DIR/llvm-ar" ]]; then
    AR="$TOOLCHAIN_DIR/bin/$BIN_DIR/llvm-ar"
elif [[ -x "$TOOLCHAIN_DIR/bin/$BIN_DIR/llvm-ar.exe" ]]; then
    AR="$TOOLCHAIN_DIR/bin/$BIN_DIR/llvm-ar.exe"
elif [[ -x "$TOOLCHAIN_DIR/bin/llvm-ar" ]]; then
    AR="$TOOLCHAIN_DIR/bin/llvm-ar"
elif [[ -x "$TOOLCHAIN_DIR/bin/llvm-ar.exe" ]]; then
    AR="$TOOLCHAIN_DIR/bin/llvm-ar.exe"
elif command -v llvm-ar >/dev/null 2>&1; then
    AR="$(command -v llvm-ar)"
elif command -v ar >/dev/null 2>&1; then
    AR="$(command -v ar)"
else
    echo "ERROR: neither llvm-ar nor ar was found." >&2
    exit 6
fi

mkdir -p "$BUILD_DIR"
COMMON_FLAGS=(
    --target=x86_64-pc-freebsd12-elf
    -fPIC -funwind-tables -ffreestanding -mno-red-zone -fno-stack-protector
    -isysroot "$TOOLCHAIN_DIR"
    -isystem "$TOOLCHAIN_DIR/include"
    -I"$UPSTREAM_DIR"
)

# These are the two upstream translation units that implement the API used by
# goldenPluginE. The unrelated convenience functions in utils.c are not needed.
"$CLANG" "${COMMON_FLAGS[@]}" -c "$UPSTREAM_DIR/kernelrw.c" -o "$BUILD_DIR/kernelrw.o"
"$CLANG" "${COMMON_FLAGS[@]}" -c "$UPSTREAM_DIR/jailbreak.c" -o "$BUILD_DIR/jailbreak.o"
rm -f "$ARCHIVE"
"$AR" rcs "$ARCHIVE" "$BUILD_DIR/jailbreak.o" "$BUILD_DIR/kernelrw.o"

echo "Built $ARCHIVE using $CLANG"
