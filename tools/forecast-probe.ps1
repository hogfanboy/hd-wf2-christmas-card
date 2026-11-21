# Forecast layout probe: jumps to the show's first weather item and shows only its forecast in each layout, one
# minute each. The layout number is shown in red at the bottom right of the panel:
#   0 plain (today's), 1 every other column shaded, 2 every column a shaded tile, 3 four days, every other shaded.
#   powershell -ExecutionPolicy Bypass -File tools\forecast-probe.ps1 [-Ip 192.168.50.16] [-Styles 0,1,2,3] [-Seconds 60]
param([string]$Ip = '192.168.50.16', [int[]]$Styles = @(0, 1, 2, 3), [int]$Seconds = 60)
foreach ($s in $Styles) {
  try { Invoke-RestMethod -Method Post -Uri "http://$Ip/api/fcprobe?s=$s" -TimeoutSec 10 | Out-Null }
  catch { Write-Host "Couldn't reach the card at $Ip : $($_.Exception.Message)"; exit 1 }
  Write-Host ("{0}  layout {1} on the panel now ({2} s)" -f (Get-Date -Format 'HH:mm:ss'), $s, $Seconds)
  Start-Sleep -Seconds $Seconds
}
Write-Host 'Done. Tell Claude which layout looked best (the last one stays until the card restarts).'
