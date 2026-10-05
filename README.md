# VBB Abfahrtsdisplay Berlin – Heltec Vision Master E290

Berlin/VBB-Umbau des Projekts `Fluddel94/MVG_Abfahrtsdisplay_E290`.

## Fertiger Funktionsumfang

- VBB-Live-Abfahrten über `v6.vbb.transport.rest`
- bis zu **4 Bahnhöfe/Haltestellen**
- S-Bahn, U-Bahn, Tram, Bus, Regional- und Fernverkehr
- Verspätungen, Ausfälle und Warnhinweise
- pro Bahnhof Zielgruppen **Richtung A / Richtung B / ignorieren**
- pro Bahnhof frei benennbare Richtung A/B und Standardrichtung
- gemischter Modus mit Seite 1 (1–4) und Seite 2 (5–8)
- BOOT kurz: alle Ansichten der belegten Bahnhöfe durchschalten
- BOOT 2 Sekunden: direkt zum nächsten belegten Bahnhof
- Auto-Rückkehr nach 30 Sekunden zur Standardansicht
- mittlere Taste: WLAN-QR kurz / Systeminfo + Portal 3 Sekunden
- Webportal mit VBB-Suche und A/B-Zuordnung
- OTA-Firmwareupload im Portal
- ESP-Web-Tools-Webinstaller vorbereitet
- Browser-Simulator `VBB_Preview.html`

## Schnellster Weg zum kompletten Arduino-Projekt unter Windows

1. Dieses Paket entpacken.
2. Rechtsklick/PowerShell im Ordner öffnen.
3. Ausführen:
   `powershell -ExecutionPolicy Bypass -File .\make_full_source.ps1`
4. Danach liegt das komplette Projekt unter
   `fertiges_projekt\MVG_Abfahrtsdisplay_E290\`.
5. Arduino IDE: ESP32-Core **3.3.12**, Library **heltec-eink-modules 4.6.0**, **ArduinoJson 7.4.3**.
6. Board: **Heltec Vision Master E290**.

Das Skript lädt das GPL-3.0-Originalprojekt von GitHub, legt die Berlin-Dateien darüber und ändert nur die zwei sichtbaren MVG-Branding-Texte in `display.cpp`.

## Lokal Firmware + Webinstaller erzeugen

Nach `make_full_source.ps1` kannst du auch komplett lokal bauen:

1. Im erzeugten Projekt `secrets.h` entfernen, falls vorhanden.
2. Arduino IDE: **Sketch → Kompilierte Binärdatei exportieren**.
3. Danach im erzeugten Projekt `werkzeuge\firmware_fuer_installer.bat` starten.
4. Das angepasste Export-Skript prüft weiterhin den Geheimnis-Marker und die Firmware-Kennung und legt `firmware.bin`, `bootloader.bin` und `partitions.bin` unter `docs\firmware` ab; `boot_app0.bin` bleibt aus dem Original erhalten.
5. Den Ordner `docs` über HTTPS (z. B. GitHub Pages) veröffentlichen.

Der Improv-Firmware-Name und `docs/manifest.json` heißen beide exakt **VBB Abfahrtsdisplay Berlin**, damit ESP Web Tools Install/Update korrekt zuordnet.

## Automatischer Build auf GitHub

Die Datei `.github/workflows/build-vbb.yml` kann dieses Paket als eigenes GitHub-Repository bauen. Sie lädt das Originalprojekt, wendet den Overlay an, kompiliert mit ESP32 3.3.12 und erzeugt als Build-Artefakt:

- `firmware.bin` für OTA im Portal
- `bootloader.bin`
- `partitions.bin`
- `boot_app0.bin`
- vollständiges Source-ZIP
- fertige Webinstaller-Seite

Optional: Repository-Variable `DEPLOY_PAGES=true` setzen und GitHub Pages auf **GitHub Actions** stellen; dann wird der Webinstaller automatisch veröffentlicht.

## Webinstaller

`docs/manifest.json` nutzt dieselben Offsets wie das Original:

- Bootloader `0x0000`
- Partitionstabelle `0x8000`
- boot_app0 `0xE000`
- Firmware `0x10000`

Bei Updates **Erase device nicht aktivieren**, damit WLAN und Bahnhofs-Einstellungen erhalten bleiben.

## Migration vom Original

Im Webinstaller gibt es dafür einen eigenen Button **„MVG → VBB aktualisieren“**. Er verwendet `manifest-mvg-migration.json` mit dem alten Improv-Namen nur für diesen Übergang. **Erase device ausgeschaltet lassen.** Danach meldet sich die neue Firmware regulär als **VBB Abfahrtsdisplay Berlin**.

WLAN, QR- und Verkehrsmittel-Einstellungen aus der Original-Firmware bleiben erhalten. Die alte MVG-Station wird **absichtlich nicht** übernommen: MVG-`globalId` und VBB-`stopId` sind unterschiedliche ID-Systeme. Nach dem ersten Start öffnet sich deshalb die Stationsauswahl; dort Bahnhof 1–4 neu über VBB wählen und anschließend optional die A/B-Ziele festlegen.

## Hinweis zum Build in dieser Chat-Umgebung

Der Quellcode und die Build-Automation sind vorbereitet. In der hier verwendeten Arbeitsumgebung ist weder Arduino CLI noch der ESP32-/Heltec-Toolchain vorinstalliert und direkte GitHub-Downloads aus dem Container sind gesperrt. Deshalb liegt in diesem Paket noch keine von mir lokal kompilierte `.bin`; der beigefügte GitHub-Actions-Workflow erzeugt sie reproduzierbar mit den vom Originalprojekt dokumentierten Versionen.
