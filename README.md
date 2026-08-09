# CtPU-multi-curveXY

**База:** [OpenHantek/OpenHantek6022 @ 3.4.0](https://github.com/OpenHantek/OpenHantek6022/tree/3.4.0) (официальный апстрим, Qt5)
**Ветка:** `CtPU-multi-curveXY`

Добавляет к официальному 3.4.0 три фичи и три класса фиксов, ничего не убирая из стандартной функциональности (T-Y, RLC, XYэкспорт).

## Что добавлено

- **XY continuous recorder** — непрерывный самописец (pen-plotter), каскадная децимация, потоковая запись.
- **CtPU** (Conversion to Physical Units) — линейное преобразование АЦП-отсчётов в физические величины (°C, kPa, A, W, ...) на канал, настраивается через `Oscilloscope → Settings → CtPU / Math → CtPU`.
- **Math Stack** — 4 виртуальных математических канала (M1–M4), базовые (+-/*) операции над произвольными парами каналов, свой CtPU-юнит на каждый. `... → CtPU / Math → Math`.
- **Multi-curve XY** — до 4 независимых XY-кривых, произвольная пара каналов (включая math-каналы) на кривую. `... → CtPU / Math → XY`.
CCtPU (Calibrated Conversion to Physical Units)установка значений преобразования по эталонным мерам линейное преобразование АЦП-отсчётов в физические величины (°C, kPa, A, W, ...) на канал, настраивается через `Oscilloscope → Settings → CtPU/CCtPU`.


## Прошивка

В самой ветке (`openhantek/res/firmware/isds205b-firmware.hex`) —  **fx2adc** (Steve Markgraf) прошивка ISDS205B, протестированная на реальном железе в различных сценариях. Заменяет стоковую прошивку апстрима с обнаруженной  ошибкой - не верные значения амплитуд для диапазонов 100мВ/50мВ/20мВ.

Отдельно, в **[pre-release `CCtPU_v.0.1.zip`](https://github.com/dtba3a-del/OpenHantek6022/releases/tag/CCtPU_v.0.1.zip)** — готовая сборка для FX2-осциллографов (ISDS205 и совместимые), использующая **fx2adc** (Steve Markgraf) вместо штатной прошивки — альтернативный путь для этого класса железа, показавший более стабильную работу именно на нём. Туда же входит `Zadig` для установки WinUSB/libusb-драйвера.

## Сборка

```bash
git clone --branch CtPU-multi-curveXY https://github.com/dtba3a-del/OpenHantek6022.git
cd OpenHantek6022
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
mingw32-make -j$(nproc)
```

Собрано и запущено в этом же процессе разработки (headless Linux/Qt5/Xvfb, `_GLIBCXX_ASSERTIONS` включён): 3/3 юнит-теста (`ctest`), демо-режим стабилен, все вкладки настроек CtPU/Math/XY проверены интерактивно (скриншоты + клики через `xdotool`).


## Статус

Pre-release. Возможны регрессии, нехарактерные для стабильных выпусков апстрима.
