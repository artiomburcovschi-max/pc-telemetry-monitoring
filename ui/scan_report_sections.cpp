#include "scan_report_sections.h"

#include <QJsonArray>
#include <QStringList>

namespace orion::app
{
namespace
{

[[nodiscard]] QString textValue(const QJsonValue& value, const int decimals = 0)
{
    if (value.isNull() || value.isUndefined())
        return QStringLiteral("н/д");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'f', decimals);
    if (value.isBool())
        return value.toBool() ? QStringLiteral("да") : QStringLiteral("нет");
    const auto text = value.toString().trimmed();
    return text.isEmpty() ? QStringLiteral("н/д") : text;
}

[[nodiscard]] QString numberWithUnit(const QJsonValue& value, const QString& unit,
                                     const int decimals = 1)
{
    return value.isDouble()
        ? QStringLiteral("%1%2").arg(value.toDouble(), 0, 'f', decimals).arg(unit)
        : QStringLiteral("н/д");
}

[[nodiscard]] QString enabledState(const QJsonValue& value)
{
    if (!value.isBool())
        return QStringLiteral("—");
    return value.toBool() ? QStringLiteral("включено") : QStringLiteral("отключено");
}

void appendHardware(QString& output, const QJsonObject& hardware)
{
    output += QStringLiteral("\n=== Характеристики ПК ===\n");
    if (hardware.isEmpty())
    {
        output += QStringLiteral("Сведения о компьютере: не собраны.\n");
        return;
    }

    const auto osValue = hardware.value(QStringLiteral("os"));
    if (osValue.isObject())
    {
        const auto os = osValue.toObject();
        output += QStringLiteral("ОС: %1; выпуск %2; архитектура %3.\n")
                      .arg(textValue(os.value(QStringLiteral("version"))),
                           textValue(os.value(QStringLiteral("release"))),
                           textValue(os.value(QStringLiteral("machine"))));
    }
    else
    {
        output += QStringLiteral("ОС: %1.\n").arg(textValue(osValue));
    }

    const auto board = hardware.value(QStringLiteral("motherboard")).toObject();
    output += QStringLiteral("Материнская плата: %1 %2.\n")
                  .arg(textValue(board.value(QStringLiteral("vendor"))),
                       textValue(board.value(QStringLiteral("model"))));
    const auto bios = hardware.value(QStringLiteral("bios")).toObject();
    if (!bios.isEmpty())
    {
        output += QStringLiteral("BIOS / UEFI: %1 %2; дата %3.\n")
                      .arg(textValue(bios.value(QStringLiteral("vendor"))),
                           textValue(bios.value(QStringLiteral("version"))),
                           textValue(bios.value(QStringLiteral("date"))));
    }

    const auto cpu = hardware.value(QStringLiteral("cpu")).toObject();
    const auto frequency = cpu.value(QStringLiteral("max_frequency_mhz")).isDouble()
        ? cpu.value(QStringLiteral("max_frequency_mhz"))
        : cpu.value(QStringLiteral("frequency_mhz"));
    output += QStringLiteral("CPU: %1; ядер %2, потоков %3; %4 МГц; архитектура %5.\n")
                  .arg(textValue(cpu.value(QStringLiteral("model"))),
                       textValue(cpu.value(QStringLiteral("physical_cores"))),
                       textValue(cpu.value(QStringLiteral("logical_threads"))), textValue(frequency),
                       textValue(cpu.value(QStringLiteral("architecture"))));
    if (cpu.contains(QStringLiteral("socket")) || cpu.contains(QStringLiteral("socket_count")))
    {
        output += QStringLiteral("Сокет CPU: %1; физических сокетов %2.\n")
                      .arg(textValue(cpu.value(QStringLiteral("socket"))),
                           textValue(cpu.value(QStringLiteral("socket_count"))));
    }

    const auto ram = hardware.value(QStringLiteral("ram")).toObject();
    output += QStringLiteral("RAM: %1; тип / частота: %2.\n")
                  .arg(numberWithUnit(ram.value(QStringLiteral("total_gb")), QStringLiteral(" ГБ")),
                       textValue(ram.value(QStringLiteral("type_and_speed"))));
    int moduleNumber = 0;
    for (const auto& value : ram.value(QStringLiteral("modules")).toArray())
    {
        const auto module = value.toObject();
        ++moduleNumber;
        output += QStringLiteral("  Модуль RAM #%1: %2 · %3 · %4 · %5 МГц · part %6 · serial %7.\n")
                      .arg(moduleNumber)
                      .arg(textValue(module.value(QStringLiteral("DeviceLocator"))),
                           numberWithUnit(module.value(QStringLiteral("capacity_gb")),
                                          QStringLiteral(" ГБ")),
                           textValue(module.value(QStringLiteral("type"))),
                           textValue(module.value(QStringLiteral("speed_mhz"))),
                           textValue(module.value(QStringLiteral("PartNumber"))),
                           textValue(module.value(QStringLiteral("SerialNumber"))));
    }
    if (moduleNumber == 0)
        output += QStringLiteral("  Модули RAM: детализация SPD/SMBIOS недоступна.\n");

    const auto gpus = hardware.value(QStringLiteral("gpu")).toArray();
    if (gpus.isEmpty())
    {
        output += QStringLiteral("GPU: не обнаружен.\n");
    }
    else
    {
        int index = 0;
        for (const auto& value : gpus)
        {
            const auto gpu = value.toObject();
            ++index;
            output += QStringLiteral("GPU #%1: %2; выделенная память %3; источник %4.\n")
                          .arg(index)
                          .arg(textValue(gpu.value(QStringLiteral("model"))),
                               numberWithUnit(gpu.value(QStringLiteral("memory_mb")),
                                              QStringLiteral(" МБ"), 0),
                               textValue(gpu.value(QStringLiteral("source"))));
        }
    }

    const auto npu = hardware.value(QStringLiteral("npu")).toObject();
    const auto npuDevices = npu.value(QStringLiteral("devices")).toArray();
    if (npuDevices.isEmpty())
    {
        output += QStringLiteral("NPU: %1; отдельный тест не запускался.\n")
                      .arg(npu.value(QStringLiteral("data_quality")) == QStringLiteral("collector_error")
                               ? QStringLiteral("обнаружение не завершено")
                               : QStringLiteral("выделенное устройство не обнаружено"));
    }
    else
    {
        for (const auto& value : npuDevices)
        {
            const auto device = value.toObject();
            output += QStringLiteral("NPU: %1 — %2; статус %3; отдельный тест не запускался.\n")
                          .arg(textValue(device.value(QStringLiteral("name"))),
                               textValue(device.value(QStringLiteral("vendor"))),
                               textValue(device.value(QStringLiteral("status"))));
        }
    }

    const auto diskReport = hardware.value(QStringLiteral("disks"));
    if (diskReport.isObject())
    {
        const auto disks = diskReport.toObject();
        const auto appendVolume = [&output](const QJsonObject& volume, const QString& prefix)
        {
            QStringList roles;
            if (volume.value(QStringLiteral("is_system")).toBool())
                roles.append(QStringLiteral("системный"));
            if (volume.value(QStringLiteral("spanned")).toBool())
                roles.append(QStringLiteral("составной"));
            output += QStringLiteral("    %1том %2: %3; всего %4, занято %5 (%6), свободно %7%8.\n")
                          .arg(prefix, textValue(volume.value(QStringLiteral("mountpoint"))),
                               textValue(volume.value(QStringLiteral("fstype"))),
                               numberWithUnit(volume.value(QStringLiteral("total_gb")),
                                              QStringLiteral(" ГБ")),
                               numberWithUnit(volume.value(QStringLiteral("used_gb")),
                                              QStringLiteral(" ГБ")),
                               numberWithUnit(volume.value(QStringLiteral("used_percent")),
                                              QStringLiteral("%")),
                               numberWithUnit(volume.value(QStringLiteral("free_gb")),
                                              QStringLiteral(" ГБ")),
                               roles.isEmpty() ? QString() : QStringLiteral("; %1").arg(roles.join(", ")));
        };
        const auto groups = disks.value(QStringLiteral("groups")).toArray();
        for (const auto& value : groups)
        {
            const auto group = value.toObject();
            const auto disk = group.value(QStringLiteral("physical")).toObject();
            const auto bytesValue = disk.value(QStringLiteral("Size"));
            const double bytes = bytesValue.isDouble() ? bytesValue.toDouble()
                                                       : bytesValue.toString().toDouble();
            const QString capacity = bytes > 0.0
                ? QStringLiteral("%1 ГБ").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1)
                : QStringLiteral("н/д");
            const auto number = textValue(disk.value(QStringLiteral("Index")));
            output += QStringLiteral("  Диск %1: %2 · %3 · %4 · %5; serial %6; прошивка %7.\n")
                          .arg(number, textValue(disk.value(QStringLiteral("Model"))), capacity,
                               textValue(disk.value(QStringLiteral("bus_type"))),
                               textValue(disk.value(QStringLiteral("storage_type"))),
                               textValue(disk.value(QStringLiteral("SerialNumber"))),
                               textValue(disk.value(QStringLiteral("FirmwareRevision"))));
            const auto volumes = group.value(QStringLiteral("volumes")).toArray();
            if (volumes.isEmpty())
                output += QStringLiteral("    Смонтированные тома не сопоставлены.\n");
            for (const auto& volume : volumes)
                appendVolume(volume.toObject(), QStringLiteral("Диск %1 — ").arg(number));
        }
        const auto unmapped = disks.value(QStringLiteral("unmapped_volumes")).toArray();
        for (const auto& volume : unmapped)
            appendVolume(volume.toObject(), QStringLiteral("несопоставленный "));
        if (groups.isEmpty() && unmapped.isEmpty())
            output += QStringLiteral("Накопители: не обнаружены или сведения недоступны.\n");
    }
    else
    {
        const auto disks = diskReport.toArray();
        if (disks.isEmpty())
            output += QStringLiteral("Накопители: не обнаружены или сведения недоступны.\n");
        for (const auto& value : disks)
        {
            const auto disk = value.toObject();
            output += QStringLiteral("  Том %1: %2; всего %3, свободно %4, занято %5.\n")
                          .arg(textValue(disk.value(QStringLiteral("mountpoint"))),
                               textValue(disk.value(QStringLiteral("type"))),
                               numberWithUnit(disk.value(QStringLiteral("total_gb")),
                                              QStringLiteral(" ГБ")),
                               numberWithUnit(disk.value(QStringLiteral("free_gb")),
                                              QStringLiteral(" ГБ")),
                               numberWithUnit(disk.value(QStringLiteral("used_percent")),
                                              QStringLiteral("%")));
        }
    }

