param(
  [string]$IexPcap = "data/raw/iex/20241001_IEXTP1_DPLS1.0.pcap.gz",
  [int]$Runs = 100,
  [string]$BuildDir = "build/Debug"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expandedDir = "data/raw/expanded"
New-Item -ItemType Directory -Force $expandedDir | Out-Null
$iexOut = Join-Path $expandedDir "iex.bin"

# Expand only once
$inStream = [System.IO.File]::OpenRead($IexPcap)
try {
  $gzip = New-Object System.IO.Compression.GzipStream($inStream, [System.IO.Compression.CompressionMode]::Decompress)
  try {
    $outStream = [System.IO.File]::Create($iexOut)
    try { $gzip.CopyTo($outStream) } finally { $outStream.Dispose() }
  } finally { $gzip.Dispose() }
} finally { $inStream.Dispose() }

$exe = Join-Path $BuildDir "phase1_parser_validate.exe"
if (!(Test-Path $exe)) { throw "missing exe: $exe" }

$crcSet = New-Object 'System.Collections.Generic.HashSet[string]'
for ($i=1; $i -le $Runs; $i++) {
  $out = & $exe --iex-only $iexOut
  $line = ($out | Where-Object { $_ -like 'crc32=*' } | Select-Object -First 1)
  if (-not $line) { throw "run $i missing crc line" }
  $crc = $line.Split('=')[1].Trim()
  [void]$crcSet.Add($crc)
  if ($i -eq 1 -or $i -eq $Runs) {
    Write-Host "run=$i crc=$crc"
  }
}

Write-Host "unique_crc_count=$($crcSet.Count)"
if ($crcSet.Count -eq 1) {
  Write-Host "deterministic=true"
} else {
  Write-Host "deterministic=false"
}
