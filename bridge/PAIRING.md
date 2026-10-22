# Явное первое сопряжение по USB (Stage E)

**Кандидат NOT APPROVED. Прошивка/сброс/действия с устройствами не разрешены и здесь не выполнялись.** Инструкция ниже предназначена для будущей отдельной аппаратной приёмки после разрешения владельца, а не предлагает сейчас копировать UF2 на устройство.

## Обычный ввод и однократная настройка

Sofle left остаётся BLE central для своей правой половинки и BLE HID peripheral для bridge. Optional bridge — BLE HID host → стандартный USB keyboard/consumer/mouse. В clean-ветке DONGLE = **BT_SEL 3** (профили 0/1/2 = Macs, 4 запасной). Bluetooth-настройки ОС и постоянное приложение для ввода не нужны. Это не Studio RPC, не remapper, не прошивальщик.

## Автосопряжение (v2, по умолчанию)

Если в bond-хранилище донгла **ноль** связей и появляется connectable-объявление с HID UUID и **точным полным именем** `CONFIG_BRIDGE_PEER_NAME` (по умолчанию `Sofle_Oled`), донгл сам соединяется и бондируется — страница не нужна. Клавиатура рекламирует такое имя только когда профиль в режиме pairing (BT_SEL 3 без связи), поэтому flow: прошить донгл → BT_SEL 3 на клавиатуре → связь устанавливается сама.

Риск и граница: имя из рекламы недостоверно (может быть подделано соседним устройством). Автопаринг принимает **первое** точное совпадение имени, пока хранилище пусто. При любом существующем bond путь полностью отключён. Повторное сопряжение: отправить **SETUP_UNBOND** (страница), затем снова BT_SEL 3 на клавиатуре. Для заведомо точного выбора адреса остаётся ручной SCAN/SELECT/ARM ниже.

Не предполагать, что профиль3 свободен. Если он занят, остановиться. Если bridge уже хранит bond, SELECT/ARM возвращают BONDED и не заменяют его; автопаринг тоже неактивен. Единственный путь очистки — **явная** команда SETUP_UNBOND (ничего не очищается неявно: ни при перезагрузке, ни при USB reset, ни при неудаче pairing).

## Страница настройки

