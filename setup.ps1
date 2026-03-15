<#
.SYNOPSIS
    开发环境一键配置：自动检测并安装 Git、VS Build Tools、vcpkg 及项目依赖。

.DESCRIPTION
    适用于首次配置的 Windows 机器。使用 winget 安装缺失工具，vcpkg 安装到
    %LOCALAPPDATA%\vcpkg 并持久写入 VCPKG_ROOT 环境变量。支持重复运行（幂等）。

.PARAMETER SkipVS
    跳过 VS 2022 Build Tools 安装（已安装完整 VS 2022 的用户）。

.EXAMPLE
    ./setup.ps1
    ./setup.ps1 -SkipVS
#>

param(
    [switch]$SkipVS
)

$ErrorActionPreference = "Stop"

# ── 辅助函数 ──────────────────────────────────────────

function Write-Step  { param([string]$msg) Write-Host ">> $msg" -ForegroundColor Cyan }
function Write-Ok    { param([string]$msg) Write-Host "   $msg" -ForegroundColor Green }
function Write-Warn  { param([string]$msg) Write-Host "   $msg" -ForegroundColor Yellow }
function Write-Fail  { param([string]$msg) Write-Host "   $msg" -ForegroundColor Red }

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-VSBuildTools {
    # 使用 vswhere 检测已安装的 MSVC 工具集
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $false }
    $result = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    return [bool]$result
}

# ── 0. 管理员权限检测 ─────────────────────────────────

if (-not $SkipVS -and -not (Test-Admin)) {
    Write-Warn "安装 VS Build Tools 需要管理员权限，正在请求提权..."
    $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    if ($SkipVS) { $argList += " -SkipVS" }
    Start-Process PowerShell -Verb RunAs -ArgumentList $argList -Wait
    exit $LASTEXITCODE
}

# ── 1. 检测 winget ────────────────────────────────────

Write-Step "检测 winget..."
$winget = Get-Command winget -ErrorAction SilentlyContinue
if (-not $winget) {
    Write-Fail "未找到 winget。"
    Write-Fail "winget 是 Windows 10 (1709+) / 11 内置的包管理器。"
    Write-Fail "请从 Microsoft Store 安装「应用安装程序」或升级 Windows 版本。"
    exit 1
}
Write-Ok "winget 就绪。"

# ── 2. 检测 / 安装 Git ────────────────────────────────

Write-Step "检测 Git..."
$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) {
    Write-Ok "Git 已安装: $($git.Source)"
} else {
    Write-Warn "未找到 Git，正在安装..."
    winget install Git.Git --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "Git 安装失败。"
        exit 1
    }
    Write-Ok "Git 安装完成。"
    # 刷新 PATH
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
}

# ── 3. 检测 / 安装 VS 2022 Build Tools ───────────────

if ($SkipVS) {
    Write-Step "跳过 VS Build Tools 安装（-SkipVS）。"
} else {
    Write-Step "检测 VS 2022 Build Tools (MSVC)..."
    if (Test-VSBuildTools) {
        Write-Ok "MSVC 工具集已安装。"
    } else {
        Write-Warn "未找到 MSVC 工具集，正在安装 VS 2022 Build Tools..."
        Write-Warn "首次安装需下载约 2-4 GB，请耐心等待。"
        winget install Microsoft.VisualStudio.2022.BuildTools `
            --accept-package-agreements --accept-source-agreements `
            --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621"
        if ($LASTEXITCODE -ne 0) {
            Write-Fail "VS Build Tools 安装失败。"
            exit 1
        }
        Write-Ok "VS 2022 Build Tools 安装完成。"
    }
}

# ── 4. 检测 / 安装 vcpkg ─────────────────────────────

Write-Step "检测 vcpkg..."

$vcpkgRoot = $null
$vcpkgDefaultPath = Join-Path $env:LOCALAPPDATA "vcpkg"

# 4a. 检查已有环境变量
if ($env:VCPKG_ROOT -and (Test-Path "$env:VCPKG_ROOT\vcpkg.exe")) {
    $vcpkgRoot = $env:VCPKG_ROOT
    Write-Ok "通过环境变量找到 vcpkg: $vcpkgRoot"
}

# 4b. 检查默认安装路径
if (-not $vcpkgRoot -and (Test-Path "$vcpkgDefaultPath\vcpkg.exe")) {
    $vcpkgRoot = $vcpkgDefaultPath
    Write-Ok "在默认路径找到 vcpkg: $vcpkgRoot"
}

# 4c. 自动克隆
if (-not $vcpkgRoot) {
    $vcpkgRoot = $vcpkgDefaultPath
    Write-Warn "未找到 vcpkg，正在克隆到 $vcpkgRoot ..."
    git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "vcpkg 克隆失败。"
        exit 1
    }
    & "$vcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "vcpkg bootstrap 失败。"
        exit 1
    }
    Write-Ok "vcpkg 安装完成。"
}

# 持久写入用户级环境变量
$currentEnvRoot = [System.Environment]::GetEnvironmentVariable("VCPKG_ROOT", "User")
if ($currentEnvRoot -ne $vcpkgRoot) {
    [System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, "User")
    Write-Ok "已将 VCPKG_ROOT=$vcpkgRoot 写入用户环境变量。"
}
$env:VCPKG_ROOT = $vcpkgRoot

# ── 5. vcpkg 安装项目依赖 ─────────────────────────────

Write-Step "安装 vcpkg 依赖包（ffmpeg, openal-soft）..."
& "$vcpkgRoot\vcpkg.exe" install ffmpeg openal-soft --triplet x64-windows
if ($LASTEXITCODE -ne 0) {
    Write-Fail "vcpkg 依赖安装失败。"
    exit 1
}
Write-Ok "依赖安装完成。"

# ── 6. 完成 ───────────────────────────────────────────

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  环境配置完成！" -ForegroundColor Green
Write-Host "  VCPKG_ROOT = $vcpkgRoot" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "下一步：" -ForegroundColor Cyan
Write-Host "  1. 重启终端（使环境变量生效）" -ForegroundColor White
Write-Host "  2. 运行 ./build.ps1 开始构建" -ForegroundColor White
