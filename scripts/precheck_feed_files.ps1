param(
  [Parameter(Mandatory=$true)][string[]]$Files
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Classify-File([string]$Path) {
  if (!(Test-Path $Path)) {
    return [pscustomobject]@{ File=$Path; Exists=$false; Type='missing'; Detail='not found' }
  }

  $bytes = [System.IO.File]::ReadAllBytes($Path)
  $n = [Math]::Min($bytes.Length, 4096)
  if ($n -eq 0) {
    return [pscustomobject]@{ File=$Path; Exists=$true; Type='empty'; Detail='0 bytes' }
  }

  $printable = 0
  $commas = 0
  $newlines = 0
  for ($i=0; $i -lt $n; $i++) {
    $b = $bytes[$i]
    if (($b -ge 32 -and $b -le 126) -or $b -eq 9 -or $b -eq 10 -or $b -eq 13) { $printable++ }
    if ($b -eq 44) { $commas++ }
    if ($b -eq 10) { $newlines++ }
  }

  $ratio = [double]$printable / [double]$n
  if ($ratio -gt 0.95 -and $commas -gt 10 -and $newlines -gt 5) {
    return [pscustomobject]@{ File=$Path; Exists=$true; Type='text/csv-like'; Detail=("printable_ratio={0:N3}, commas={1}, newlines={2}" -f $ratio,$commas,$newlines) }
  }

  $b0 = $bytes[0]
  $b1 = if ($bytes.Length -gt 1) { $bytes[1] } else { 0 }
  return [pscustomobject]@{ File=$Path; Exists=$true; Type='binary-like'; Detail=("printable_ratio={0:N3}, first_bytes=0x{1:X2} 0x{2:X2}" -f $ratio,$b0,$b1) }
}

$rows = foreach ($f in $Files) { Classify-File $f }
$rows | Format-Table -AutoSize
