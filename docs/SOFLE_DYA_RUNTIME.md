# PandaKB Sofle v1 MX — DYA runtime support

This extends the verified minimal-OLED build5be6e18. W-Corne is not involved.
The ordinary ZMK Studio keymap protocol alone does NOT provide runtime macros.
This version adds the actual custom-RPC subsystems used by DYA Studio.

## Pinned stack

- cormoran/zmk v0.3-branch+dya:4493783ef88ce2e653bf8217c92ee17140df71e3
- Core Studio messages:89b81d2e587fce807b668dff2a6967a40beef421 (imported by that core).
- runtime-macro:38530962eb7061b60efcb98e84585cfb1f0cb6de
- runtime-combo:d9490e7b5d41c516221d131241009e81740efcee
- custom-settings:statix05 forka638e9ff962f12b45f68ed84fed3629dba3ac397,
  based on cormoran c6a7fef3a3be3d3ace5de9a4b0628c6418cd1f3f. Two compatibility
  changes: configdefault MAIN_STACK_SIZE becomes config; ROM_SECTIONS linker
  insertion becomes SECTIONS (retaining ITERABLE_SECTION_ROM descriptors).
  Protocol, keyspace implementation and macro storage are unchanged.
- settings-rpc:78f86df9e6c5edaf57bef3ccbd7f360cfdf49291
- ble-management:851661cd21f2aded8ec649da86e01a207dc4b973
  (same protobuf as latest, before unsupported v0.4 endpoint-function rename).
- battery-history:307755dd2ad4d320e14de162e8e5ef018f29d929

Zephyr stays on3.5 and LVGL on8. Hardware sofle/nice_nano sources compare
byte-identical to upstreamv0.3.0. The new OLED C sources and all bindings stay
unchanged. There is no return to the old nice_oled renderer.

## Features and scope

Central-only snippet sofle-dya-central enables runtime macro/combo custom RPC,
custom-settings RPC, BLE management, activity-settings RPC and battery-history RPC.
Shared configuration enables activity settings/split relay on both controllers.

- DYA Macro/Combo tab: real cormoran__runtime_macro and cormoran__runtime_combo
  subsystems, not just the static dbl_click macro.
- Eight runtime macros; shared pool1024bytes; max encoded body256bytes/macro.
- Eight runtime combos. No predefined combo is added or enabled.
- Runtime behavior rmacro is registered and selectable in Studio, but none of
  the existing keyboard positions are reassigned to it automatically.
- BLE profiles/output management and runtime idle/sleep configuration.
- Battery-history firmware RPC is present. This does NOT claim a ready integrated
  history graph in production DYA; the module's separate UI metadata still uses
  its upstream URL. OLED localbattery logic remains unchanged.
- No nonexistent trackball, OS-specific layer switching, fast-keymap engine or
  input-stream diagnostics are fabricated/enabled by this update.

DYA stablev2026.09.20.0 uses the same core custom-protocol messages89b81d2.
Runtime module wire schemas were checked against that release. The live web
bundle/device connection still needs an actual keyboard test; build success is
not a claim that all DYA tabs were exercised on hardware.

## Preserve current settings

Keep SETTINGS_TABLE behavior IDs, NOT CRC16. CRC16 is recommended in module
README examples but is not a runtime-macro/combo hard dependency. Switching ID
schemes could break saved Studio bindings, so this build explicitly retains the
existing table and existing behavior-device names. No settings_reset, bond clear
or Restore Stock Settings is required or recommended.

The include behaviors/runtime_macro.dtsi only registers a new behavior. Automated
comparison verifies every original layer binding and sensor-binding is unchanged.
Core switch means BOTH halves must be updated to the matching new pair; do not
run mixed old/new halves as the final setup.

RPC RX/TX buffers256bytes, custom request payload192bytes, RPC stack6000bytes;
main/system/low-priority stacks4096bytes. Buffers protect macro read/write and
settings boot paths. Explicit root manifest pins override module dependency
imports so custom-settings does not silently advance to main.

## Manual acceptance after matching left/right UF2

1. Reconnect the LEFT Sofle to USB and open https://studio.dya.cormoran.works/.
2. In Macro/Combo verify that 'Runtime macro subsystem not found' is gone.
3. Create a harmless short macro (e.g. a letter), save and note its assigned
   numeric slot. Assign Runtime Macro(slot) to a spare position through DYA.
4. Verify exactly one playback per press, release and persistence after reboot.
   Restore the test key afterwards. Do not test with a destructive shortcut.
5. Create/disable a harmless combo; check its activation and persistence.
6. Read BLE/profile and idle/sleep settings; do not clear bonds as a test.
7. Check original typing, encoders, separate OLED toggles and absence of old art.

Until these steps are actually performed, hardware/UI status is 'not tested'.
For rollback use the archived5be6e18 left/right pair, without settings_reset.
