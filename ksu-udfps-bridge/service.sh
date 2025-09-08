#!/system/bin/sh

# Wait for sysfs nodes
HBM_NODE="/sys/panel_feature/hbm_mode"
FOD_NODE="/sys/devices/platform/goodix_ts.0/gesture/fod_en"
TIMEOUT=15
while [ $TIMEOUT -gt 0 ]; do
  [ -w "$HBM_NODE" ] && [ -w "$FOD_NODE" ] && break
  sleep 1
  TIMEOUT=$((TIMEOUT-1))
done

# Enable FOD gesture once
if [ -w "$FOD_NODE" ]; then
  echo 1 > "$FOD_NODE"
fi

# Start userspace daemon
chmod 0755 "$MODPATH/bin/udfpsd"
exec "$MODPATH/bin/udfpsd" 2>&1 | logcat -s udfpsd &
exit 0
#!/system/bin/sh
# Start low-overhead udfpsd (netlink/uevent based)
MODPATH="${0%/*}"
BIN="$MODPATH/system/bin/udfpsd"

if [ -x "$BIN" ]; then
  "$BIN" &
fi
exit 0
