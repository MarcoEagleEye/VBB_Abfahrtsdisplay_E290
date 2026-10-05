# Teststatus – VBB Abfahrtsdisplay 3.0.0-vbb

Stand: 2026-10-02

## In dieser Arbeitsumgebung geprüft

- Browser-Simulator: JavaScript-Syntax mit Node.js geprüft
- Geräte-Webportal: eingebettetes JavaScript extrahiert und mit Node.js geprüft
- Webinstaller: eingebettetes JavaScript geprüft
- `manifest.json` und `manifest-mvg-migration.json`: gültiges JSON, ESP32-S3 und erwartete vier Flash-Offets geprüft
- GitHub-Actions-Workflow: gültiges YAML geprüft
- Sicherheitsmarker für OTA und `secrets.h`-Schutz vorhanden
- keine alten MVG-`firmware.bin`/`bootloader.bin`/`partitions.bin` im Berlin-Webinstaller-Paket
- 4-Bahnhof-/A-B-/BOOT-Logik statisch geprüft
- GPL-3.0-Lizenz beigefügt

## Noch nicht in dieser Arbeitsumgebung möglich

Ein echter Arduino/ESP32-S3-Compile- und Hardware-Flash-Test konnte hier nicht ausgeführt werden, weil Arduino CLI, ESP32-Core und Heltec-Toolchain nicht lokal vorhanden sind. Der enthaltene GitHub-Actions-Workflow ist deshalb der erste echte Compile-Gate und erzeugt bei Erfolg die vier Webinstaller-Binaries sowie `firmware.bin` für OTA.

Für den Hardwaretest danach: erst Webinstaller/USB, dann VBB-Suche, 4 Slots, A/B-Zuordnung, BOOT kurz/lang und OTA prüfen.
