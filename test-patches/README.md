# Testpatches für libpd 3DS

## Installation
1. **Alte Testpatches auf der SD-Karte zuerst löschen**, dann alle `.pd`-Dateien aus diesem
   Ordner nach `3ds/libpd_3ds/patches/` kopieren (Ordner ggf. anlegen).
2. App starten. Die Patches erscheinen alphabetisch: 00, 00b, 01, 02 … danach `main` (eingebaut).
3. **START** = nächster Patch. Oben steht `Patch [n/m]: <Name>`.

Ton: Sync AN = links PD, rechts Sync-Puls. Sync AUS (Optionen → SELECT) = PD in Stereo,
alle Testpatches spielen dann auf beiden Seiten.

## Diagnose (wenn ein Patch stumm bleibt)
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

## Was du hören solltest

| Patch | Bedienung | Erwartung | Testet |
|---|---|---|---|
| `00_sd_test_dauerton` | – | Dauerton 220 Hz sofort nach dem Laden | Laden von der SD-Karte |
| `00b_sd_test_kommentar` | – | gleicher Dauerton | Kommentarzeilen (`#X text`) |
| `00c_zahlen_argumente` | – | Ton 440 Hz + 3 Log-Zeilen `T1-f: 220`, `T2-plus: 5`, `T3-msg: 220` | Zahlen-Argumente in Objektboxen |
| `01_touch_theremin` | Finger auf Touchscreen bewegen | Ton nur solange der Finger drauf ist. Links tief, rechts hoch. Oben laut, unten leise. Loslassen = Ton blendet aus | `touch-x`, `touch-y`, `touch-on`, `touch-off`, `line~` |
| `02_tasten_piep` | A, B, Y drücken | A tiefer, B mittlerer, Y hoher Piepton (je ca. 0,3 s). A setzt zusätzlich Tap-Tempo | `btn-a`, `btn-b`, `btn-y`, `vline~` |
| `03_pad_filter` | Circle-Pad bewegen | Dauerhafter Sägezahn-Brummton. Links = dumpf, rechts = hell. Hoch = Filter pfeift mehr (Resonanz). **Hoch/Runter verändert gleichzeitig das BPM**, das ist normal | `pad-x`, `pad-y`, `phasor~`, `vcf~` |
| `04_sequencer_bpm` | B, Y | 8-Ton-Melodie in 16teln. Start: Anzeige **BPM 120**. B → **90**, Y → **150**. Melodie *und* Sync-Puls rechts werden langsamer/schneller | `[send pd-bpm]`, `metro`, `mtof`, `lop~` |
| `05_delay_echo` | Touchscreen antippen | Klick mit mehreren Echos. Links tippen = schnelle Echos, rechts = langsame | `noise~`, `delwrite~`, `delread~` |
| `06_stress_032osc` | – | Akkord aus 32 Oszillatoren, muss sauber klingen | CPU-Last leicht |
| `07_stress_128osc` | – | Akkord aus 128 Oszillatoren. Knackt es, stockt der Sync-Puls oder reagiert die Anzeige träge → CPU-Grenze erreicht | CPU-Last hoch |

Bitte notieren, welche Patches funktionieren und was abweicht (z. B. „04: BPM-Anzeige
ändert sich nicht“, „07: knackt“). Das sagt uns genau, wo es hakt.

## Hinweise
- Beim Patchwechsel wird der alte Patch geschlossen; laufende Töne hören sofort auf.
- In Test 4 folgt die Melodie nur dem BPM, das der Patch selbst setzt. Änderst du BPM mit
  dem D-Pad, ändert sich nur der Sync-Puls. Das ändert sich mit dem nächsten Schritt (Clock an PD).
- Die Patches nutzen nur Vanilla-Objekte (kein `expr`, kein `print`, damit der Bildschirm frei bleibt).
- Alle Patches wurden am Rechner mit libpd 0.57 geprüft (Ton links und rechts, bei 01/02/05
  nach simulierten Eingaben). Auf dem 3DS blieben 01–07 bisher stumm; die Ursache ist noch offen.
