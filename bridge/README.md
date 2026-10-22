# Необязательный BLE HID-host → USB HID bridge

**Кандидат, не разрешён к прошивке. Hardware не тестировался.**
Полный аудит/ограничения: [`../docs/BRIDGE_DESIGN.md`](../docs/BRIDGE_DESIGN.md), происхождение: [`UPSTREAM.md`](UPSTREAM.md).

Безопасный default: `BRIDGE_PHYSICAL_PAIRING=n`, ни GPIO, ни battery/OLED/matrix не придуманы.

**Сопряжение v2 (`BRIDGE_AUTO_PAIR=y`, default):** при **пустом** bond-хранилище донгл сам находит connectable HID-объявление с точным именем `BRIDGE_PEER_NAME` (`Sofle_Oled`) и бондится — WebHID-страница для первого сопряжения не нужна (клавиатура так рекламируется только в режиме pairing: BT_SEL 3). Повторное сопряжение: кнопка **«Разорвать связь»** на странице (команда `SETUP_UNBOND`), затем снова BT_SEL 3. Ручной путь scan → точный адрес+type → ARM 20 с сохранён для спорных случаев. Любой существующий bond отключает автопаринг; ничего не очищается неявно. Детали и риски: [`PAIRING.md`](PAIRING.md), страница [`pairing/index.html`](pairing/index.html). USB/BLE/WebHID на железе пока НЕ проверены.

Проверки и candidate build (только файлы на компьютере):

```sh
python3 bridge/tools/test_native.py
node bridge/tests/test_setup_ui.js
python3 bridge/tools/derive_descriptor.py /path/to/pinned-zmk --check
python3 bridge/tools/bootstrap_sdk.py "$HOME/.hermes/cache/scratch/sofle-bridge-ncs"
python3 bridge/tools/build_candidate.py "$HOME/.hermes/cache/scratch/sofle-bridge-ncs"
```

Скрипт создаёт `bridge/build/sofle_bridge_NOT_APPROVED.uf2`, base **0x26000**, family **0x239a00b3**, явно наблюдавшийся в CURRENT.UF2 третьего контроллера. Отличие от `0xADA52840` объяснено исходниками Adafruit bootloader 0.6.0: CURRENT.UF2 использует board-specific `(VID << 16) | PID`, то есть `0x239a00b3`, и upstream принимает оба application ID. Это всё ещё НЕ разрешение к прошивке и не доказательство точного состава установленного clone bootloader. В образе нет SoftDevice/bootloader/UICR/settings payload; compile-only candidate storage layout всё ещё требует отдельного review. Ничего не копировать на UF2-том.

CI запускается только вручную: `.github/workflows/bridge-build.yml`. Workflow подготовлен, но удалённо не запускался; push/commit/flash не выполнялись.
