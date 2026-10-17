# Modern Sofle build diagnostics

## CI39029091984 — commit0a2b4a96c0b97b4081f227c42d94f44914a1af53

All four left/right release/debug configurations stopped before C compilation.
This is a failed firmware build, not a passed migration.

Primary error:

```
warning: WS2812_STRIP (.../boards/shields/sofle/Kconfig.defconfig:56)
         defined without a type
error: Aborting due to Kconfig warnings
```

Modern CMake still includes the repository's legacy Sofle Kconfig defaults even
though generated DTS uses the correct upstream Sofle overlays. The legacy
WS2812_STRIP option was renamed to the real driver option WS2812_STRIP_SPI.
Only this Sofle-specific default is updated; GPIO, matrix and role definitions
are untouched. RGB remains disabled.

Release also requested PRINTK=n while the default BOOT_BANNER selects PRINTK.
Release now explicitly sets BOOT_BANNER=n rather than suppressing warnings or
leaving unwanted production console output enabled. KSCAN emits a deprecation
notice; the working matrix is not migrated to a different input driver here.

Secondary metadata-only failure:

```
RuntimeError: cannot freeze; project babblesim_base is uncloned
```

west manifest --freeze also attempts inactive projects. The build does not need
inactive BabbleSim simulation projects. The metadata tool now resolves the real
west manifest via its verified Python API, freezes every ACTIVE project at its
actual checked-out HEAD SHA, fails if any active project is missing, and records
all excluded inactive names. It never silently omits a required dependency.
Actual API compatibility was exercised against west1.3.0 on the installed SDK.

Full original logs (all jobs, not overwritten):
/Users/statix/.hermes/cache/scratch/sofle-modern-ci-0a2b4a9-failure/logs.zip
Extracted job logs: same directory /logs/.

No device was flashed or reset in response to this build failure.

## CI39029906605 — commit071aef440203e3a2152d11697415d48f5f7bbe55

Both RIGHT release/debug builds passed; both LEFT builds failed at the new
profile-zoom device registration:

```
DT_N_INST_0_statix_profile_zoom_FULL_NAME undeclared
```

Cause: CONFIG_ZMK_SPLIT_ROLE_CENTRAL is a Kconfig result, unavailable when
Devicetree is preprocessed. The CONFIG conditional removed the new DTS nodes
and retained old sensor fallbacks. These conditionals were removed; node/default
bindings are now declared in both DTS trees, while the actual implementations
and RPC features remain central-only through CMake/Kconfig.

Added generated-DTS validation: three real60-binding layers, correct pushes,
RSR volume/nav/zoom/nav bindings, SETTINGS_TABLE, split roles and unchanged
application/NVS/bootloader partitions. A negative check correctly rejects the
previously compiled RIGHT artifact because its RSR nodes were missing: a
successful compile alone would have been a false positive for StageC.

Full archive:
/Users/statix/.hermes/cache/scratch/sofle-modern-ci-071aef4-failure/logs.zip
The two intermediate right UF2s are NOT a final matched firmware pair.

## Additional verified HID routing issue

The modern upstream HOG sender selected the ACTIVE profile at dequeue time.
During a profile change, even old-endpoint empty reports could therefore go to
the new peer. The local modern-core fork now carries profile_index in all three
HOG queues and endpoints pass the intended profile explicitly. Existing public
sender APIs remain compatibility wrappers; report bodies/descriptors are
unchanged. Source-extracted queue tests (ASan/UBSan) passed17 mock transport
notifications including old-target clears, profile switching, disconnect and
existing overflow-policy routing. These are unit tests, not hardware traces.
