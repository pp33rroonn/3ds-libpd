# libpd 3DS - Pure Data Vanilla Port fuer Nintendo 3DS

## Stereo-Kanal-Belegung (fest)

```
3DS Kopfhoererausgang
├── Links  ──> PD Audio-Ausgang  (Mono-Mix aus PD)
└── Rechts ──> 2ppqn Sync-Puls  (sync_clock, unabhaengig von PD-CPU-Last)
```

Externe Hardware (Volcas, Pocket Operators, Eurorack) kann per Y-Kabel
am rechten Kanal im 3DS-Tempo laufen.
SELECT-Taste: rechten Kanal (Sync) stumm/aktiv schalten.

---

## Steuerung

**Modi:** L+R = OPTIONEN ein/aus · R = PATCH ↔ CTRL · START = nächster Patch · HOME = beenden

| Modus | Eingabe | Funktion |
|-------|---------|----------|
| OPTIONEN | D-Pad Links/Rechts | BPM -1 / +1 |
| OPTIONEN | D-Pad Hoch/Runter | BPM +10 / -10 |
| OPTIONEN | A / X / SELECT / Circle-Pad Y | Tap-Tempo / ppqn / Sync ein/aus / BPM stufenlos |
| CTRL | D-Pad Hoch/Runter, Touch | Regler-Liste scrollen, Regler bedienen |
| PATCH + CTRL | alles unten | geht an den PD-Patch |

### PD-Empfänger (PATCH-Modus; im CTRL-Modus ohne D-Pad und Touch)

| Empfänger | Wert |
|-----------|------|
| `btn-a` `btn-b` `btn-x` `btn-y` `btn-select` | bang |
| `btn-up` `btn-down` `btn-left` `btn-right` | bang (nur PATCH) |
| `btn-l` | bang (L allein; R bleibt für den Moduswechsel) |
| `btn-zl` `btn-zr` | bang (nur New 3DS) |
| `pad-x` `pad-y` | Circle-Pad, -1..+1 |
| `cstick-x` `cstick-y` | C-Stick (nur New 3DS), -1..+1, rechts/oben = positiv |
| `touch-x` `touch-y` / `touch-on` `touch-off` | 0..319 / 0..239, bang (nur PATCH) |
| `accel-x` `accel-y` `accel-z` | Neigung, ca. -1..+1 (1 = ca. 1 g; flach liegend: y ≈ -1, x/z ≈ 0) |
| `gyro-x` `gyro-y` `gyro-z` | Drehgeschwindigkeit -1..+1 (1 = 360 °/s; x = Rollen, y = Nicken, z = Gieren) |

PD-Patch kann BPM ändern: `[send pd-bpm 130]`

### Patch-Slots und Eingabe-Routing

- OPTIONEN → **Patch 1..5**: jeder Slot ist beim Start **Empty**; es wird kein Patch automatisch geladen.
  Alle geladenen Patches laufen gleichzeitig (Audio wird summiert, Lautstärke im Mixer).
- **Sync** steht beim Start auf **OFF** (echtes Stereo). In OPTIONEN einschalten, wenn der Sync-Puls gebraucht wird.
- Eingaben (`btn-*`, `pad-*`, `touch-*`, `accel-*`, `gyro-*`, `cstick-*` und die Regler des CTRL-Panels)
  gehen **nur an den Patch der aktiven Seite** (ZL/ZR). Auf der Waveform-Seite, im PD-Log und in OPTIONEN
  bekommt kein Patch Eingaben; alle Patches spielen weiter.
- Patch-Dateien behalten die einfachen Namen (`[r touch-x]`, `[r btn-a]`, `[r cutoff]`). Beim Laden schreibt die App
  eine Kopie nach `sdmc:/3ds/libpd_3ds/.slots/s<N>/_slot_main.pd` und benennt diese Empfänger (und passende `[s ...]`,
  GUI-Send/Receive-Felder, Message-Box-Ziele nach `;`) in `s<N>-<name>` um (N = Slot 1..5, z. B. `s1-touch-x`).
  Kommentare, `$0`-Namen sowie `pd-*`/`slot-*` bleiben unverändert. Beim Seitenwechsel bekommt der alte Patch ein
  `touch-off`, der neue sofort die aktuellen Werte von Neigung und C-Stick.
- **Abstractions** (eigene `.pd`-Dateien, die ein Patch als Objekt aufruft, z. B. `[lib/stimme 220]`) werden mit
  umbenannt: Die App kopiert sie (auch verschachtelt) in denselben Slot-Ordner, mit gleicher relativer Struktur
  (`.slots/s2/lib/stimme.pd`). Gesucht wird wie in Pd: zuerst neben dem aufrufenden Patch, für Aufrufe im
  Haupt-Patch zusätzlich in `sdmc:/3ds/libpd_3ds/` und den anderen Suchpfaden. Dadurch reagiert `[r btn-a]` auch
  **innerhalb** einer Abstraction nur im aktiven Slot. Der Log zeigt z. B. `slot 1: 1 dac~ -> volume, 3 inputs -> s2-, 2 abs`.
  Grenzen: max. 32 Dateien und 6 Ebenen pro Slot; nicht kopiert werden Abstractions über absolute Pfade,
  `[declare -path]` oder per Message erzeugte Objekte – diese laufen dann aus dem Original und reagieren in allen Slots.
  Ein alter Ordner `.slots/slotN.pd` aus früheren Versionen kann gelöscht werden (harmlos).

