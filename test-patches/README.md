# Testpatches für libpd 3DS

## Installation
1. **Alte Testpatches auf der SD-Karte zuerst löschen**, dann alle `.pd`-Dateien aus diesem
   Ordner nach `3ds/libpd_3ds/patches/` kopieren (Ordner ggf. anlegen).
2. App starten. Die Patches erscheinen alphabetisch: 00, 00b, 01, 02 … danach `main` (eingebaut).
3. **START** = nächster Patch. Oben steht `Patch [n/m]: <Name>`.

Ton: Sync AN = links PD, rechts Sync-Puls. Sync AUS (Optionen → SELECT) = PD in Stereo,
alle Testpatches spielen dann auf beiden Seiten.

## Diagnose (wenn ein Patch stumm bleibt)
- **Oben `Aussetzer` / `DSP-Last`**: `Aussetzer` zählt Tonabrisse seit dem Start. `DSP-Last` = PD-Rechenzeit in
  Prozent der Echtzeit. Über 100 % schafft die CPU den Patch nicht; nach 30 Frames Überlast wird DSP gestoppt,
  damit der 3DS nicht hängt.
- **Oben `PD-Pegel L/R`**: Spitzenwert der PD-Ausgabe (0–32767). `0 (PD stumm)` heißt:
  PD erzeugt nichts. Eine Zahl über 0 bei fehlendem Ton heißt: Das Problem liegt bei der Ausgabe.
- **Unten `PD-Meldungen`**: die letzten PD-Meldungen, z. B. `geladen: …`, `FEHLER oeffnen: …`,
  `… couldn't create` oder `read failed`. Am besten ein Foto davon schicken.
- `00_sd_test_dauerton`: identisch zum eingebauten `main` (Dauerton 220 Hz), aber von der SD-Karte.
- `00b_sd_test_kommentar`: wie 00, zusätzlich mit einer Kommentarzeile (wie alle anderen Testpatches).
  - 00 stumm → das Laden von der SD-Karte ist das Problem.
  - 00 klingt, 00b stumm → das Einlesen von Kommentaren ist auf dem 3DS kaputt.
  - beide klingen → einzelne Objekte in den anderen Patches; siehe `PD-Meldungen`.
- `00c_zahlen_argumente`: prüft, ob Zahlen in Objektboxen (`[f 220]`, `[+ 5]`, `[osc~ 220]`) richtig ankommen.
  Unten unter `PD-Meldungen` sollte stehen: `T1-f: 220`, `T2-plus: 5`, `T3-msg: 220`. Dazu ein Ton mit **440 Hz**
  (höher als bei main). Steht bei T1/T2 `0` oder eine unsinnige Zahl → Zahlen-Argumente in Objektboxen
  sind auf dem 3DS kaputt (libpd-Build-Problem), Nachrichten dagegen funktionieren.
- `00d_osc_werte`: **stumm**, schreibt echte Samplewerte ins Log: `SR: 44100` und 6–7 Zeilen `OSC: a b`
  (a = `[osc~ 220]`, b = `[osc~]` mit 330 Hz per Nachricht). Die Werte müssen sich von Zeile zu Zeile
  ändern (zwischen -1 und 1). Immer `1 1` oder gleiche Werte → `osc~` steht still. Grund für den Test:
  Der PD-Pegel zeigt nur den Spitzenwert, ein Sinus und ein Gleichanteil ergeben beide 6553.
- `00e_hoertest`: wechselt jede Sekunde 220 → 440 → 880 Hz bei Lautstärke 0.5, Log zeigt `HZ: …`.
  Am besten mit **Kopfhörern** und **Sync AUS** testen. Hörst du nur 440/880, aber nicht 220 →
  die 3DS-Lautsprecher geben tiefe Töne kaum wieder (kein Fehler in der App).

## Was du hören solltest

| Patch | Bedienung | Erwartung | Testet |
|---|---|---|---|
| `00_sd_test_dauerton` | – | Dauerton 220 Hz sofort nach dem Laden | Laden von der SD-Karte |
| `00b_sd_test_kommentar` | – | gleicher Dauerton | Kommentarzeilen (`#X text`) |
| `00c_zahlen_argumente` | – | Ton 440 Hz + 3 Log-Zeilen `T1-f: 220`, `T2-plus: 5`, `T3-msg: 220` | Zahlen-Argumente in Objektboxen |
| `00d_osc_werte` | – | stumm; Log `SR: 44100` + wechselnde `OSC: a b`-Werte | ob `osc~` wirklich schwingt |
| `00e_hoertest` | – | 220/440/880 Hz im Sekundentakt, Log `HZ: …` | Hörbarkeit tiefer Töne |
| `01_touch_theremin` | Finger auf Touchscreen bewegen | Ton nur solange der Finger drauf ist. Links tief, rechts hoch. Oben laut, unten leise. Loslassen = Ton blendet aus | `touch-x`, `touch-y`, `touch-on`, `touch-off`, `line~` |
| `02_tasten_piep` | A, B, Y drücken | A tiefer, B mittlerer, Y hoher Piepton (je ca. 0,3 s). A setzt zusätzlich Tap-Tempo | `btn-a`, `btn-b`, `btn-y`, `vline~` |
| `03_pad_filter` | Circle-Pad bewegen | Dauerhafter Sägezahn-Brummton. Links = dumpf, rechts = hell. Hoch = Filter pfeift mehr (Resonanz). **Hoch/Runter verändert gleichzeitig das BPM**, das ist normal | `pad-x`, `pad-y`, `phasor~`, `vcf~` |
| `04_sequencer_bpm` | B, Y | 8-Ton-Melodie in 16teln. Start: Anzeige **BPM 120**. B → **90**, Y → **150**. Melodie *und* Sync-Puls rechts werden langsamer/schneller | `[send pd-bpm]`, `metro`, `mtof`, `lop~` |
| `05_delay_echo` | Touchscreen antippen | Klick mit mehreren Echos. Links tippen = schnelle Echos, rechts = langsame | `noise~`, `delwrite~`, `delread~` |
| `06_stress_032osc` | – | A-Dur-Akkord (ab 440 Hz) aus 32 Oszillatoren, muss sauber klingen | CPU-Last leicht |
| `06b_stress_064osc` | – | gleicher Akkord, 64 Oszillatoren | CPU-Last mittel |
| `07_stress_128osc` | – | gleicher Akkord, 128 Oszillatoren. Ist die CPU zu schwach, stoppt die App den Ton nach 1–3 s und zeigt `CPU-UEBERLAST` (START = weiter) | CPU-Last hoch |

