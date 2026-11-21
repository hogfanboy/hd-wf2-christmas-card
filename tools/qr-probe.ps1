# QR camera probe: shows the card's address screen in fast scan at 2, 3, 4 and 5 color bits, one minute each.
# The bit depth is shown in red at the bottom right of the panel. Point the phone camera at the QR code each time
# and note which depths still scan and which one looks best.
#   powershell -ExecutionPolicy Bypass -File tools\qr-probe.ps1 [-Ip 192.168.50.16] [-Bits 2,3,4,5] [-Seconds 60]
param([string]$Ip = '192.168.50.16', [int[]]$Bits = @(2, 3, 4, 5), [int]$Seconds = 60)
foreach ($b in $Bits) {
  try { Invoke-RestMethod -Method Post -Uri "http://$Ip/api/qr?bits=$b" -TimeoutSec 10 | Out-Null }
  catch { Write-Host "Couldn't reach the card at $Ip : $($_.Exception.Message)"; exit 1 }
  Write-Host ("{0}  {1}-bit: try the phone camera now ({2} s)" -f (Get-Date -Format 'HH:mm:ss'), $b, $Seconds)
  Start-Sleep -Seconds $Seconds
}
Write-Host 'Done. Tell Claude which bit depths scanned and which looked best.'
