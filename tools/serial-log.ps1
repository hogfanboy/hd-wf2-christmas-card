# Shows the card's serial log (USB) for a while; reconnects if the card restarts.
param([int]$Seconds = 600)
function Find-CardPort {
  foreach ($p in [System.IO.Ports.SerialPort]::GetPortNames()) {
    $dev = Get-CimInstance Win32_PnPEntity -Filter "Name LIKE '%($p)%'" -ErrorAction SilentlyContinue
    if ($dev.PNPDeviceID -match 'VID_303A') { return $p }
  }
}
$end = (Get-Date).AddSeconds($Seconds)
Write-Host "Card log for $Seconds s (Ctrl-C to stop)"
while ((Get-Date) -lt $end) {
  $port = Find-CardPort
  if (-not $port) { Start-Sleep -Milliseconds 300; continue }
  try {
    $sp = New-Object System.IO.Ports.SerialPort $port, 115200
    $sp.DtrEnable = $false; $sp.RtsEnable = $false; $sp.Open()
    while ((Get-Date) -lt $end -and $sp.IsOpen) { Write-Host -NoNewline $sp.ReadExisting(); Start-Sleep -Milliseconds 200 }
    $sp.Close()
  } catch { Start-Sleep -Milliseconds 500 }
}