    const auto displays = hardware.value(QStringLiteral("displays")).toArray();
    int displayNumber = 0;
    for (const auto& value : displays)
    {
        const auto display = value.toObject();
        ++displayNumber;
        output += QStringLiteral("Дисплей #%1: %2 · %3 @ %4 Гц · scale %5.\n")
                      .arg(displayNumber)
                      .arg(textValue(display.value(QStringLiteral("name"))),
                           textValue(display.value(QStringLiteral("resolution"))),
                           textValue(display.value(QStringLiteral("refresh_rate_hz")), 1),
                           textValue(display.value(QStringLiteral("scale")), 2));
    }

    const auto battery = hardware.value(QStringLiteral("battery"));
    if (battery.isObject())
    {
        const auto object = battery.toObject();
        output += QStringLiteral("Батарея: %1; питание %2.\n")
                      .arg(numberWithUnit(object.value(QStringLiteral("percent")), QStringLiteral("%")),
                           object.value(QStringLiteral("plugged_in")).isBool()
                               ? (object.value(QStringLiteral("plugged_in")).toBool()
                                      ? QStringLiteral("от сети")
                                      : QStringLiteral("от батареи"))
                               : QStringLiteral("н/д"));
    }
    else
    {
        output += QStringLiteral("Батарея: не обнаружена (стационарный ПК).\n");
    }

