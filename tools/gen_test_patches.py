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
        with open(os.path.join(OUT, name), "w") as f:
            f.write("#N canvas 0 50 700 500 10;\n")
            f.write("\n".join(self.lines + self.conns) + "\n")


# ---- 00 SD-Test: Dauerton ohne Bedienung ---------------------------------
# Gleicher Aufbau wie romfs/patches/main.pd, aber von der SD-Karte geladen.
# Bewusst OHNE Kommentar-Zeile. Hoert man hier nichts, liegt es am Laden
# von der SD-Karte und nicht an den Objekten in den anderen Patches.
p = Patch()
osc = p.obj("osc~ 220", 10, 30); amp = p.obj("*~ 0.2", 10, 60); dac = p.obj("dac~", 10, 90)
p.c(osc, amp); p.c(amp, dac)
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

# ---- 04 Sequencer + [send pd-bpm] -----------------------------------------
p = Patch()
p.text("TEST 4 Sequencer: Start 120 BPM \\, B = 90 BPM \\, Y = 150 BPM (Sync-Puls rechts muss mitgehen)", 10, 5)
lb = p.obj("loadbang", 10, 30); m120 = p.msg("120", 10, 55); start = p.msg("1", 90, 55)
rb = p.obj("r btn-b", 150, 30); m90 = p.msg("90", 150, 55)
ry = p.obj("r btn-y", 230, 30); m150 = p.msg("150", 230, 55)
t = p.obj("t f f", 10, 85); snd = p.obj("s pd-bpm", 120, 110)
sw = p.obj("swap 15000", 10, 110); dv = p.obj("/", 10, 135)
met = p.obj("metro 125", 10, 165)
f = p.obj("f", 10, 195); inc = p.obj("+ 1", 50, 195); md = p.obj("mod 8", 90, 195)
n1 = p.obj("* 7", 10, 225); n2 = p.obj("mod 12", 10, 250); n3 = p.obj("+ 48", 10, 275)
mt = p.obj("mtof", 10, 300); saw = p.obj("phasor~", 10, 325)
env = p.msg("1 1 \\, 0 100 1", 200, 225); vl = p.obj("vline~", 200, 255)
mul = p.obj("*~", 10, 355); lp = p.obj("lop~ 2000", 10, 380); amp = p.obj("*~ 0.3", 10, 405); dac = p.obj("dac~", 10, 430)
for x, y in [(lb, m120), (lb, start), (m120, t), (rb, m90), (m90, t), (ry, m150), (m150, t), (sw, dv),
             (start, met), (met, f), (f, inc), (inc, md), (f, n1), (n1, n2), (n2, n3), (n3, mt), (mt, saw),
             (met, env), (env, vl), (saw, mul), (mul, lp), (lp, amp), (amp, dac)]:
    p.c(x, y)
p.c(t, snd, 1, 0); p.c(t, sw, 0, 0); p.c(sw, dv, 1, 1); p.c(dv, met, 0, 1)
p.c(md, f, 0, 1); p.c(vl, mul, 0, 1)
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
for count, name in [(32, "06_stress_032osc.pd"), (128, "07_stress_128osc.pd")]:
    p = Patch()
    p.text(f"TEST CPU: {count} Oszillatoren als Akkord. Knackt es oder stockt der Sync-Puls ist die CPU zu voll", 10, 5)
    out = p.obj(f"*~ {0.5 / count:.5f}", 10, 400); dac = p.obj("dac~", 10, 430)
    base = [110, 138.59, 164.81, 220]
    for i in range(count):
        o = p.obj(f"osc~ {base[i % 4] * (1 + i // 4 * 0.003):.2f}", 10 + (i % 16) * 40, 30 + (i // 16) * 30)
        p.c(o, out)
    p.c(out, dac)
    p.save(name)
print("ok")
