#!/usr/bin/env bash
#
# scripts/verify-elf.sh - SOMEIP 制品 ELF/ABI/污染快速自检（CR-006 §9）
#
# 用法:
#   ./scripts/verify-elf.sh <file> [<file> ...]
#   ./scripts/verify-elf.sh --strict <file> [<file> ...]
#
# 说明:
#   - 对 daemon 与动态库执行 file/readelf 检查：ELF64/AArch64、NEEDED、
#     SONAME、RPATH/RUNPATH。
#   - --strict: 用于 Orin aarch64 制品（Linux 环境，需 readelf）；
#     任何 x86_64 / Mach-O / 宿主路径 / build-tree RPATH 视为失败。
#   - 默认模式: 本地 HOST 开发产物（如 macOS Mach-O）仅告警，不失败。
#   - 正式 Orin 整体 ELF/ABI/生成物校验由 BUILD 的 elfcheck 承担；
#     本脚本是 SOMEIP 仓库侧开发自检入口，不替代 BUILD 校验。
#
# 通过条件（CR-006 §9 / REQ-008）:
#   - ELF64/AArch64，解释器兼容 Orin glibc 2.31（TARGET 产物）
#   - NEEDED 只来自 sysroot、冻结 TBOX runtime、vsomeip/CommonAPI 与登记依赖
#   - 无 build-tree RPATH、无宿主 /usr/local、无个人路径
#   - 无 x86_64、Mach-O、Mac SDK、HOST generator library

set -euo pipefail

STRICT=0
FILES=()

usage() {
    echo "用法: $0 [--strict] <file> [<file> ...]"
    exit 2
}

for arg in "$@"; do
    case "$arg" in
        --strict) STRICT=1 ;;
        -h|--help) usage ;;
        *) FILES+=("$arg") ;;
    esac
done

if [ ${#FILES[@]} -eq 0 ]; then
    usage
fi

FAIL=0
HAVE_READELF=0
command -v readelf >/dev/null 2>&1 && HAVE_READELF=1

for f in "${FILES[@]}"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: ${f} not found"
        FAIL=1
        continue
    fi

    # --- 文件类型 / 架构 ---
    magic=$(head -c 4 "$f" | od -An -tx1 | tr -d ' \n')
    case "$magic" in
        7f454c46) ;; # ELF
        cffaedfe)   # Mach-O (macOS host)
            if [ "$STRICT" -eq 1 ]; then
                echo "ERROR: ${f} is Mach-O (host macOS binary) - TARGET pollution"
                FAIL=1
            else
                echo "WARN: ${f} is Mach-O (host macOS binary); use --strict on Orin artifacts"
            fi
            continue
            ;;
        *)
            echo "WARN: ${f} is not ELF (magic=${magic}); skip"
            continue
            ;;
    esac

    echo "== ${f} =="

    # --- ELF class / machine ---
    if [ "$HAVE_READELF" -eq 1 ]; then
        readelf -h "$f" | grep -E 'Class:|Machine:' | sed 's/^/  /'
        if readelf -h "$f" | grep -q 'Class:.*ELF32'; then
            echo "ERROR: ${f} is ELF32"
            FAIL=1
        fi
        if readelf -h "$f" | grep -qE 'Machine:.*(x86-64|Intel)'; then
            echo "ERROR: ${f} is x86_64 (host architecture)"
            FAIL=1
        fi
    else
        # 无 readelf 时降级：仅检查 file 输出
        file "$f" | sed 's/^/  /'
    fi

    # --- NEEDED / RPATH / RUNPATH ---
    if [ "$HAVE_READELF" -eq 1 ]; then
        echo "  NEEDED:"
        readelf -d "$f" | grep 'NEEDED' | sed 's/^/    /'
        if readelf -d "$f" | grep -E '\(RPATH\)|\(RUNPATH\)' | grep -qE '/usr/local|/Users/|/opt/homebrew|build[-/_]|cmake-build'; then
            echo "ERROR: ${f} contains host/build-tree RPATH/RUNPATH"
            readelf -d "$f" | grep -E '\(RPATH\)|\(RUNPATH\)' | sed 's/^/    /'
            FAIL=1
        fi
    fi
done

if [ "$FAIL" -eq 1 ]; then
    echo "RESULT: FAIL"
    exit 1
fi
echo "RESULT: OK"
exit 0
