# Optional BLE HID host → USB HID bridge: Stage E

## Статус

**Исходники, host-тесты и реальная локальная Arm-сборка готовы. К прошивке НЕ разрешено.** Никаких аппаратных writes, reset, bond-clear, bootloader changes, внешних commit/push или запусков удалённого CI не выполнялось. Подключённый UF2-том не использовался для записи. Не путать успешную компиляцию с проверкой Bluetooth/USB на железе.

Blocker неизвестной кнопки устранён **в исходниках и локальной сборке**, не догадкой о GPIO: добавлен отдельный USB vendor HID интерфейс и явный first-pair setup scan → показ кандидатов → выбор точного BLE address+type → отдельный ARM на20 с, одна попытка только этой цели. Пустой NVS не открывает окно. `BRIDGE_PHYSICAL_PAIRING=n`; ни pin0/sw0/reset/LED не назначены. Инструкция и фиксированный протокол: [`../bridge/PAIRING.md`](../bridge/PAIRING.md), UI [`../bridge/pairing/index.html`](../bridge/pairing/index.html). **USB/WebHID/BLE на железе ещё не проверены**, разрешения на прошивку нет; аппаратная приёмка остаётся blocker. Для обычного ввода после сохранения bond страница/приложение не нужны.

## Неизменяемая архитектура клавиатуры

```text
right Sofle peripheral ── ZMK split BLE ──> left Sofle central
                                               │ обычный BLE HID peripheral
                    ┌──────────────────────────┼──────────────────────┐
                 Mac profile 0            Mac profile 1        DONGLE profile 4
                 существующий             резерв будущего           │
                                                        optional bridge BLE central
                                                                  │ USB HID
                                                       Windows / Linux dual boot
```

DONGLE — **`BT_SEL 4`**, не автоматически придуманное значение 2. Один и тот же firmware половинок продолжает работать с Macs напрямую или через optional bridge; правой половинке не меняется split role. Bridge — отдельный **HOGP host**, НЕ ZMK split-central, НЕ template dongle с raw matrix. ОС видит USB HID; её Bluetooth pairing и постоянное host-ПО не нужны для обычного набора. DYA/ZMK Studio RPC, OLED, keymap, макросы и энкодеры bridge не зеркалирует. Подтверждённый double-click правого encoder push BASE остаётся обязанностью половинок и не менялся.

## Выбор кандидата по коду

### A: выбран

`hirosatosou/roba-ble-hid-bridge@5bbc2ff3227ec662261db80054d820d312d5cd76`, `firmware/m1-hogp/src/main.c`, `prj.conf`, board support, LICENSE/NOTICE. NCS **2.9.3**.

README описывает fixed roBa map, однако **этот конкретный checkout уже содержит dynamic Report Map capture в NVS и reboot**, плюс optional CDC keystroke injection. Решение принято по исходникам, а не по устаревшему README. Сохраняется его узкая цепочка NCS scan → security → GATT DM → HOGP → USB serialized queue. Вместо динамической карты/перезагрузок фиксируется строго проверенный настоящий map этой Sofle.

Проблемы A, устранённые в адаптации:

- `pairing_mode=true` при отсутствии bonds и `connect_if_match=1` автоматически выбирали произвольный HID. Теперь окно закрыто при каждом старте, пустой NVS не меняет политику, auto-connect выключен; scan callback проверяет bond или explicit window.
- Directed advertisements обходили первичный filter; теперь directed reconnect допускается **только для известного bond**.
- `gatt_discover()` мог запускаться даже при ошибке security. Теперь discovery только после L2; ошибка разрывает соединение, не удаляет bond.
- Нет release-all при disconnect; теперь очищаются ожидающие presses/deltas и отправляются нулевые keyboard/consumer/mouse reports.
- roBa consumer FULL 6×LE16 не соответствует здешнему BASIC 6×uint8. Исправлена карта/длина.
- Mouse coalescing перезаписывал buttons последним состоянием и мог схлопнуть press/release. Теперь mouse-события идут дискретно, в порядке FIFO; все четыре signed int16 axes сохраняются.
- Жёсткие 2M, min=max=6, отказ запросам latency и повторное навязывание интервала удалены. Софле `EXPERIMENTAL_CONN` не обязана поддерживать 2M. Начальный запрос — 7.5–30 ms, latency ≤30; callback допускает запросы периферии, в рамках проверки контроллера. Это не обещание конкретной задержки.
- Указатели очереди больше не могут переиспользовать in-flight USB buffer. Busy retries происходят независимо от нового входа. Overflow приводит к release-all, а не молчаливой потере последнего key-up; при overload возможна потеря событий, не гарантируется lossless-stream.
- Убраны payload dumps, CDC injection, dynamic descriptor persistence/reboot, bond clear, Raytac GPIO/LED, board init/UICR REGOUT0 и Raytac partitions. Никаких erase/flash команд в реализации.

