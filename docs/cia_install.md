# CIA-Build und FBI-Installation

## Voraussetzungen

| Tool | Quelle |
|------|--------|
| devkitARM + libctru | https://devkitpro.org/wiki/Getting_Started |
| bannertool | https://github.com/Steveice10/bannertool |
| makerom | https://github.com/3DSGuy/Project_CTR |
| FBI (auf dem 3DS) | https://github.com/Steveice10/FBI |
| 3DS mit CFW | z.B. Luma3DS via https://3ds.hacks.guide |

Alle Tools muessen im PATH verfuegbar sein.

---

## Build-Schritte

### 1. libpd cross-compilieren (einmalig)
Siehe `build_libpd.md`.

### 2. Icon vorbereiten
```
resources/icon.png   48x48 Pixel, PNG
resources/banner.wav  optional: 3 Sekunden WAV (16-bit, 22050 Hz, Stereo)
                      Wenn nicht vorhanden: CIA ohne Audio-Banner
```

### 3. Bauen
```bash
# Nur .3dsx (Homebrew Launcher, kein FBI):
make

# .3dsx PLUS .cia (FBI-installierbar):
make cia
```

### 4. CIA mit FBI installieren
Zwei Wege:

**Weg A: Ueber SD-Karte (einfacher)**
```
SD-Karte:
└── cia/
    └── libpd_3ds.cia    <- output/libpd_3ds.cia hier ablegen
```
Auf dem 3DS: FBI -> SD -> cia -> libpd_3ds.cia -> Install

**Weg B: Ueber FTP (komfortabler)**
```bash
# 3DS-IP herausfinden: FBI -> Remote Install -> Receive URLs -> via ftpd
# Datei per curl senden:
curl -T output/libpd_3ds.cia ftp://192.168.x.x:5000/cia/
# Dann in FBI: SD -> cia -> libpd_3ds.cia -> Install
```

---

## SD-Karten-Ordnerstruktur fuer Samples

Nach der Installation muessen Samples und Patches auf die SD-Karte:

```
SD-Karte (Wurzel)
└── 3ds/
    └── libpd_3ds/
        ├── patches/              <- PD-Patches (zusaetzlich zu romfs)
        │   ├── main.pd
        │   └── mein_patch.pd
        ├── samples/              <- WAV/AIFF-Samples fuer [soundfiler]
        │   ├── kick.wav
        │   ├── snare.wav
        │   ├── hihat.wav
        │   └── bass/
        │       ├── bass_c3.wav
        │       └── bass_g3.wav
        └── recordings/           <- [writesf~] schreibt hier (auto-erstellt)
            └── (leer zu Beginn)
```

Diese Verzeichnisse werden beim Start automatisch angelegt (durch `soundfiler_shim_init()`).

---

## Pfade in PD-Patches

### Relativer Pfad (empfohlen)
Der Shim sucht automatisch in allen registrierten Verzeichnissen:
```pd
; In PD-Patch: kick.wav suchen (Shim findet es automatisch)
[open kick.wav(
|
[soundfiler]
```

### Absoluter SD-Karten-Pfad
```pd
[open sdmc:/3ds/libpd_3ds/samples/kick.wav(
|
[soundfiler]
```

### Absoluter romfs-Pfad (eingebettete Samples)
```pd
[open romfs:/samples/kick.wav(
|
[soundfiler]
```

### Aufnahme (writesf~)
Schreibt automatisch nach `sdmc:/3ds/libpd_3ds/recordings/`:
```pd
[open meine_aufnahme.wav(
|
[writesf~]
```

---

## Suchpfad-Reihenfolge (soundfiler_shim)

Der Shim durchsucht Pfade in dieser Reihenfolge (erste Fundstelle gewinnt):

1. `sdmc:/3ds/libpd_3ds/`        (Wurzel)
2. `sdmc:/3ds/libpd_3ds/samples/`
3. `sdmc:/3ds/libpd_3ds/patches/`
4. `romfs:/samples/`
5. `romfs:/patches/`

Eigene Pfade koennen in `pd_bridge.c` via `soundfiler_shim_add_search_path()` ergaenzt werden.

---

## Sync-Ausgang (rechter Kanal)

```
3DS Kopfhoerer-Klinke (3.5mm TRRS)
├── Spitze (L) : PD Audio
├── Ring 1 (R) : 2ppqn Sync-Puls
├── Ring 2     : Masse
└── Huelse     : Mikrofon (nicht belegt)

Y-Kabel benoetigt:
  3DS Kopfhoerer-Ausgang
       |
  [Y-Kabel]
  /          \
 L-Mono       R-Mono
 (PD Audio)   (Sync -> SYNC-IN an Volca/PO/Modular)
```

SELECT-Taste: Sync-Puls ein/aus (rechter Kanal stumm).

---

## Kompatibilitaet der Sync-Eingaenge

| Geraet | Sync-Typ | Kompatibel |
|--------|----------|-----------|
| Korg Volca Series | 2ppqn | Ja (direkt) |
| Teenage Engineering PO | 2ppqn | Ja (direkt) |
| Korg Monotribe | 1ppqn | Teilweise* |
| Eurorack (Midiclock) | 24ppqn | Nein** |
| Roland TR-8S | MIDI | Nein |

*Monotribe: jede zweite Pulsflanke nutzen (Frequenzteiler noetig)
**Eurorack: SYNC_PPQN auf 24 setzen in Makefile (CPU-Last steigt minimal)