    const auto adapters = hardware.value(QStringLiteral("network_adapters")).toArray();
    for (const auto& value : adapters)
    {
        const auto adapter = value.toObject();
        output += QStringLiteral("Сеть: %1 — %2; IPv4 %3; приём %4, передача %5 Мбит/с.\n")
                      .arg(textValue(adapter.value(QStringLiteral("name"))),
                           adapter.value(QStringLiteral("is_up")).toBool() ? QStringLiteral("активна")
                                                                           : QStringLiteral("неактивна"),
                           textValue(adapter.value(QStringLiteral("ipv4"))),
                           textValue(adapter.value(QStringLiteral("receive_link_mbps")), 1),
                           textValue(adapter.value(QStringLiteral("transmit_link_mbps")), 1));
    }
}

void appendAutostart(QString& output, const QJsonObject& report)
{
    output += QStringLiteral("\n=== Автозагрузка ===\n");
    const auto collection = report.value(QStringLiteral("autostart_collection")).toObject();
    const auto entries = report.value(QStringLiteral("autostart_entries")).toArray();
    int systemCount = 0;
    QJsonArray userEntries;
    for (const auto& value : entries)
    {
        const auto entry = value.toObject();
        if (entry.value(QStringLiteral("category")) == QStringLiteral("system"))
            ++systemCount;
        else
            userEntries.append(entry);
    }
    output += QStringLiteral("Качество: %1; всего %2, системных %3, пользовательских %4.\n")
                  .arg(textValue(collection.value(QStringLiteral("data_quality"))))
                  .arg(entries.size())
                  .arg(systemCount)
                  .arg(userEntries.size());
    if (!collection.value(QStringLiteral("note")).toString().trimmed().isEmpty())
        output += QStringLiteral("Примечание: %1\n").arg(collection.value(QStringLiteral("note")).toString());
    if (systemCount > 0)
        output += QStringLiteral("Системные записи свёрнуты: %1.\n").arg(systemCount);
    if (userEntries.isEmpty())
        output += QStringLiteral("Пользовательские записи: не обнаружены.\n");
    for (const auto& value : userEntries)
    {
        const auto entry = value.toObject();
        output += QStringLiteral("  [%1] %2 (%3)\n")
                      .arg(textValue(entry.value(QStringLiteral("source"))),
                           textValue(entry.value(QStringLiteral("name"))),
                           enabledState(entry.value(QStringLiteral("enabled"))));
        if (!entry.value(QStringLiteral("command")).toString().trimmed().isEmpty())
            output += QStringLiteral("    %1\n").arg(entry.value(QStringLiteral("command")).toString());
    }
}

