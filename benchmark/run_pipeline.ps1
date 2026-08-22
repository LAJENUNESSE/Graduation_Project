# 一键实验流水线：构建基准 Editor → 跑矩阵 → 正确性门禁汇总 → 生成论文图表。
# 改完代码后挂上本脚本即可自动完成全流程，无需手动逐步执行。
#
# 用法示例：
#   ./benchmark/run_pipeline.ps1 -Mode quick                 # 冒烟验证（~5 分钟）
#   ./benchmark/run_pipeline.ps1                             # 正式全量矩阵（30 组，耗时数小时）
#   ./benchmark/run_pipeline.ps1 -CudaScan blelloch -Name ablation_blelloch   # 前缀和消融
#   ./benchmark/run_pipeline.ps1 -Resume                     # 中断后续跑（复用完整分组）
#   ./benchmark/run_pipeline.ps1 -OutputRoot results_v3      # 论文定稿数据放仓库根目录
#
# 退出码：0 成功；2 构建/矩阵失败；3 正确性门禁未通过；4 图表生成失败。

[CmdletBinding()]
param(
    # quick：1000 粒子 × 20 帧 × 1 次，仅验证端到端管线可用；
    #   warmup 太短密度未收敛，正确性门禁自动跳过（只跑通流程，不判物理对错）。
    # full：规范参数（6 迭代 / 100 warmup / 1000 帧 / 5 次），门禁生效。
    [ValidateSet("quick", "full")]
    [string]$Mode = "full",

    # 结果子目录名，默认 run_<时间戳>，位于 -OutputRoot 下。
    [string]$Name = "",

    # 输出根目录（仓库相对路径）。默认 benchmark/results/（已被 .gitignore 排除）；
    # 论文定稿数据建议显式指定到仓库根目录（如 results_v3）以便留存。
    [string]$OutputRoot = "benchmark/results",

    [ValidateSet("opengl", "cuda", "vulkan")]
    [string[]]$Backends = @("opengl", "cuda", "vulkan"),

    [ValidateSet("wcsph", "pcisph")]
    [string[]]$Solvers = @("wcsph", "pcisph"),

    # 留空使用规范规模（full: 1000..50000；quick 强制为 1000）。
    [uint32[]]$Particles = @(),

    # CUDA Grid Build 前缀和实现（消融开关）：cub=NVIDIA 官方库，blelloch=与 GL/VK 同源的三 pass 实现。
    [ValidateSet("cub", "blelloch")]
    [string]$CudaScan = "cub",

    # 均值门禁：跨后端平均密度相对偏差上限。results_v2 校准值 0.002（0.2%，见 commit 1895f34 后的重校准）。
    [double]$DensityRelativeTolerance = 0.002,

    # L∞ 门禁：逐样本最大密度偏差与 OpenGL 基线自身水平的绝对差上限。
    [double]$MaxDensityErrorTolerance = 5.0,

    [switch]$SkipBuild,
    [switch]$SkipFigures,
    [switch]$Resume,
    # 只打印将执行的命令，不做任何实际操作。
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

$cmake = "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$editorRel = "build-benchmark/Editor/RelWithDebInfo/Editor.exe"
$vulkanSdk = "C:/Dev/Environments/VulkanSDK/1.4.341.1"

function Get-UvPath {
    $cmd = Get-Command uv -ErrorAction SilentlyContinue
    if ($null -ne $cmd) { return $cmd.Source }
    $fallback = Join-Path $env:USERPROFILE ".local\bin\uv.exe"
    if (Test-Path $fallback) { return $fallback }
    throw "uv not found in PATH or at $fallback"
}

if ($Mode -eq "quick") {
    if ($Particles.Count -eq 0) { $Particles = @(1000) }
}
elseif ($Particles.Count -eq 0) {
    $Particles = @(1000, 5000, 10000, 20000, 50000)
}

if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = "run_" + (Get-Date -Format "yyyyMMdd_HHmmss")
}
$resultsDirRel = Join-Path $OutputRoot $Name
$resultsDir = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $resultsDirRel))
$rawCsv = Join-Path $resultsDir "raw_results.csv"
$summaryCsv = Join-Path $resultsDir "summary.csv"

Write-Host ("[pipeline] mode={0} output={1} cudaScan={2}" -f $Mode, $resultsDirRel, $CudaScan)
Write-Host ("[pipeline] groups: {0} backends x {1} solvers x {2} particle scales" -f `
        $Backends.Count, $Solvers.Count, $Particles.Count)

if ($DryRun) {
    Write-Host "[pipeline] DRY RUN - no build, no benchmark, no analysis."
}

# ── Step 1: 构建基准 Editor（独立目录，不影响日常 build/）──────────────
if (-not $SkipBuild) {
    if ($DryRun) {
        Write-Host "[step 1] would configure preset vs2022-benchmark and build Editor (RelWithDebInfo)"
    }
    else {
        Write-Host "[step 1] building benchmark Editor..."
        $env:VULKAN_SDK = $vulkanSdk
        & $cmake --preset vs2022-benchmark
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed with exit code ${LASTEXITCODE}" }
        & $cmake --build build-benchmark --config RelWithDebInfo --target Editor
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed with exit code ${LASTEXITCODE}" }
    }
}
else {
    Write-Host "[step 1] skipped (-SkipBuild)"
}

if (-not (Test-Path (Join-Path $projectRoot $editorRel) -PathType Leaf)) {
    throw "Editor executable not found: $editorRel (build first or drop -SkipBuild)"
}

# ── Step 2: 跑基准矩阵 ────────────────────────────────────────────────
$env:ENGINE_CUDA_SCAN = $CudaScan

$matrixArgs = @{
    Editor          = $editorRel
    Backends        = $Backends
    Solvers         = $Solvers
    Particles       = $Particles
    OutputDirectory = $resultsDirRel
}
if ($Mode -eq "quick") { $matrixArgs.Quick = $true }
if ($Resume)           { $matrixArgs.Resume = $true }

Write-Host ("[step 2] running benchmark matrix ({0})..." -f $(if ($DryRun) { "dry run" } else { "this is the long part" }))
if ($DryRun) {
    & (Join-Path $PSScriptRoot "run_matrix.ps1") @matrixArgs -DryRun
}
else {
    & (Join-Path $PSScriptRoot "run_matrix.ps1") @matrixArgs
    if ($LASTEXITCODE -ne 0) { throw "run_matrix.ps1 failed with exit code ${LASTEXITCODE}" }
}

if ($DryRun) {
    Write-Host ("[step 3] would summarize: summarize.py --density-relative-tolerance {0} --max-density-error-tolerance {1}" -f `
            $DensityRelativeTolerance, $MaxDensityErrorTolerance)
    Write-Host "[step 4] would check correctness gates on summary.csv"
    Write-Host "[step 5] would plot figures into $resultsDirRel\figures"
    Write-Host "[pipeline] dry run complete."
    exit 0
}

