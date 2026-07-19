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

            Write-Host ("[benchmark] {0} {1} particles={2}" -f $backend, $solver, $particleCount)
            if ($DryRun) {
                Write-Host ("  {0} {1}" -f $editorPath, ($benchmarkArgs -join " "))
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
                Write-Host "  complete result found; skipping"
                $partFiles.Add($partFile)
                continue
            }

            & $editorPath @benchmarkArgs
            if ($LASTEXITCODE -ne 0) {
                throw "Benchmark failed with exit code ${LASTEXITCODE}: $backend/$solver/$particleCount"
            }
            if (-not (Test-Path -LiteralPath $partFile -PathType Leaf)) {
                throw "Benchmark did not create its CSV output: $partFile"
            }
            $partFiles.Add($partFile)
        }
    }
}

if ($DryRun) {
    Write-Host "[benchmark] Dry run complete; no process was started."
    exit 0
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

Write-Host "[benchmark] Raw matrix complete: $rawResult"
Write-Host "[benchmark] Summarize with: python benchmark/summarize.py `"$rawResult`""
