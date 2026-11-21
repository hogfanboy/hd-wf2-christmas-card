# Effects probe: saves the card's show, plays the newest effect, then puts the card's own show back (about 40 s):
#   1 Christmas lights full screen (20 s: twinkle, chase, alternate blink, over snow)
#   2 "Merry Christmas" over the lights (they only twinkle behind text)  3 Clock over the lights
#   powershell -ExecutionPolicy Bypass -File tools\effects-probe.ps1 [-Ip 192.168.50.16] [-Loops 1]
param([string]$Ip = '192.168.50.16', [int]$Loops = 1)
$base = "http://$Ip"
try { $saved = Invoke-WebRequest -UseBasicParsing -Uri "$base/api/show" -TimeoutSec 10 }
catch { Write-Host "Couldn't reach the card at $Ip : $($_.Exception.Message)"; exit 1 }
$t = "`t"
$lines = @(
  "fx${t}20${t}FFFFFF${t}000000${t}4${t}lights${t}@",
  "text${t}10${t}theme${t}000000${t}11${t}lights${t}Merry Christmas${t}@",
  "clock${t}8${t}theme${t}000000${t}12${t}lights${t}12${t}@")
$demo = ($lines -join "`n") + "`n"
function Send-Show($text) {
  Invoke-RestMethod -Method Post -Uri "$base/api/show" -ContentType 'text/plain' -Body ([Text.Encoding]::UTF8.GetBytes($text)) -TimeoutSec 10 | Out-Null
  Invoke-RestMethod -Method Post -Uri "$base/api/play" -TimeoutSec 10 | Out-Null
}
Send-Show $demo
Write-Host ("{0}  effects demo playing ({1} loop(s), about {2} s)" -f (Get-Date -Format 'HH:mm:ss'), $Loops, 40 * $Loops)
Start-Sleep -Seconds (40 * $Loops)
Send-Show $saved.Content
Write-Host 'Your own show is back. Tell Claude what to change (size, speed, colors) or "effects look good".'