### B: исследован и отклонён для этого узкого контракта

`jfedor2/hid-remapper@51ab8b367b810d1deb0d3e487e9b7fe8c7e9c24e`, `firmware-bluetooth/src/main.cc`, common remapper/parser, `BLUETOOTH.md`, workflow. Upstream BLE назван experimental, CI использует `nordicplayground/nrfconnect-sdk:v2.2-branch`, поддержаны Feather/Xiao, не ProMicro.

Плюсы: настоящий parser/normalizer, несколько устройств, keyboard/consumer/mouse, существующее WebHID pairing/config UI. При выборе B pairing следовало бы делать через его веб-интерфейс, не выдумывать user switch GPIO. Но здесь normalization не нужен: есть один фиксированный ZMK map. Дополнительные remapping/config/monitoring/bootloader команды повышают поверхность изменений и риск преобразования reports.

Конкретные дефекты исследованного checkout:

- `scan_setup_filters()` при нуле bonds переводит `peers_only=false`: auto-pairing.
- `scan_filter_no_match()` при directed advertising не проверяет, что это известный peer.
- В `hogp_notify_cb()` `buf.len=bt_hogp_rep_size(rep)+1`, затем `memcpy(buf.data+1,data,buf.len)` читает **на один байт больше BLE body**; корректно копировать body size с отдельной проверкой capacity.
- `hogp_map_read_cb()` пишет `their_descriptor.data+offset` без проверки 512-byte capacity и error handling.
- Требует sw0 и двух LEDs; копировать эти предположения с Feather/Xiao на неизвестный ProMicro нельзя.

B GPL-код не копировался. Лицензии A сохранены в `bridge/LICENSE`, `bridge/NOTICE`, `bridge/LICENSES`; Nordic-derived файлы ограничены Nordic IC, что подходит nRF52840. Граф/происхождение файлов: `bridge/UPSTREAM.md`.

## Настоящий ZMK report contract

Источник: `cormoran/zmk@e5c9b6915b56801193e359dd9bad4a167ce0d1b8`, `app/include/zmk/hid.h`, `app/src/hid.c`, `app/src/hog.c`, `app/include/zmk/{keys,pointing}.h`; baseline `config/sofle.conf` и замороженный основным stage pointing snippet. Фиксированные настройки половинок: HKRO, keyboard size6, consumer size6, consumer BASIC, pointing=y, NKRO=n. Smooth scrolling/LED outputs не включены в этот контракт.

| BLE Report Reference (ID,type) | BLE characteristic **body**, sizeof | USB IN payload |
|---|---|---|
| `(1,1 input)` | 8: modifiers:uint8, reserved:uint8, keys[6] | 9: `01` + восемь body bytes |
| `(2,1 input)` | 6: шесть BASIC uint8 consumer usages | 7: `02` + шесть body bytes |
| `(3,1 input)` | 9: buttons:uint8 + X,Y,vertical scroll,horizontal scroll, каждый int16 LE | 10: `03` + девять body bytes |

GATT notify из `hog.c` передаёт **body без report ID**. ID/type берутся из HOGP Report Reference, **не из порядка discovery, characteristic handle или первого data byte**. USB descriptor включает ID, поэтому bridge добавляет его ровно один раз. Длины проверяются строго, неизвестные IDs/type и malformed sizes отвергаются, повторно уже prefixed USB report не принимается как BLE body.

`bridge/tools/derive_descriptor.py` компилирует **реальный initializer descriptor и реальные report structs из pinned hid.h** с минимальным HID macro shim. Compile-time `_Static_assert` проверяет body8/6/9 и USB9/7/10, используя настоящие uint8 typedef из keys.h/pointing.h. Native независимый HID item walker проверяет input bit lengths64/48/72. Получена карта **175 bytes**, SHA256 `74d336d8452b95651d393840c3b66ccce7a07894ac73da62a75e46fcc365166b`. `hid.h` SHA256 `dd161a6631a9dad59f320389a0c146712b2c953215f4a97a8d0d64b0b3ffdee7`.

