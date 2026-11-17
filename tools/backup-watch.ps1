# Waits for an ESP32-S3 (USB VID 303A) serial port to appear, then immediately
# reads chip/flash info and dumps the whole flash to firmware-backup\.
param([int]$TimeoutSec = 180)
$esptool = "$env:LOCALAPPDATA\Programs\esptool\esptool-windows-amd64\esptool.exe"
$outDir  = Join-Path $PSScriptRoot '..\firmware-backup'
New-Item -ItemType Directory -Force $outDir | Out-Null
$before = @([System.IO.Ports.SerialPort]::GetPortNames())
Write-Host "Watching for the card (existing ports: $($before -join ', '))..."
$deadline = (Get-Date).AddSeconds($TimeoutSec)
while ((Get-Date) -lt $deadline) {
  $now = @([System.IO.Ports.SerialPort]::GetPortNames())
  $new = $now | Where-Object { $before -notcontains $_ }
  foreach ($p in $new) {
    $dev = Get-CimInstance Win32_PnPEntity -Filter "Name LIKE '%($p)%'" -ErrorAction SilentlyContinue
    if ($dev.PNPDeviceID -notmatch 'VID_303A') { continue }
    Write-Host "$(Get-Date -Format HH:mm:ss.fff) Card appeared on $p ($($dev.PNPDeviceID))"
    & $esptool --chip esp32s3 --port $p --before usb-reset --after no-reset flash-id
    if ($LASTEXITCODE -ne 0) { Write-Host "flash-id failed (exit $LASTEXITCODE)"; $before = $now; continue }
    $file = Join-Path $outDir ("wf2_backup_{0}.bin" -f (Get-Date -Format yyyyMMdd_HHmmss))
    & $esptool --chip esp32s3 --port $p --before no-reset --after no-reset read-flash 0 ALL $file
    Write-Host "read-flash exit code: $LASTEXITCODE  file: $file"
    exit $LASTEXITCODE
  }
  $before = $now
  Start-Sleep -Milliseconds 150
}
Write-Host "Timed out: card never appeared."
exit 2
