#include "details_panel.h"
#include "cpu_core_chart_widget.h"
#include "orion/core/thresholds.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSet>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace orion::app
{
namespace
{

    bool available(double value) { return std::isfinite(value) && value >= 0.0; }

    std::optional<double> measurement(double value)
    {
        return available(value) ? std::optional<double>(value) : std::nullopt;
    }

    QString number(double value, const QString& unit, int precision = 1)
    {
        return available(value) ? QString::number(value, 'f', precision) + unit : QStringLiteral("н/д");
    }

    QString withPeak(double value, std::optional<double> peak, const QString& unit)
    {
        QString result = number(value, unit);
        if (peak && available(*peak))
        {
            result += QStringLiteral("  (макс. за сессию: %1)").arg(number(*peak, unit));
        }
        return result;
    }

    orion::core::StatusLevel componentStatus(double usage, double temperature, double critical)
    {
        return orion::core::worse(orion::core::levelForPercent(measurement(usage), critical),
            orion::core::levelForTemperature(measurement(temperature)));
    }

} // namespace

DetailCard::DetailCard(const QString& title, const QString& key, bool showStripe, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("MetricCard"));
    setProperty("detailSection", key);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    stripe_ = new QFrame(this);
    stripe_->setObjectName(QStringLiteral("DetailStatusStripe"));
    stripe_->setFixedWidth(4);
    outer->addWidget(stripe_);
    stripe_->setVisible(showStripe);
    content_ = new QVBoxLayout;
    content_->setContentsMargins(14, 12, 16, 12);
    content_->setSpacing(6);
    auto* heading = new QLabel(title, this);
    heading->setObjectName(QStringLiteral("CardTitle"));
    heading->setWordWrap(true);
    heading->setTextFormat(Qt::PlainText);
    content_->addWidget(heading);
    subtitle_ = new QLabel(this);
    subtitle_->setObjectName(QStringLiteral("CardDetail"));
    subtitle_->setWordWrap(true);
    subtitle_->setTextFormat(Qt::PlainText);
    subtitle_->hide();
    content_->addWidget(subtitle_);
    fields_ = new QVBoxLayout;
    fields_->setSpacing(3);
    content_->addLayout(fields_);
    outer->addLayout(content_, 1);
    refreshStripe();
}

void DetailCard::setSubtitle(const QString& text)
{
    subtitle_->setText(text);
    subtitle_->setVisible(!text.isEmpty());
}

QLabel* DetailCard::setField(const QString& key, const QString& caption, const QString& value)
{
    auto* result = values_.value(key, nullptr);
    if (!result)
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(10);
        auto* label = new QLabel(caption, this);
        label->setObjectName(QStringLiteral("FieldLabel"));
        label->setWordWrap(true);
        result = new QLabel(this);
        result->setObjectName(QStringLiteral("FieldValue"));
        result->setProperty("detailField", key);
        result->setWordWrap(true);
        result->setTextFormat(Qt::PlainText);
        result->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(label, 2);
        row->addWidget(result, 3);
        fields_->addLayout(row);
        values_.insert(key, result);
    }
    result->setText(value);
    result->setToolTip(value);
    return result;
}

QString DetailCard::fieldValue(const QString& key) const
{
    auto* value = values_.value(key, nullptr);
    return value ? value->text() : QString();
}

void DetailCard::addContent(QWidget* widget) { content_->addWidget(widget); }

void DetailCard::setStatus(orion::core::StatusLevel status)
{
    if (status_ == status)
        return;
    status_ = status;
    refreshStripe();
}

void DetailCard::setThemeColors(const QColor& accent, const QColor& muted)
{
    accent_ = accent;
    muted_ = muted;
    refreshStripe();
}

void DetailCard::refreshStripe()
{
    using orion::core::StatusLevel;
    const QColor color = status_ == StatusLevel::Critical ? QColor(QStringLiteral("#FF3B30"))
        : status_ == StatusLevel::Warning                 ? accent_
        : status_ == StatusLevel::Ok                      ? QColor(QStringLiteral("#2ECC71"))
                                                          : muted_;
    setProperty("statusLevel", QString::fromLatin1(orion::core::toString(status_).data()));
    stripe_->setStyleSheet(QStringLiteral("background: %1; border: none; "
                                          "border-top-left-radius: 12px; border-bottom-left-radius: 12px;")
            .arg(color.name()));
}

