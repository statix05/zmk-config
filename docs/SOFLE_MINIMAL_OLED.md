# PandaKB Sofle v1 MX — minimal OLED replacement

Target: classic Sofle, nice_nano_v2-compatible Pro Micro nRF52840 controllers,
two SSD1306 panels128x32. W-Corne is not part of this change.

## Scope

This replaces only the Sofle display provider. build.yaml retains sofle_left
and sofle_right but removes the additional nice_oled shield. No keymap,
kscan, matrix transform, GPIO, central role, charging circuitry or Bluetooth
settings are changed. Existing USB Studio transport/snippets remain enabled.
Basic DYA keymap-editor support remains; cormoran runtime extensions are NOT
added by this display update.

The root module is opt-in through CONFIG_SOFLE_STATUS_SCREEN=y only in
config/sofle.conf. Other configurations retain their previous path.

## One frame, not overlays

There is exactly one LVGL image on the screen. Its physical frame is128x32,
520bytes including the indexed1bit palette. The deterministic C renderer draws
in portrait32x128 logical coordinates and maps them clockwise once to the panel.
The existing SSD1306 DTS remap/inversion settings remain unchanged. Palette
compensation uses the DTS inversion-on property. No canvas160x160, stock
animation, cat, crystal, Vim, WPM or separate graphic children are created.

Logical bounding boxes (all half-open; no intersections):
- Union logo: x0..32,y0..32.
- BAT/USB: y40..47; battery percentage or --%: y50..57.
- Central OUT U / OUT B: y70..77; BT profile+state: y82..89.
- Central LAYER: y100..107; active layer name: y112..119.
- Peripheral LINK: y70..77; ON/OFF: y82..89; RIGHT: y112..119.

BT indices match BT_SEL0..4, not displayed index+1. Suffix '+' means the selected
Bluetooth profile is connected, '-' bonded/disconnected, '?' open/unbonded.
OUT U indicates current USB output; OUT B indicates BLE output. The BT line is
still the stored active profile, even while output is USB.

Right-side LINK describes the split connection, not the host's Bluetooth profile.
There is no invented peripheral layer/profile state: synchronization of these
central values is a separate feature, not implemented by ZMK's basic display API.

## Battery and USB

Each half reads ONLY its own chosen battery sensor. Reads are serialized on the
same low-priority queue used by ZMK battery.c. On battery, estimate samples every
60seconds while active. On boot/wake and after USB disconnect, allow3000ms before
fetching a fresh sample. Until valid: BAT --%. USB always shows USB / --%, not
Charging, Full, or an inflated voltage-derived percentage. A USB transition during
sampling discards that conversion. Failed samples show --%, not a made-up level.

These are voltage-based SoC estimates, not fuel-gauge accuracy. The board target
proves the configured VDDH source, not the exact electrical schematic of a clone.

## OLED toggle/idle

Existing behavior node behavior_oled_toggle and locality EVENT_SOURCE remain.
A press toggles only that controller's RAM flag; release is a no-op. One display
queue owns both manual and idle blanking. CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=n
removes the competing core activity/unblank listener; our state determines
panel_on = active && !manual_off. No delayed200ms reblank, no settings writes.
Updated frame is flushed before an idle/off panel is turned back on. Ordinary
key activity cannot clear manual_off. Full power-off resets manual state to ON.

LVGL ticks every250ms; the image is invalidated only when bytes change. Local
battery sampling stops on idle/USB and resumes after an appropriate event.

## Verification

Run the exact firmware renderer on the host, not an approximate mock drawing:

    python3 tools/test_sofle_status.py --lvgl /path/to/pinned/lvgl

Pinned LVGL commit used for host headers:
8a6a2d1d29d17d1e4bdc94c243c146a39d635fdd (Zephyr dependency used with ZMKv0.3.0).

An additional native LVGL test (tools/test_sofle_lvgl_frame.c) uses the same
pinned LVGL source to create the real single image object and compare every
flushed pixel against the renderer. All four USB/palette states pass with zero
pixel mismatches and exactly one child object. This does not emulate SSD1306
hardware or Bluetooth.

Tests cover384 central/peripheral, USB, valid/invalid, link, battery and layer
states; byte guards, logo preservation, disjoint bounding boxes, inversion
palette, rotation and USB percentage masking. docs/oled-preview/overview.png
shows real C-rendered example states, not readings from the physical keyboard.

Logo generation:

    python3 tools/generate_sofle_logo.py /path/to/Union.svg

The generator reads the vector path, not a Finder thumbnail. Asset is32x32,
128pixel bytes+8palette bytes. All viewBox edges are preserved.

## Update / rollback — DO NOT RESET SETTINGS for this display-only update

1. Back up/export current Studio assignments if they were changed since the last
   backup. This build leaves the static keymap untouched; saved remaps persist.
2. Identify the physical PandaKB Sofle half independently of its USB name. If
   ambiguous, disconnect W-Corne and connect only the desired Sofle half.
3. Flash only the corresponding left/right UF2. Do not use settings_reset, clear
   bonds or Restore Stock Settings just to replace the display.
4. Roll back by flashing the previous matching left/right images from the archive,
   again without a settings reset. Bluetooth bonds and remaps are retained.

Physical acceptance still required: logo orientation/contrast, no old artwork,
localbattery on both halves, USB --%, recovery after USB removal, separate OLED
toggles, manual-off surviving normal keypress/idle-wake, and unchanged typing,
encoders and Studio connection. Host tests/CI do not claim physical success.
