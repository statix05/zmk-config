# Реальные промежуточные ошибки и исправления

Ниже сохранены **выдержки из фактического tool output**, а не полный первоначальный build.log: build.log затем заменён успешной контрольной сборкой. Это не ошибка shell/gitstatus. Ничего не прошивалось.

## Попытка 1: BOARD_ROOT не виден sysbuild

Команда: `west build -b bridge_nano/nrf52840 -s .../bridge -d .../bridge/build -p always`.

```text
Loading Zephyr module(s) (Zephyr base): sysbuild_default
No board named 'bridge_nano' found.
```

Причина: NCS west по умолчанию запускает sysbuild; BOARD_ROOT, заданный только в CMake приложения, ещё не виден при выборе board для sysbuild. Исправление: в собственной build-команде явно `--no-sysbuild -- -DBOARD_ROOT=<absolute bridge path>`. Образ только приложения, без MCUboot/SoftDevice/bootloader image. BOARD_ROOT остаётся ограничен bridge.

## Попытка 2: неверно предполагаемое возвращаемое значение HOGP init

```text
src/main.c: In function 'main':
error: void value not ignored as it ought to be
    int err = bt_hogp_init(&hogp, &hogp_init_params);
```

Причина: в pinned NCS 2.9.3 `bt_hogp_init()` возвращает void. Исправлено на обычный вызов, как в исходном кандидате A.
Также проверка `.config` обнаружила, что custom board не выбирает BT controller автоматически; добавлены явные `CONFIG_BT_CTLR=y` и `CONFIG_BT_LL_SOFTDEVICE=y`. Без этого green compile не доказывал бы рабочий радиоконтроллер. Финальный verifier требует обе опции.
Предупреждения PRINTK устранены отключением NCS/Zephyr boot banners; peripheral-only GAP_AUTO_UPDATE_CONN_PARAMS оставлен n, при этом callback принимает запросы параметров клавиатуры и не форсирует PHY/interval.

## Попытка 3: ошибка собственного HEX verifier, не линкера

```text
Memory region         Used Size  Region Size  %age Used
           FLASH:      181264 B       744 KB     23.79%
             RAM:       46160 B       256 KB     17.61%
AssertionError: ('0x6000', '0x6010')
```

BIN vectors и `.config` уже подтверждали `0x26000`. GNU objcopy выдаёт Intel HEX **type 02 extended segment address** (`:020000022000DC`), а первый verifier учитывал только type 04. Исправлено декодирование обоих типов. Повторная чистая сборка и verifier/package завершились exit 0, HEX `[0x26000,0x52410)`, без UICR/bootloader/settings records. Финальный полный журнал: `build.log`; машинный результат: `build/verification.json`.

## Не блокирующие предупреждения финальной сборки

- dtc 1.8.1 сообщает дубли unit-address clock/power и ACL/NVMC в upstream nRF52840 SoC DTS; board-specific warning не скрывался.
- USB VID 0x2FE3 / PID 0x0100 — тестовые Zephyr IDs, НЕ выделенные product IDs. Финальная поставка потребует корректных IDs.
- Ошибки editor LSP о GCC-only flags (`-mfp16-format=ieee` и т.п.) относятся к локальному clang language server. Настоящий Arm GCC 12.2.0 собрал и слинковал финальный ELF.

## USB first-pair setup: проверка pinned descriptor на изменённом parent checkout

Первый повтор `derive_descriptor.py /Users/statix/.hermes/cache/scratch/sofle-modern-core --check` корректно остановился:

```text
AssertionError: 16afbfa5bde6f35611f1e83824617c06cc833a2d
```

Это не ошибка сборки bridge: общий core checkout параллельно обновлялся parent, а checker намеренно требует HEAD `e5c9b6915b56801193e359dd9bad4a167ce0d1b8`. Проверка не ослаблялась, parent checkout не менялся. Создан собственный shared sparse clone внутри `bridge/.cache/pinned-descriptor-core`, checkout исходного pinned commit; повтор `--check` прошёл: descriptor175 bytes, SHA256 `74d336d8452b95651d393840c3b66ccce7a07894ac73da62a75e46fcc365166b`.

Чистая NCS2.9.3 сборка USB setup прошла с первого запуска, затем повторена после reference-safe timeout обработки: exit0, FLASH183328/RAM46744. Финальные результаты — `build.log`, `build/verification.json`. Прежний UF2 и verification сохранены в `archive/pre-usb-setup/`, прежние `build-pairing-gate/` и `pairing-gate-test.log` не удалялись.

