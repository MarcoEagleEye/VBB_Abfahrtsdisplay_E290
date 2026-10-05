param(
  [Parameter(Mandatory=$true)][string]$OriginalOrdner
)
$ErrorActionPreference='Stop'
$here=$PSScriptRoot
$root=(Resolve-Path $OriginalOrdner).Path
if(-not (Test-Path (Join-Path $root 'MVG_Abfahrtsdisplay_E290.ino'))){throw 'Der angegebene Ordner ist nicht das Originalprojekt MVG_Abfahrtsdisplay_E290.'}
$oldReadme=Join-Path $root 'README.md'
$oldReadmeCopy=Join-Path $root 'README_UPSTREAM_MVG.md'
if((Test-Path $oldReadme) -and -not(Test-Path $oldReadmeCopy)){Copy-Item $oldReadme $oldReadmeCopy}
$files=@(
 'MVG_Abfahrtsdisplay_E290.ino','config.h',
 'src\settings.h','src\settings.cpp','src\buttons.h','src\buttons.cpp',
 'src\mvg_api.h','src\mvg_api.cpp','src\portal.h','src\portal.cpp',
 'docs\index.html','docs\manifest.json','docs\manifest-mvg-migration.json','docs\firmware\README.txt','werkzeuge\firmware_fuer_installer.ps1','VBB_Preview.html','README.md','README_BERLIN.md','LICENSE_NOTE.md'
)
foreach($f in $files){$src=Join-Path $here $f;$dst=Join-Path $root $f;$dir=Split-Path -Parent $dst;if(-not(Test-Path $dir)){New-Item -ItemType Directory -Force $dir|Out-Null};Copy-Item -Force $src $dst}
# Sicherheitsmassnahme: keine alten MVG-Webinstaller-Binaries unter neuem VBB-Manifest stehen lassen.
$fwDir=Join-Path $root 'docs\firmware'
if(Test-Path $fwDir){@('firmware.bin','bootloader.bin','partitions.bin') | ForEach-Object {$x=Join-Path $fwDir $_; if(Test-Path $x){Remove-Item -Force $x}}}
# Nur Branding im ansonsten unveraenderten Original-Display austauschen.
$display=Join-Path $root 'src\display.cpp'
$text=[IO.File]::ReadAllText($display)
$text=$text.Replace('MVG Abfahrtsdisplay','VBB Abfahrtsdisplay')
$text=$text.Replace('MVG-Abfahrten aktuell','VBB-Abfahrten aktuell')
[IO.File]::WriteAllText($display,$text,(New-Object Text.UTF8Encoding($false)))
# Geraetename im ESP-Web-Tools/Improv-Dialog ebenfalls auf VBB umstellen.
$improv=Join-Path $root 'src\improv_serial.cpp'
if(Test-Path $improv){
  $it=[IO.File]::ReadAllText($improv).Replace('#define IMPROV_FIRMWARE_NAME "MVG Abfahrtsdisplay"','#define IMPROV_FIRMWARE_NAME "VBB Abfahrtsdisplay Berlin"')
  if(-not $it.Contains('#define IMPROV_FIRMWARE_NAME "VBB Abfahrtsdisplay Berlin"')){throw 'Improv-Firmware-Name konnte nicht angepasst werden; Upstream hat sich vermutlich geaendert.'}
  [IO.File]::WriteAllText($improv,$it,(New-Object Text.UTF8Encoding($false)))
}
Write-Host "Berlin/VBB-Patch angewendet auf: $root" -ForegroundColor Green
Write-Host 'Danach in Arduino IDE kompilieren: Board Heltec Vision Master E290.'
