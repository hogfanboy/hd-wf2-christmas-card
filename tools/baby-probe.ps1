# Baby countdown probe: saves the card's show, plays the baby screens, then puts the card's own show back (about 1 min):
#   1 the stork effect full screen   2 "DAYS UNTIL BABY EMMA" (due in 100 days: footprints, rattles, hearts behind)
#   3 the due day ("TODAY!", the small stork flies behind)   4 five days past due ("SOON!", "ANY DAY NOW")
#   powershell -ExecutionPolicy Bypass -File tools\baby-probe.ps1 [-Ip 192.168.50.16] [-Loops 1]
param([string]$Ip = '192.168.50.16', [int]$Loops = 1)
$base = "http://$Ip"
try { $saved = Invoke-WebRequest -UseBasicParsing -Uri "$base/api/show" -TimeoutSec 10 }
catch { Write-Host "Couldn't reach the card at $Ip : $($_.Exception.Message)"; exit 1 }
$t = "`t"
$soon = (Get-Date).AddDays(100).ToString('yyyy-MM-dd')
$today = (Get-Date).ToString('yyyy-MM-dd')
$late = (Get-Date).AddDays(-5).ToString('yyyy-MM-dd')
$lines = @(
  "order${t}inorder",
  "fx${t}16${t}FFFFFF${t}000000${t}4${t}baby${t}@",
  "countdown${t}12${t}theme${t}000000${t}12${t}baby${t}baby:$soon${t}Baby Emma${t}@",
  "countdown${t}14${t}theme${t}000000${t}12${t}baby${t}baby:$today${t}Baby Emma${t}@",
  "countdown${t}12${t}theme${t}000000${t}12${t}baby${t}baby:$late${t}${t}@")
$demo = ($lines -join "`n") + "`n"
function Send-Show($text) {
  Invoke-RestMethod -Method Post -Uri "$base/api/show" -ContentType 'text/plain' -Body ([Text.Encoding]::UTF8.GetBytes($text)) -TimeoutSec 10 | Out-Null
  Invoke-RestMethod -Method Post -Uri "$base/api/play" -TimeoutSec 10 | Out-Null
}
Send-Show $demo
Write-Host ("{0}  baby demo playing ({1} loop(s), about {2} s)" -f (Get-Date -Format 'HH:mm:ss'), $Loops, 54 * $Loops)
Start-Sleep -Seconds (54 * $Loops)
Send-Show $saved.Content
Write-Host 'Your own show is back. Tell Claude what to change (stork, footprints, colors, words) or "baby looks good".'