Bitte notieren, welche Patches funktionieren und was abweicht (z. B. „04: BPM-Anzeige
ändert sich nicht“, „07: knackt“). Das sagt uns genau, wo es hakt.

## Hinweise
- Beim Patchwechsel wird der alte Patch geschlossen; laufende Töne hören sofort auf.
- In Test 4 folgt die Melodie nur dem BPM, das der Patch selbst setzt. Änderst du BPM mit
  dem D-Pad, ändert sich nur der Sync-Puls. Das ändert sich mit dem nächsten Schritt (Clock an PD).
- Die Patches nutzen nur Vanilla-Objekte (kein `expr`, kein `print`, damit der Bildschirm frei bleibt).
- Alle Patches wurden am Rechner mit libpd 0.57 geprüft (Ton links und rechts, bei 01/02/05
  nach simulierten Eingaben). Auf dem 3DS blieben 01–07 bisher stumm; die Ursache ist noch offen.

## CTRL-Modus (R-Taste)

- `08_ctrl_regler.pd`: 4 Regler – `freq` (55–880 Hz), `cutoff` (100–5000 Hz), `volume` (0–1) und Toggle `an`.
  Hinweis: Das Panel zeigt die Regler beim Start auf ihrem Minimum, der Patch startet aber mit
  freq 110 / cutoff 1000 / volume 0.3 / an 1. Beim ersten Antippen springt der Wert auf die Touch-Position.
- `08b_ctrl_scroll.pd`: 16 Regler (`p01`–`p16`) zum Testen von Scrollen (D-Pad hoch/runter) und Scrollbalken.

## Bewegung und Schultertasten

- `09_bewegung_schulter.pd`: Sägezahn-Dauerton.
  - 3DS **links/rechts neigen** (`accel-x`) = Tonhöhe (± 1 Oktave, rechts = höher), **vor/zurück neigen** (`accel-z`) = Filter dumpf/hell.
  - 3DS **drehen/schwenken** (`gyro-x/y/z`) = Rauschen, je schneller desto lauter. In Ruhe kein Rauschen.
  - **L** = Piep 440 Hz, **ZL** = 660 Hz, **ZR** = 880 Hz (ZL/ZR nur New 3DS). R wechselt weiterhin in den CTRL-Modus.
  - Unten im Log steht einmal pro Sekunde `ACC: x y z`. Gemessen: flach auf dem Tisch ca. `-0.01 -0.97 -0.02`,
    also zeigt `accel-y` die Schwerkraft senkrecht zum unteren Bildschirm. Wenn Richtungen vertauscht
    sind (rechts neigen = tiefer), bitte melden.
  - Funktioniert auch im CTRL-Modus, nicht im OPTIONEN-Modus.

## C-Stick (nur New 3DS)

- `10_cstick.pd`: Sägezahn-Dauerton (440 Hz in Ruhe).
  - C-Stick (kleiner Stick rechts über den Tasten) **links/rechts** (`cstick-x`) = Tonhöhe (± 1 Oktave, rechts = höher).
  - **oben/unten** (`cstick-y`) = Filter (oben = hell, unten = dumpf).
  - Der C-Stick reagiert auf Druck, nicht auf Bewegung: leicht seitlich drücken reicht.
  - Unten im Log steht einmal pro Sekunde `CS: x y`. In Ruhe `0 0`, voll ausgelenkt ca. ±1.
  - Funktioniert auch im CTRL-Modus, nicht im OPTIONEN-Modus. Auf dem Old 3DS bleibt der Ton fest.

## Circle-Pad + Steuerkreuz + C-Stick kombiniert

- `11_dpad_cstick.pd`: Alle drei linken/rechten Bedienelemente gleichzeitig (im **PATCH-Modus** testen).
  - **Circle-Pad** (großer runder Stick links): Ton erklingt, solange der Stick ausgelenkt ist.
    Links/rechts = Tonhöhe (± 1 Oktave, rechts = höher). Loslassen = Ton aus.
  - **Steuerkreuz** (das Kreuz unter dem Circle-Pad): ↑ C5, ↓ C4, ← F4, → G4 (kurze Töne).
  - **C-Stick** (kleiner Stick rechts, nur New 3DS): links/rechts = Stereo-Pan (Kopfhörer!),
    oben/unten = Lautstärke (oben laut, Mitte halb, unten stumm). Erzeugt allein keinen Ton.
    Pan und Lautstärke werden über `line~` (30 ms) geglättet, sonst knistert es beim Bewegen.
  - Log: `PADX`/`PADY` (Circle-Pad), `DPAD: UP/DOWN/LEFT/RIGHT` (Steuerkreuz), `CSX`/`CSY` (C-Stick).
