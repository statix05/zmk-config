# Происхождение, лицензии, граф кода

Выбран **кандидат A**, а не новый стек: [hirosatosou/roba-ble-hid-bridge](https://github.com/hirosatosou/roba-ble-hid-bridge), commit `5bbc2ff3227ec662261db80054d820d312d5cd76`.
`src/main.c` — сокращённая и переработанная адаптация `firmware/m1-hogp/src/main.c`: сохранены NCS scan callbacks → security → GATT DM → HOGP → Report Reference → notify → сериализация USB HID, порядок BT-before-USB.
`src/transport.[ch]` выделяет/исправляет upstream ring serializer для настоящих host-тестов. Это НЕ ZMK split-central и НЕ новый BLE/GATT стек.

Сохранены без изменений upstream `LICENSE`, `NOTICE`, `LICENSES/`; исходные пути внутри этих документов относятся к upstream, а не означают наличие исключённых erase/board файлов в этой поставке.
Для производных Nordic sample файлов действует `LicenseRef-Nordic-5-Clause`: использование только с Nordic IC. Для nRF52840 ограничение выполнено. Generated ZMK descriptor — MIT; полный текст `LICENSES/ZMK-MIT.txt`. Новые build/test scripts и board glue предоставлены как MIT, кроме файлов с явным Nordic SPDX.

**Не перенесены**: Raytac board init / UICR REGOUT0, Raytac partition map, erase sample, DFU commands, GPIO/LED numbers, bond-clear, bootloader/reset, CDC keystroke injection, debug payload dumps, descriptor persistence/reboot, mouse coalescing that overwrites button transitions, forced 2M/7.5-ms loop.

## Кандидат B, исследованный, но не включённый

[jfedor2/hid-remapper](https://github.com/jfedor2/hid-remapper), commit `51ab8b367b810d1deb0d3e487e9b7fe8c7e9c24e`, `firmware-bluetooth/src/main.cc`, `firmware-bluetooth/{prj.conf,CMakeLists.txt}`, `BLUETOOTH.md`, `.github/workflows/build-nrf52.yml`.
Его GPL-3.0 код в реализацию не копировался. Веб-конфигурация B была бы правильным способом pairing без проверенного switch; выдумывать `sw0`/pin0 на ProMicro нельзя. B auto-pairs при пустом списке bonds, имеет устаревшую NCS v2.2-branch сборку и более широкую поверхность конфигурации/мониторинга.

## Зависимости / точки входа

- `sdk-lock.yml` → NCS 2.9.3 + его Zephyr 3.7.99-ncs2-3 + NCS HOGP/GATT DM/scan + SoftDevice Controller/MPSL. Не смешивать с Zephyr 4.1 основного ZMK.
- `CMakeLists.txt` → `src/main.c`, `src/transport.c`, `src/setup.c`.
- `boards/sofle/bridge_nano` → только SoC, USB, GPIO controllers, RC LFCLK, candidate application/storage partitions; ни matrix, ни OLED, ни battery/charger/EXT_POWER.
- `src/zmk_descriptor.h` ← `tools/derive_descriptor.py` ← настоящий `app/include/zmk/hid.h` pinned ZMK.
- `src/setup.[ch]` (новый MIT код, не копия GPL remapper UI) → чистый bounded RAM state machine: scan/наблюдения/select exact address+type/arm/claim/expiry и отдельный vendor feature descriptor. `main.c` связывает его с `HID_1` EP0 feature callbacks и NCS scan/connection/SMP gates, оставляя bonded reconnect и исходный `HID_0` descriptor. Старый wildcard-window helper `pairing_policy.h` удалён.
- `pairing/index.html`, `pairing/setup-ui.js` (новый MIT код) → только ручные WebHID feature команды, точные USB filters, никакого inputreport/RPC/erase/bootloader транспорта.
- `tests/test_transport.c` → реальные `transport.c`, generated descriptor, ASan+UBSan.
- `tests/test_setup.c` → реальный `setup.c`, ASan+UBSan: envelope validation, neighbor-first rejection, exact target, deadlines, one attempt, busy/bond gates и отдельный descriptor. `tests/test_setup_ui.js` → настоящий UI encoder/filter/parser с mocked HID exchange, не аппаратный browser test.
- `tools/build_candidate.py` → west no-sysbuild → `verify_build.py` → `candidate_uf2.py`. Ни один шаг не взаимодействует с контроллером.
