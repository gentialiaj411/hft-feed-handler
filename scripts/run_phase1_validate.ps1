param(
  [string]$NasdaqGz = "data/raw/nasdaq/tvagg.gz",
  [string]$IexGz = "data/raw/iex/20241001_IEXTP1_DPLS1.0.pcap.gz",
  [string]$CboeGz = "",
  [string]$BuildDir = "build/Debug"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Expand-GzipFile {
  param([string]$InputPath, [string]$OutputPath)
  if (!(Test-Path $InputPath)) {
    throw "Missing input gzip: $InputPath"
  }
  $inStream = [System.IO.File]::OpenRead($InputPath)
  try {
    $gzip = New-Object System.IO.Compression.GzipStream($inStream, [System.IO.Compression.CompressionMode]::Decompress)
    try {
      $outStream = [System.IO.File]::Create($OutputPath)
      try {
        $gzip.CopyTo($outStream)
      } finally {
        $outStream.Dispose()
      }
    } finally {
      $gzip.Dispose()
    }
  } finally {
    $inStream.Dispose()
  }
}

$expandedDir = "data/raw/expanded"
New-Item -ItemType Directory -Force $expandedDir | Out-Null

$nasdaqOut = Join-Path $expandedDir "nasdaq.bin"
$iexOut = Join-Path $expandedDir "iex.bin"
Expand-GzipFile -InputPath $NasdaqGz -OutputPath $nasdaqOut
Expand-GzipFile -InputPath $IexGz -OutputPath $iexOut

$exe = Join-Path $BuildDir "phase1_parser_validate.exe"
if (!(Test-Path $exe)) {
  throw "Validator executable not found: $exe"
}

if ($CboeGz -and (Test-Path $CboeGz)) {
  $cboeOut = Join-Path $expandedDir "cboe.bin"
  Expand-GzipFile -InputPath $CboeGz -OutputPath $cboeOut
  & $exe $nasdaqOut $iexOut $cboeOut
} else {
  & $exe $nasdaqOut $iexOut
}
