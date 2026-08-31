[CmdletBinding()]
param(
    [string]$Editor = "build-benchmark/Editor/RelWithDebInfo/Editor.exe",
    [ValidateSet("opengl", "cuda", "vulkan")]
    [string[]]$Backends = @("opengl", "cuda", "vulkan"),
    [ValidateSet("wcsph", "pcisph")]
    [string[]]$Solvers = @("wcsph", "pcisph"),
    [uint32[]]$Particles = @(1000, 5000, 10000, 20000, 50000),
    [uint32]$Iterations = 6,
    [uint32]$Warmup = 100,
    [uint32]$Frames = 1000,
    [uint32]$Runs = 5,
    [double]$FixedDeltaTime = (1.0 / 120.0),
    [uint32]$Seed = 42,
    [string]$OutputDirectory = "benchmark/results",
    [switch]$Quick,
    [switch]$Resume,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$editorPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $Editor))
$resultRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))

if (-not (Test-Path -LiteralPath $editorPath -PathType Leaf)) {
    throw "Editor executable not found: $editorPath"
}

if ($Quick) {
    $Particles = @(1000)
    $Warmup = 5
    $Frames = 20
    $Runs = 1
}

New-Item -ItemType Directory -Force -Path $resultRoot | Out-Null
$rawResult = Join-Path $resultRoot "raw_results.csv"
$partFiles = [System.Collections.Generic.List[string]]::new()

function Test-CompleteResult {
    param(
        [string]$Path,
        [string]$ExpectedBackend,
        [string]$ExpectedSolver,
        [uint32]$ExpectedParticles,
        [uint32]$ExpectedIterations,
        [uint32]$ExpectedWarmup,
        [uint32]$ExpectedFrames,
        [uint32]$ExpectedRuns
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }

    try {
        $rows = @(Import-Csv -LiteralPath $Path)
        if ($rows.Count -ne ($ExpectedFrames * $ExpectedRuns)) {
            return $false
        }

        $sampleKeys = [System.Collections.Generic.HashSet[string]]::new()
        foreach ($row in $rows) {
            if (($row.Backend.ToLowerInvariant() -ne $ExpectedBackend) -or
                ($row.Solver.ToLowerInvariant() -ne $ExpectedSolver) -or
                ([uint32]$row.Particles -ne $ExpectedParticles) -or
                ([uint32]$row.Iterations -ne $ExpectedIterations) -or
                ([uint32]$row.Warmup -ne $ExpectedWarmup) -or
                ([uint32]$row.Frame -lt 1) -or
                ([uint32]$row.Frame -gt $ExpectedFrames) -or
                ([uint32]$row.Run -lt 1) -or
                ([uint32]$row.Run -gt $ExpectedRuns) -or
                ($row.SampleValid -ne "1") -or
                ([uint32]$row.OutOfBoundsCount -ne 0)) {
                return $false
            }
            if (-not $sampleKeys.Add(("{0}:{1}" -f $row.Run, $row.Frame))) {
                return $false
            }
        }
    }
    catch {
        return $false
    }

    return $true
}

$total = $Backends.Count * $Solvers.Count * $Particles.Count
$done = 0
$failed = [System.Collections.Generic.List[string]]::new()
$matrixSw = [System.Diagnostics.Stopwatch]::StartNew()
# 组级进度/结果必须 Write-Output（P-33）：Write-Host 走 information stream，重定向不捕获
foreach ($backend in $Backends) {
    foreach ($solver in $Solvers) {
        foreach ($particleCount in $Particles) {
            $partFile = Join-Path $resultRoot ("{0}_{1}_{2}.csv" -f $backend, $solver, $particleCount)
            $benchmarkArgs = @(
                "--benchmark-fluid",
                "--backend", $backend,
                "--solver", $solver,
                "--particles", $particleCount,
                "--iterations", $Iterations,
                "--warmup", $Warmup,
                "--frames", $Frames,
                "--runs", $Runs,
                "--fixed-dt", $FixedDeltaTime.ToString("R", [System.Globalization.CultureInfo]::InvariantCulture),
                "--seed", $Seed,
                "--output", $partFile
            )

            Write-Output ("[benchmark] {0} {1} particles={2}" -f $backend, $solver, $particleCount)
            if ($DryRun) {
                Write-Output ("  {0} {1}" -f $editorPath, ($benchmarkArgs -join " "))
                continue
            }

            if ($Resume -and (Test-CompleteResult `
                    -Path $partFile `
                    -ExpectedBackend $backend `
                    -ExpectedSolver $solver `
                    -ExpectedParticles $particleCount `
                    -ExpectedIterations $Iterations `
                    -ExpectedWarmup $Warmup `
                    -ExpectedFrames $Frames `
                    -ExpectedRuns $Runs)) {
                Write-Output "  complete result found; skipping"
                $partFiles.Add($partFile)
                $done++
                continue
            }

            # P1：失败重试（共 3 次尝试），最终失败降级为记录并继续——
            # 一组崩溃不再炸掉整条矩阵；完整列表在结尾打印并置退出码 2
            $ok = $false
            for ($attempt = 1; $attempt -le 3 -and -not $ok; $attempt++) {
                if ($attempt -gt 1) {
                    Write-Output "  retry $attempt after previous failure; cooling 10s"
                    Start-Sleep -Seconds 10
                }
                & $editorPath @benchmarkArgs
                $exitCode = $LASTEXITCODE
                $lineCount = 0
                if (Test-Path -LiteralPath $partFile -PathType Leaf) {
                    $lineCount = (Get-Content -LiteralPath $partFile).Count
                }
                if ($exitCode -eq 0 -and $lineCount -eq ($Frames * $Runs + 1)) {
                    $ok = $true
                    $partFiles.Add($partFile)
                }
                else {
                    Write-Output ("  FAILED exit={0} lines={1} (expected {2})" -f $exitCode, $lineCount, ($Frames * $Runs + 1))
                    if (Test-Path -LiteralPath $partFile -PathType Leaf) {
                        Remove-Item -LiteralPath $partFile -Force
                    }
                }
            }
            if (-not $ok) {
                $failed.Add("$backend/$solver/$particleCount")
            }
            $done++
            $etaMin = if ($done -gt 0) { $matrixSw.Elapsed.TotalMinutes / $done * ($total - $done) } else { 0 }
            Write-Output ("[progress] {0}/{1} elapsed={2:F0}min eta={3:F0}min" -f $done, $total, $matrixSw.Elapsed.TotalMinutes, $etaMin)
        }
    }
}

if ($DryRun) {
    Write-Output "[benchmark] Dry run complete; no process was started."
    exit 0
}

if ($failed.Count -gt 0) {
    Write-Output ("[benchmark] FAILED groups ({0}): {1}" -f $failed.Count, ($failed -join "; "))
    exit 2
}

$firstFile = $true
foreach ($partFile in $partFiles) {
    $lines = Get-Content -LiteralPath $partFile
    if ($firstFile) {
        Set-Content -LiteralPath $rawResult -Value $lines -Encoding utf8
        $firstFile = $false
    }
    else {
        Add-Content -LiteralPath $rawResult -Value ($lines | Select-Object -Skip 1) -Encoding utf8
    }
}

Write-Output "[benchmark] Raw matrix complete: $rawResult"
Write-Host "[benchmark] Summarize with: python benchmark/summarize.py `"$rawResult`""
