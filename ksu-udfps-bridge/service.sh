#!/system/bin/sh
# Start low-overhead udfpsd (netlink/uevent based)
MODPATH="${0%/*}"
BIN="$MODPATH/system/bin/udfpsd"

if [ -x "$BIN" ]; then
  "$BIN" &
fi
exit 0
