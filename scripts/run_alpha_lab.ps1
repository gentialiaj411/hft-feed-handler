$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
$Build = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build-wsl" }
wsl -e bash -lc "cd /mnt/c/Users/bhask/Documents/PROJECTS/market-data-handler && bash scripts/run_alpha_lab.sh"
