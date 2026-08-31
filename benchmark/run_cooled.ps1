# 凉机分段基准：空调/低温环境下无人值守跑全量 30 组。
# 与 run_pipeline.ps1 的差异：按预估 GPU 负载把 30 组切成小段，段间自动等待
# GPU 温度降至阈值以下且节流解除，避免连续负载触发功耗/温度墙导致
# 后段组掉入热稳态档（v3 run_20260831_110725 的教训：全后端 +55%~97%）。
#
# 用法（空调房、插电、GPU 凉透后）：
#   powershell -NoProfile -ExecutionPolicy Bypass -File benchmark/run_cooled.ps1
# 可选参数：
#   -SkipBuild        跳过构建（二进制已是最新时用）
#   -CoolTempC 55     段间等待的 GPU 温度阈值（默认 55）
#   -SegBudgetMin 6   单段 GPU 负载时长预算（分钟，默认 6）
#   -DryRun           只打印分段计划，不跑
#
# 退出码：0 成功；2 构建/矩阵失败；3 门禁未通过；4 出图失败。

[CmdletBinding()]
param(
    [string]$OutputRoot = "benchmark/results",
    [double]$CoolTempC = 55.0,
    [double]$SegBudgetMin = 6.0,
    [int]$CoolTimeoutMin = 25,
    [switch]$SkipBuild,
    [switch]$SkipCooling,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $projectRoot

$cmake = "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$editorRel = "build-benchmark/Editor/RelWithDebInfo/Editor.exe"
$vulkanSdk = "C:/Dev/Environments/VulkanSDK/1.4.341.1"
$uv = Join-Path $env:USERPROFILE ".local\bin\uv.exe"
if (-not (Test-Path $uv)) { $uv = "uv" }

$backends = @("opengl", "cuda", "vulkan")
$solvers  = @("wcsph", "pcisph")
$particles = @(1000, 5000, 10000, 20000, 50000)

# 凉机速度下的单组 GPU 负载预估（秒）≈ v2 各组 Mean_ms × 5500 帧 / 1000，
# 用于贪心切段；实际组耗时另记 durations.csv 供后续轮次校准。
$estSec = @{
    "opengl|wcsph|1000" = 2;   "opengl|wcsph|5000" = 3;    "opengl|wcsph|10000" = 5;
    "opengl|wcsph|20000" = 13; "opengl|wcsph|50000" = 49;
    "opengl|pcisph|1000" = 13; "opengl|pcisph|5000" = 80;  "opengl|pcisph|10000" = 104;
    "opengl|pcisph|20000" = 173; "opengl|pcisph|50000" = 503;
    "cuda|wcsph|1000" = 2;   "cuda|wcsph|5000" = 2;    "cuda|wcsph|10000" = 3;
    "cuda|wcsph|20000" = 8;  "cuda|wcsph|50000" = 23;
    "cuda|pcisph|1000" = 10; "cuda|pcisph|5000" = 58;  "cuda|pcisph|10000" = 82;
    "cuda|pcisph|20000" = 135; "cuda|pcisph|50000" = 394;
    "vulkan|wcsph|1000" = 2; "vulkan|wcsph|5000" = 2;  "vulkan|wcsph|10000" = 4;
    "vulkan|wcsph|20000" = 11; "vulkan|wcsph|50000" = 44;
    "vulkan|pcisph|1000" = 12; "vulkan|pcisph|5000" = 71; "vulkan|pcisph|10000" = 91;
    "vulkan|pcisph|20000" = 147; "vulkan|pcisph|50000" = 413;
}

function Get-GpuState
{
    # 返回 @{ TempC; Throttled }；无 nvidia-smi 时返回 @{ TempC = $null; Throttled = $false }
    try
    {
        $line = (nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>$null) | Select-Object -First 1
        if (-not $line) { return @{ TempC = $null; Throttled = $false } }
        $temp = [int]($line.ToString().Trim())
        $perf = nvidia-smi -q -d PERFORMANCE 2>$null | Select-String "^\s+SW (Power Cap|Thermal Slowdown)\s+: Active"
        return @{ TempC = $temp; Throttled = ($null -ne $perf) }
    }
    catch
    {
        return @{ TempC = $null; Throttled = $false }
    }
}

function Wait-GpuCool
{
    param([string]$Reason)
    if ($SkipCooling)
    {
        Write-Output "[cool] skipped (-SkipCooling) before $Reason"
        return $true
    }
    $state = Get-GpuState
    if ($null -eq $state.TempC)
    {
        Write-Output "[cool] nvidia-smi unavailable, skip cooling wait before $Reason"
        return $true
    }
    if ($state.TempC -lt $CoolTempC -and -not $state.Throttled)
    {
        Write-Output ("[cool] gpu {0}C, no throttle; start {1}" -f $state.TempC, $Reason)
        return $true
    }
    Write-Output ("[cool] gpu {0}C throttled={1}; waiting before {2} (limit {3} min)" -f `
        $state.TempC, $state.Throttled, $Reason, $CoolTimeoutMin)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalMinutes -lt $CoolTimeoutMin)
    {
        Start-Sleep -Seconds 30
        $state = Get-GpuState
        Write-Output ("[cool] ... gpu {0}C throttled={1} elapsed={2:F0}s" -f `
            $state.TempC, $state.Throttled, $sw.Elapsed.TotalSeconds)
        if ($state.TempC -lt $CoolTempC -and -not $state.Throttled)
        {
            Write-Output ("[cool] cooled after {0:F0}s; start {1}" -f $sw.Elapsed.TotalSeconds, $Reason)
            return $true
        }
    }
    Write-Output "[cool] WARNING: cooling timeout, continue anyway"
    return $false
}

