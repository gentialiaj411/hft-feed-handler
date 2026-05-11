param(
  [string]$IexPcap = "data/raw/iex/20241001_IEXTP1_DPLS1.0.pcap.gz",
  [int]$Runs = 10,
  [string]$BuildDir = "build/Debug"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expandedDir = "data/raw/expanded"
New-Item -ItemType Directory -Force $expandedDir | Out-Null
$iexOut = Join-Path $expandedDir "iex.bin"

if (!(Test-Path $iexOut)) {
  $inStream = [System.IO.File]::OpenRead($IexPcap)
  try {
    $gzip = New-Object System.IO.Compression.GzipStream($inStream, [System.IO.Compression.CompressionMode]::Decompress)
    try {
      $outStream = [System.IO.File]::Create($iexOut)
      try { $gzip.CopyTo($outStream) } finally { $outStream.Dispose() }
    } finally { $gzip.Dispose() }
  } finally { $inStream.Dispose() }
}

$exe = Join-Path $BuildDir "phase1_parser_validate.exe"
if (!(Test-Path $exe)) { throw "missing exe: $exe" }

$timesMs = New-Object System.Collections.Generic.List[Double]
$frames = 0
for ($i=1; $i -le $Runs; $i++) {
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  $out = & $exe --iex-only $iexOut
  $sw.Stop()
  $timesMs.Add($sw.Elapsed.TotalMilliseconds)

  if ($i -eq 1) {
    $fline = ($out | Where-Object { $_ -like 'frames=*' } | Select-Object -First 1)
    if (-not $fline) { throw "missing frames line" }
    $frames = [int64]($fline.Split('=')[1])
  }
}

$sorted = $timesMs | Sort-Object
function pct([double]$p) {
  $idx = [int][Math]::Floor(($sorted.Count-1) * $p)
  return $sorted[$idx]
}

$avgMs = ($timesMs | Measure-Object -Average).Average
$p50 = pct 0.50
$p99 = pct 0.99
$p999 = pct 0.999

$avgMsgsPerSec = if ($avgMs -gt 0) { ($frames / ($avgMs / 1000.0)) } else { 0 }

Write-Host "runs=$Runs"
Write-Host "frames_per_run=$frames"
Write-Host ("avg_ms={0:N3}" -f $avgMs)
Write-Host ("p50_ms={0:N3}" -f $p50)
Write-Host ("p99_ms={0:N3}" -f $p99)
Write-Host ("p99.9_ms={0:N3}" -f $p999)
Write-Host ("avg_msgs_per_sec={0:N2}" -f $avgMsgsPerSec)
