#!/usr/bin/env bash
# Disable the defective microphone of the USB webcam (328f:0073).
# This camera's flaky firmware binds its audio interface at varying
# interface numbers (observed 02 and 03), so we unbind ANY interface
# claimed by snd-usb-audio on this device. Video (uvcvideo) is unaffected.
# A udev rule with a bind-retry makes it persistent across replugs.
set -euo pipefail

VENDOR=328f
PRODUCT=0073
RULE=/etc/udev/rules.d/99-disable-usb-cam-mic.rules

if [[ $EUID -ne 0 ]]; then
    exec sudo "$0" "$@"
fi

echo "==> Unbinding any camera audio interface on the device currently plugged in..."
targets=()
for dev in /sys/bus/usb/drivers/snd-usb-audio/*:*; do
    [[ -e $dev/bInterfaceNumber ]] || continue
    tgt=$(readlink -f "$dev")
    if [[ $(cat "$tgt/../idVendor") == "$VENDOR" \
       && $(cat "$tgt/../idProduct") == "$PRODUCT" ]]; then
        echo "$(basename "$dev")" > /sys/bus/usb/drivers/snd-usb-audio/unbind
        echo "    unbound $(basename "$dev")"
        targets+=("$tgt")
    fi
done
[[ ${#targets[@]} -gt 0 ]] || echo "    no camera audio interface found (already unbound or camera not plugged in) - skipping"

echo "==> Installing persistent udev rule: $RULE"
cat > "$RULE" <<EOF
ACTION=="add", SUBSYSTEM=="usb", ATTRS{idVendor}=="$VENDOR", ATTRS{idProduct}=="$PRODUCT", RUN+="/bin/sh -c 'for i in 1 2 3 4 5 6 7 8 9 10; do [ -d /sys/bus/usb/drivers/snd-usb-audio/%k ] && echo -n %k > /sys/bus/usb/drivers/snd-usb-audio/unbind && exit 0; sleep 0.5; done'"
EOF

echo "==> Reloading udev rules..."
udevadm control --reload-rules

if [[ ${#targets[@]} -gt 0 ]]; then
    echo "==> Re-firing add event on matched interface(s) so the rule handles any stragglers..."
    for tgt in "${targets[@]}"; do
        udevadm trigger --action=add "$tgt" 2>/dev/null || true
    done
fi

sleep 1
echo
echo "==> Audio cards now:"
cat /proc/asound/cards
echo
echo "==> Audio interfaces still bound to the camera:"
ls /sys/bus/usb/drivers/snd-usb-audio/ 2>/dev/null | grep -E '^1-2:' || echo "    none - camera mic fully disabled"
echo
if grep -q Camera /proc/asound/cards; then
    echo "WARNING: camera mic card still present - unbind did not take effect."
else
    echo "OK: camera mic is disabled. The webcam's video still works as before."
fi