void appendSensors(QString& output, const QJsonObject& diagnostics)
{
    output += QStringLiteral("\n=== Датчики после нагрузки ===\n");
    const auto sensors = diagnostics.value(QStringLiteral("sensors")).toObject();
    if (sensors.isEmpty() || sensors.value("data_quality") == QStringLiteral("not_collected"))
    {
        output += QStringLiteral("Снимок после нагрузки не собран.\n");
        return;
    }
    output += QStringLiteral("Снимок: %1; качество %2.\n")
                  .arg(textValue(sensors.value(QStringLiteral("captured_at"))),
                       textValue(sensors.value(QStringLiteral("data_quality"))));
    for (const auto& pair : {std::pair{"temperature_collection", "Температуры"},
                             std::pair{"fan_collection", "Вентиляторы"}})
    {
        const auto collection = sensors.value(QLatin1String(pair.first)).toObject();
        if (!collection.isEmpty())
            output += QStringLiteral("%1: качество %2; время замера %3.\n")
                .arg(QString::fromUtf8(pair.second), textValue(collection.value("quality")),
                     textValue(collection.value("observed_at")));
    }
    for (const auto& note : sensors.value("notes").toArray())
        output += QStringLiteral("Примечание: %1\n").arg(note.toString());
    QStringList sources;
    for (const auto& value : sensors.value(QStringLiteral("sources")).toArray())
        sources.append(value.toString());
    output += QStringLiteral("Источники: %1.\n")
                  .arg(sources.isEmpty() ? QStringLiteral("не опубликованы") : sources.join(", "));

    const auto temperature = diagnostics.value(QStringLiteral("temperature")).toObject();
    for (const auto& component : {QStringLiteral("cpu"), QStringLiteral("gpu")})
    {
        const auto state = temperature.value(component).toObject();
        output += QStringLiteral("%1: %2; состояние %3; качество %4")
                      .arg(component.toUpper(),
                           numberWithUnit(state.value(QStringLiteral("current_value_c")),
                                          QStringLiteral("°C")),
                           textValue(state.value(QStringLiteral("level"))),
                           textValue(state.value(QStringLiteral("data_quality"))));
        if (!state.value(QStringLiteral("reason")).toString().trimmed().isEmpty())
            output += QStringLiteral(" (%1)").arg(state.value(QStringLiteral("reason")).toString());
        output += QStringLiteral(".\n");
    }

    const auto temperatures = sensors.value(QStringLiteral("temperatures")).toArray();
    if (temperatures.isEmpty())
        output += QStringLiteral("Температурные датчики: не опубликованы доступными источниками.\n");
    for (const auto& value : temperatures)
    {
        const auto sensor = value.toObject();
        output += QStringLiteral("  %1 / %2: %3 (%4)")
                      .arg(textValue(sensor.value(QStringLiteral("component"))).toUpper(),
                           textValue(sensor.value(QStringLiteral("label"))),
                           numberWithUnit(sensor.value(QStringLiteral("value_c")), QStringLiteral("°C")),
                           textValue(sensor.value(QStringLiteral("source"))));
        if (sensor.contains("quality"))
            output += QStringLiteral("; качество %1").arg(textValue(sensor.value("quality")));
        if (sensor.value(QStringLiteral("high_c")).isDouble())
            output += QStringLiteral("; высокий порог %1")
                          .arg(numberWithUnit(sensor.value(QStringLiteral("high_c")), QStringLiteral("°C")));
        if (sensor.value(QStringLiteral("critical_c")).isDouble())
            output += QStringLiteral("; критический порог %1")
                          .arg(numberWithUnit(sensor.value(QStringLiteral("critical_c")),
                                              QStringLiteral("°C")));
        output += QStringLiteral(".\n");
    }

    const auto fans = sensors.value(QStringLiteral("fans")).toArray();
    if (fans.isEmpty())
        output += QStringLiteral("Вентиляторы: обороты не опубликованы доступными источниками.\n");
    for (const auto& value : fans)
    {
        const auto fan = value.toObject();
        const QString amount = fan.value(QStringLiteral("rpm")).isDouble()
            ? numberWithUnit(fan.value(QStringLiteral("rpm")), QStringLiteral(" об/мин"), 0)
            : numberWithUnit(fan.value(QStringLiteral("percent")), QStringLiteral("%"));
        output += QStringLiteral("  %1 / %2: %3 (%4).\n")
                      .arg(textValue(fan.value(QStringLiteral("component"))).toUpper(),
                           textValue(fan.value(QStringLiteral("label"))), amount,
                           textValue(fan.value(QStringLiteral("source"))));
        if (fan.contains("quality"))
            output += QStringLiteral("    Качество: %1.\n").arg(textValue(fan.value("quality")));
    }
}