# ── Step 3: 汇总统计 + 正确性门禁 ─────────────────────────────────────
# quick 模式 warmup 仅 5 帧、WCSPH 密度未收敛，真实门禁数值无意义；
# 但 plot_results.py 要求 Speedup 列非空（只有传入门禁才会填充），
# 因此传入极宽松容差让图表管线走通——下方 gate check 仍按 quick 跳过判定。
$uv = Get-UvPath
if ($Mode -eq "full") {
    $meanTolerance = $DensityRelativeTolerance
    $linfTolerance = $MaxDensityErrorTolerance
}
else {
    $meanTolerance = 1.0     # 100% 相对偏差：任何结果都放行
    $linfTolerance = 1000000.0
}
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
$summarizeArgs = @(
    "run", "--project", "docs/thesis/figures/scripted", "python", "benchmark/summarize.py", $rawCsv,
    "--density-relative-tolerance", $meanTolerance.ToString("R", $invariant),
    "--max-density-error-tolerance", $linfTolerance.ToString("R", $invariant)
)

Write-Host "[step 3] summarizing with correctness gates..."
& $uv @summarizeArgs
if ($LASTEXITCODE -ne 0) { throw "summarize.py failed with exit code ${LASTEXITCODE}" }

# summarize.py 门禁失败仍返回 0（只在 CSV 标 CorrectnessValid=False），
# 因此在这里解析 summary.csv 统一判败。
$summary = @(Import-Csv -LiteralPath $summaryCsv)
$expectedGroups = $Backends.Count * $Solvers.Count * $Particles.Count
$failing = @($summary | Where-Object { $_.CorrectnessValid -ne "True" })

if ($Mode -eq "full") {
    if ($summary.Count -lt $expectedGroups) {
        Write-Host ("[gate] WARNING: expected {0} groups but summary has {1}" -f $expectedGroups, $summary.Count) -ForegroundColor Yellow
    }
    if ($failing.Count -gt 0) {
        Write-Host ("[gate] FAILED: {0}/{1} groups rejected by correctness gates:" -f $failing.Count, $summary.Count) -ForegroundColor Red
        $failing | ForEach-Object {
            Write-Host ("  {0} {1} particles={2}: MeanDensity={3} MaxDensityError={4}" -f `
                    $_.Backend, $_.Solver, $_.Particles, $_.MeanDensity, $_.MaxDensityError)
        }
        exit 3
    }
    Write-Host ("[gate] PASSED: all {0} groups within tolerance (mean<={1}, Linf<={2})" -f `
            $summary.Count, $DensityRelativeTolerance, $MaxDensityErrorTolerance) -ForegroundColor Green
}
else {
    Write-Host "[gate] SKIPPED (quick mode: warmup too short for converged density)"
}

# ── Step 4: 生成论文图表 ──────────────────────────────────────────────
# plot_results.py 的逐帧分布图硬性要求完整五规模矩阵，quick 数据必然缺组，
# 因此 quick 模式自动跳过出图（除非显式 -SkipFigures 也无妨，行为一致）。
$plotFigures = (-not $SkipFigures) -and ($Mode -eq "full")
if ($SkipFigures) {
    Write-Host "[step 4] skipped (-SkipFigures)"
}
elseif ($Mode -eq "quick") {
    Write-Host "[step 4] SKIPPED (quick mode: distribution figures require the full 5-scale matrix)"
}
else {
    Write-Host "[step 4] plotting figures..."
    & $uv run --project docs/thesis/figures/scripted python benchmark/plot_results.py `
        --summary $summaryCsv `
        --raw $rawCsv `
        --output-dir (Join-Path $resultsDir "figures")
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[step 4] plot_results.py failed; data and summary remain valid." -ForegroundColor Yellow
        exit 4
    }
}

# ── 完成报告 ──────────────────────────────────────────────────────────
Write-Host ""
Write-Host ("[pipeline] DONE: {0}" -f $resultsDirRel) -ForegroundColor Green
Write-Host ("  raw csv     : {0}" -f $rawCsv)
Write-Host ("  summary csv : {0}" -f $summaryCsv)
if ($plotFigures) { Write-Host ("  figures     : {0}\figures" -f $resultsDir) }
exit 0
