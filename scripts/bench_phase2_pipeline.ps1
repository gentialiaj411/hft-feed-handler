param(
  [int64]$Events = 2000000,
  [int64]$GapWindow = 256,
  [int64]$Capacity = 1048576
)

$ErrorActionPreference = "Stop"

$exe = "build/Debug/phase2_pipeline_bench.exe"
if (-not (Test-Path $exe)) {
  Write-Host "phase2_pipeline_bench not found; building..."
  cmake --build build -j | Out-Host
}

& $exe --events $Events --gap-window $GapWindow --capacity $Capacity
