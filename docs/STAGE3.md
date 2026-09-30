# Stage 3 checkpoint — restoring the original application shell

This checkpoint starts the direct, screen-by-screen migration from the
Python/PySide6 functional reference. Stage 1 and Stage 2 remain underneath it;
they are not separate applications or abandoned previews.

## Restored from the Python main window

- the `Настройки`, `Deep Telemetry`, global pause and `Проблема сейчас`
  toolbar actions;
- global alarm and pause banners above all pages;
- the canonical tabs: `Обзор`, `Детали`, `Сети`, `Диспетчер задач`, `Железо`,
  `Автозагрузка`, `Диагностика и тесты`, `Проблемное приложение`;
- a separate settings window instead of treating settings as an application
  page;
- the five overview cards: CPU, GPU, RAM, storage and network;
- the original two-column dock layout with a full-width network card;
- movable and floating Qt dock cards with persisted layout;
- status stripes and 60-sample sparkline history;
- a timestamped `Проблема сейчас` marker in the native log.

CPU, RAM and network cards receive real native telemetry. GPU and storage are
explicitly shown as not yet ported; missing collectors are never represented as
zero or healthy data. The Networks and Hardware tabs already expose the native
data currently available. The remaining tabs have their final navigation
positions and honest migration-state panels, ready to be replaced one by one.

## Verification

- 8/8 CTest targets pass;
- the main-window test locks the eight tab names/order, five overview docks and
  four original toolbar commands;
- telemetry pause/resume still passes its lifecycle test;
- all 17 shared diagnostic golden cases still pass;
- live Windows verification reports 20 logical processors;
- the rendered GUI was inspected after receiving live telemetry.

## Next direct ports

1. storage/disk inventory and performance into the overview and Details;
2. GPU inventory/utilization/VRAM through a platform collector;
3. process list for Task Manager;
4. full hardware inventory and autostart read-only collectors;
5. diagnostics hub widgets on top of the already ported diagnostic contracts.
