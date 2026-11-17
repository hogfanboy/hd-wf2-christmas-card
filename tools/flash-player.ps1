# Flash the Pocket Jumbotron GIF player + the GIF storage image (firmware/build).
$b = Join-Path $PSScriptRoot '..\firmware\build'
$core = "$env:LOCALAPPDATA\Arduino15\packages\esp32\hardware\esp32\3.3.13\tools\partitions\boot_app0.bin"
& (Join-Path $PSScriptRoot 'flash-watch.ps1') -LogSec 60 -Images @(
  "0x0=$b\pocket_jumbotron\pocket_jumbotron.ino.bootloader.bin",
  "0x8000=$b\pocket_jumbotron\pocket_jumbotron.ino.partitions.bin",
  "0xe000=$core",
  "0x10000=$b\pocket_jumbotron\pocket_jumbotron.ino.bin",
  "0x390000=$b\littlefs.bin")