void appendRuntime(QString& output, const QJsonObject& runtime)
{
    output += QStringLiteral("\n=== Память до/после нагрузки ===\n");
    const auto before = runtime.value(QStringLiteral("before")).toObject();
    if (runtime.value("observation_kind") == QStringLiteral("before_only"))
        output += QStringLiteral("Собран только снимок до нагрузки; замер после нагрузки отсутствует.\n");
    else if (runtime.isEmpty())
        output += QStringLiteral("Снимки памяти не собраны.\n");
    else
        output += QStringLiteral("Режим: два моментальных снимка, не непрерывное наблюдение.\n");
    output += QStringLiteral("Качество: до %1 → после %2.\n")
                  .arg(textValue(before.value(QStringLiteral("data_quality"))),
                       textValue(runtime.value(QStringLiteral("data_quality"))));
    const auto appendPair = [&output, &before, &runtime](const QString& label, const QString& key,
                                                        const QString& unit, const int decimals = 1)
    {
        output += QStringLiteral("%1: %2 → %3.\n")
                      .arg(label, numberWithUnit(before.value(key), unit, decimals),
                           numberWithUnit(runtime.value(key), unit, decimals));
    };
    appendPair(QStringLiteral("RAM занято"), QStringLiteral("ram_used_percent"), QStringLiteral("%"));
    appendPair(QStringLiteral("RAM доступно"), QStringLiteral("ram_available_percent"),
               QStringLiteral("%"));
    appendPair(QStringLiteral("Commit"), QStringLiteral("commit_used_percent"), QStringLiteral("%"));
    appendPair(QStringLiteral("Файл подкачки"), QStringLiteral("pagefile_used_percent"),
               QStringLiteral("%"));
    appendPair(QStringLiteral("Страниц ввода в секунду"), QStringLiteral("pages_input_per_sec"),
               QString(), 1);
    appendPair(QStringLiteral("Чтений страниц в секунду"), QStringLiteral("page_reads_per_sec"),
               QString(), 1);
    output += QStringLiteral("Давление памяти: %1 → %2.\n")
                  .arg(textValue(before.value(QStringLiteral("memory_pressure"))),
                       textValue(runtime.value(QStringLiteral("memory_pressure"))));
    output += QStringLiteral("Подкачка: %1 → %2; hard faults: %3 → %4.\n")
                  .arg(textValue(before.value(QStringLiteral("paging_activity"))),
                       textValue(runtime.value(QStringLiteral("paging_activity"))),
                       textValue(before.value(QStringLiteral("hard_fault_activity"))),
                       textValue(runtime.value(QStringLiteral("hard_fault_activity"))));
    if (!runtime.value(QStringLiteral("paging_interpretation")).toString().trimmed().isEmpty())
        output += QStringLiteral("Вывод: %1\n")
                      .arg(runtime.value(QStringLiteral("paging_interpretation")).toString());
}

