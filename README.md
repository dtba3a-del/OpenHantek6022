# OpenHantek6022 — XY Recorder Branch

**База:** OpenHantek6022 v3.4.0 (Qt5)  
**Ветка:** `xy-recorder-qt5`  
**Назначение:** цифровой самописец (pen-plotter) CH1→X, CH2→Y с каскадной децимацией и потоковой записью на диск.

## Чем отличается от официального релиза

| Фича | Официальный v3.4.0 | Этот бранч |
|---|---|---|
| XY-режим | Мгновенный снимок кадра (без накопления) | Непрерывная траектория, кадр за кадром |
| Децимация | Нет / простое прореживание | Каскадный FIR box-car (anti-aliasing) |
| Режимы записи | — | Finite (лист) и Tape (лента с потоковым сбросом на диск) |
| Экспорт | — | Чистый XY-CSV + полный заголовок настроек осциллографа |
| Envelope / Sigma | — | Peak envelope и скользящее σ |
| Управление | — | Master axis, slew rate, target points/density |

## Новые и изменённые файлы
openhantek/src/xyrecorder.h                 (новый)
openhantek/src/xyrecorder.cpp               (новый)
openhantek/src/dsowidget.h                  (изменён)
openhantek/src/dsowidget.cpp                (изменён)
openhantek/src/glscope.cpp                    (изменён)
openhantek/src/glscope.h                      (изменён — поля m_vaoXY, m_xyBuffer, xyPointCount)
openhantek/src/docks/HorizontalDock.h         (изменён)
openhantek/src/docks/HorizontalDock.cpp       (изменён)
openhantek/src/mainwindow.cpp                 (изменён)
openhantek/src/scopesettings.h              (изменён — поле xyContinuous)
openhantek/CMakeLists.txt                     (изменён — добавлен xyrecorder.cpp)
docs/XYRecorder_User_Guide.md                 (новый)
docs/XYRecorder_Technical_Documentation.md    (новый)

## Сборка

Как обычно для Qt5-ветки OpenHantek6022:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```
## Документация
docs/XYRecorder_User_Guide.md — инструкция пользователя (рус.)
docs/XYRecorder_Technical_Documentation.md — техническое описание каскада, режимов, интеграции (рус.)
## Статус
Qt5 / C++17
DCO sign-off не выполнен (это неофициальный экспериментальный форк)
Настройки рекордера не сохраняются между сессиями (только флаг xyContinuous)