param()

$ErrorActionPreference = "Stop"

$gitDir = git rev-parse --git-dir
if (-not $gitDir) {
  throw "Not a git repository"
}

$hooksDir = Join-Path $gitDir "hooks"
if (-not (Test-Path $hooksDir)) {
  New-Item -Path $hooksDir -ItemType Directory | Out-Null
}

$hookPath = Join-Path $hooksDir "pre-commit"
$hookBody = @'
#!/usr/bin/env bash
set -euo pipefail
powershell -ExecutionPolicy Bypass -File scripts/check_repo_hygiene.ps1
'@

Set-Content -Path $hookPath -Value $hookBody -NoNewline

Write-Host "installed pre-commit hook at $hookPath"
