# 3DS libpd build environment - source this before building:
#   source setup_env.sh && make cia
#
# Restores the pieces that do NOT survive a VM restart (they live outside
# /home/ubuntu or get downloaded once): the /opt/devkitpro symlink and the
# makerom / bannertool CLIs under ~/.tmp/bin. Safe to run repeatedly.

DKP_IMG=/home/ubuntu/dkp_img/root/opt/devkitpro
TOOLBIN=/home/ubuntu/.tmp/bin

# 1) devkitPro toolchain symlink
if [ ! -e /opt/devkitpro ]; then
  echo "setup: linking /opt/devkitpro -> $DKP_IMG"
  sudo ln -sfn "$DKP_IMG" /opt/devkitpro
fi

# 2) makerom + bannertool (needed by 'make cia')
mkdir -p "$TOOLBIN"
if [ ! -x "$TOOLBIN/makerom" ]; then
  echo "setup: downloading makerom"
  curl -sL --max-time 60 -o /tmp/makerom.zip \
    "https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.4/makerom-v0.18.4-ubuntu_x86_64.zip" \
    && unzip -o /tmp/makerom.zip -d "$TOOLBIN" >/dev/null && chmod +x "$TOOLBIN/makerom"
fi
if [ ! -x "$TOOLBIN/bannertool" ]; then
  echo "setup: downloading bannertool"
  curl -sL --max-time 60 -o /tmp/bt.zip \
    "https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip" \
    && unzip -o /tmp/bt.zip -d /tmp/bt_extract >/dev/null \
    && cp /tmp/bt_extract/linux-x86_64/bannertool "$TOOLBIN/bannertool" && chmod +x "$TOOLBIN/bannertool" \
    && rm -rf /tmp/bt_extract
fi

# 3) environment for the Makefile
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$TOOLBIN:/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:$PATH

echo "setup: ready. Build with:  make cia   (output/libpd_3ds.cia)"