При подключении карта peer читается целиком и сравнивается **byte-exact**; mismatch разрывает соединение до передачи input. Перед deployment сравнить ещё и финальную `.config` половинки: изменение NKRO, consumer FULL, indicators или smooth scrolling потребует повторной генерации/тестов, оно не должно незаметно трактоваться старой картой. Caps/Num LED outputs, mouse resolution features и Studio RPC в bridge отсутствуют. USB boot protocol выключен; **BIOS/UEFI/boot-menu совместимость не заявляется**.

## Pairing, reconnect, privacy

- Startup: settings_load → bondable=false → сканирование; незнакомые HID игнорируются даже при нуле bonds.
- Существующий peer допускается по восстановленному bond/identity, BT_PRIVACY включён для RPA resolution. Directed advertising без bond игнорируется. Реальные reconnect/RPA/sleep сценарии ещё не измерены на железе.
- Новое сопряжение: только явный USB SCAN (сбор30 с, max16 connectable HIDS объявлений) → SELECT точного наблюдавшегося address+type → отдельный ARM **того же** target на20 с, одна попытка. Выбор по имени, первому/случайному HID, пустому/ANY/wildcard target запрещён. Ни scan, ни select сами pairing не разрешают. Deadline проверяется непосредственно в connection/SMP gate, не только периодическим таймером. При смене RPA до первого bond требуется новый выбор, а не расширение до ANY.
- Отдельный `HID_1`: vendor Application TLC page0xFF31/usage0x74, **Feature ID0x40**, fixed body32 / wire33, version1, строгая проверка type/ID/size/version/op/reserved bytes. `HID_0` и исходная175-byte карта IDs1/2/3 не менялись. USB HID device count2, composite. Нет Studio RPC proxy, input/output reports на setup-интерфейсе и payload logs. WebHID filter содержит одновременно bridge VID/PID и vendor usage; UI дополнительно отвергает protected keyboard/mouse коллекции. Выбран отдельный интерфейс, чтобы не зависеть от browser-исключений для feature внутри keyboard TLC. Browser/device accessibility **не заявляется проверенной**: см. ссылки на Chrome protected-usage docs/spec и точный framing в PAIRING.md.
- При активном connection/pending или armed window scan/select/arm не вмешиваются. При уже сохранённом bond select/arm запрещены без автоматической замены/очистки. USB disconnect/reset/suspend закрывает разрешение, существующий bond не стирается. Physical path не реализован вообще; прежний negative GPIO safety gate сохранён.
- `BT_SMP_APP_PAIRING_ACCEPT` отдельно допускает новый SMP только для точного выбранного peer внутри окна; запрещает re-pair/замену существующего bond. При timeout неизвестное незабондированное соединение разрывается; успех закрывает окно. Имя/RSSI не устанавливают личность клавиатуры; даже BLE address можно подделать. UI показывает это предупреждение и требует отдельного подтверждения цели. **Just Works не защищает от активного MITM**, WebHID-страница не делает BLE криптографически аутентифицированным.
- Один сохранённый bond; overwrite-oldest выключен. Bond-clear, автоматический unpair, resets/reboot и удаление settings отсутствуют. Миграция старых чужих bonds/storage не решается стиранием: нужна отдельная проверка совместимости и разрешение владельца.
- На отключении BLE и при USB re-enumeration очередь приводится к release-all. In-flight USB frame завершится перед нулевыми frames; при отсутствующем USB нули удерживаются до готовности endpoint.
- Production `.config`: LOG=n, PRINTK=n, CONSOLE=n, SERIAL=n, boot banners=n; payload не логируется вообще. Счётчик overflow — только RAM, не input payload.

## Третий контроллер: UF2 evidence и границы

От parent получены read-only INFO_UF2: bootloader0.6.0, Model nice!nano, Board-ID nRF52840-nicenano, SoftDevice S1406.1.1, build Jun19 2021. Проверен сохранённый metadata JSON:
`/Users/statix/Desktop/Проекты/zmk-config/backups/sofle-baseline-1f62b15/third-controller-uf2-metadata.json`:
2752 valid blocks, диапазон `[0x1000,0xad000)`, app vectors0x26000, SP0x20040000, reset0x2cf99, header family0x239a00b3. Это read-only evidence, не approval.

