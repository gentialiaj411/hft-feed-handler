param(
  [int64]$MaxBytes = 100000000
)

$ErrorActionPreference = "Stop"

$tracked = git ls-files
$failed = $false

foreach ($path in $tracked) {
  if ($path -like "data/raw/*") {
    Write-Error "Tracked raw data is forbidden: $path"
    $failed = $true
  }
}

foreach ($path in $tracked) {
  if (-not (Test-Path $path)) {
    continue
  }
  $size = (Get-Item $path).Length
  if ($size -gt $MaxBytes) {
    Write-Error "Tracked file exceeds ${MaxBytes} bytes: $path ($size)"
    $failed = $true
  }
}

if ($failed) {
  exit 1
}

Write-Host "repo_hygiene: OK"