### CTRL-Panel (unterer Bildschirm)

Beim Laden liest die App die steuerbaren Empfänger des Haupt-Patches ein (eine Liste pro Slot):

| Im Patch | Im Panel | Bedienung |
|---|---|---|
| `[r name]`, `hsl`/`vsl`/`nbx` (Send-Name) | Slider (Rahmen + Griff) | antippen / ziehen |
| `tgl` | Kasten, AN = Kreuz | antippen schaltet um |
| `bng` | Kasten mit Kreis, gefüllt solange gedrückt | antippen sendet Bang |
| `hradio`/`vradio` (n Zellen) | n Zellen, gewählte gefüllt, Wert 0..n-1 | Zelle antippen / ziehen |

- Hat das GUI-Objekt ein **Label**, zeigt das Panel das Label statt des Empfängernamens.
- `@ctrl name lo hi` (Kommentar) setzt den Bereich eines Sliders (Toggle/Bang/Radio haben feste Bereiche).
- **Rückmeldung:** Werte, die der Patch selbst an diese Namen schickt (`[s cutoff]`, loadbang, Sequencer),
  erscheinen im Panel. Die Empfänger werden schon vor dem Öffnen des Patches angemeldet, damit loadbang-Startwerte
  ankommen. Die Reglerstände bleiben beim Seitenwechsel erhalten, bis der Slot neu geladen wird.
- Nicht im Panel: GUIs in Abstractions und Namen mit `$0`.

Hinweis zu L: Wer für OPTIONEN zuerst L und dann R drückt, löst dabei einmal `btn-l` aus.
Achsen der Beschleunigung: x = links/rechts neigen (rechts = positiv, wie `pad-x`), z = vor/zurück neigen, y = senkrecht zum unteren
Bildschirm (flach liegend ca. -0.97, gemessen auf echter Hardware). `ACCEL_1G` = 512 passt damit
auf wenige Prozent; bei Bedarf im Makefile mit `-DACCEL_1G=...` anpassbar.
C-Stick: Rohwert ±156 (laut 3dbrew) wird auf -1..+1 normalisiert, Totzone 16 (ca. 0.1), gesendet nur bei
Änderung ≥ 0.01; anpassbar mit `-DCSTICK_MAX=...` / `-DCSTICK_DEADZONE=...`. Aktiv in PATCH und CTRL.

### Waveform-Seite (oberer Bildschirm)

Zeigt das PD-Ausgangssignal (vor dem Sync-Puls). Ansicht über OPTIONEN → **SpecAn**:

| Modus | Anzeige |
|-------|---------|
| Waveform (Standard) | getriggertes Oszilloskop (Mono-Mix, steigender Nulldurchgang) |
| Envelope | scrollender Pegelverlauf, L oben / R unten, neueste Werte rechts |
| Stereo | Goniometer (Mitte senkrecht, Seite waagerecht) + Korrelationsanzeige |
| Level | RMS-Balken L/R in dB (-60..0) mit Peak-Hold, ab -6 dB in Akzentfarbe |
| ASCII Wave | scrollende Wellenform aus Zeichen (`@#%&$*+=~:.`), Kanten in Achtel-Blöcken |
| ASCII Rain | scrollende Wellenform aus flackernden Zufallszeichen + fallende Tropfen (mehr bei mehr Pegel) |
| ASCII Scope | Oszilloskop aus `_ - ` / \ |`-Zeichen auf Punktraster |
| Braille Scope | Oszilloskop in Braille-Punkten (2x4 pro Zeichen, feinste Auflösung) |
| Phosphor | Oszilloskop mit Nachleuchten (Zeichen-Rampe bis ░▒▓█), bei Clipping Glitch-Runen |
| Stereo Dots | Goniometer als Braille-Punktwolke, ohne Beschriftung/Hilfslinien, Auto-Zoom der Seitenachse (bis 8x) |
| Stereo Glow | Goniometer als Zeichen-Dichtekarte mit Nachleuchten, heiße Stellen blitzen als Runen |
| Stereo Field | Strahlenfächer von unten: Winkel = Stereo-Position (links/rechts), Länge = Pegel |

Die ASCII-Ansichten nutzen ein 6x9-px-Zeichenraster; Blöcke, Braille, Linien und „Runen“ werden prozedural gezeichnet (kein Unicode-Font nötig).
Die Daten kommen aus einem 2048-Frame-Ringpuffer (`audio_get_scope`); gezeichnet wird nur, wenn die Seite sichtbar ist.

---

## Projektstruktur

```
3ds-libpd-project/
│
├── Makefile                        Haupt-Build (devkitARM)
│
├── source/
│   ├── main.c                      Einstiegspunkt; Main-Loop
│   ├── audio/
│   │   ├── audio.h/.c              NDSP dual-buffer + Sync-Interleave
│   ├── sync/
│   │   ├── sync_clock.h/.c         2ppqn Pulsgenerator (Phase-Akkumulator)
│   ├── input/
│   │   ├── touch_input.h/.c        Touchscreen -> libpd Messages
│   │   ├── motion_input.h/.c       Beschleunigung + Gyro -> libpd (accel-*, gyro-*)
│   │   ├── cstick_input.h/.c       C-Stick (New 3DS) -> libpd (cstick-x, cstick-y)
│   │   ├── button_input.h/.c       D-Pad / Schulter / Tap-Tempo / Circle-Pad
│   └── pd_bridge/
│       ├── pd_bridge.h/.c          libpd Init + pd-bpm Receiver
│
├── lib/
│   ├── libpd/
│   │   ├── libpd.a                 Cross-compiliert (siehe docs/build_libpd.md)
│   │   └── include/                z_libpd.h, m_pd.h, pd/
│   └── ctru/                       Verweis auf DEVKITPRO/libctru
│
├── romfs/
│   └── patches/
│       └── main.pd                 PD-Patch (eingebettet als romfs:/)
│
├── patches/
│   └── main.pd                     Entwicklungs-Kopie des Patches
│
├── resources/
│   └── icon.png                    48x48 PNG fuer SMDH-Banner
│
├── docs/
│   └── build_libpd.md              Anleitung libpd cross-compilieren
│
├── build/                          Objekt-Dateien (auto-generiert)
└── output/                         .elf / .3dsx (auto-generiert)
```

---

## Build

```bash
# 1. libpd cross-compilieren (einmalig)
#    Siehe docs/build_libpd.md

