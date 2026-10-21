# Sofle modern stack + optional BLE HID → USB HID bridge

## Scope and non-destructive policy

PandaKB Sofle v1 MX ONLY. Left remains split central, right split peripheral.
Third Pro Micro nRF52840 is an optional ordinary BLE HID host/USB HID bridge,
NOT a ZMK split central. No flashing, settings resets, bond deletion, bootloader
changes, UICR writes or W-Corne actions without separate user authorization.

## A — Verified baseline

- User project: /Users/statix/Desktop/Проекты/zmk-config
- Original active branch: feat/sofle-dya-runtime
- Baseline commit: 1f62b1545169235d3bbbc2eb186e0b362bb7f721
- Latest matching successful build: CI38775036025.
- Published main8d8d69d and fix/sofle-minimal-oled5be6e18 are older/incomplete.
- Published power/domains-and-device-runtime b1ccd40 is an old2023 template
  branch, not a newer Sofle implementation; it removes our custom UI/runtime
  files. It is not merged or used.
- Original tree has modified firmware/nice_settings_reset.uf2 and untracked
  firmware archives; source files have no uncommitted modifications. Preserved.
- Local tag: baseline/sofle-runtime-1f62b15.
- Backup: /Users/statix/Desktop/Проекты/zmk-config/backups/sofle-baseline-1f62b15/
  contains verified source.bundle, local-working.diff and original firmwarepair.
- Baseline firmware SHA256s match VERIFICATION.json:
  left a7fdf7cd2d8add7807bed3b2d3c971d12abcb9d2f8482d58137f7ecc8f008bfb
  right7875992104e027a8b59e4d774bf604445f1a4d69bc2cf2db2af356c10570b7a3
- Old renderer test:384statesPASS, all existing docs/oled-preview PNGs unchanged.
  LVGL8 reference8a6a2d1d29d17d1e4bdc94c243c146a39d635fdd.
- Actual USB runtime backup saved privately at baseline backup/runtime-usb/.
  Parent verified92file SHA256s,180bindings,27behavior metadata entries and28
  actual custom GET/LIST wire payloads. Standard requests are read/status only.
  This is not raw NVS/flash or bond-key backup; those keys were not requested.

## User-confirmed runtime facts

- Double click is the RIGHT encoder push on BASE: preserve that binding.
- MAC1 = BT_SEL0, currently used.
- MAC2 = BT_SEL1, not paired yet, reserved for future Mac.
- DONGLE = BT_SEL4 (explicit index4, NOT fourth ordinal/index3).
- Profiles2 and3 remain reserved; no stored bonds are reassigned or cleared.
- Right AUX1 push has no newly requested action: set none unless newer runtime
  information says otherwise.

## Connected third controller — READ ONLY observations

User identified the third controller as currently connected as a USB flash disk.
Only /Volumes/NICENANO/INFO_UF2.TXT and CURRENT.UF2 metadata were read.
INFO: UF2Bootloader0.6.0, Model nice!nano, Board-ID nRF52840-nicenano,
SoftDeviceS1406.1.1, buildJun19 2021.
CURRENT.UF2:2752validblocks, addressrange0x1000..0xad000, valid application
vectors at0x26000 (SP0x20040000/reset0x2cf99).
IMPORTANT: exposed CURRENT.UF2 familyID0x239a00b3 differs from the standard
NRF52840/previous half-artifact family0xada52840. Do not silently normalize this
or flash a candidate. Investigate bootloader export/acceptance semantics first.
No controller firmware, settings or bootloader was written.

## Selected modern candidates (verified once at migration start)

- cormoran/ZMK main+dya: e5c9b6915b56801193e359dd9bad4a167ce0d1b8
- cormoran main+custom-studio-protocol:1fc72aaff7a42bd533c1f4c9b3f23c5317c745f4
- upstreammain:4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85
- Selected core manifest: Zephyr v4.1.0+zmk-fixes; LVGL
  f1db87ee98f1810328a8419572fa42a3b5f352ae; custom Studio messages
  89b81d2e587fce807b668dff2a6967a40beef421.
- HWMv2 target candidate: nice_nano@2.0.0/nrf52840/zmk. Must validate generated
  DTS/pins/partitions against baseline, not assume the target string is enough.
- Build image resolved linux/amd64:
  zmkfirmware/zmk-build-arm@sha256:ca4978c179533772d3a77296bc628f8a3ae1ed75ee1cc8b411570f9810fd821e
  env ZEPHYR_VERSION=4.1.0. Actual SDK/tool versions will be captured by CI.
- DYA module candidate SHAs saved in baseline backup/modern-module-candidates.json.
  The old Zephyr3.5 custom-settings backport and old BLE API pin will NOT be
  carried automatically onto the modern stack.

## File/ownership map

- config/west.yml, build.yaml, .github/workflows/build.yml, snippets/, Kconfig:
  parent migration/integration.
- src/sofle_status_screen.c LVGL API-only port, logo descriptors, host tests:
  independent LVGL migration stream; renderer/pixel design preserved.
- Global OLED state and profile-aware zoom: separate logic to integrate AFTER
  LVGL port to avoid concurrent modifications of screen lifecycle code.
- bridge/: independent candidate adaptation with its own SDK/manifest.