# ── 切段：贪心装填，段预算 SegBudgetMin 分钟 ─────────────────────────
$allGroups = @()
foreach ($b in $backends) { foreach ($s in $solvers) { foreach ($p in $particles) { $allGroups += @{ Backend = $b; Solver = $s; Particles = $p } } } }

$segments = [System.Collections.Generic.List[object]]::new()
$cur = [System.Collections.Generic.List[object]]::new()
$curSec = 0.0
foreach ($g in $allGroups)
{
    $sec = $estSec["$($g.Backend)|$($g.Solver)|$($g.Particles)"]
    if ($cur.Count -gt 0 -and ($curSec + $sec) -gt ($SegBudgetMin * 60))
    {
        $segments.Add($cur)
        $cur = [System.Collections.Generic.List[object]]::new()
        $curSec = 0.0
    }
    $cur.Add($g)
    $curSec += $sec
}
if ($cur.Count -gt 0) { $segments.Add($cur) }

$editorPath = Join-Path $projectRoot $editorRel
Write-Host ("[cooled] {0} groups in {1} segments (budget {2} min/segment, cool below {3}C)" -f `
    $allGroups.Count, $segments.Count, $SegBudgetMin, $CoolTempC)
for ($i = 0; $i -lt $segments.Count; $i++)
{
    $sum = ($segments[$i] | ForEach-Object { $estSec["$($_.Backend)|$($_.Solver)|$($_.Particles)"] } | Measure-Object -Sum).Sum
    $names = ($segments[$i] | ForEach-Object { "$($_.Backend)/$($_.Solver)/$($_.Particles)" }) -join ", "
    Write-Output ("[cooled] segment {0}: {1:F1}s ~ {2}" -f ($i + 1), $sum, $names)
}
if ($DryRun)
{
    Write-Output "[cooled] dry run complete; no build, no benchmark."
    exit 0
}

# ── 构建（先于凉机检查：构建本身是负载，跑完再等凉）─────────────────
if (-not $SkipBuild)
{
    Write-Output "[step build] building benchmark Editor..."
    $env:VULKAN_SDK = $vulkanSdk
    & $cmake --preset vs2022-benchmark
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed with exit code ${LASTEXITCODE}" }
    & $cmake --build build-benchmark --config RelWithDebInfo --target Editor
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed with exit code ${LASTEXITCODE}" }
}
if (-not (Test-Path $editorPath -PathType Leaf)) { throw "Editor executable not found: $editorPath" }

# ── 开跑前置：GPU 必须先凉透 ─────────────────────────────────────────
Wait-GpuCool -Reason "first segment" | Out-Null

$env:ENGINE_CUDA_SCAN = "cub"
$name = "run_" + (Get-Date -Format "yyyyMMdd_HHmmss")
$resultsDirRel = Join-Path $OutputRoot $name
$resultsDir = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $resultsDirRel))
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null
Write-Output "[cooled] output: $resultsDirRel"

$clockBefore = nvidia-smi --query-gpu=clocks.sm,temperature.gpu,power.draw --format=csv,noheader 2>$null
Write-Output "[cooled] clock before matrix: $clockBefore"

$partFiles = [System.Collections.Generic.List[string]]::new()
$durationsCsv = Join-Path $resultsDir "durations.csv"
Set-Content -LiteralPath $durationsCsv -Value "Backend,Solver,Particles,DurationSec,Attempt" -Encoding utf8
$failed = [System.Collections.Generic.List[string]]::new()
$total = $allGroups.Count
$done = 0
$matrixSw = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($segIndex in 1..$segments.Count)
{
    if ($segIndex -gt 1)
    {
        Wait-GpuCool -Reason ("segment {0}/{1}" -f $segIndex, $segments.Count) | Out-Null
    }
    foreach ($g in $segments[$segIndex - 1])
    {
        $partFile = Join-Path $resultsDir ("{0}_{1}_{2}.csv" -f $g.Backend, $g.Solver, $g.Particles)
        $benchArgs = @(
            "--benchmark-fluid", "--backend", $g.Backend, "--solver", $g.Solver,
            "--particles", $g.Particles, "--iterations", "6", "--warmup", "100",
            "--frames", "1000", "--runs", "5", "--fixed-dt", "0.008333333333333333",
            "--seed", "42", "--output", $partFile
        )
        $attempt = 0
        $ok = $false
        while (-not $ok -and $attempt -lt 3)
        {
            $attempt++
            $groupSw = [System.Diagnostics.Stopwatch]::StartNew()
            Write-Output ("[run {0}/{1}] {2} {3} particles={4} (attempt {5})" -f `
                ($done + 1), $total, $g.Backend, $g.Solver, $g.Particles, $attempt)
            & $editorPath @benchArgs
            $code = $LASTEXITCODE
            $groupSw.Stop()
            $lines = 0
            if (Test-Path $partFile -PathType Leaf) { $lines = (Get-Content $partFile).Count }
            if ($code -eq 0 -and $lines -eq (1000 * 5 + 1))
            {
                $ok = $true
                Add-Content -LiteralPath $durationsCsv -Value ("{0},{1},{2},{3:F0},{4}" -f `
                    $g.Backend, $g.Solver, $g.Particles, $groupSw.Elapsed.TotalSeconds, $attempt) -Encoding utf8
                $partFiles.Add($partFile)
            }
            else
            {
                Write-Output ("[run] FAILED exit={0} lines={1}; retry {2}" -f $code, $lines, $attempt)
                if (Test-Path $partFile) { Remove-Item $partFile -Force }
                Start-Sleep -Seconds 10
            }
        }
        if (-not $ok) { $failed.Add("$($g.Backend)/$($g.Solver)/$($g.Particles)") }
        $done++
        $elapsedMin = $matrixSw.Elapsed.TotalMinutes
        $etaMin = if ($done -gt 0) { $elapsedMin / $done * ($total - $done) } else { 0 }
        Write-Output ("[progress] {0}/{1} elapsed={2:F0}min eta={3:F0}min" -f $done, $total, $elapsedMin, $etaMin)
    }
}

$clockAfter = nvidia-smi --query-gpu=clocks.sm,temperature.gpu,power.draw --format=csv,noheader 2>$null
Write-Output "[cooled] clock after matrix: $clockAfter"

if ($failed.Count -gt 0)
{
    Write-Output ("[cooled] FAILED groups ({0}): {1}" -f $failed.Count, ($failed -join "; "))
    exit 2
}

# ── 合并 raw_results.csv（同 run_matrix.ps1 逻辑）────────────────────
$rawCsv = Join-Path $resultsDir "raw_results.csv"
$first = $true
foreach ($f in $partFiles)
{
    $lines = Get-Content -LiteralPath $f
    if ($first) { Set-Content -LiteralPath $rawCsv -Value $lines -Encoding utf8; $first = $false }
    else { Add-Content -LiteralPath $rawCsv -Value ($lines | Select-Object -Skip 1) -Encoding utf8 }
}

# ── 汇总 + 门禁 + 出图（同 run_pipeline.ps1 full 模式参数）───────────
Write-Output "[step summarize] gates: mean<=0.002, Linf<=5.0"
& $uv run --project docs/thesis/figures/scripted python benchmark/summarize.py $rawCsv `
    --density-relative-tolerance 0.002 --max-density-error-tolerance 5.0
if ($LASTEXITCODE -ne 0) { throw "summarize.py failed with exit code ${LASTEXITCODE}" }

$summaryCsv = Join-Path $resultsDir "summary.csv"
$summary = @(Import-Csv -LiteralPath $summaryCsv)
$failing = @($summary | Where-Object { $_.CorrectnessValid -ne "True" })
if ($summary.Count -lt $total) { Write-Output ("[gate] WARNING: expected {0} groups, summary has {1}" -f $total, $summary.Count) }
if ($failing.Count -gt 0)
{
    Write-Output ("[gate] FAILED: {0}/{1} groups rejected" -f $failing.Count, $summary.Count)
    $failing | ForEach-Object { Write-Output ("  {0} {1} particles={2}: MeanDensity={3} MaxDensityError={4}" -f `
        $_.Backend, $_.Solver, $_.Particles, $_.MeanDensity, $_.MaxDensityError) }
    exit 3
}
Write-Output ("[gate] PASSED: all {0} groups" -f $summary.Count)

Write-Output "[step figures] plotting..."
& $uv run --project docs/thesis/figures/scripted python benchmark/plot_results.py `
    --summary $summaryCsv --raw $rawCsv --output-dir (Join-Path $resultsDir "figures")
if ($LASTEXITCODE -ne 0)
{
    Write-Output "[step figures] plot failed; data and summary remain valid."
    exit 4
}

Write-Output ""
Write-Output ("[cooled] DONE: {0}" -f $resultsDirRel)
Write-Output "  raw:     $rawCsv"
Write-Output "  summary: $summaryCsv"
Write-Output "  figures: $resultsDir\figures"
exit 0
