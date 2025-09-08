#!/system/bin/sh

MODPATH="${0%/*}"
BIN1="$MODPATH/bin/udfpsd"
BIN2="$MODPATH/system/bin/udfpsd"

# Wait for sysfs nodes (finger_hbm or hbm_mode, and fod_en)
TIMEOUT=20
HBM_NODE=""
FOD_NODE=""
while [ $TIMEOUT -gt 0 ]; do
  if [ -w /sys/panel_feature/finger_hbm ]; then
    HBM_NODE="/sys/panel_feature/finger_hbm"
  elif [ -w /sys/panel_feature/hbm_mode ]; then
    HBM_NODE="/sys/panel_feature/hbm_mode"
  fi

  for P in \
    /sys/devices/platform/goodix_ts.0/gesture/fod_en \
    /sys/devices/platform/goodix_ts.1/gesture/fod_en \
    /sys/devices/platform/goodix_ts/gesture/fod_en; do
    [ -w "$P" ] && FOD_NODE="$P" && break
  done

  [ -n "$HBM_NODE" ] && [ -n "$FOD_NODE" ] && break
  sleep 1
  TIMEOUT=$((TIMEOUT-1))
done

# Enable FOD gesture once
if [ -n "$FOD_NODE" ]; then
  echo 1 > "$FOD_NODE"
fi

# Start userspace daemon
if [ -x "$BIN1" ]; then
  chmod 0755 "$BIN1"
  "$BIN1" 2>&1 | logcat -s udfpsd &
elif [ -x "$BIN2" ]; then
  chmod 0755 "$BIN2"
  "$BIN2" 2>&1 | logcat -s udfpsd &
fi
exit 0