void appendTray(QString& output, const QJsonObject& tray)
{
    output += QStringLiteral("\n=== Трей и уведомления ===\n");
    if (tray.isEmpty())
    {
        output += QStringLiteral("Состояние трея: не передано в снимок проверки.\n");
        return;
    }
    output += QStringLiteral("Показывать в трее: %1.\n")
                  .arg(textValue(tray.value(QStringLiteral("icon_enabled"))));
    output += QStringLiteral("Уведомления о превышении порога: %1.\n")
                  .arg(textValue(tray.value(QStringLiteral("notifications_enabled"))));
    output += QStringLiteral("Системный трей доступен: %1; значок сейчас видим: %2; мониторинг на паузе: %3.\n")
                  .arg(textValue(tray.value(QStringLiteral("system_available"))),
                       textValue(tray.value(QStringLiteral("visible"))),
                       textValue(tray.value(QStringLiteral("monitoring_paused"))));
}

} // namespace

QString formatExtendedScanSections(const QJsonObject& report)
{
    QString output;
    appendHardware(output, report.value(QStringLiteral("hardware")).toObject());
    appendAutostart(output, report);
    appendSensors(output, report.value(QStringLiteral("diagnostics")).toObject());
    appendRuntime(output, report.value(QStringLiteral("runtime")).toObject());
    appendTray(output, report.value(QStringLiteral("tray")).toObject());
    return output;
}

} // namespace orion::app
