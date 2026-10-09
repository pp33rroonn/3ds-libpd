#!/usr/bin/env python3
"""Ersetzt va_arg(x, t_atomtype) durch (t_atomtype)va_arg(x, int) in den Pd-Quellen.

devkitARM nutzt kurze Enums (1 Byte). Enums werden bei variablen Argumenten
zu int befoerdert; va_arg mit dem Enum-Typ ist undefiniert, GCC setzt dort
eine Abbruch-Anweisung (udf) -> Crash "undefined instruction" in class_doaddmethod.
Mehrfach ausfuehrbar (idempotent).
"""
import os
import re
import sys

src = sys.argv[1] if len(sys.argv) > 1 else "/tmp/libpd/pure-data/src"
pat = re.compile(r"va_arg\(\s*(\w+)\s*,\s*t_atomtype\s*\)")
total = 0
for root, _, files in os.walk(src):
    for name in files:
        if not name.endswith(".c"):
            continue
        path = os.path.join(root, name)
        with open(path, encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        new, n = pat.subn(r"(t_atomtype)va_arg(\1, int)", text)
        if n:
            with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                f.write(new)
            print(f"  gepatcht: {path} ({n}x)")
            total += n
print(f"Ersetzungen gesamt: {total}")
left = 0
for root, _, files in os.walk(src):
    for name in files:
        if name.endswith(".c"):
            with open(os.path.join(root, name), encoding="utf-8", errors="surrogateescape") as f:
                left += len(pat.findall(f.read()))
if left:
    print(f"FEHLER: noch {left} Stellen uebrig")
    sys.exit(1)
