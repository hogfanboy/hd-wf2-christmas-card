# Effects probe: saves the card's show, plays the newest effects, then puts the card's own show back (about 2.5 min):
#   1 Santa's sleigh full screen (night town, moon, double-size sleigh)   2 turkey + leaves   3 Easter bunny + eggs
#   4 bats + pumpkins   5 shamrocks   6 US flag   7 countdown to Thanksgiving (match the holiday: leaves behind)
#   8 countdown to the next holiday   9 countdown to Easter (eggs behind)   10 "Happy 4th!" over the flag
#   11 balloons   12 aquarium   13 hearts   14 "Merry Christmas" over the small sleigh
#   powershell -ExecutionPolicy Bypass -File tools\effects-probe.ps1 [-Ip 192.168.50.16] [-Loops 1]
param([string]$Ip = '192.168.50.16', [int]$Loops = 1)
$base = "http://$Ip"
try { $saved = Invoke-WebRequest -UseBasicParsing -Uri "$base/api/show" -TimeoutSec 10 }
catch { Write-Host "Couldn't reach the card at $Ip : $($_.Exception.Message)"; exit 1 }
$t = "`t"
$lines = @(
  "order${t}inorder",
  "fx${t}16${t}FFFFFF${t}000000${t}4${t}sleigh${t}@",
  "fx${t}14${t}FFFFFF${t}000000${t}4${t}turkey${t}@",
  "fx${t}12${t}FFFFFF${t}000000${t}4${t}bunny${t}@",
  "fx${t}12${t}FFFFFF${t}000000${t}4${t}bats${t}@",
  "fx${t}10${t}FFFFFF${t}000000${t}4${t}shamrocks${t}@",
  "fx${t}10${t}FFFFFF${t}000000${t}4${t}flag${t}@",
  "countdown${t}8${t}theme${t}000000${t}12${t}auto${t}thanksgiving${t}${t}@",
  "countdown${t}8${t}theme${t}000000${t}12${t}auto${t}next${t}${t}@",
  "countdown${t}8${t}theme${t}000000${t}12${t}auto${t}easter${t}${t}@",
  "text${t}10${t}FFFFFF${t}000000${t}11${t}flag${t}Happy 4th!${t}@",
  "fx${t}10${t}FFFFFF${t}000000${t}4${t}balloons${t}@",
  "fx${t}10${t}FFFFFF${t}000000${t}4${t}fish${t}@",
  "fx${t}10${t}FFFFFF${t}000000${t}4${t}hearts${t}@",
  "text${t}12${t}FFFFFF${t}000000${t}11${t}sleigh${t}Merry Christmas${t}@")
$demo = ($lines -join "`n") + "`n"
function Send-Show($text) {
  Invoke-RestMethod -Method Post -Uri "$base/api/show" -ContentType 'text/plain' -Body ([Text.Encoding]::UTF8.GetBytes($text)) -TimeoutSec 10 | Out-Null
  Invoke-RestMethod -Method Post -Uri "$base/api/play" -TimeoutSec 10 | Out-Null
}
Send-Show $demo
Write-Host ("{0}  effects demo playing ({1} loop(s), about {2} s)" -f (Get-Date -Format 'HH:mm:ss'), $Loops, 155 * $Loops)
Start-Sleep -Seconds (155 * $Loops)
Send-Show $saved.Content
Write-Host 'Your own show is back. Tell Claude what to change (size, speed, colors) or "effects look good".'
