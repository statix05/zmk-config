# Sofle: перенос OLED на LVGL 9

## Версии и границы

- Cormoran ZMK: `e5c9b6915b56801193e359dd9bad4a167ce0d1b8`.
- Его `app/west.yml`: `zmkfirmware/lvgl` **`f1db87ee98f1810328a8419572fa42a3b5f352ae`**, LVGL **9.3.0**.
- Только PandaKB Sofle v1 MX, SSD1306 128×32. Конфиги, workflows, W-Corne и оборудование не затрагивались.
- `src/sofle_status_render.c/.h` побайтово сохранены. `capture_state`, battery/event/toggle/lifecycle-логика не менялись.

## Изменения

В `src/sofle_status_screen.c`, `src/sofle_union_logo.c/.h` и генераторе:
`lv_image_dsc_t`, `LV_IMAGE_HEADER_MAGIC`, `LV_COLOR_FORMAT_I1`, явный stride
16 байт для кадра и 4 для логотипа; `lv_image_create/set_src` и
`lv_image_cache_drop`. Новая структура дескриптора не является старым заголовком
с переименованным типом. Размеры данных сохранены: кадр 520 байт, логотип 136 байт,
включая по 8 байт палитры. Палитра, MSB-first, инверсия и единственный поворот
32×128 → 128×32 по часовой стрелке неизменны.

`tools/sofle_native_lv_conf.h` использует параметры LVGL 9 и монохромный I1
software renderer. В host-тесте включён image cache. `tools/test_sofle_lvgl_frame.c`
создаёт настоящий `lv_display_t`, декодирует indexed image средствами LVGL,
принимает packed-I1 flush (пропуская 8 байт палитры display buffer) и проверяет
каждый физический пиксель. Один image/source/descriptor многократно обновляется
через RAM и cache drop, как в прошивке; дополнительных детей/графики нет.

## Проверка

**До правок** 11 исходных PNG сохранены с SHA256 в
`/Users/statix/.hermes/cache/scratch/sofle-stageb-golden/`; старый тест с LVGL 8
повторно прошёл 384 состояния. Там же `frames.bin`: 768 исходных кадров
(384 состояния × обычная/инвертированная палитра), 399360 байт.

Команды из корня worktree:

```sh
# До переноса, исходный тест:
python3 tools/test_sofle_status.py --lvgl /Users/statix/.hermes/cache/scratch/sofle-old-lvgl --out /Users/statix/.hermes/cache/scratch/sofle-stageb-golden/old-render

# После переноса:
python3 tools/generate_sofle_logo.py docs/Union.svg
python3 tools/test_sofle_status.py --lvgl /Users/statix/.hermes/cache/scratch/sofle-stageb-lvgl9 --out docs/oled-preview-modern --golden docs/oled-preview --golden-frames /Users/statix/.hermes/cache/scratch/sofle-stageb-golden/frames.bin --native
git diff --check
```

Результат: **384 состояния / 768 кадров побайтово совпадают с LVGL 8**;
guard bytes, непересечение областей, сохранность логотипа и USB battery masking — PASS.
Все **9 новых preview PNG побайтово равны исходным**. Все **11 исходных PNG**,
включая оба изображения логотипа после регенерации, неизменны.

```text
LVGL 9.3.0 native I1: states=384 frames=768 flushes=768 mismatches=0 children=1
```

- SHA256 последовательности 768 кадров: `019ec5fb3628e6715e12112e46d1f9437c74cdb5ef4fc067e44f3a5faaeabae7`.
- SHA256 136 байт логотипа: `71dccfdf65711880d0f09091dd1e4b890e29ae3e317f12893b0ca3f4cae98858`.
- Оба golden SHA встроены в тест; для повторного запуска внешний `--golden-frames` необязателен.
- Артефакты: `docs/oled-preview-modern/*.png`, `test-results.json`.

Это host-проверка настоящей библиотеки LVGL и C renderer, не Zephyr firmware build
и не подтверждение на физическом OLED. CI, flash, commit и push не выполнялись.
