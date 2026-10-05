# VBB/Berlin-Version des Upstream-Exportskripts.
# Erwartet vorher: Arduino IDE -> Sketch -> Kompilierte Binaerdatei exportieren.
$ErrorActionPreference = 'Stop'
$repo       = Split-Path -Parent $PSScriptRoot
$sketch     = 'MVG_Abfahrtsdisplay_E290'
$build      = Join-Path $repo 'build\esp32.esp32.heltec_vision_master_e290'
$ziel       = Join-Path $repo 'docs\firmware'
$manifest   = Join-Path $repo 'docs\manifest.json'
$migration  = Join-Path $repo 'docs\manifest-mvg-migration.json'
$maxApp     = 3342336

function Ende([int]$code) {
  if (-not $env:FW_EXPORT_BAT) { Write-Host ''; Read-Host 'Enter zum Schliessen' | Out-Null }
  exit $code
}
function Abbruch([string]$text) {
  Write-Host ''; Write-Host "ABBRUCH: $text" -ForegroundColor Red
  Write-Host 'Es wurde nichts kopiert.'; Ende 1
}

try {
  $teile = [ordered]@{
    'bootloader.bin' = "$sketch.ino.bootloader.bin"
    'partitions.bin' = "$sketch.ino.partitions.bin"
    'firmware.bin'   = "$sketch.ino.bin"
  }
  $daten = @{}
  foreach ($name in $teile.Keys) {
    $pfad = Join-Path $build $teile[$name]
    if (-not (Test-Path $pfad)) { Abbruch "$($teile[$name]) fehlt. Zuerst in Arduino IDE: Sketch -> Kompilierte Binaerdatei exportieren." }
    $daten[$name] = [IO.File]::ReadAllBytes($pfad)
  }

  $latin1 = [Text.Encoding]::GetEncoding(28591)
  foreach ($name in $teile.Keys) {
    if ($latin1.GetString($daten[$name]).Contains('secrets.h eingebunden')) {
      Abbruch "$name wurde MIT secrets.h gebaut. secrets.h entfernen und neu exportieren."
    }
  }
  $fw = $daten['firmware.bin']
  if ($fw.Length -lt 1024 -or $fw[0] -ne 0xE9) { Abbruch 'firmware.bin ist keine ESP32-Firmware.' }
  if ($fw.Length -gt $maxApp) { Abbruch 'firmware.bin ist groesser als die App-Partition.' }
  if (-not $latin1.GetString($fw).Contains('MVG_Abfahrtsdisplay_E290/firmware')) { Abbruch 'Projektkennung fehlt; falscher Sketch?' }
  if ($daten['bootloader.bin'][0] -ne 0xE9) { Abbruch 'bootloader.bin ist kein ESP32-Bootloader.' }
  $pt = $daten['partitions.bin']
  if ($pt.Length -ne 3072 -or $pt[0] -ne 0xAA -or $pt[1] -ne 0x50) { Abbruch 'partitions.bin ist keine gueltige Partitionstabelle.' }
  if (-not (Test-Path (Join-Path $ziel 'boot_app0.bin'))) { Abbruch 'docs\firmware\boot_app0.bin fehlt. Das komplette Originalprojekt als Basis verwenden.' }

  $fwZeit = (Get-Item (Join-Path $build $teile['firmware.bin'])).LastWriteTime
  $code = @(Get-ChildItem $repo -File | Where-Object { $_.Extension -in '.ino','.h','.csv' }) + @(Get-ChildItem (Join-Path $repo 'src') -File)
  $neuer = $code | Where-Object { $_.LastWriteTime -gt $fwZeit -and $_.Name -ne 'secrets.h' }
  if ($neuer) {
    Write-Host 'Achtung: nach dem Export geaendert:' -ForegroundColor Yellow
    $neuer | ForEach-Object { Write-Host "  $($_.Name)" }
    if ((Read-Host 'Trotzdem uebernehmen? (j/n)') -ne 'j') { Abbruch 'Bitte neu exportieren.' }
  }

  $ino = [IO.File]::ReadAllText((Join-Path $repo "$sketch.ino"))
  $treffer = [regex]::Match($ino, '#define\s+FW_VERSION\s+"([^"]+)"')
  if (-not $treffer.Success) { Abbruch 'FW_VERSION nicht gefunden.' }
  $version = $treffer.Groups[1].Value

  # Erst alle Ziele/Manifeste lesen, damit ein Lesefehler vor dem Schreiben auffaellt.
  $jsonMain = [IO.File]::ReadAllText($manifest)
  $jsonMig  = [IO.File]::ReadAllText($migration)
  foreach ($name in $teile.Keys) { [IO.File]::WriteAllBytes((Join-Path $ziel $name), $daten[$name]) }
  $jsonMain = [regex]::Replace($jsonMain, '"version"\s*:\s*"[^"]*"', "`"version`": `"$version`"")
  $jsonMig  = [regex]::Replace($jsonMig,  '"version"\s*:\s*"[^"]*"', "`"version`": `"$version`"")
  [IO.File]::WriteAllText($manifest,  $jsonMain, (New-Object Text.UTF8Encoding($false)))
  [IO.File]::WriteAllText($migration, $jsonMig,  (New-Object Text.UTF8Encoding($false)))

  Write-Host ''; Write-Host "Fertig: VBB Firmware $version nach docs\firmware kopiert." -ForegroundColor Green
  foreach ($name in $teile.Keys) { Write-Host ("  {0,-15} {1,10:N0} Bytes" -f $name, $daten[$name].Length) }
  Write-Host 'Beide Webinstaller-Manifeste wurden aktualisiert.'
  Write-Host 'Pruefung "secrets.h eingebunden": nicht enthalten.'
} catch { Abbruch "Unerwarteter Fehler: $($_.Exception.Message)" }
Ende 0