# 2. Patch in romfs legen
cp mein_patch.pd romfs/patches/main.pd

# 3. Bauen
make

# Output:
#   output/libpd_3ds.3dsx   -> auf SD-Karte nach /3ds/libpd_3ds/
```

---

## Architektur-Uebersicht

```
Main-Loop (60fps VBlank)
│
├── hidScanInput()                  EINMAL pro Frame (alle Module lesen davon)
│
├── button_input_poll()
│   ├── D-Pad / Schulter -> delta BPM -> clamp(BPM_MIN..BPM_MAX)
│   ├── Circle-Pad Y     -> delta BPM (kontinuierlich)
│   ├── A-Taste          -> Tap-Tempo (Phase-Akkumulator, CPU_CLOCK Ticks)
│   └── SELECT           -> sync_on Flag
│
├── touch_input_poll()
│   └── KEY_TOUCH -> libpd_float("touch-x"), libpd_float("touch-y"), bang
│
├── audio_set_sync_enabled()        sync_muted Atomic-Flag aktualisieren
│
└── audio_fill_buffer()
    ├── NDSP_WBUF_DONE?  Nein -> sofort zurueck (kein Busy-Wait)
    │                    Ja  ->
    ├── sync_clock_fill()           Sync-Puls fuer rechten Kanal (NDSP_BUFSIZE smp)
    ├── libpd_process_short() x 2   PD-Audio fuer linken Kanal
    ├── Interleave L+R -> pcm_data
    ├── DSP_FlushDataCache()
    └── ndspChnWaveBufAdd()
```

---

## Sync-Protokoll (rechter Kanal)

```
BPM 120, 2ppqn, 44100 Hz:
  Puls-Abstand = 44100 * 60 / (120 * 2) = 11025 Samples = 250 ms
  Puls-Dauer   = SYNC_PULSE_DUTY_SAMPLES = 16 Samples = 0.36 ms

  |████|          |████|          |████|
   HIGH    LOW     HIGH    LOW     HIGH
  <-------- 11025 smp ----------->
```

- Pegel HIGH: INT16_MAX (~voller Pegel am Kopfhoerer-Amp)
- Phase-Akkumulator: sub-sample-genau, kein Drift
- BPM-Aenderung: kein Phase-Reset (keine Timing-Gluitch)

---

## Schlussel-Designentscheidungen

| Thema | Entscheidung | Begruendung |
|-------|-------------|-------------|
| Sync-Kanal | Rechter Audiokanal | Trennbar per Y-Kabel |
| Sync-Protokoll | 2ppqn Square Wave | Korg/TE-kompatibel |
| BPM-Steuerung | Atomic Float | Glitch-frei aus Main-Loop |
| PD-BPM | [send pd-bpm] Message | Patch steuert Tempo |
| hidScanInput | Nur einmal in main.c | Kein Doppel-Scan-Bug |
| NDSP-Buffer | Dual-Buffer Ping-Pong | Glitch-freies Playback |
| Sync-Mute | Atomic bool im AudioCtx | Race-free SELECT-Taste |
