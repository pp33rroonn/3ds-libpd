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
| `touch-x` `touch-y` / `touch-on` `touch-off` | 0..319 / 0..239, bang (nur PATCH) |
| `accel-x` `accel-y` `accel-z` | Neigung, ca. -1..+1 (1 = ca. 1 g; flach liegend: y ≈ -1, x/z ≈ 0) |
| `gyro-x` `gyro-y` `gyro-z` | Drehgeschwindigkeit -1..+1 (1 = 360 °/s; x = Rollen, y = Nicken, z = Gieren) |

PD-Patch kann BPM ändern: `[send pd-bpm 130]`

Hinweis zu L: Wer für OPTIONEN zuerst L und dann R drückt, löst dabei einmal `btn-l` aus.
Achsen der Beschleunigung: x = links/rechts neigen (rechts = positiv, wie `pad-x`), z = vor/zurück neigen, y = senkrecht zum unteren
Bildschirm (flach liegend ca. -0.97, gemessen auf echter Hardware). `ACCEL_1G` = 512 passt damit
auf wenige Prozent; bei Bedarf im Makefile mit `-DACCEL_1G=...` anpassbar.

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
