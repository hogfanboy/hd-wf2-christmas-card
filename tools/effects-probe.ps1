# New effects probe (fw 1.0): saves the card's show, plays each new effect full screen and behind text, then puts
# the card's own show back. Order on the panel (about 90 s):
#   1 Santa's sleigh (15 s)  2 Balloons  3 Aquarium  4 Rain  5 "Merry Christmas" over the sleigh
#   6 Clock over the aquarium  7 Weather with "match the weather" (snow/rain only when it does)  8 Christmas countdown over balloons
#   powershell -ExecutionPolicy Bypass -File tools\effects-probe.ps1 [-Ip 192.168.50.16] [-Loops 1]
param([string]$Ip = '192.168.50.16', [int]$Loops = 1)
$base = "http://$Ip"
try { $saved = Invoke-WebRequest -UseBasicParsing -Uri "$base/api/show" -TimeoutSec 10 }
catch { Write-Host "Couldn't reach the card at $Ip : $($_.Exception.Message)"; exit 1 }
$t = "`t"
$lines = @(
  "fx${t}15${t}FFFFFF${t}000000${t}4${t}sleigh${t}@",
  "fx${t}10${t}FFFFFF${t}000000${t}4${t}balloons${t}@",
  "fx${t}10${t}FFFFFF${t}000000${t}4${t}fish${t}@",
  "fx${t}8${t}FFFFFF${t}000000${t}4${t}rain${t}@",
  "text${t}10${t}theme${t}000000${t}11${t}sleigh${t}Merry Christmas${t}@",
  "clock${t}8${t}theme${t}000000${t}12${t}fish${t}12${t}@",
  "weather${t}8${t}theme${t}000000${t}12${t}auto${t}A${t}auto${t}auto${t}${t}@",
  "countdown${t}8${t}theme${t}000000${t}12${t}balloons${t}12-25${t}Christmas${t}@")
$demo = ($lines -join "`n") + "`n"
function Send-Show($text) {
  Invoke-RestMethod -Method Post -Uri "$base/api/show" -ContentType 'text/plain' -Body ([Text.Encoding]::UTF8.GetBytes($text)) -TimeoutSec 10 | Out-Null
  Invoke-RestMethod -Method Post -Uri "$base/api/play" -TimeoutSec 10 | Out-Null
}
Send-Show $demo
Write-Host ("{0}  effects demo playing ({1} loop(s), about {2} s)" -f (Get-Date -Format 'HH:mm:ss'), $Loops, 77 * $Loops)
Start-Sleep -Seconds (77 * $Loops)
Send-Show $saved.Content
Write-Host 'Your own show is back. Tell Claude what to change (size, speed, colors) or "effects look good".'
