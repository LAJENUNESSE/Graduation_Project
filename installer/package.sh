#!/usr/bin/env bash
# =============================================================================
# GameEngine Editor — 一键打包脚本
#
# 完整流程:
#   1. CMake configure (如未配置)
#   2. 构建 Editor (RelWithDebInfo)
#   3. cpack 生成 ZIP + staging 目录
#   4. ISCC 编译 Inno Setup 安装器
#   5. 把最终的 setup.exe 和便携 ZIP 拷到 temp/package/
#
# 运行环境: Git Bash / MINGW / MSYS2
# 用法:
#   bash package.sh          # 一键打包 (默认)
#   bash package.sh --clean  # 先清空 temp/package/ 里的旧产物再打包
# =============================================================================

set -euo pipefail

# ── 路径配置 ────────────────────────────────────────────────────────────────
# 脚本可放项目内任意位置 (项目根 / installer / scripts / ...)，
# 会自动向上查找含 CMakeLists.txt 的目录作为项目根。
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR"
while [[ "$ROOT" != "/" && "$ROOT" != "" && ! -f "$ROOT/CMakeLists.txt" ]]; do
    ROOT="$(dirname "$ROOT")"
done
if [[ ! -f "$ROOT/CMakeLists.txt" ]]; then
    echo "ERROR: 无法定位项目根 (从 $SCRIPT_DIR 向上未找到 CMakeLists.txt)" >&2
    exit 1
fi

TEMP_DIR="$ROOT/temp"
PACKAGE_DIR="$TEMP_DIR/package"
BUILD_DIR="$ROOT/build"
DIST_DIR="$ROOT/dist"
INSTALLER_DIR="$ROOT/installer"

# ── 工具路径发现 (按顺序尝试) ────────────────────────────────────────────────
find_exe() {
    local label="$1"; shift
    for candidate in "$@"; do
        if [[ -f "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    echo "ERROR: 未找到 $label，尝试过的路径：" >&2
    for candidate in "$@"; do echo "  - $candidate" >&2; done
    return 1
}

CMAKE="$(find_exe "cmake.exe" \
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    "C:/Program Files/CMake/bin/cmake.exe")"

CPACK="$(find_exe "cpack.exe" \
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cpack.exe" \
    "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cpack.exe" \
    "C:/Program Files/CMake/bin/cpack.exe")"

ISCC="$(find_exe "ISCC.exe (Inno Setup 6)" \
    "C:/Dev/Tools/Inno Setup 6/ISCC.exe" \
    "C:/Program Files (x86)/Inno Setup 6/ISCC.exe" \
    "C:/Program Files/Inno Setup 6/ISCC.exe")"

# ── 颜色输出 ────────────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    RED="\033[1;31m"; GREEN="\033[1;32m"; YELLOW="\033[1;33m"; CYAN="\033[1;36m"; RESET="\033[0m"
else
    RED=""; GREEN=""; YELLOW=""; CYAN=""; RESET=""
fi

info()  { echo -e "${CYAN}[INFO]${RESET} $*"; }
ok()    { echo -e "${GREEN}[OK]${RESET}   $*"; }
warn()  { echo -e "${YELLOW}[WARN]${RESET} $*"; }
error() { echo -e "${RED}[ERROR]${RESET}$*" >&2; }
die()   { error "$*"; exit 1; }

step() {
    echo
    echo -e "${CYAN}===========================================================${RESET}"
    echo -e "${CYAN}  $*${RESET}"
    echo -e "${CYAN}===========================================================${RESET}"
}

# ── 参数解析 ────────────────────────────────────────────────────────────────
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        -h|--help)
            sed -n '2,16p' "${BASH_SOURCE[0]}"
            exit 0 ;;
        *) die "未知参数: $arg (使用 --help 查看用法)" ;;
    esac
done

# ── Step 0: 前置检查 ─────────────────────────────────────────────────────────
step "Step 0 / 5: 前置检查"

