# libpd für devkitARM cross-compilieren

## Voraussetzungen
- devkitARM installiert (`$DEVKITPRO` gesetzt)
- libpd-Quellen: `git clone https://github.com/libpd/libpd`

## Build (statische Bibliothek für ARM11)

```bash
cd libpd

make \
  CC=$(DEVKITARM)/bin/arm-none-eabi-gcc \
  AR=$(DEVKITARM)/bin/arm-none-eabi-ar \
  CFLAGS="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
          -O2 -DPD -DUSEAPI_DUMMY -DHAVE_UNISTD_H" \
  MULTI_INSTANCE=0 \
  UTIL=0 \
  libpd_static
```

Danach:
```
cp libpd.a  ../lib/libpd/
cp -r c/     ../lib/libpd/include/pd/
cp z_libpd.h ../lib/libpd/include/
cp m_pd.h    ../lib/libpd/include/
```

## Verzeichnisstruktur nach dem Copy
```
lib/
└── libpd/
    ├── libpd.a           ← statische Bibliothek
    └── include/
        ├── z_libpd.h
        ├── m_pd.h
        └── pd/           ← Pure Data C-Quellen (für libpd interne includes)
```

## Wichtige Flags
| Flag             | Bedeutung |
|-----------------|-----------|
| `USEAPI_DUMMY`  | Kein Sound-API in libpd selbst – wir nutzen NDSP |
| `MULTI_INSTANCE=0` | Vereinfacht Threading-Modell |
| `-mfloat-abi=hard` | Hardware-FPU des ARM11 nutzen |
