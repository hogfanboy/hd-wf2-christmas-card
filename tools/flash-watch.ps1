# Waits for an ESP32-S3 (USB VID 303A) port, resets it into ROM download mode,
# writes a merged firmware image at 0x0, reboots it and shows its serial log.
# Usage: tools\flash-watch.ps1 -Bin firmware\build\wf2_colortest\wf2_colortest.ino.merged.bin
param([Parameter(Mandatory)][string]$Bin, [int]$TimeoutSec = 180, [int]$LogSec = 25)
$esptool = "$env:LOCALAPPDATA\Programs\esptool\esptool-windows-amd64\esptool.exe"
$Bin = (Resolve-Path $Bin).Path

function Find-CardPort {
  foreach ($p in [System.IO.Ports.SerialPort]::GetPortNames()) {
    $dev = Get-CimInstance Win32_PnPEntity -Filter "Name LIKE '%($p)%'" -ErrorAction SilentlyContinue
    if ($dev.PNPDeviceID -match 'VID_303A') { return $p }
  }
}

Write-Host "Flashing $Bin"
Write-Host "Waiting for the card... (plug it in now)"
$deadline = (Get-Date).AddSeconds($TimeoutSec)
$rc = -1
while ((Get-Date) -lt $deadline) {
  $port = Find-CardPort
  if (-not $port) { Start-Sleep -Milliseconds 100; continue }
  Write-Host "$(Get-Date -Format HH:mm:ss.fff) Card on $port, connecting..."
  Start-Sleep -Milliseconds 250   # let Windows finish setting up the port
  & $esptool --chip esp32s3 --port $port --before usb-reset --after hard-reset write-flash --flash-mode dio --flash-size 8MB 0x0 $Bin
  $rc = $LASTEXITCODE
  if ($rc -eq 0) { break }
  Write-Host "Attempt failed (exit $rc) - unplug and plug in again (holding S1); still watching..."
  while (Find-CardPort) { Start-Sleep -Milliseconds 200 }   # wait for it to go away
}
Write-Host "write-flash exit code: $rc"
if ($rc -ne 0) { exit $rc }

Write-Host "Reading the card's log for $LogSec seconds..."
$end = (Get-Date).AddSeconds($LogSec)
while ((Get-Date) -lt $end) {
  $port = Find-CardPort
  if (-not $port) { Start-Sleep -Milliseconds 300; continue }
  try {
    $sp = New-Object System.IO.Ports.SerialPort $port, 115200
    $sp.DtrEnable = $true; $sp.ReadTimeout = 500; $sp.Open()
    while ((Get-Date) -lt $end) { try { Write-Host -NoNewline $sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 200 }
    $sp.Close()
  } catch { Start-Sleep -Milliseconds 300 }
}
Write-Host "`nDone."
