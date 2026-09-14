# webcam-mic-disabler

Disable the defective built-in microphone of cheap USB UVC webcams on Linux —
without touching video.

## Symptoms this fixes

- Webcam video works in raw capture (e.g. with `v4l2test`, below) but dies
  roughly 30-40 seconds into any browser call (Chrome, Google Meet, webcam test
  sites). The camera LED turns on, then off.
- The device periodically re-enumerates on the USB bus — visible in
  `journalctl -k -f` as `usb x-x: USB disconnect` followed by re-probing, even
  though nobody unplugged anything.
- The kernel log shows `cannot get freq at ep 0x84` from `snd-usb-audio` every
  time the camera is plugged in.

## Root cause

The affected webcam (generic `328f:0073 "USB 2.0 Camera"`) ships a broken USB
audio implementation:

- the sample-rate control on its asynchronous capture endpoint fails at probe
  time,
- every ALSA capture read fails with `-EIO` (0 frames captured in 75 seconds of
  continuous retries),
- when the host audio stack (PipeWire → ALSA → `snd-usb-audio`) touches that
  endpoint while video is streaming, the camera's shared firmware wedges and the
  device performs a USB self-reset about 35 seconds after the stream starts.

Video (`uvcvideo`) is completely unaffected. Disabling the camera's audio
interface stops the crashes entirely: after the fix, calls run indefinitely with
zero resets and zero kernel errors.

## Quick start

```bash
./disable-webcam-mic.sh
```

The script:

1. re-executes itself with sudo if needed,
2. unbinds the sound driver (`snd-usb-audio`) from every audio interface of the
   camera that is currently plugged in,
3. installs `/etc/udev/rules.d/99-disable-usb-cam-mic.rules`, so the same
   unbind happens automatically on every future replug and at boot — including
   a retry loop that wins the race against driver binding,
4. prints the remaining sound cards so you can verify the "Camera" card is gone.

It matches devices by vendor/product ID (`328f:0073`) and **any** audio
interface number, since this camera's firmware alternates between interfaces.
Edit `VENDOR`/`PRODUCT` at the top of the script for your own device (find them
with `lsusb`).

### Verify

```bash
cat /proc/asound/cards                     # "Camera" card must be gone
ls /sys/bus/usb/drivers/snd-usb-audio/     # no 1-2:x.x entries for the camera
```

### Undo

```bash
sudo rm /etc/udev/rules.d/99-disable-usb-cam-mic.rules
sudo udevadm control --reload-rules
```

Then replug the camera.

## Diagnostic tools

Build with `make` (requires gcc):

```bash
./v4l2test /dev/video1 1280 720 MJPG 300    # stream 300 frames
./audiocap plughw:2,0 60                    # try to capture mic audio for 60 s
```

- `v4l2test DEVICE [W] [H] [MJPG|YUYV] [frames]` — enumerates the camera's
  formats, negotiates a mode, and streams N frames via V4L2 mmap. Use it to
  verify video-side health independently of any browser.
- `audiocap [device] [seconds]` — opens the camera's microphone through
  `libasound` (loaded via dlopen, no ALSA headers required), retrying on
  errors. On the affected hardware every read fails with `-EIO`, which
  documents the audio-side defect.

## Tested on

- Ubuntu, x86_64, kernel 7.0.0-31-generic
- Google Chrome 151 with PipeWire (`pipewire-pulse`), Google Meet
- Hardware: generic "USB 2.0 Camera", ID `328f:0073`, serial `SN0001`

## Notes

- The camera's audio endpoint is defective at the firmware level, not the driver
  level — no kernel update will fix it. Disabling the audio interface is the only
  reliable workaround.
- Quirk `0x2` is forced for `uvcvideo` via `/etc/modprobe.d/uvcvideo-quirks.conf`
  to work around the camera's sloppy UVC control responses.

## License

MIT — see [LICENSE](LICENSE).

## Tools

- Gitg — the goat
