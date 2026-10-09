#!/bin/bash
# libpd fuer den 3DS neu bauen (va_arg-Fix), class_set_extern_dir entfernen,
# libpd.a ins Projekt kopieren.
# Aufruf aus dem Projektordner:  bash tools/rebuild_libpd.sh
set -e
LIBPD_SRC=${LIBPD_SRC:-/tmp/libpd}
PROJ=$(cd "$(dirname "$0")/.." && pwd)
DKA=${DEVKITARM:-/opt/devkitpro/devkitARM}
AR="$DKA/bin/arm-none-eabi-ar"
OBJCOPY="$DKA/bin/arm-none-eabi-objcopy"
NM="$DKA/bin/arm-none-eabi-nm"
OBJDUMP="$DKA/bin/arm-none-eabi-objdump"

echo "== 1. Quellen patchen"
python3 "$PROJ/tools/patch_libpd_va_arg.py" "$LIBPD_SRC/pure-data/src"

echo "== 2. CMake-Build-Ordner suchen"
CACHE=$(find "$LIBPD_SRC" -maxdepth 3 -name CMakeCache.txt | head -1)
if [ -z "$CACHE" ]; then echo "FEHLER: kein CMakeCache.txt unter $LIBPD_SRC"; exit 1; fi
BUILD=$(dirname "$CACHE")
echo "   $BUILD"

echo "== 3. Neu bauen"
cmake --build "$BUILD"

LIB=$(find "$BUILD" -name 'libpd*.a' | head -1)
if [ -z "$LIB" ]; then echo "FEHLER: keine libpd*.a in $BUILD"; exit 1; fi
echo "   Bibliothek: $LIB"

echo "== 4. class_set_extern_dir aus s_loader entfernen"
MEMBER=$("$AR" t "$LIB" | grep 's_loader' | head -1)
TMP=$(mktemp -d)
( cd "$TMP" && "$AR" x "$LIB" "$MEMBER" && "$OBJCOPY" -N class_set_extern_dir "$MEMBER" && "$AR" r "$LIB" "$MEMBER" )
rm -rf "$TMP"
echo "   Restliche Definitionen (soll leer sein):"
"$NM" "$LIB" 2>/dev/null | grep ' T class_set_extern_dir' || echo "   (keine)"

echo "== 5. Pruefen: keine udf-Abbrueche mehr in class_doaddmethod"
UDF=$("$OBJDUMP" -d "$LIB" 2>/dev/null | awk '/<class_doaddmethod>:/{f=1;next} /^$/{f=0} f' | grep -c 'udf' || true)
echo "   udf in class_doaddmethod: $UDF (soll 0 sein)"

echo "== 6. Ins Projekt kopieren"
cp "$LIB" "$PROJ/lib/libpd/libpd.a"
echo "Fertig. Jetzt: rm -f output/libpd_3ds.elf && make && make cia"
