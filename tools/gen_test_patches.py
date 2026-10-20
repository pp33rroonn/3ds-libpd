"""Erzeugt die Testpatches fuer 3ds-libpd (test-patches/*.pd)."""
import os, sys

OUT = sys.argv[1]


class Patch:
    def __init__(self):
        self.lines, self.conns, self.n = [], [], 0
        self.dacs = set()

    def add(self, kind, text, x, y):
        self.lines.append(f"#X {kind} {x} {y} {text};")
        self.n += 1
        if kind == "obj" and text == "dac~":
            self.dacs.add(self.n - 1)
        return self.n - 1

    def obj(self, text, x, y): return self.add("obj", text, x, y)
    def msg(self, text, x, y): return self.add("msg", text, x, y)
    def text(self, t, x, y):
        self.lines.append(f"#X text {x} {y} {t};")
        self.n += 1

    def c(self, a, b, ao=0, bi=0):
        self.conns.append(f"#X connect {a} {ao} {b} {bi};")
        # Mono-Signal in dac~ links automatisch auch nach rechts fuehren
        # (Sync AUS = Stereo -> Ton auf beiden Kanaelen)
        if b in self.dacs and bi == 0:
            self.conns.append(f"#X connect {a} {ao} {b} 1;")

    def save(self, name):
        path = os.path.join(OUT, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write("#N canvas 0 50 700 500 10;\n")
            f.write("\n".join(self.lines + self.conns) + "\n")


# ---- 00 SD-Test: Dauerton ohne Bedienung ---------------------------------
# Gleicher Aufbau wie romfs/patches/main.pd, aber von der SD-Karte geladen.
# Bewusst OHNE Kommentar-Zeile. Hoert man hier nichts, liegt es am Laden
# von der SD-Karte und nicht an den Objekten in den anderen Patches.
# 660 Hz / 0.5: 220 Hz / 0.2 war auf den 3DS-Lautsprechern kaum hoerbar.
p = Patch()
osc = p.obj("osc~", 10, 30); amp = p.obj("*~ 0.5", 10, 60); dac = p.obj("dac~", 10, 90)
m = p.msg("660", 10, 10); lb = p.obj("loadbang", 120, 10)
p.c(lb, m); p.c(m, osc); p.c(osc, amp); p.c(amp, dac)
p.save("00_sd_test_dauerton.pd")

# ---- 00b wie 00, aber MIT Kommentar-Zeile (inkl. escaptem Komma) ----------
# 00 klingt, 00b stumm -> das Einlesen der Kommentare ist auf dem 3DS kaputt.
p = Patch()
p.text("TEST 0b Dauerton 220 Hz \\, mit Kommentar", 10, 5)
osc = p.obj("osc~ 220", 10, 30); amp = p.obj("*~ 0.2", 10, 60); dac = p.obj("dac~", 10, 90)
p.c(osc, amp); p.c(amp, dac)
p.save("00b_sd_test_kommentar.pd")

# ---- 00c Zahlen-Argumente: Erstellungs-Argument vs. Nachricht --------------
# Verdacht: Zahlen-Argumente in Objektboxen ([osc~ 220], [f 220], [+ 5])
# kommen auf dem 3DS als 0 / Muell an, Nachrichten ([220( ) dagegen korrekt.
# Erwartete Ausgaben unten im Log:  T1-f: 220   T2-plus: 5   T3-msg: 220
# Ton: 440 Hz, Frequenz per Nachricht gesetzt (wie im eingebauten main).
p = Patch()
lb = p.obj("loadbang", 10, 10)
f = p.obj("f 220", 10, 40); pf = p.obj("print T1-f", 10, 65)
pl = p.obj("+ 5", 110, 40); pp = p.obj("print T2-plus", 110, 65)
m = p.msg("220", 220, 40); pm = p.obj("print T3-msg", 220, 65)
m4 = p.msg("440", 330, 40); osc = p.obj("osc~", 330, 65)
amp = p.obj("*~ 0.2", 330, 90); dac = p.obj("dac~", 330, 115)
for a, b in ((lb, f), (lb, pl), (lb, m), (lb, m4), (f, pf), (pl, pp), (m, pm),
             (m4, osc), (osc, amp), (amp, dac)):
    p.c(a, b)
p.save("00c_zahlen_argumente.pd")

# ---- 00d Oszillator-Werte: laeuft osc~ oder steht er still? ---------------
# Der PD-Pegel zeigt nur den Spitzenwert: Sinus 0.2 und Gleichspannung 0.2
# ergeben beide 6553. Hier werden echte Samplewerte ins Log geschrieben.
# Erwartet:  SR: 44100  und 6 Zeilen "OSC: a b" mit WECHSELNDEN Werten
# zwischen -1 und 1. Immer "1 1" (oder gleiche Werte) -> osc~ steht still.
# A = [osc~ 220] (Argument), B = [osc~] mit Frequenz 330 per Nachricht.
p = Patch()
lb = p.obj("loadbang", 10, 10)
sr = p.obj("samplerate~", 10, 40); psr = p.obj("print SR", 10, 65)
m3 = p.msg("330", 150, 40)
oa = p.obj("osc~ 220", 250, 40); ob = p.obj("osc~", 150, 65)
dl = p.obj("delay 200", 10, 95); mt = p.obj("metro 137", 10, 120)
cf = p.obj("f", 10, 145); inc = p.obj("+ 1", 50, 145); sel = p.obj("sel 6", 10, 170)
stop = p.msg("stop", 10, 195)
tb = p.obj("t b b", 100, 170)
sa = p.obj("snapshot~", 250, 195); sb = p.obj("snapshot~", 150, 195)
pk = p.obj("pack f f", 250, 220); po = p.obj("print OSC", 250, 245)
for a, b, ao, bi in ((lb, sr, 0, 0), (sr, psr, 0, 0), (lb, m3, 0, 0), (m3, ob, 0, 0),
                     (lb, dl, 0, 0), (dl, mt, 0, 0), (mt, cf, 0, 0), (cf, inc, 0, 0),
                     (inc, cf, 0, 1), (cf, sel, 0, 0), (sel, stop, 0, 0), (stop, mt, 0, 0),
                     (cf, tb, 0, 0), (tb, sb, 1, 0), (tb, sa, 0, 0),
                     (oa, sa, 0, 0), (ob, sb, 0, 0), (sb, pk, 0, 1), (sa, pk, 0, 0),
                     (pk, po, 0, 0)):
    p.c(a, b, ao, bi)
p.save("00d_osc_werte.pd")

# ---- 00e Hoertest: tief/hoch abwechselnd, lauter --------------------------
# 00d hat gezeigt: osc~ rechnet auf dem 3DS korrekt. Verdacht: 220 Hz bei 0.2
# ist auf den kleinen 3DS-Lautsprechern kaum hoerbar (main klingt nur, weil
# Touch die Tonhoehe auf 110..1067 Hz hebt). Hier wechselt jede Sekunde
# 220 / 440 / 880 Hz bei Lautstaerke 0.5, das Log zeigt die aktuelle Frequenz.
p = Patch()
lb = p.obj("loadbang", 10, 10); mt = p.obj("metro 1000", 10, 35)
cf = p.obj("f", 10, 60); inc = p.obj("+ 1", 50, 60); md = p.obj("mod 3", 10, 85)
sel = p.obj("sel 0 1 2", 10, 110)
f1 = p.msg("220", 10, 135); f2 = p.msg("440", 60, 135); f3 = p.msg("880", 110, 135)
osc = p.obj("osc~", 10, 165); amp = p.obj("*~ 0.5", 10, 190); dac = p.obj("dac~", 10, 215)
pr = p.obj("print HZ", 120, 165)
for a, b, ao, bi in ((lb, mt, 0, 0), (mt, cf, 0, 0), (cf, inc, 0, 0), (inc, cf, 0, 1),
                     (cf, md, 0, 0), (md, sel, 0, 0), (sel, f1, 0, 0), (sel, f2, 1, 0),
                     (sel, f3, 2, 0), (f1, osc, 0, 0), (f2, osc, 0, 0), (f3, osc, 0, 0),
                     (f1, pr, 0, 0), (f2, pr, 0, 0), (f3, pr, 0, 0),
                     (osc, amp, 0, 0), (amp, dac, 0, 0)):
    p.c(a, b, ao, bi)
p.save("00e_hoertest.pd")


# ---- 01 Touch-Theremin: touch-x/y/on/off ---------------------------------
p = Patch()
p.text("TEST 1 Touch: x = Tonhoehe \\, y = Lautstaerke (oben laut) \\, nur Ton solange Finger drauf", 10, 5)
tx = p.obj("r touch-x", 10, 30); m3 = p.obj("* 3", 10, 55); a = p.obj("+ 110", 10, 80)
pk = p.obj("pack f 20", 10, 105); ln = p.obj("line~", 10, 130); osc = p.obj("osc~", 10, 155)
ty = p.obj("r touch-y", 150, 30); s = p.obj("- 239", 150, 55); sc = p.obj("* -0.0042", 150, 80)
pk2 = p.obj("pack f 20", 150, 105); ln2 = p.obj("line~", 150, 130)
on = p.obj("r touch-on", 300, 30); mon = p.msg("1 10", 300, 55)
off = p.obj("r touch-off", 380, 30); moff = p.msg("0 150", 380, 55)
gate = p.obj("line~", 300, 90)
v1 = p.obj("*~", 10, 190); v2 = p.obj("*~", 10, 220); out = p.obj("*~ 0.3", 10, 250); dac = p.obj("dac~", 10, 280)
for x, y in [(tx, m3), (m3, a), (a, pk), (pk, ln), (ln, osc), (osc, v1), (ty, s), (s, sc), (sc, pk2),
             (pk2, ln2), (on, mon), (mon, gate), (off, moff), (moff, gate), (v1, v2), (v2, out), (out, dac)]:
    p.c(x, y)
p.c(ln2, v1, 0, 1); p.c(gate, v2, 0, 1)
p.save("01_touch_theremin.pd")

# ---- 02 Tasten: btn-a/b/y -> Pieptoene ------------------------------------
p = Patch()
p.text("TEST 2 Tasten: A = tief (440) \\, B = mittel (660) \\, Y = hoch (880)", 10, 5)
out = p.obj("*~ 0.3", 10, 200); dac = p.obj("dac~", 10, 230)
for i, (btn, f) in enumerate([("btn-a", 440), ("btn-b", 660), ("btn-y", 880)]):
    x = 10 + i * 150
    r = p.obj(f"r {btn}", x, 30); env = p.msg("1 2 \\, 0 300 2", x, 55); vl = p.obj("vline~", x, 85)
    o = p.obj(f"osc~ {f}", x + 60, 85); m = p.obj("*~", x, 120)
    p.c(r, env); p.c(env, vl); p.c(o, m); p.c(vl, m, 0, 1); p.c(m, out)
p.c(out, dac)
p.save("02_tasten_piep.pd")

# ---- 03 Circle-Pad Filter: pad-x/y ----------------------------------------
p = Patch()
p.text("TEST 3 Circle-Pad: links/rechts = Filter dunkel/hell \\, hoch/runter = Resonanz (aendert auch BPM!)", 10, 5)
ph = p.obj("phasor~ 110", 10, 30); dc = p.obj("-~ 0.5", 10, 55)
px = p.obj("r pad-x", 150, 30); x1 = p.obj("+ 1", 150, 55); x2 = p.obj("* 1500", 150, 80)
x3 = p.obj("+ 200", 150, 105); xp = p.obj("pack f 30", 150, 130); xl = p.obj("line~", 150, 155)
py = p.obj("r pad-y", 300, 30); y1 = p.obj("+ 1", 300, 55); y2 = p.obj("* 4", 300, 80); y3 = p.obj("+ 1", 300, 105)
lb = p.obj("loadbang", 450, 30); z = p.msg("0", 450, 55)
vcf = p.obj("vcf~ 1", 10, 190); amp = p.obj("*~ 0.5", 10, 220); dac = p.obj("dac~", 10, 250)
for x, y in [(ph, dc), (dc, vcf), (px, x1), (x1, x2), (x2, x3), (x3, xp), (xp, xl), (py, y1), (y1, y2),
             (y2, y3), (lb, z), (z, x1), (z, y1), (amp, dac)]:
    p.c(x, y)
p.c(xl, vcf, 0, 1); p.c(y3, vcf, 0, 2); p.c(vcf, amp, 1, 0)   # Tiefpass-Ausgang
p.save("03_pad_filter.pd")

# ---- 04 Sequencer on the global clock -------------------------------------
# No [metro], no [loadbang] tempo: [r clock-step] (16th counter since RESET,
# same for every slot) -> [mod 8] picks the note, so two copies in two slots
# play the same note at the same time. [r clock-ms] sets the decay to 80 %
# of a 16th, [r clock-bpm] is printed to the PD log. B / Y request 90 / 150
# BPM via [send pd-bpm] (allowed once the patch is loaded).
p = Patch()
p.text("TEST 4 Sequencer auf clock-step: Tempo aus OPTIONS \\, B = 90 BPM \\, Y = 150 BPM \\, zwei Kopien laufen synchron", 10, 5)
cs = p.obj("r clock-step", 10, 30); md = p.obj("mod 8", 10, 55); tr = p.obj("t b f", 10, 80)
n1 = p.obj("* 7", 60, 110); n2 = p.obj("mod 12", 60, 135); n3 = p.obj("+ 48", 60, 160)
mt = p.obj("mtof", 60, 185); saw = p.obj("osc~", 60, 210)
cm = p.obj("r clock-ms", 200, 30); dk = p.obj("* 0.8", 200, 55)
fd = p.obj("f 100", 10, 110); env = p.msg("1 1 \\, 0 $1 1", 10, 135); vl = p.obj("vline~", 10, 160)
cb = p.obj("r clock-bpm", 330, 30); pb = p.obj("print clock-bpm", 330, 55)
rb = p.obj("r btn-b", 330, 90); m90 = p.msg("90", 330, 115)
ry = p.obj("r btn-y", 410, 90); m150 = p.msg("150", 410, 115)
snd = p.obj("s pd-bpm", 330, 145)
mul = p.obj("*~", 10, 245); lp = p.obj("lop~ 2000", 10, 270); amp = p.obj("*~ 0.3", 10, 295); dac = p.obj("dac~", 10, 320)
# version marker in the PD log: old 04 files (own [metro]) do not print it
lb = p.obj("loadbang", 500, 30); ver = p.msg("v2 clock-step - Tempo aus OPTIONS", 500, 55); vp = p.obj("print SEQ04", 500, 80)
for x, y, xo, yi in [(lb, ver, 0, 0), (ver, vp, 0, 0), (cs, md, 0, 0), (md, tr, 0, 0), (tr, n1, 1, 0), (n1, n2, 0, 0), (n2, n3, 0, 0),
                     (n3, mt, 0, 0), (mt, saw, 0, 0), (tr, fd, 0, 0), (cm, dk, 0, 0), (dk, fd, 0, 1),
                     (fd, env, 0, 0), (env, vl, 0, 0), (cb, pb, 0, 0), (rb, m90, 0, 0), (ry, m150, 0, 0),
                     (m90, snd, 0, 0), (m150, snd, 0, 0), (saw, mul, 0, 0), (vl, mul, 0, 1),
                     (mul, lp, 0, 0), (lp, amp, 0, 0), (amp, dac, 0, 0)]:
    p.c(x, y, xo, yi)
p.save("04_sequencer_bpm.pd")

# ---- 05 Delay-Echo: delwrite~/delread~ -----------------------------------
p = Patch()
p.text("TEST 5 Echo: Antippen = Klick mit Echo \\, x-Position = Echo-Zeit (links kurz \\, rechts lang)", 10, 5)
nz = p.obj("noise~", 10, 60); on = p.obj("r touch-on", 120, 30); env = p.msg("1 1 \\, 0 60 1", 120, 55)
vl = p.obj("vline~", 120, 80); m = p.obj("*~", 10, 110); lp = p.obj("lop~ 3000", 10, 135)
dw = p.obj("delwrite~ echo3ds 1000", 10, 170)
tx = p.obj("r touch-x", 250, 30); t1 = p.obj("* 2", 250, 55); t2 = p.obj("+ 50", 250, 80)
dr = p.obj("delread~ echo3ds 300", 250, 110); fb = p.obj("*~ 0.6", 250, 140)
amp = p.obj("*~ 0.4", 10, 210); dac = p.obj("dac~", 10, 240)
for x, y in [(nz, m), (on, env), (env, vl), (m, lp), (lp, dw), (lp, amp), (tx, t1), (t1, t2), (t2, dr),
             (dr, fb), (fb, dw), (dr, amp), (amp, dac)]:
    p.c(x, y)
p.c(vl, m, 0, 1)
p.save("05_delay_echo.pd")

# ---- 06/07 CPU-Stresstest -------------------------------------------------
# Lage A-Dur ab 440 Hz (gut hoerbar auf den 3DS-Lautsprechern). Lautstaerke
# pro Oszillator 0.35/sqrt(n): verstimmte Sinusse addieren sich nach der
# Wurzel, so sind alle Stresstests etwa gleich laut (vorher 0.5/n = sehr leise).
import math
for count, name in [(32, "06_stress_032osc.pd"), (64, "06b_stress_064osc.pd"),
                    (128, "07_stress_128osc.pd")]:
    p = Patch()
    p.text(f"TEST CPU: {count} Oszillatoren als Akkord. Oben DSP-Last beobachten", 10, 5)
    out = p.obj(f"*~ {0.35 / math.sqrt(count):.5f}", 10, 400); dac = p.obj("dac~", 10, 430)
    base = [440, 554.37, 659.26, 880]
    for i in range(count):
        o = p.obj(f"osc~ {base[i % 4] * (1 + i // 4 * 0.003):.2f}", 10 + (i % 16) * 40, 30 + (i // 16) * 30)
        p.c(o, out)
    p.c(out, dac)
    p.save(name)

# ---- 09 Gyro / Beschleunigung / Schultertasten ----------------------------
# Neigen links/rechts (accel-x) = Tonhoehe, vor/zurueck (accel-z) = Filter.
# Achsen (am Geraet gemessen): flach liegend zeigt accel-y ca. -1 (Schwerkraft
# senkrecht zum unteren Bildschirm), x und z ca. 0.
# Drehen (gyro-x/y/z) = Rauschen ("Wusch"). L / ZL / ZR = Pieptoene.
# Log: einmal pro Sekunde "ACC: x y z". Flach auf dem Tisch sollte y ca.
# +1 oder -1 sein; sonst stimmt die Kalibrierung ACCEL_1G nicht.
p = Patch()
p.text("TEST 9 Bewegung: neigen links/rechts = Tonhoehe \\, vor/zurueck = Filter \\, drehen = Rauschen \\, L/ZL/ZR = Piep", 10, 5)
# Tonhoehe: MIDI 57 +/- 12 Halbtoene
ax = p.obj("r accel-x", 10, 30); a1 = p.obj("* 12", 10, 55); a2 = p.obj("+ 57", 10, 80)
a3 = p.obj("mtof", 10, 105); ap = p.obj("pack f 40", 10, 130); al = p.obj("line~", 10, 155)
ph = p.obj("phasor~", 10, 180); dc = p.obj("-~ 0.5", 10, 205)
# Filter: 200..3200 Hz
ay = p.obj("r accel-z", 150, 30); y1 = p.obj("+ 1", 150, 55); y2 = p.obj("* 1500", 150, 80)
y3 = p.obj("+ 200", 150, 105); yp = p.obj("pack f 40", 150, 130); yl = p.obj("line~", 150, 155)
vcf = p.obj("vcf~ 3", 10, 235)
# Startwerte, falls der Sensor nichts liefert
lb = p.obj("loadbang", 300, 30); z0 = p.msg("0", 300, 55)
# Drehung -> Rauschen: |gyro| je Achse -> line~, Signale summieren sich am Eingang
noise = p.obj("noise~", 450, 180); nmul = p.obj("*~", 450, 235)
mix = p.obj("*~ 0.3", 10, 330); dac = p.obj("dac~", 10, 360)
for a, b, ao, bi in ((ax, a1, 0, 0), (a1, a2, 0, 0), (a2, a3, 0, 0), (a3, ap, 0, 0), (ap, al, 0, 0),
                     (al, ph, 0, 0), (ph, dc, 0, 0), (dc, vcf, 0, 0),
                     (ay, y1, 0, 0), (y1, y2, 0, 0), (y2, y3, 0, 0), (y3, yp, 0, 0), (yp, yl, 0, 0),
                     (yl, vcf, 0, 1), (lb, z0, 0, 0), (z0, a1, 0, 0), (z0, y1, 0, 0),
                     (vcf, mix, 1, 0), (noise, nmul, 0, 0), (nmul, mix, 0, 0), (mix, dac, 0, 0)):
    p.c(a, b, ao, bi)
for i, axis in enumerate("xyz"):
    x = 450 + i * 80
    g = p.obj(f"r gyro-{axis}", x, 30); ga = p.obj("abs", x, 55)
    gp = p.obj("pack f 30", x, 80); gl = p.obj("line~", x, 105)
    p.c(g, ga); p.c(ga, gp); p.c(gp, gl); p.c(gl, nmul, 0, 1)
# Schultertasten: L 440, ZL 660, ZR 880
for i, (btn, f) in enumerate([("btn-l", 440), ("btn-zl", 660), ("btn-zr", 880)]):
    x = 10 + i * 150
    r = p.obj(f"r {btn}", x, 260); env = p.msg("1 2 \\, 0 300 2", x, 280); vl = p.obj("vline~", x, 300)
    o = p.obj(f"osc~ {f}", x + 60, 300); m = p.obj("*~", x + 60, 320)
    p.c(r, env); p.c(env, vl); p.c(o, m); p.c(vl, m, 0, 1); p.c(m, mix)
# Log: ACC x y z einmal pro Sekunde
mt = p.obj("metro 1000", 300, 390); lb2 = p.obj("loadbang", 300, 365)
tb = p.obj("t b b b", 300, 415)
fx = p.obj("f", 300, 440); fy = p.obj("f", 340, 440); fz = p.obj("f", 380, 440)
rx = p.obj("r accel-x", 300, 340); ry = p.obj("r accel-y", 380, 340); rz = p.obj("r accel-z", 460, 340)
pk = p.obj("pack f f f", 300, 465); pr = p.obj("print ACC", 300, 490)
for a, b, ao, bi in ((lb2, mt, 0, 0), (mt, tb, 0, 0), (tb, fz, 2, 0), (tb, fy, 1, 0), (tb, fx, 0, 0),
                     (rx, fx, 0, 1), (ry, fy, 0, 1), (rz, fz, 0, 1),
                     (fz, pk, 0, 2), (fy, pk, 0, 1), (fx, pk, 0, 0), (pk, pr, 0, 0)):
    p.c(a, b, ao, bi)
p.save("09_bewegung_schulter.pd")

# ---- 10 C-Stick (nur New 3DS) ---------------------------------------------
# cstick-x = Tonhoehe (MIDI 69 +/- 12, rechts = hoeher),
# cstick-y = Filter (oben = hell, unten = dumpf). Ohne C-Stick: fester Ton.
# Log: einmal pro Sekunde "CS: x y".
p = Patch()
p.text("TEST 10 C-Stick: links/rechts = Tonhoehe \\, oben/unten = Filter hell/dumpf (nur New 3DS)", 10, 5)
cx = p.obj("r cstick-x", 10, 30); c1 = p.obj("* 12", 10, 55); c2 = p.obj("+ 69", 10, 80)
c3 = p.obj("mtof", 10, 105); cp = p.obj("pack f 30", 10, 130); cl = p.obj("line~", 10, 155)
ph = p.obj("phasor~", 10, 180); dc = p.obj("-~ 0.5", 10, 205)
# Filter: 300..4300 Hz
cy = p.obj("r cstick-y", 150, 30); y1 = p.obj("+ 1", 150, 55); y2 = p.obj("* 2000", 150, 80)
y3 = p.obj("+ 300", 150, 105); yp = p.obj("pack f 30", 150, 130); yl = p.obj("line~", 150, 155)
vcf = p.obj("vcf~ 2", 10, 235)
lb = p.obj("loadbang", 300, 30); z0 = p.msg("0", 300, 55)
mix = p.obj("*~ 0.3", 10, 270); dac = p.obj("dac~", 10, 300)
for a, b, ao, bi in ((cx, c1, 0, 0), (c1, c2, 0, 0), (c2, c3, 0, 0), (c3, cp, 0, 0), (cp, cl, 0, 0),
                     (cl, ph, 0, 0), (ph, dc, 0, 0), (dc, vcf, 0, 0),
                     (cy, y1, 0, 0), (y1, y2, 0, 0), (y2, y3, 0, 0), (y3, yp, 0, 0), (yp, yl, 0, 0),
                     (yl, vcf, 0, 1), (lb, z0, 0, 0), (z0, c1, 0, 0), (z0, y1, 0, 0),
                     (vcf, mix, 1, 0), (mix, dac, 0, 0)):
    p.c(a, b, ao, bi)
# Log: CS x y einmal pro Sekunde
lb2 = p.obj("loadbang", 300, 340); mt = p.obj("metro 1000", 300, 365); tb = p.obj("t b b", 300, 390)
fx = p.obj("f", 300, 415); fy = p.obj("f", 340, 415)
rx = p.obj("r cstick-x", 300, 315); ry = p.obj("r cstick-y", 380, 315)
pk = p.obj("pack f f", 300, 440); pr = p.obj("print CS", 300, 465)
for a, b, ao, bi in ((lb2, mt, 0, 0), (mt, tb, 0, 0), (tb, fy, 1, 0), (tb, fx, 0, 0),
                     (rx, fx, 0, 1), (ry, fy, 0, 1), (fy, pk, 0, 1), (fx, pk, 0, 0), (pk, pr, 0, 0)):
    p.c(a, b, ao, bi)
p.save("10_cstick.pd")
# ---- 12 Stereo: getrennte Signale links/rechts ---------------------------
# Stimme 1: links 220 Hz, rechts 221.5 Hz -> der Klang wandert langsam
#           zwischen den Ohren (Schwebung zwischen den Kanaelen).
# Stimme 2: gefilterter Saegezahn 110 Hz mit Autopan (4 s von links nach rechts).
# btn-a   : Rauschstoss, abwechselnd nur links / nur rechts.
# dac~ 1 2 mit getrennten Eingaengen (keine Mono-Kopie). Sync auf OFF lassen,
# sonst ist der rechte Kanal der Sync-Puls.
p = Patch()
p.text("TEST 12 Stereo: L 220 Hz / R 221.5 Hz \\, Saegezahn mit Autopan \\, A = Rauschen abwechselnd links/rechts (Sync OFF)", 10, 5)
o1 = p.obj("osc~ 220", 10, 30); o2 = p.obj("osc~ 221.5", 120, 30)
g1 = p.obj("*~ 0.12", 10, 60); g2 = p.obj("*~ 0.12", 120, 60)
saw = p.obj("phasor~ 110", 250, 30); sd = p.obj("-~ 0.5", 250, 55)
lp = p.obj("lop~ 1200", 250, 80); sg = p.obj("*~ 0.25", 250, 105)
lfo = p.obj("osc~ 0.25", 400, 30); l1 = p.obj("*~ 0.5", 400, 55); pan = p.obj("+~ 0.5", 400, 80)
inv1 = p.obj("*~ -1", 480, 105); inv = p.obj("+~ 1", 480, 130)
sl = p.obj("*~", 250, 160); sr = p.obj("*~", 330, 160)
ba = p.obj("r btn-a", 10, 200); tb = p.obj("t b b", 10, 225)
env = p.msg("0.3 \\, 0 200", 10, 250); vl = p.obj("vline~", 10, 275)
cf = p.obj("f", 120, 250); ci = p.obj("+ 1", 160, 250); cm = p.obj("mod 2", 160, 275)
e0 = p.obj("== 0", 120, 300); e1 = p.obj("== 1", 200, 300)
nz = p.obj("noise~", 10, 300); ne = p.obj("*~", 10, 325)
nl = p.obj("*~ 0", 10, 350); nr = p.obj("*~ 0", 120, 350)
al1 = p.obj("+~", 10, 400); al2 = p.obj("+~", 10, 425)
ar1 = p.obj("+~", 120, 400); ar2 = p.obj("+~", 120, 425)
dac = p.obj("dac~ 1 2", 10, 460)
lb = p.obj("loadbang", 400, 200)
info = p.msg("L 220Hz R 221.5Hz - autopan - A = noise L/R", 400, 225)
pr = p.obj("print STEREO", 400, 250)
for a, b, ao, bi in ((o1, g1, 0, 0), (o2, g2, 0, 0),
                     (saw, sd, 0, 0), (sd, lp, 0, 0), (lp, sg, 0, 0),
                     (lfo, l1, 0, 0), (l1, pan, 0, 0), (pan, inv1, 0, 0), (inv1, inv, 0, 0),
                     (sg, sl, 0, 0), (inv, sl, 0, 1), (sg, sr, 0, 0), (pan, sr, 0, 1),
                     (ba, tb, 0, 0), (tb, env, 0, 0), (tb, cf, 1, 0), (env, vl, 0, 0),
                     (cf, ci, 0, 0), (ci, cm, 0, 0), (cm, cf, 0, 1),
                     (cf, e0, 0, 0), (cf, e1, 0, 0), (e0, nl, 0, 1), (e1, nr, 0, 1),
                     (nz, ne, 0, 0), (vl, ne, 0, 1), (ne, nl, 0, 0), (ne, nr, 0, 0),
                     (g1, al1, 0, 0), (sl, al1, 0, 1), (al1, al2, 0, 0), (nl, al2, 0, 1),
                     (g2, ar1, 0, 0), (sr, ar1, 0, 1), (ar1, ar2, 0, 0), (nr, ar2, 0, 1),
                     (al2, dac, 0, 0), (ar2, dac, 0, 1),
                     (lb, info, 0, 0), (info, pr, 0, 0)):
    p.c(a, b, ao, bi)
p.save("12_stereo.pd")

# ---- 13 Abstraction: eigene Bausteine aus dem Unterordner lib/ ------------
# lib/abs_huelle.pd : Huellkurve, Bang am Eingang -> vline~ (kurz an, 400 ms aus)
# lib/abs_stimme.pd : Stimme mit Grundton $1 ([+ $1], beim Laden 0 -> Grundton). Reagiert SELBST auf btn-a
#                     (startet die Huelle, Log "ABS-A: <Grundton>") und pad-x
#                     (Tonhoehe +-50 Hz). Benutzt [abs_huelle] (verschachtelt).
# 13_abstraktion.pd : [lib/abs_stimme 220] + [lib/abs_stimme 330] -> dac~.
# Die App kopiert Patch UND Abstractions pro Slot und benennt btn-a / pad-x
# auch darin um -> A loest nur im aktiven Slot aus. Den Ordner lib/ mit auf
# die SD-Karte kopieren (neben die Patches).
p = Patch()
i = p.obj("inlet", 10, 10); t = p.obj("t b", 10, 35)
m = p.msg("1 5 \\, 0 400 5", 10, 60); v = p.obj("vline~", 10, 85); o = p.obj("outlet~", 10, 110)
for a, b in ((i, t), (t, m), (m, v), (v, o)):
    p.c(a, b)
p.save("lib/abs_huelle.pd")

p = Patch()
ba = p.obj("r btn-a", 10, 10); inl = p.obj("inlet", 120, 10)
hu = p.obj("abs_huelle", 10, 60)
fa = p.obj("f $1", 250, 35); pa = p.obj("print ABS-A", 250, 60)
lb = p.obj("loadbang", 400, 10); f1 = p.msg("0", 400, 35)
px = p.obj("r pad-x", 500, 10); pm = p.obj("* 50", 500, 35)
ad = p.obj("+ $1", 400, 85); osc = p.obj("osc~", 400, 110)
env = p.obj("*~", 400, 140); amp = p.obj("*~ 0.25", 400, 165); out = p.obj("outlet~", 400, 190)
for a, b, ao, bi in ((ba, hu, 0, 0), (inl, hu, 0, 0), (ba, fa, 0, 0), (fa, pa, 0, 0),
                     (lb, f1, 0, 0), (f1, ad, 0, 0),
                     (px, pm, 0, 0), (pm, ad, 0, 0), (ad, osc, 0, 0),
                     (osc, env, 0, 0), (hu, env, 0, 1), (env, amp, 0, 0), (amp, out, 0, 0)):
    p.c(a, b, ao, bi)
p.save("lib/abs_stimme.pd")

p = Patch()
p.text("TEST 13 Abstraction: zwei [lib/abs_stimme] \\, A in der Abstraction = beide Stimmen \\, Circle-Pad = Tonhoehe \\, B = Log", 10, 5)
s1 = p.obj("lib/abs_stimme 220", 10, 40); s2 = p.obj("lib/abs_stimme 330", 200, 40)
mix = p.obj("+~", 10, 80); dac = p.obj("dac~", 10, 110)
bb = p.obj("r btn-b", 350, 80); fb = p.msg("B im Haupt-Patch", 350, 105); pb = p.obj("print MAIN", 350, 130)
lb = p.obj("loadbang", 350, 160)
info = p.msg("13: A = Stimmen (Abstraction) - Pad = Tonhoehe - B = Log", 350, 185)
pr = p.obj("print ABS", 350, 210)
for a, b, ao, bi in ((s1, mix, 0, 0), (s2, mix, 0, 1), (mix, dac, 0, 0),
                     (bb, fb, 0, 0), (fb, pb, 0, 0), (lb, info, 0, 0), (info, pr, 0, 0)):
    p.c(a, b, ao, bi)
p.save("13_abstraktion.pd")

# ---- 14 GUI-Elemente: Slider / Toggle / Bang / Radio im CTRL-Panel ----------
# Prueft die neuen Panel-Typen und die Rueckmeldung PD -> Panel:
#   hsl "Filter" (cutoff 200..4000) -> lop~-Grenzfrequenz
#   tgl "An"     (an)   -> Ton an/aus
#   bng "Kick"   (kick) -> kurzer Bass-Kick + Log "KICK"
#   hradio "Note" (note, 4 Zellen) -> Grundton 220/262/330/392 Hz
#   tgl "Auto"   (auto) -> metro schaltet "Note" selbst weiter (Panel folgt)
# loadbang setzt Startwerte per [s cutoff] / [s an] / [s note]: das Panel
# muss sie ohne Beruehrung anzeigen (Filter 1200, An = AN, Note 1/4).
p = Patch()
p.text("TEST 14 GUI: Filter-Slider \\, An-Toggle \\, Kick-Bang \\, Note-Radio \\, Auto-Toggle (CTRL-Panel)", 10, 5)
COL = "#fcfcfc #000000 #000000"
p.obj(f"hsl 128 15 200 4000 0 0 cutoff empty Filter -2 -8 0 10 {COL} 0 1", 10, 40)
p.obj(f"tgl 15 0 an empty An 17 7 0 10 {COL} 0 1", 10, 70)
p.obj(f"bng 15 250 50 0 kick empty Kick 17 7 0 10 {COL}", 80, 70)
p.obj(f"hradio 15 1 0 4 note empty Note 0 -8 0 10 {COL} 0", 10, 110)
p.obj(f"tgl 15 0 auto empty Auto 17 7 0 10 {COL} 0 1", 150, 70)
# Ton: Note -> Saegezahn -> lop~ (cutoff) -> *~ (an)
rn = p.obj("r note", 10, 150); sel = p.obj("sel 0 1 2 3", 10, 175)
nm = [p.msg(str(f), 10 + 45 * i, 200) for i, f in enumerate((220, 262, 330, 392))]
ph = p.obj("osc~", 10, 230); dc = p.obj("-~ 0", 10, 255)
lop = p.obj("lop~ 1200", 10, 280); rc = p.obj("r cutoff", 120, 255)
ra = p.obj("r an", 200, 150); ga = p.obj("* 0.2", 200, 175); pk = p.obj("pack f 20", 200, 200)
ln = p.obj("line~", 200, 225); gate = p.obj("*~", 10, 310)
# Kick: Bang -> Huellkurve auf osc~ 55
rk = p.obj("r kick", 350, 150); km = p.msg("0.6 \\, 0 250 5", 350, 175); kv = p.obj("vline~", 350, 200)
ko = p.obj("osc~ 55", 450, 200); kx = p.obj("*~", 350, 230); kp = p.obj("print KICK", 450, 175)
mix = p.obj("+~", 10, 350); dac = p.obj("dac~", 10, 380)
# Auto: follows the global clock (OPTIONS tempo): one note per 8th,
# note = (clock-step div 2) mod 4 -> same note in every slot, no own [metro]
cst = p.obj("r clock-step", 500, 225); rau = p.obj("r auto", 600, 225); me = p.obj("spigot", 500, 250)
cf = p.obj("div 2", 500, 275); cm = p.obj("mod 4", 500, 300)
sn = p.obj("s note", 500, 350)
# Startwerte (Rueckmeldung ins Panel ohne Beruehrung)
lb = p.obj("loadbang", 300, 300); tb = p.obj("t b b b", 300, 325)
m1 = p.msg("1200", 300, 350); m2 = p.msg("1", 350, 350); m3 = p.msg("0", 400, 350)
s1 = p.obj("s cutoff", 300, 375); s2 = p.obj("s an", 350, 400); s3 = p.obj("s note", 400, 425)
info = p.msg("14: Filter/An/Kick/Note/Auto im CTRL-Panel", 300, 450); pr = p.obj("print GUI", 300, 475)
for a, b, ao, bi in ((rn, sel, 0, 0), (sel, nm[0], 0, 0), (sel, nm[1], 1, 0), (sel, nm[2], 2, 0),
                     (sel, nm[3], 3, 0), (nm[0], ph, 0, 0), (nm[1], ph, 0, 0), (nm[2], ph, 0, 0),
                     (nm[3], ph, 0, 0), (ph, dc, 0, 0), (dc, lop, 0, 0), (rc, lop, 0, 1),
                     (ra, ga, 0, 0), (ga, pk, 0, 0), (pk, ln, 0, 0), (lop, gate, 0, 0), (ln, gate, 0, 1),
                     (rk, km, 0, 0), (rk, kp, 0, 0), (km, kv, 0, 0), (ko, kx, 0, 0), (kv, kx, 0, 1),
                     (gate, mix, 0, 0), (kx, mix, 0, 1), (mix, dac, 0, 0),
                     (cst, me, 0, 0), (rau, me, 0, 1), (me, cf, 0, 0), (cf, cm, 0, 0),
                     (cm, sn, 0, 0),
                     (lb, tb, 0, 0), (tb, m1, 2, 0), (tb, m2, 1, 0), (tb, m3, 0, 0),
                     (m1, s1, 0, 0), (m2, s2, 0, 0), (m3, s3, 0, 0), (tb, info, 0, 0), (info, pr, 0, 0)):
    p.c(a, b, ao, bi)
p.save("14_gui_elemente.pd")

# ---- 15 Mikrofon: adc~ 1 mit niedrigem Gain + mic-level Anzeige ----------
# WARNUNG: Rueckkopplungsgefahr ueber Lautsprecher! Deshalb niedriger Gain
# (*~ 0.3) und lop~ 2000 (zaehmt die hohen Frequenzen). Kopfhoerer empfohlen.
p = Patch()
p.text("TEST 15 Mikrofon: adc~ 1 -> lop~ 2000 -> *~ 0.3 -> dac~"
       " (NIEDRIGER GAIN - Rueckkopplungsgefahr bei Lautsprechern \\,"
       " Kopfhoerer empfohlen)", 10, 5)
ad  = p.obj("adc~ 1", 10, 60)      # Mikrofon-Eingang (vom Host resampled)
lp  = p.obj("lop~ 2000", 10, 90)   # hohe Frequenzen daempfen
ga  = p.obj("*~ 0.3", 10, 120)     # niedriger Gain gegen Rueckkopplung
dac = p.obj("dac~", 10, 160)
# Pegelanzeige: globales mic-level (0..1) vom Host, einmal pro Buffer
rl = p.obj("r mic-level", 220, 60)
fa = p.add("floatatom", "8 0 0 0 - - -", 220, 90)
p.text("Pegel (0..1)", 320, 92)
for a, b, ao, bi in ((ad, lp, 0, 0), (lp, ga, 0, 0), (ga, dac, 0, 0),
                     (rl, fa, 0, 0)):
    p.c(a, b, ao, bi)
p.save("15_mic.pd")

print("ok")
