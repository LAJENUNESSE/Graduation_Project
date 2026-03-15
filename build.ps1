<#
.SYNOPSIS
    一键构建脚本：自动完成子模块初始化、vcpkg 检测/安装、CMake 配置与构建。

.PARAMETER Cuda
    启用 CUDA 加速（使用 vs2022-cuda 预设）。需要安装 NVIDIA CUDA Toolkit。

.PARAMETER Config
    构建配置。默认 RelWithDebInfo。可选：Debug, Release, RelWithDebInfo, MinSizeRel。

.EXAMPLE
    ./build.ps1
    ./build.ps1 -Cuda
    ./build.ps1 -Config Debug
    ./build.ps1 -Cuda -Config Release
#>

param(
    [switch]$Cuda,
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"

# ── 辅助函数 ──────────────────────────────────────────

function Write-Step  { param([string]$msg) Write-Host ">> $msg" -ForegroundColor Cyan }
function Write-Ok    { param([string]$msg) Write-Host "   $msg" -ForegroundColor Green }
function Write-Warn  { param([string]$msg) Write-Host "   $msg" -ForegroundColor Yellow }
function Write-Fail  { param([string]$msg) Write-Host "   $msg" -ForegroundColor Red }

function Find-Executable {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# ── 1. Git 子模块 ─────────────────────────────────────

Write-Step "初始化 Git 子模块..."
git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    Write-Fail "Git 子模块初始化失败。"
    exit 1
}
Write-Ok "子模块就绪。"

# ── 2. vcpkg 检测 / 安装 ──────────────────────────────

Write-Step "检测 vcpkg..."

$vcpkgRoot = $null

# 2a. 检查环境变量
if ($env:VCPKG_ROOT -and (Test-Path "$env:VCPKG_ROOT/vcpkg.exe")) {
    $vcpkgRoot = $env:VCPKG_ROOT
    Write-Ok "通过环境变量找到 vcpkg: $vcpkgRoot"
}

# 2b. 检查常见路径
if (-not $vcpkgRoot) {
    $commonPaths = @("C:/vcpkg", "$HOME/vcpkg", "$PSScriptRoot/../vcpkg")
    foreach ($p in $commonPaths) {
        $resolved = [System.IO.Path]::GetFullPath($p)
        if (Test-Path "$resolved/vcpkg.exe") {
            $vcpkgRoot = $resolved
            Write-Ok "在 $vcpkgRoot 找到 vcpkg。"
            break
        }
    }
}

# 2c. 自动克隆
if (-not $vcpkgRoot) {
    $vcpkgRoot = [System.IO.Path]::GetFullPath("$PSScriptRoot/../vcpkg")
    Write-Warn "未找到 vcpkg，将自动克隆到 $vcpkgRoot ..."
    git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "vcpkg 克隆失败。"
        exit 1
    }
    & "$vcpkgRoot/bootstrap-vcpkg.bat" -disableMetrics
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "vcpkg bootstrap 失败。"
        exit 1
    }
    Write-Ok "vcpkg 安装完成。"
}

# 设置当前会话环境变量
$env:VCPKG_ROOT = $vcpkgRoot

# ── 3. CMake 检测 ─────────────────────────────────────

Write-Step "检测 CMake..."

$cmake = Find-Executable "cmake"
if (-not $cmake) {
    $vsPath = "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    if (Test-Path $vsPath) {
        $cmake = $vsPath
        Write-Ok "使用 VS 2022 Build Tools 内置 CMake。"
    } else {
        Write-Fail "未找到 CMake。请安装 CMake 或 VS 2022 Build Tools。"
        exit 1
    }
} else {
    Write-Ok "CMake: $cmake"
}

# ── 4. CMake 配置 ─────────────────────────────────────

$preset = if ($Cuda) { "vs2022-cuda" } else { "default" }
Write-Step "CMake 配置（预设: $preset）..."

& $cmake --preset $preset
if ($LASTEXITCODE -ne 0) {
    Write-Fail "CMake 配置失败。"
    exit 1
}
Write-Ok "配置完成。"

# ── 5. CMake 构建 ─────────────────────────────────────

Write-Step "构建 Editor（配置: $Config）..."

& $cmake --build build --config $Config --target Editor
if ($LASTEXITCODE -ne 0) {
    Write-Fail "构建失败。"
    exit 1
}
Write-Ok "构建完成！"

# ── 6. 完成 ───────────────────────────────────────────

$exePath = "build/Editor/$Config/Editor.exe"
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  构建成功！" -ForegroundColor Green
Write-Host "  可执行文件: $exePath" -ForegroundColor Green
if ($Cuda) {
    Write-Host "  CUDA 加速: 已启用" -ForegroundColor Green
}
Write-Host "========================================" -ForegroundColor Green·
Write-Host ""
Write-Host "运行编辑器:" -ForegroundColor Cyan
Write-Host "  ./$exePath" -ForegroundColor White
