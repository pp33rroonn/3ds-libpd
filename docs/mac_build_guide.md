# libpd 3DS — Build-Anleitung für macOS (Terminal)

Zeitaufwand: ca. 20–30 Minuten beim ersten Mal.  
Alles läuft im Terminal (`/Applications/Utilities/Terminal.app`).

---

## Schritt 1 — Homebrew installieren (falls noch nicht vorhanden)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Überprüfen:
```bash
brew --version
```
→ zeigt `Homebrew 4.x.x`

---

## Schritt 2 — devkitPro installieren

devkitPro stellt den ARM-Cross-Compiler für die 3DS bereit.

```bash
# devkitPro-Paketmanager holen
brew install devkitpro-pacman
```

Dann 3DS-Werkzeuge installieren:
```bash
sudo dkp-pacman -Sy
sudo dkp-pacman -S 3ds-dev
```

Bei der Frage „Install all? [Y/n]" → `Y` eingeben.

Umgebungsvariablen setzen (einmalig dauerhaft in ~/.zshrc schreiben):
```bash
echo 'export DEVKITPRO=/opt/devkitpro' >> ~/.zshrc
echo 'export DEVKITARM=/opt/devkitpro/devkitARM' >> ~/.zshrc
echo 'export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH' >> ~/.zshrc
source ~/.zshrc
```

Überprüfen:
```bash
arm-none-eabi-gcc --version
```
→ zeigt `arm-none-eabi-gcc 13.x.x`

---

## Schritt 3 — bannertool und makerom installieren

Diese zwei Tools erzeugen das CIA-Format.

```bash
# bannertool
brew install python3   # falls nicht vorhanden
git clone https://github.com/Steveice10/bannertool.git /tmp/bannertool
cd /tmp/bannertool
cmake -B build && cmake --build build
sudo cp build/bannertool /usr/local/bin/

# makerom
git clone https://github.com/3DSGuy/Project_CTR.git /tmp/project_ctr
cd /tmp/project_ctr/makerom
cmake -B build && cmake --build build
sudo cp build/makerom /usr/local/bin/
```

Überprüfen:
```bash
bannertool --version
makerom --version
```

---

## Schritt 4 — Projektdateien herunterladen und entpacken

Lade das Projektverzeichnis aus dem Abacus AI Chat herunter (Files-Button oben rechts → `3ds-libpd-project`).  
Entpacke es und wechsle hinein:

```bash
cd ~/Downloads
unzip 3ds-libpd-project.zip   # oder: tar xf 3ds-libpd-project.tar.gz
cd 3ds-libpd-project
```

---

## Schritt 5 — libpd für ARM cross-compilieren (einmalig)

```bash
# libpd-Quellen holen
git clone https://github.com/libpd/libpd /tmp/libpd
cd /tmp/libpd

# Für ARM11 (3DS) bauen
make \
  CC=$DEVKITARM/bin/arm-none-eabi-gcc \
  AR=$DEVKITARM/bin/arm-none-eabi-ar \
  CFLAGS="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -DPD -DUSEAPI_DUMMY -DHAVE_UNISTD_H" \
  MULTI_INSTANCE=0 \
  UTIL=0 \
  libpd_static
```

Ergebnis in das Projekt kopieren:
```bash
cd ~/Downloads/3ds-libpd-project

mkdir -p lib/libpd/include/pd

cp /tmp/libpd/libs/libpd.a        lib/libpd/libpd.a
cp /tmp/libpd/libpd.h              lib/libpd/include/
cp /tmp/libpd/z_libpd.h           lib/libpd/include/
cp /tmp/libpd/m_pd.h              lib/libpd/include/
cp -r /tmp/libpd/pure-data/src/   lib/libpd/include/pd/
```

---

## Schritt 6 — CIA bauen

```bash
cd ~/Downloads/3ds-libpd-project
make cia
```

Wenn alles klappt, erscheint am Ende:
```
built ... libpd_3ds.cia
```

Die fertige Datei liegt in:
```
output/libpd_3ds.cia
```

---

## Schritt 7 — Auf die 3DS SD-Karte kopieren

SD-Karte in den Mac stecken. Dann:

```bash
# CIA-Datei kopieren (FBI braucht sie irgendwo auf der SD)
cp output/libpd_3ds.cia /Volumes/NO\ NAME/libpd_3ds.cia

# App-Ordner und Sample-Ordner anlegen
mkdir -p /Volumes/NO\ NAME/3ds/libpd_3ds/samples
mkdir -p /Volumes/NO\ NAME/3ds/libpd_3ds/patches
mkdir -p /Volumes/NO\ NAME/3ds/libpd_3ds/recordings

# Demo-Patch kopieren
cp romfs/patches/main.pd /Volumes/NO\ NAME/3ds/libpd_3ds/patches/main.pd
```

> **Hinweis:** Der Laufwerksname (`NO NAME`) kann bei deiner SD-Karte anders heißen.  
> Prüfen mit: `ls /Volumes/`

---

## Schritt 8 — Per FBI installieren

1. SD-Karte zurück in die 3DS.
2. FBI starten (Homebrew Launcher oder CIA-installiert).
3. `SD` → zur `libpd_3ds.cia` navigieren → `A` → `Install CIA`.
4. Fertig — App erscheint im Home-Menü.

---

## Fehler-Lösungen

| Fehlermeldung | Lösung |
|---------------|--------|
| `arm-none-eabi-gcc: command not found` | Schritt 2 wiederholen, `source ~/.zshrc` nicht vergessen |
| `bannertool: command not found` | Schritt 3 wiederholen |
| `libpd.a: No such file or directory` | Schritt 5 überprüfen, Zielpfad `lib/libpd/libpd.a` |
| `makerom: RSF parse error` | `resources/app.rsf` liegt im Projekt, sollte automatisch gefunden werden |
| `make: *** No rule to make target` | `source ~/.zshrc` und dann `make cia` nochmal |

---

## SD-Karten-Ordnerstruktur (Übersicht)

```
SD:/
├── libpd_3ds.cia              ← vor Installation, danach löschbar
└── 3ds/
    └── libpd_3ds/
        ├── patches/
        │   └── main.pd        ← PD-Patch (wird beim Start geladen)
        ├── samples/           ← WAV-Dateien für soundfiler~
        └── recordings/        ← writesf~-Ausgabe
```