[[ -f "$ROOT/CMakeLists.txt" ]] || die "CMakeLists.txt 未找到: $ROOT"
[[ -f "$INSTALLER_DIR/Editor.iss" ]] || die "installer/Editor.iss 未找到"
[[ -f "$INSTALLER_DIR/redist/VC_redist.x64.exe" ]] || die \
"缺少 installer/redist/VC_redist.x64.exe，请下载：
     https://aka.ms/vs/17/release/vc_redist.x64.exe"

ok "cmake:   $CMAKE"
ok "cpack:   $CPACK"
ok "ISCC:    $ISCC"
ok "项目根:  $ROOT"

# ── Step 1: CMake Configure ─────────────────────────────────────────────────
step "Step 1 / 5: CMake Configure"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    info "首次配置 (--preset default)"
    "$CMAKE" --preset default
    ok "配置完成"
else
    info "已配置过，跳过 (如需强制重新配置: rm -rf build/ 后重跑)"
fi

# ── Step 2: 构建 Editor ──────────────────────────────────────────────────────
step "Step 2 / 5: 构建 Editor (RelWithDebInfo)"

"$CMAKE" --build "$BUILD_DIR" --config RelWithDebInfo --target Editor
ok "Editor.exe 构建完成"

# ── Step 3: CPack 打 ZIP + staging ──────────────────────────────────────────
step "Step 3 / 5: CPack 生成 staging 目录 + ZIP"

"$CPACK" --config "$BUILD_DIR/CPackConfig.cmake" -G ZIP -C RelWithDebInfo -B "$BUILD_DIR"
ok "CPack 完成"

# ── Step 4: Inno Setup 编译 ──────────────────────────────────────────────────
step "Step 4 / 5: 编译 Inno Setup 安装器"

"$ISCC" "$INSTALLER_DIR/Editor.iss"
ok "Inno Setup 编译完成"

# ── Step 5: 归档产物到 temp/package/ ────────────────────────────────────────
step "Step 5 / 5: 归档产物到 temp/package/"

mkdir -p "$PACKAGE_DIR"

if [[ "$CLEAN" == "1" ]]; then
    info "--clean: 清空 temp/package/ 下旧的 GameEngineEditor-* 产物 (不影响 temp/ 其他内容)"
    rm -f "$PACKAGE_DIR"/GameEngineEditor-*.exe "$PACKAGE_DIR"/GameEngineEditor-*.zip
fi

SETUP_SRC="$DIST_DIR/GameEngineEditor-Setup-0.1.0.exe"
ZIP_SRC="$BUILD_DIR/GameEngineEditor-0.1.0-win64.zip"

if [[ -f "$SETUP_SRC" ]]; then
    cp "$SETUP_SRC" "$PACKAGE_DIR/"
    ok "安装器 (setup.exe) -> $PACKAGE_DIR/$(basename "$SETUP_SRC")"
else
    warn "未找到 setup.exe 原位置: $SETUP_SRC"
fi

if [[ -f "$ZIP_SRC" ]]; then
    cp "$ZIP_SRC" "$PACKAGE_DIR/"
    ok "便携 ZIP       -> $PACKAGE_DIR/$(basename "$ZIP_SRC")"
else
    warn "未找到 ZIP 原位置: $ZIP_SRC"
fi

# ── 完成 ─────────────────────────────────────────────────────────────────────
step "打包完成"

echo
info "所有产物 ($PACKAGE_DIR):"
ls -lh "$PACKAGE_DIR"/GameEngineEditor-*.{exe,zip} 2>/dev/null | awk '{print "  "$0}' || true
echo
info "测试安装: 双击 $PACKAGE_DIR/GameEngineEditor-Setup-0.1.0.exe"
info "便携版:   解压 $PACKAGE_DIR/GameEngineEditor-0.1.0-win64.zip 到任意目录，运行 Editor/Editor.exe"
echo