DetailsPanel::DetailsPanel(QWidget* parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("DetailsScroll"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(this);
    auto* columns = new QHBoxLayout(page);
    columns->setContentsMargins(10, 10, 10, 10);
    columns->setSpacing(12);
    auto* left = new QWidget(page);
    auto* right = new QWidget(page);
    left->setObjectName(QStringLiteral("DetailsLeftColumn"));
    right->setObjectName(QStringLiteral("DetailsRightColumn"));
    auto* leftLayout = new QVBoxLayout(left);
    auto* rightLayout = new QVBoxLayout(right);
    for (auto* layout : { leftLayout, rightLayout })
    {
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
    }
    columns->addWidget(left, 5);
    columns->addWidget(right, 4);
    cpu_ = new DetailCard(QStringLiteral("CPU"), QStringLiteral("cpu"), true, left);
    gpu_ = new DetailCard(QStringLiteral("GPU"), QStringLiteral("gpu"), true, left);
    os_ = new DetailCard(QStringLiteral("ОС"), QStringLiteral("os"), false, right);
    ram_ = new DetailCard(QStringLiteral("RAM"), QStringLiteral("ram"), true, right);
    auto* chartTitle = new QLabel(QStringLiteral("Загрузка логических ядер"), cpu_);
    chartTitle->setObjectName(QStringLiteral("FieldLabel"));
    chartTitle->setWordWrap(true);
    cpu_->addContent(chartTitle);
    coreChart_ = new CpuCoreChartWidget(cpu_);
    cpu_->addContent(coreChart_);
    leftLayout->addWidget(cpu_);
    leftLayout->addWidget(gpu_);
    rightLayout->addWidget(os_);
    rightLayout->addWidget(ram_);
    diskContainer_ = new QWidget(right);
    diskContainer_->setObjectName(QStringLiteral("DetailsDisks"));
    diskLayout_ = new QVBoxLayout(diskContainer_);
    diskLayout_->setContentsMargins(0, 0, 0, 0);
    diskLayout_->setSpacing(8);
    emptyDisks_
        = new DetailCard(QStringLiteral("ХРАНИЛИЩЕ"), QStringLiteral("storage_empty"), false, diskContainer_);
    emptyDisks_->setField(
        QStringLiteral("status"), QStringLiteral("Статус"), QStringLiteral("Диски не обнаружены"));
    diskLayout_->addWidget(emptyDisks_);
    rightLayout->addWidget(diskContainer_);
    rightLayout->addStretch(1);

    // Keep native sensor details available without displacing the original five blocks.
    sensors_
        = new DetailCard(QStringLiteral("ДАТЧИКИ И ВЕНТИЛЯТОРЫ"), QStringLiteral("sensors"), false, left);
    auto* expand = new QToolButton(sensors_);
    expand->setText(QStringLiteral("Показать показания"));
    expand->setCheckable(true);
    expand->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    expand->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    expand->setArrowType(Qt::RightArrow);
    sensors_->addContent(expand);
    sensorRows_ = new QWidget(sensors_);
    sensorRows_->setObjectName(QStringLiteral("DetailsSensorRows"));
    sensorLayout_ = new QVBoxLayout(sensorRows_);
    sensorLayout_->setContentsMargins(0, 0, 0, 0);
    sensorLayout_->setSpacing(6);
    sensors_->addContent(sensorRows_);
    sensorRows_->hide();
    connect(expand, &QToolButton::toggled, this,
        [this, expand](bool checked)
        {
            sensorRows_->setVisible(checked);
            expand->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        });
    leftLayout->addWidget(sensors_);
    leftLayout->addStretch(1);
    setWidget(page);
    updateSnapshot({}, {});
}

void DetailsPanel::updateSnapshot(const DetailsSnapshot& sample, const orion::core::SessionPeaks& peaks)
{
    using namespace orion::core;
    cpu_->setSubtitle(sample.cpuName);
    cpu_->setField(QStringLiteral("usage"), QStringLiteral("Общая загрузка"),
        withPeak(sample.cpuPercent, peaks.cpu, QStringLiteral("%")));
    auto* temperature = cpu_->setField(QStringLiteral("temp"), QStringLiteral("Температура"),
        withPeak(sample.cpuTemperatureC, peaks.cpuTemperatureC, QStringLiteral("°C")));
    if (!available(sample.cpuTemperatureC))
    {
        temperature->setToolTip(
            QStringLiteral("Температура CPU недоступна без аппаратного источника. "
                           "Предыдущий максимум, если показан, относится к этой сессии."));
    }
    cpu_->setField(QStringLiteral("freq"), QStringLiteral("Средняя частота"),
        number(sample.cpuFrequencyMhz, QStringLiteral(" МГц"), 0));
    cpu_->setField(QStringLiteral("logical_cores"), QStringLiteral("Логических ядер"),
        sample.logicalCpus > 0 ? QString::number(sample.logicalCpus) : QStringLiteral("н/д"));
    cpu_->setStatus(componentStatus(sample.cpuPercent, sample.cpuTemperatureC, kCpuCriticalPercent));
    os_->setField(QStringLiteral("name"), QStringLiteral("Версия"),
        sample.operatingSystem.isEmpty() ? QStringLiteral("н/д") : sample.operatingSystem);
    ram_->setField(QStringLiteral("usage"), QStringLiteral("Загрузка"),
        withPeak(sample.ramPercent, peaks.ram, QStringLiteral("%")));
    ram_->setField(QStringLiteral("total"), QStringLiteral("Занято / всего"),
        QStringLiteral("%1 / %2").arg(number(available(sample.ramPercent) && sample.ramTotalGiB > 0
                                              ? sample.ramTotalGiB * sample.ramPercent / 100.0
                                              : -1.0,
                                          {}),
            number(sample.ramTotalGiB, QStringLiteral(" ГБ"))));
    ram_->setStatus(levelForPercent(measurement(sample.ramPercent), kRamCriticalPercent));
    gpu_->setSubtitle(QStringLiteral("%1 · %2 VRAM")
            .arg(sample.gpuName, number(sample.gpuMemoryTotalGiB, QStringLiteral(" ГБ"))));
    gpu_->setField(QStringLiteral("usage"), QStringLiteral("Загрузка"),
        withPeak(sample.gpuPercent, peaks.gpu, QStringLiteral("%")));
    gpu_->setField(QStringLiteral("temp"), QStringLiteral("Температура"),
        withPeak(sample.gpuTemperatureC, peaks.gpuTemperatureC, QStringLiteral("°C")));
    gpu_->setField(QStringLiteral("vram"), QStringLiteral("VRAM использовано"),
        QStringLiteral("%1 (%2)").arg(number(sample.gpuMemoryUsedGiB, QStringLiteral(" ГБ")),
            number(sample.gpuMemoryPercent, QStringLiteral("%"))));
    gpu_->setField(QStringLiteral("vram_total"), QStringLiteral("VRAM всего"),
        number(sample.gpuMemoryTotalGiB, QStringLiteral(" ГБ")));
    gpu_->setStatus(componentStatus(sample.gpuPercent, sample.gpuTemperatureC, kGpuCriticalPercent));
    updateDisks(sample.disks);
    updateSensors(sample);
}

void DetailsPanel::updateDisks(const QVector<DiskTelemetry>& disks)
{
    emptyDisks_->setVisible(disks.isEmpty());
    QSet<QString> seen;
    int position = 1;
    for (const auto& disk : disks)
    {
        const QString key = disk.name + QChar(0x1f) + disk.mountPoint;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        auto* card = disks_.value(key, nullptr);
        if (!card)
        {
            card = new DetailCard(
                QStringLiteral("Диск %1").arg(disk.mountPoint.isEmpty() ? disk.name : disk.mountPoint),
                QStringLiteral("storage"), true, diskContainer_);
            card->setProperty("diskIdentity", key);
            card->setThemeColors(accent_, muted_);
            disks_.insert(key, card);
        }
        if (diskLayout_->indexOf(card) != position)
            diskLayout_->insertWidget(position, card);
        ++position;
        card->setSubtitle(disk.storageType);
        card->setField(QStringLiteral("space"), QStringLiteral("Занято / всего"),
            QStringLiteral("%1 / %2 (%3)")
                .arg(number(disk.usedGiB, {}), number(disk.totalGiB, QStringLiteral(" ГБ")),
                    number(disk.usedPercent, QStringLiteral("%"))));
        card->setField(
            QStringLiteral("free"), QStringLiteral("Свободно"), number(disk.freeGiB, QStringLiteral(" ГБ")));
        card->setField(QStringLiteral("read"), QStringLiteral("Чтение"),
            QStringLiteral("%1  (всего %2)")
                .arg(number(disk.readMiBPerSecond, QStringLiteral(" МБ/с")),
                    number(disk.totalReadGiB, QStringLiteral(" ГБ"))));
        card->setField(QStringLiteral("write"), QStringLiteral("Запись"),
            QStringLiteral("%1  (всего %2)")
                .arg(number(disk.writeMiBPerSecond, QStringLiteral(" МБ/с")),
                    number(disk.totalWrittenGiB, QStringLiteral(" ГБ"))));
        card->setField(QStringLiteral("filesystem"), QStringLiteral("Файловая система"),
            disk.fileSystem.isEmpty() ? QStringLiteral("н/д") : disk.fileSystem);
        using orion::core::StatusLevel;
        const double free
            = disk.totalGiB > 0 && available(disk.freeGiB) ? disk.freeGiB / disk.totalGiB * 100.0 : -1;
        card->setStatus(free < 0 ? StatusLevel::Unknown
                : free < 5       ? StatusLevel::Critical
                : free < 15      ? StatusLevel::Warning
                                 : StatusLevel::Ok);
    }
    for (const auto& key : disks_.keys())
    {
        if (!seen.contains(key))
            delete disks_.take(key);
    }
}

void DetailsPanel::updateSensors(const DetailsSnapshot& sample)
{
    QMap<QString, QString> readings;
    for (const auto& sensor : sample.temperatures)
    {
        const QString key = QStringLiteral("temp:") + sensor.component + QChar(0x1f) + sensor.label
            + QChar(0x1f) + sensor.source;
        QString value = number(sensor.valueC, QStringLiteral("°C"));
        if (available(sensor.criticalC))
            value += QStringLiteral(" · крит. %1").arg(number(sensor.criticalC, QStringLiteral("°C")));
        else if (available(sensor.highC))
            value += QStringLiteral(" · макс. %1").arg(number(sensor.highC, QStringLiteral("°C")));
        readings[key]
            = QStringLiteral("%1 · %2 · %3\n%4").arg(sensor.component, sensor.label, value, sensor.source);
    }
    for (const auto& fan : sample.fans)
    {
        const QString key
            = QStringLiteral("fan:") + fan.component + QChar(0x1f) + fan.label + QChar(0x1f) + fan.source;
        readings[key] = QStringLiteral("%1 · %2 · %3 · %4\n%5")
                            .arg(fan.component, fan.label, number(fan.rpm, QStringLiteral(" об/мин"), 0),
                                number(fan.percent, QStringLiteral("%")), fan.source);
    }
    if (readings.isEmpty())
        readings[QStringLiteral("empty")] = QStringLiteral("Датчики не обнаружены");
    for (auto it = readings.cbegin(); it != readings.cend(); ++it)
    {
        auto* label = sensorValues_.value(it.key(), nullptr);
        if (!label)
        {
            label = new QLabel(sensorRows_);
            label->setObjectName(QStringLiteral("CardDetail"));
            label->setWordWrap(true);
            label->setTextFormat(Qt::PlainText);
            sensorLayout_->addWidget(label);
            sensorValues_.insert(it.key(), label);
        }
        label->setText(it.value());
        label->setToolTip(it.value());
    }
    for (const auto& key : sensorValues_.keys())
    {
        if (!readings.contains(key))
            delete sensorValues_.take(key);
    }
}

void DetailsPanel::setThemeColors(const QColor& accent, const QColor& muted)
{
    accent_ = accent;
    muted_ = muted;
    for (auto* card : findChildren<DetailCard*>())
        card->setThemeColors(accent, muted);
}

} // namespace orion::app