## Current implementation progress

- Modern dependency SHAs are now explicitly pinned in config/west.yml, including
  current upstream custom-settings and BLE management (old3.5 workarounds removed).
- Build matrix now has separate left/right release and debug targets using
  nice_nano@2.0.0/nrf52840/zmk; no settings_reset target is part of this work.
- Locally adapted reusable CI pins the build-container digest and captures actual
  SDK/compiler, full frozen west manifest, .config, DTS and ELF/map per target.
- Added release/debug snippets; release disables logs/printk/console, debug keeps
  INFO-level diagnostics rather than per-key debug payload logging.
- DYA encoder/physical-layout/input-stream features configured centrally. Input
  stream is opt-in through the RPC subscriber, not production keystroke logging.
- New display-control wire format and central/peripheral adapter prepared in
  src/sofle_display_protocol.* and src/sofle_display_sync.*. Not yet integrated
  with the in-flight LVGL port, therefore not yet claimed to work on hardware.
- Protocol native test PASS:72serialization roundtrips, duplicate/stale/conflicting
  SETs, manual OFF+typing, reconnect, central reboot, sequence wrap and malformed
  input. No independent right toggle or settings writes.

## Integration checkpoint

- Parent reran LVGL9 native tests:384states/768frames, mismatches0, one child;
  all original PNGs/renderer source still unchanged. Only shared-state capture
  and modern endpoint API were changed after the port; no UI redesign.
- Actual static key arrays checked against baseline:60/60/60, BASE and RAI
  identical, AUX1 only position43 OLED binding changed to none.
- Modern core now uses statix05/zmk@f0da79310f0903246394d8e0dbb1be00f9dcad5f,
  directly based on the selected cormoran e5c9b69. Adds non-mutating recipient-
  pinned HID snapshots and corrects all HOG queues' dequeue-time profile routing.
  Body/report maps, settings format, board/shield and RPC schemas are unchanged.
- Source-extracted HOG queue unit test passed17mock notifications with ASan/UBSan;
  zoom planner17920cases and OLED wire72roundtrips also passed. Not hardware tests.
- CI39029091984 failed Kconfig; CI39029906605 built both right variants but left
  failed because CONFIG guards removed DTS nodes before Kconfig existed. Fixed
  explicit DTS declarations/defaults; generated-DTS gates now reject false greens.
  Complete logs and causes are in docs/SOFLE_BUILD_FAILURES.md and scratch archives.
- Right CI metadata verifies actual image SDK0.16.9/GCC12.2.0, west1.5.0, plus69
  active dependency SHAs. App/NVS/bootloader regions match old nice_nano_v2:
  0x26000/0xc6000,0xec000/0x8000,0xf4000/0xc000. No physical acceptance yet.
- USB backup: user explicitly identified LEFT Sofle and disconnected W-Corne/
  third controller. Initial bootloader mode was rejected; after reconnect the
  actual working ZMK USB interfaces are visible. Real read-only runtime export
  completed and independently checked; no SET/save/reset/unlock/reboot calls.
  Profile0 connected;1/2/3/4 open, so DONGLE4 needs no existing bond deletion.
  One disabled incomplete Combo0 is preserved byte-for-byte (behaviorID0);
  specialized combo LIST/GET returns-19. Compatibility fix is being evaluated
  without deleting, normalizing or enabling that stored record.
- Bridge first candidate was locally compiled; first-pairing gate still needed
  an explicit safe action without guessed GPIO. USB vendor-HID setup is being
  added separately. Original intermediate failures retained, not hidden.

## Delivery checkpoint

CI39033141263 / sourcea8bc7f7: all four half targets PASSED compilation and
actual generated-DTS/config gates. Core579892a adds serialized HID updates.
Downloaded and independently parsed all four UF2s: complete unique block IDs,
application-only address ranges, family0xada52840, exact136-byte logo in both
halves and175-byte bridge HID descriptor in left firmware. Metadata hashes and
frozen manifests checked. No hardware flashing or acceptance was performed.

Bundle: /Users/statix/Desktop/Проекты/zmk-config/firmware/sofle-modern-a8bc7f7/
README.ru.md and VERIFICATION.json explain limitations and original-pair rollback.
Bridge candidate has explicit USB setup and independently passed native C/JS,
linked descriptor and UF2 checks; still NOT_APPROVED for physical flashing.

NOT COMPLETE: existing disabled Combo0 still yields specialized RPC ENODEV;
its raw record is preserved. Investigation was stopped at the owner's budget
request, and unverified scratch edits were not integrated. No new agents.
Actual DYA write/persistence and all physical/manual tests remain pending.

## Remaining acceptance gates

B: HWMv2/LVGL9/CI port, exact golden graphics, both-half builds.
C: current DYA runtime macro/combo/encoders/physical-layout RPC and ID preservation.
D: central-owned explicit ON/OFF sync with reconnect, profile-aware zoom and
   release semantics, no changes to BASE encoder behavior or static dbl_click.
E: chosen existing bridge adapted to ProMicro bootloader, report-map validation,
   release-all/reconnect/pairing safety, reproducible build; no hardware claims.
F: freeze dependencies, release/debug distinction, memory report, rollback and
   manual test checklist. Never declare whole task complete from one phase.
