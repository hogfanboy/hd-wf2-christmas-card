# Soak test: asks each card for /api/info every minute and logs its memory health to soak\<name>.csv.
# Prints a line per card per poll and a summary at the end (lowest free heap, smallest free block, restarts seen).
#   powershell -ExecutionPolicy Bypass -File tools\soak-log.ps1 -Cards 192.168.50.16,192.168.50.199 -Hours 24
# Safe to stop with Ctrl+C: the CSV is written as it goes; run it again with -Summary to only print the summary.
param(
  [string[]]$Cards = @('192.168.50.16', '192.168.50.199'),
  [double]$Hours = 24,
  [int]$EverySec = 60,
  [switch]$Summary
)
$dir = Join-Path (Split-Path $PSScriptRoot -Parent) 'soak'
New-Item -ItemType Directory -Force $dir | Out-Null

function Show-Summary {
  foreach ($ip in $Cards) {
    $f = Join-Path $dir ($ip + '.csv')
    if (-not (Test-Path $f)) { continue }
    $rows = @(Import-Csv $f | Where-Object { $_.ok -eq '1' })
    $miss = @(Import-Csv $f | Where-Object { $_.ok -ne '1' }).Count
    if (-not $rows.Count) { "$ip : no answers ($miss missed)"; continue }
    $restarts = 0; $prev = $null
    foreach ($r in $rows) { if ($prev -and [int]$r.up -lt [int]$prev.up) { $restarts++ }; $prev = $r }
    $minHeap = ($rows | Measure-Object -Property heap -Minimum).Minimum
    $minBlock = ($rows | Measure-Object -Property block -Minimum).Minimum
    $minEver = ($rows | Measure-Object -Property minHeap -Minimum).Minimum
    $last = $rows[-1]
    "{0} : {1} polls ({2} missed), fw {3}, restarts seen {4} (last start: {5}/{6}), quiet {7}, crashes {8}" -f `
      $ip, $rows.Count, $miss, $last.fw, $restarts, $last.boot, $last.bootWhy, $last.quietRestarts, $last.crashes
    "    free heap low {0}, biggest block low {1}, lowest-ever free {2}, up {3:N1} h" -f $minHeap, $minBlock, $minEver, ([int]$last.up / 3600)
  }
}
if ($Summary) { Show-Summary; exit }

$end = (Get-Date).AddHours($Hours)
"Logging $($Cards -join ', ') every $EverySec s until $end -> $dir"
while ((Get-Date) -lt $end) {
  foreach ($ip in $Cards) {
    $f = Join-Path $dir ($ip + '.csv')
    if (-not (Test-Path $f)) { 'time,ok,fw,up,heap,block,minHeap,boot,bootWhy,quietRestarts,crashes' | Out-File $f -Encoding utf8 }
    $t = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
    try {
      $i = Invoke-RestMethod -Uri "http://$ip/api/info" -TimeoutSec 8
      "$t,1,$($i.fw),$($i.up),$($i.heap),$($i.block),$($i.minHeap),$($i.boot),$($i.bootWhy),$($i.quietRestarts),$($i.crashes)" | Out-File $f -Append -Encoding utf8
      "{0} {1,-15} fw {2}  up {3,6:N1} h  free {4,6}  block {5,6}  lowest {6,6}  start {7}" -f $t, $ip, $i.fw, ($i.up / 3600), $i.heap, $i.block, $i.minHeap, $i.boot
    } catch {
      "$t,0,,,,,,,,," | Out-File $f -Append -Encoding utf8
      "{0} {1,-15} no answer" -f $t, $ip
    }
  }
  Start-Sleep -Seconds $EverySec
}
Show-Summary