Источник: `bridge/pairing/index.html`, `setup-ui.js`, без CDN/внешнего сервиса. После разрешения аппаратного теста страницу можно обслуживать локально:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory bridge/pairing
# Chrome/Edge: http://localhost:8765/
```

Сервер привязан только к loopback; нужен secure context localhost или доверенный HTTPS. Safari/Firefox не заявляются поддержанными. На Linux доступ может блокироваться правами hidraw — не отключать browser protected-usage guards и не выдавать сайту доступ ко всем HID. Никакой установки native host-приложения инструкция не требует.

1. Пользователь нажимает **Открыть USB bridge** и выбирает устройство. Filter одновременно задаёт **VID 0x2FE3, PID 0x0100, vendor usage page 0xFF31 / usage 0x74**. Это временные test IDs, не выделенные product IDs; перед распространением поменять согласованно firmware и UI. UI дополнительно проверяет vendor-only collection и feature ID0x40; стандартный keyboard/mouse интерфейс не открывает. Если браузер вернул несколько интерфейсов, выбирается только один точный vendor match; неоднозначность — отказ.
2. На целевой Sofle выбрать DONGLE profile4, если он действительно свободен. Нажать **Сканировать 30 с**. Это разрешает только сбор connectable HIDS UUID advertisements, НЕ соединение/сопряжение с незнакомыми устройствами. Background scan для существующего bond работает и без UI.
3. Нажать **Обновить объявления**. Отображаются точный адрес, тип public/random, RSSI и доступное недостоверное имя (до18 ASCII bytes; имя может отсутствовать, в частности если оно только в отдельном scan response). Лимит16 уникальных address+type за сессию; переполнение не выбирает соседа, лишние записи игнорируются. При необходимости повторить scan, не выбирать приблизительно похожее имя.
4. Отдельно проверить, какое объявление принадлежит именно целевой клавиатуре: например, наблюдением исчезновения/появления при выключении/включении только её. **Имя и RSSI не устанавливают личность. Адрес тоже можно подделать; Just Works не даёт защиты от активного MITM.** При неопределённости остановиться. Нажать **Выбрать** рядом с конкретным address+type. Сам выбор ещё ничего не подключает.
5. До конца30-секундной scan-сессии отметить предупреждение и отдельно нажать **Разрешить pairing…**, подтвердив окно с тем же адресом/типом. Firmware сверяет целиком выбранный ранее target и допускает только **одну попытку соединения**, только этому address+type, максимум **20 с** от ARM. Окно не продлевается повторным ARM. Никаких first/random HID fallback, wildcard address/type или name-based pairing. Если privacy-адрес сменился, повторить scan/выбор; не расширять фильтр до ANY.
6. Нажать **Состояние**. `bond=true` — callback завершения bonding/восстановленный bond; `connection=true` включает pending attempt и не доказывает готовность HID. Само закрытие окна не означает успех. После отдельного подтверждения ввода/переподключения на железе страница больше не нужна. При неуспехе окно закрывается; bonds не очищаются. Следующая попытка требует нового явного scan/select/arm.

При BLE disconnect/failure, завершении pairing или expiry разрешение закрывается; USB reset/disconnect/suspend также закрывает setup-окно (это обработка события шины, не команда reset). Неизвестное соединение вне окна разрывается. Уже сохранённый bonded reconnect не требует ARM; SMP re-pair/замена существующего bond запрещены. `bt_conn_foreach` в timeout-обработке держит временную ссылку на connection; новый main-loop код не разыменовывает освобождаемый RX-потоком `default_conn`.

## Почему отдельный USB HID интерфейс

`HID_0`: **неизменённая**175-byte Sofle report map, IDs1/2/3, BLE bodies8/6/9 → USB9/7/10. `HID_1`: отдельная23-byte report map с единственной Application TLC на vendor page0xFF31/usage0x74 и единственным **Feature ID0x40**, body32 bytes. Input/Output reports на setup-интерфейсе отсутствуют. Не используется report ID1/2/3 и не добавляется feature внутрь keyboard TLC. NCS legacy USB stack собирает два HID интерфейса (`USB_HID_DEVICE_COUNT=2`, composite); control transfers идут по EP0, второй interrupt endpoint не передаёт input.

[Chrome WebHID security documentation](https://developer.chrome.com/docs/capabilities/hid) описывает protected usages keyboard/mouse TLC; [WebHID issue126](https://github.com/wicg/webhid/issues/126) отдельно отмечает различия трактовки feature reports между Chromium и blocklist specification. Реализация **не полагается на исключение для feature внутри keyboard**: отдельный vendor интерфейс исключает общие protected TLC и ID. [receiveFeatureReport specification](https://hid.spec.whatwg.org/#dom-hiddevice-receivefeaturereport) допускает, что первый байт содержит report ID; здесь UI строго требует33 bytes с первым0x40, иначе безопасно отказывает, а не угадывает формат. Документация/исходники SDK и mock-тесты — не доказательство фактического перечисления/доступа WebHID на Windows/Linux/macOS. Такой browser/device test **ещё не выполнен**; если OS/browser отдаёт иной feature framing, будет явный отказ, нужен отдельный совместимый review, не обход защиты.

## Фиксированный протокол v1

USB HID `wValue=0x0340` (Feature, ID0x40), `wLength=33`. SET/GET report принимаются только на vendor-интерфейсе; неправильные type/ID/length/prefix дают STALL (`-EINVAL`). Feature body **32 bytes**; API `sendFeatureReport(0x40, body32)` добавляет ID транспортом. Firmware принимает raw33 с ID. GET возвращает последний ответ, никаких side effects; перед ним UI отправляет команду. Каждая операция сериализована UI; состояние firmware защищено spinlock. В USB callback нет BT/storage/reset операций. Весь протокол не имеет input-payload, RPC или произвольного memory-write поля.

SET body offsets (нумерация без report ID):

| Offset | Размер | Значение |
|---|---:|---|
|0|1|version=1|
|1|1|op:1 SCAN,2 READ,3 SELECT,4 ARM,5 STATUS,6 UNBOND|
|2|1|READ index0..15, для остальных строго0|
|3|1|SELECT/ARM address type:0 public,1 random; иначе0|
|4..9|6|SELECT/ARM BLE address **little endian**; иначе нули|
|10..31|22|reserved, все0|

SELECT/ARM отвергают нулевой/FF wildcard, любые type кроме0/1, ненаблюдавшийся target, просроченную scan-сессию, существующий bond и активное соединение/окно. ARM дополнительно требует ранее выбранный **тот же** address+type. Например, отображаемый `C6:05:04:03:02:01 / random` кодируется `type=01`, bytes `01 02 03 04 05 C6`. Нельзя отправлять UI-пустоту как «любой адрес».

GET body:

| Offset | Размер | Значение |
|---|---:|---|
|0|1|version1|
|1|1|echo op|
|2|1|status:0 OK,1 INVALID,2 BUSY,3 EXPIRED,4 NOT_SEEN,5 NOT_SELECTED,6 BONDED|
|3|1|flags:bit0 scan,1 selected,2 armed,3 claimed,4 bond-present,5 connection/pending|
|4|1|echo index|
|5|1|candidate count0..16|
|6|1|peer address type, если есть|
|7..12|6|peer address LE|
|13|1|RSSI signed int8|
|14..31|18|недостоверное sanitized ASCII имя|

READ/STATUS разрешены при connection/bond и ничего не переподключают. UNBOND возвращает BUSY при активном соединении/armed-window, иначе OK и закрывает RAM-состояние; сам `bt_unpair` выполняется firmware вне USB-контекста и очищает bond для BT_ID_DEFAULT. SCAN возвращает BUSY поверх соединения/armed-window; повторный scan без них очищает **только RAM-кандидатов/выбор**, никогда не bond/settings. Bond storage: один peer, overwrite-oldest=n. Discovery и передача input остаются только после L2 encryption и точной проверки карты; Just Works pairing не обещает MITM-защиту.

## Реально выполнено / ещё не проверено

- `python3 bridge/tools/test_native.py`: реальные native C executables с ASan+UBSan; transport regression + feature envelope/commands, пустые/wildcard/malformed команды, сосед раньше выбранной клавиатуры, exact address+type, one-attempt, deadlines, busy/bond gates, explicit unbond (connected→BUSY, bonded→OK+closes state, address bytes→INVALID) и vendor descriptor — PASS.
- `node bridge/tests/test_setup_ui.js`: реальные JS-тесты filters/encoding/response validation с **mocked** HID exchange — PASS; не browser/device test.
- `python3 bridge/tools/build_candidate.py …`: реальная NCS2.9.3 / SDK0.17.0 / Arm GCC12.2.0 сборка, linked descriptors/config/HEX/UF2 verification — exit0. Хэш/память: `../docs/BRIDGE_DESIGN.md` и `build/verification.json`.
- Старый UF2 сохранён в `archive/pre-usb-setup/`; `BUILD_FAILURES.md` и старый negative-GPIO build/log сохранены. Physical pairing path теперь вообще не реализован; включение Kconfig намеренно ломает build даже при alias. Никакой GPIO не выдуман.
- Не выполнялись flash/reset/bond/device/browser permission действия, USB enumeration/Feature transfer на железе, RF pairing, sleep/RPA reconnect, input/latency/queue pressure. Эти критерии остаются обязательной аппаратной приёмкой после разрешения. Локальный UF2 по-прежнему называется **NOT_APPROVED**, не release для пользователя.
