param([string]$Ziel = (Join-Path $PSScriptRoot 'fertiges_projekt'))
$ErrorActionPreference='Stop'
$zip=Join-Path $env:TEMP 'MVG_Abfahrtsdisplay_E290-main.zip'
$tmp=Join-Path $env:TEMP ('vbb_e290_'+[guid]::NewGuid().ToString('N'))
Write-Host 'Lade Originalprojekt von GitHub...'
Invoke-WebRequest -UseBasicParsing 'https://github.com/Fluddel94/MVG_Abfahrtsdisplay_E290/archive/refs/heads/main.zip' -OutFile $zip
New-Item -ItemType Directory -Force $tmp|Out-Null
Expand-Archive -Force $zip $tmp
$src=Join-Path $tmp 'MVG_Abfahrtsdisplay_E290-main'
$project=Join-Path $Ziel 'MVG_Abfahrtsdisplay_E290'
if(Test-Path $project){Remove-Item -Recurse -Force $project}
New-Item -ItemType Directory -Force $Ziel|Out-Null
Copy-Item -Recurse -Force $src $project
& (Join-Path $PSScriptRoot 'apply_to_original.ps1') -OriginalOrdner $project
Write-Host ''
Write-Host "Fertiges Arduino-Projekt: $project" -ForegroundColor Green
Write-Host 'Datei MVG_Abfahrtsdisplay_E290.ino in Arduino IDE oeffnen.'
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
Remove-Item -Force $zip -ErrorAction SilentlyContinue