**Отличие family объяснено исходниками, не исправлено наугад.** В [Adafruit bootloader0.6.0](https://github.com/adafruit/Adafruit_nRF52_Bootloader/tree/42d9d9fe9d1f4d9a00a05f87b95ee310dbb2f673):

- `src/boards/nice_nano/board.h:56–58`: VID0x239A, UF2 PID0x00B3.
- `src/usb/uf2/uf2cfg.h:4–9`: generic app family0xADA52840; board-specific app ID `(VID << 16) | PID`.
- `src/usb/uf2/ghostfat.c:365–366`: CURRENT.UF2 экспортируется именно с `CFG_UF2_BOARD_APP_ID`, то есть0x239a00b3.
- `ghostfat.c:391–395`: upstream application receiver различает и принимает board-specific и generic app ID.

Таким образом, наблюдаемый header согласуется с legacy nice!nano board-specific ID, а не с другой моделью MCU. Это **не доказывает**, что установленный clone binary полностью равен upstream tag или что выбранная новая NVS layout совместима с его прежним приложением. Никакой silent rewrite header по имени nice!nano не сделан: packaging требует явный наблюдавшийся0x239a00b3 и base0x26000.

Отдельная bridge board description не содержит Raytac partition map или board.c. **Compile-only** layout: application `[0x26000,0xe0000)`, storage `[0xe0000,0xe8000)`, остальное зарезервировано. Это новый кандидат, а НЕ доказанный физический partition map. NCS SoftDevice Controller/MPSL линкуется внутри приложения, не использует старый S140 как системный BLE stack и не содержит его обновление. Старые SoftDevice/bootloader страницы не входят в HEX/UF2. Возможность standalone Zephyr app в этом bootloader и конфликты settings требуют review до любой будущей прошивки.

## Реально выполненные проверки

Команды из корня worktree `/Users/statix/Desktop/Проекты/zmk-sofle-modern`:

```sh
python3 bridge/tools/test_native.py
node bridge/tests/test_setup_ui.js
python3 bridge/tools/derive_descriptor.py bridge/.cache/pinned-descriptor-core --check
python3 bridge/tools/bootstrap_sdk.py /Users/statix/.hermes/cache/scratch/sofle-bridge-ncs
python3 bridge/tools/build_candidate.py /Users/statix/.hermes/cache/scratch/sofle-bridge-ncs
```

- Native C executables: ASan+UBSan, strict warnings, keyboard/modifiers/F13–F24 usages, BASIC consumer, signed16 mouse boundaries, неправильные types/lengths/IDs, shuffled references, 10000 ring wraps, disconnect release-all, overflow neutral, in-flight lifetime — **PASS**. Отдельный setup executable проверяет envelope/type/ID/length/version/op/reserved fields, wildcard/type rejection, scan dedupe/capacity, сосед с тем же именем появляется **раньше** выбранной цели, scan/select не разрешают pairing, точный target+type, one-attempt, deadlines/expiry/USB-close, busy/bond gates и vendor-only descriptor — **PASS**. Это не BLE/USB emulation.
- `node bridge/tests/test_setup_ui.js`: filters VID/PID/vendor, protected collection rejection, exact-target encoding, malformed responses и mocked feature exchange — **PASS**, не browser/device test. Нет заявления о реальном WebHID chooser/USB transfer.
- Параллельно parent сменил HEAD общего core checkout на `16afbfa5bde6f35611f1e83824617c06cc833a2d`; pinned descriptor checker корректно отказался работать на непроверенном HEAD. Не меняя checkout parent, создан отдельный read-only-by-convention sparse clone `bridge/.cache/pinned-descriptor-core` на исходном `e5c9…`; повторный `--check` прошёл, descriptor175 bytes/SHA74d336… неизменён. Воспроизводимый CI по-прежнему забирает именно pinned commit.
- Отдельный negative build с `CONFIG_BRIDGE_PHYSICAL_PAIRING=y` без DT alias **ожидаемо отклонён** compile-time сообщением `Verify physical wiring and define bridge-pair; never guess a GPIO`; `bridge/pairing-gate-test.log`. Ни тестового GPIO, ни pinout в board не добавлялось.
- Actual cross build: NCS2.9.3 `6961c01f95c2ec9f0f61870b68904bd398811f49`; NCS Zephyr `aa34a5632971262ce4002e73ed0fb09e9bd14808`; SDK **Zephyr0.17.0**, Arm GCC **12.2.0**, west1.3.0, Python3.11, macOS arm64. Все transitive commits зафиксированы `bridge/sdk-lock.yml`; `bridge/west.yml` — собственный исходный manifest.
- `build_candidate.py` использует `west build --no-sysbuild -b bridge_nano/nrf52840 ... -- -DBOARD_ROOT=<absolute bridge>`. `PARTITION_MANAGER_ENABLED=n`, `FLASH_LOAD_OFFSET=0x26000`, `FLASH_LOAD_SIZE=0xba000`. Нет MCUboot/sysbuild image.
- Финальная чистая сборка/verification/packaging с USB setup: **exit0**. FLASH **183328 bytes /744KiB**, RAM **46744 bytes /256KiB**. Это линкерные статические расходы, не измеренный runtime stack watermark. Отдельный debug image не понадобился; production logging по-прежнему выключен.
- Actual HEX: `[0x26000,0x52c20)`, без записей UICR, bootloader, SoftDevice или storage. BIN vectors SP0x200085d0, reset0x38d21. Обе карты (неизменённая input175 bytes и отдельная vendor feature23 bytes) найдены в реально слинкованном BIN. Production Kconfig, включая device count2, проверен программно.
- BIN SHA256 `ba302190789c0bcf99a49f4d9ccde2e3adb7c97603c62a9e702686194f4505d8`.
- Candidate UF2 **717 blocks**, family0x239a00b3, SHA256 `b0e76d6b660d55c76afd84be0d11746f07b5511a4666a8db8b2ccfa2d7a107b8`. Blocks/addresses/round-trip BIN проверены после упаковки. Файл **`bridge/build/sofle_bridge_NOT_APPROVED.uf2`**. Это не файл для копирования на подключённый том.
- Предыдущий UF2709 blocks сохранён без изменений: `bridge/archive/pre-usb-setup/sofle_bridge_NOT_APPROVED.uf2`, SHA256 `102bd3e828f1676ee8d950c1ba3629411689dfee28a19e94b05c8025b860755a`, рядом прежний verification.json. `bridge/BUILD_FAILURES.md`, прежний `build-pairing-gate/` и `pairing-gate-test.log` сохранены.

Артефакты и evidence: `bridge/build/zephyr/{zephyr.elf,zephyr.hex,zephyr.bin,zephyr.map,.config,zephyr.dts}`, `bridge/build/verification.json`, `bridge/build.log`, `bridge/native-tests.log`, `bridge/descriptor-verification.json`, `bridge/bootstrap.log`. Generated build directory исключён `.gitignore`, но локально существует.

Первые неудачные попытки разобраны по настоящим ошибкам, не по shell/gitstatus banner: sysbuild не видел custom BOARD_ROOT; неверное предположение о void bt_hogp_init; затем собственный HEX verifier не учитывал extended segment address type02. Исправления и сохранённые точные выдержки: `bridge/BUILD_FAILURES.md`. Финальные upstream dtc unit-address warnings и test-only USB VID/PID warning не скрывались.

`.github/workflows/bridge-build.yml` подготовлен: **только workflow_dispatch**, read-only permissions, отдельный locked NCS workspace, SDK checksum verification, native tests, regenerating ZMK descriptor, actual build/config/HEX checks, artifact с явным NOT-APPROVED названием. Actions pinned по SHA. **Сам workflow в GitHub ещё не запускался.**

## Что блокирует аппаратную приёмку

1. Явный запрет владельца на flash/reset/bond/bootloader writes остаётся в силе.
2. Новый USB-controlled first-pair path собран/проверен native/mock тестами, но **не проверен реальным USB/WebHID/BLE устройством**. Подтверждённого physical switch/overlay нет и не требуется этому пути; придумывать GPIO или autopair по-прежнему запрещено.
3. Проверить фактическую storage/bootloader совместимость нового приложения и отсутствие миграционных конфликтов; INFO/metadata не равны полному аудиту bootloader binary.
4. Выделить корректные USB VID/PID перед распространением:0x2FE3/0x0100 — Zephyr test IDs.
5. После отдельного разрешения потребуются аппаратные тесты: один явный pairing, reconnect/RPA после сна/USB unplug/reboot и Windows↔Linux boot, отсутствие stuck modifiers/buttons при RF loss/queue pressure, media keys, обе оси/scroll, direct Mac profiles0/1 и bridge4, range/latency. +8dBm сохранено; 2M не требуется. Ни один такой тест здесь не заявляется выполненным.
