#include "deep_telemetry_dialog.h"

#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/storage/logging.h"
#include "orion/storage/telemetry_files.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>
#include <limits>

namespace orion::app {
namespace {

[[nodiscard]] QJsonValue optionalNumber(const double value)
{
    return std::isfinite(value) && value >= 0.0
        ? QJsonValue {value} : QJsonValue {QJsonValue::Null};
}

[[nodiscard]] QJsonValue optionalPeak(const std::optional<double>& value)
{
    return value.has_value() ? optionalNumber(*value) : QJsonValue {QJsonValue::Null};
}

[[nodiscard]] QString peakText(const std::optional<double>& value, const QString& suffix)
{
    return value.has_value() && std::isfinite(*value) && *value >= 0
        ? QStringLiteral("%1%2").arg(*value, 0, 'f', 1).arg(suffix)
        : QStringLiteral("н/д");
}

} // namespace

BootLogWorker::BootLogWorker(QString path) : path_(std::move(path)) {}

void BootLogWorker::run()
{
    if (isInterruptionRequested()) return;
    const auto preview = orion::storage::readLogPreview(path_);
    if (isInterruptionRequested()) return;
    const auto text = !preview.error.isEmpty() ? preview.error
        : preview.text.isEmpty() ? QStringLiteral("[журнал приложения пуст]") : preview.text;
    emit resultReady(text, preview.description());
}

class PerCoreBarChart final : public QWidget {
public:
    explicit PerCoreBarChart(QString unit, const double fixedMaximum, QWidget* parent = nullptr)
        : QWidget(parent)
        , unit_(std::move(unit))
        , fixedMaximum_(fixedMaximum)
    {
        setMinimumHeight(145);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setValues(QVector<double> values, const bool critical = false, const bool aggregate = false)
    {
        values_ = std::move(values);
        critical_ = critical;
        aggregate_ = aggregate;
        const auto known = std::count_if(values_.cbegin(), values_.cend(), [](double value) {
            return std::isfinite(value) && value >= 0;
        });
        setProperty("knownValueCount", static_cast<int>(known));
        setProperty("slotCount", static_cast<int>(values_.size()));
        setProperty("aggregate", aggregate_);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(QStringLiteral("#07100B")));

        constexpr int left = 72;
        constexpr int top = 12;
        constexpr int right = 14;
        constexpr int bottom = 28;
        const QRectF plot(left, top, std::max(1, width() - left - right),
            std::max(1, height() - top - bottom));

        painter.setPen(QPen(QColor(QStringLiteral("#23482F")), 1));
        for (int line = 0; line <= 4; ++line) {
            const double y = plot.bottom() - plot.height() * line / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        painter.drawRect(plot);

        if (property("knownValueCount").toInt() == 0) {
            painter.setPen(QColor(QStringLiteral("#9BAAA0")));
            painter.drawText(plot, Qt::AlignCenter, QStringLiteral("данные недоступны"));
            return;
        }

        double maximum = fixedMaximum_;
        if (maximum <= 0.0) {
            maximum = *std::max_element(values_.cbegin(), values_.cend());
            maximum = std::max(100.0, std::ceil(maximum / 250.0) * 250.0);
        }
        painter.setPen(QColor(QStringLiteral("#9BAAA0")));
        painter.drawText(QRectF(0, plot.top() - 2, left - 7, 20),
            Qt::AlignRight | Qt::AlignTop,
            QStringLiteral("%1 %2").arg(maximum, 0, 'f', 0).arg(unit_));
        painter.drawText(QRectF(0, plot.bottom() - 16, left - 7, 20),
            Qt::AlignRight | Qt::AlignBottom, QStringLiteral("0"));

        const double cellWidth = plot.width() / static_cast<double>(values_.size());
        const double barWidth = std::clamp(cellWidth * 0.62, 2.0, 30.0);
        const QColor fill = critical_
            ? QColor(QStringLiteral("#FF3B30")) : QColor(QStringLiteral("#21D07A"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        const int labelStep = std::max(1, static_cast<int>(std::ceil(values_.size() / 16.0)));
        for (qsizetype index = 0; index < values_.size(); ++index) {
            const bool known = std::isfinite(values_[index]) && values_[index] >= 0;
            const double safe = known ? std::clamp(values_[index], 0.0, maximum) : 0;
            const double height = plot.height() * safe / maximum;
            const double x = plot.left() + cellWidth * (static_cast<double>(index) + 0.5);
            if (known) painter.drawRoundedRect(
                QRectF(x - barWidth / 2.0, plot.bottom() - height, barWidth, height), 2.0, 2.0);
            else {
                painter.setPen(QColor(QStringLiteral("#9BAAA0")));
                painter.drawText(QRectF(x - cellWidth / 2.0, plot.bottom() - 22, cellWidth, 20),
                    Qt::AlignCenter, cellWidth >= 28 ? QStringLiteral("н/д") : QStringLiteral("—"));
                painter.setPen(Qt::NoPen);
            }
            if (index % labelStep == 0) {
                painter.setPen(QColor(QStringLiteral("#9BAAA0")));
                painter.drawText(QRectF(x - cellWidth / 2.0, plot.bottom() + 4,
                    cellWidth, bottom - 4), Qt::AlignHCenter | Qt::AlignTop,
                    aggregate_ ? QStringLiteral("общая") : QString::number(index + 1));
                painter.setPen(Qt::NoPen);
            }
        }
    }

private:
    QString unit_;
    double fixedMaximum_ {0.0};
    QVector<double> values_;
    bool critical_ {false};
    bool aggregate_ {false};
};

SystemErrorsWorker::SystemErrorsWorker(QObject* parent, Collector collector)
    : QThread(parent), collector_(std::move(collector))
{
}

void SystemErrorsWorker::run()
{
    if (isInterruptionRequested()) return;
    auto result = collector_ ? collector_() : orion::diagnostics::collectSystemErrorReport(40);
    result.insert("captured_at", QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    if (!isInterruptionRequested()) emit resultReady(result);
}

DeepTelemetryDialog::DeepTelemetryDialog(
    QWidget* parent,
    const bool refreshErrorsOnOpen,
    SystemErrorsWorker::Collector errorsCollector)
    : QDialog(parent, Qt::Window), errorsCollector_(std::move(errorsCollector))
{
    setObjectName(QStringLiteral("DeepTelemetryDialog"));
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(QStringLiteral("O.R.I.O.N. — Deep Telemetry"));
    resize(960, 740);
    setMinimumSize(780, 620);

    auto* outer = new QVBoxLayout(this);
    auto* topRow = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("Deep Telemetry"), this);
    title->setObjectName(QStringLiteral("Header"));
    topRow->addWidget(title);
    topRow->addStretch(1);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("DeepTelemetryStatus"));
    statusLabel_->setWordWrap(true);
    topRow->addWidget(statusLabel_, 1);
    outer->addLayout(topRow);
    auto* actionsRow = new QHBoxLayout;
    pauseButton_ = new QPushButton(QStringLiteral("Пауза"), this);
    pauseButton_->setObjectName(QStringLiteral("DeepTelemetryPauseButton"));
    connect(pauseButton_, &QPushButton::clicked, this, &DeepTelemetryDialog::toggleLocalPause);
    actionsRow->addWidget(pauseButton_);
    auto* exportButton = new QPushButton(QStringLiteral("📤 Экспорт логов"), this);
    exportButton->setObjectName(QStringLiteral("DeepTelemetryExportButton"));
    connect(exportButton, &QPushButton::clicked, this, &DeepTelemetryDialog::exportLogs);
    actionsRow->addWidget(exportButton);
    auto* bootLogButton = new QPushButton(
        QStringLiteral("🗎 Показать системный журнал логов"), this);
    bootLogButton->setObjectName(QStringLiteral("DeepTelemetryBootLogButton"));
    bootLogButton->setToolTip(QStringLiteral(
        "Подробный технический журнал запуска и фоновых операций O.R.I.O.N."));
    connect(bootLogButton, &QPushButton::clicked, this, &DeepTelemetryDialog::showBootLog);
    actionsRow->addWidget(bootLogButton);
    actionsRow->addStretch();
    outer->addLayout(actionsRow);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    outer->addWidget(splitter, 1);

    auto* corePanel = new QWidget(splitter);
    auto* coreLayout = new QVBoxLayout(corePanel);
    auto* coreHeader = new QHBoxLayout;
    auto* coreTitle = new QLabel(QStringLiteral("Per-core frequency & load"), corePanel);
    coreTitle->setObjectName(QStringLiteral("Header"));
    coreHeader->addWidget(coreTitle);
    coreHeader->addStretch(1);
    uptimeLabel_ = new QLabel(QStringLiteral("Время сессии: 00:00:00"), corePanel);
    uptimeLabel_->setObjectName(QStringLiteral("DeepTelemetryUptime"));
    coreHeader->addWidget(uptimeLabel_);
    coreLayout->addLayout(coreHeader);

    peaksLabel_ = new QLabel(
        QStringLiteral("Пики сессии: CPU н/д · RAM н/д · GPU н/д"), corePanel);
    peaksLabel_->setObjectName(QStringLiteral("DeepTelemetryPeaks"));
    peaksLabel_->setWordWrap(true);
    coreLayout->addWidget(peaksLabel_);
    auto* frequencyTitle = new QLabel(QStringLiteral("Частота логических CPU — МГц"), corePanel);
    coreLayout->addWidget(frequencyTitle);
    frequencyChart_ = new PerCoreBarChart(QStringLiteral("МГц"), 0.0, corePanel);
    frequencyChart_->setObjectName(QStringLiteral("DeepTelemetryFrequencyChart"));
    coreLayout->addWidget(frequencyChart_, 1);
    frequencyQualityLabel_ = new QLabel(
        QStringLiteral("Частота: ожидается первый снимок"), corePanel);
    frequencyQualityLabel_->setObjectName(QStringLiteral("DeepTelemetryFrequencyQuality"));
    frequencyQualityLabel_->setWordWrap(true);
    coreLayout->addWidget(frequencyQualityLabel_);
    auto* loadTitle = new QLabel(QStringLiteral("Загрузка логических CPU — %"), corePanel);
    coreLayout->addWidget(loadTitle);
    loadChart_ = new PerCoreBarChart(QStringLiteral("%"), 100.0, corePanel);
    loadChart_->setObjectName(QStringLiteral("DeepTelemetryLoadChart"));
    coreLayout->addWidget(loadChart_, 1);

    auto* errorsPanel = new QWidget(splitter);
    auto* errorsLayout = new QVBoxLayout(errorsPanel);
    auto* errorsHeader = new QHBoxLayout;
    auto* errorsTitle = new QLabel(
        QStringLiteral("System errors (с момента загрузки ОС)"), errorsPanel);
    errorsTitle->setObjectName(QStringLiteral("Header"));
    errorsTitle->setWordWrap(true);
    errorsHeader->addWidget(errorsTitle, 1);
    refreshButton_ = new QPushButton(QStringLiteral("Обновить"), errorsPanel);
    refreshButton_->setObjectName(QStringLiteral("DeepTelemetryRefreshButton"));
    connect(refreshButton_, &QPushButton::clicked, this, &DeepTelemetryDialog::refreshSystemErrors);
    errorsHeader->addWidget(refreshButton_);
    auto* clearButton = new QPushButton(
        QStringLiteral("🗑 Очистить текущие логи сессии"), errorsPanel);
    clearButton->setObjectName(QStringLiteral("DeepTelemetryClearButton"));
    clearButton->setToolTip(QStringLiteral(
        "Очищает только текущий вид и экспортный кэш, но не журнал Windows"));
    connect(clearButton, &QPushButton::clicked, this, &DeepTelemetryDialog::clearSystemErrors);
    errorsHeader->addWidget(clearButton);
    errorsLayout->addLayout(errorsHeader);
    errorsQualityLabel_ = new QLabel(errorsPanel);
    errorsQualityLabel_->setObjectName(QStringLiteral("DeepTelemetryErrorsQuality"));
    errorsQualityLabel_->setWordWrap(true);
    errorsLayout->addWidget(errorsQualityLabel_);
    errorsOutput_ = new QTextEdit(errorsPanel);
    errorsOutput_->setObjectName(QStringLiteral("DeepTelemetryErrors"));
    errorsOutput_->setReadOnly(true);
    errorsOutput_->setLineWrapMode(QTextEdit::WidgetWidth);
    errorsOutput_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    errorsLayout->addWidget(errorsOutput_);

    splitter->addWidget(corePanel);
    splitter->addWidget(errorsPanel);
    splitter->setSizes({390, 290});

    setStyleSheet(QStringLiteral(R"(
        QDialog { background: #101715; color: #E8F2EC; }
        QLabel { color: #E8F2EC; }
        QLabel#Header { color: #37E287; font-size: 17px; font-weight: 700; }
        QPushButton { background: #1B2A24; border: 1px solid #315C47; border-radius: 5px;
                      padding: 7px 10px; color: #E8F2EC; }
        QPushButton:hover { border-color: #37E287; }
        QPushButton:disabled { color: #6E7C74; border-color: #29372F; }
        QTextEdit#DeepTelemetryErrors, QPlainTextEdit#BootLogPreviewText { background: #000000; color: #35E87B;
                      font-family: "Courier New"; font-size: 12px; border: 1px solid #175A32; }
        QSplitter::handle { background: #21352B; height: 4px; }
    )"));

    lastErrorsReport_ = {{"errors", QJsonArray{}}, {"data_quality", "unknown"},
        {"note", QStringLiteral("Журнал ещё не загружен")}};
    updateErrorsPresentation();
    if (refreshErrorsOnOpen) {
        refreshSystemErrors();
    } else {
        errorsOutput_->setPlainText(QStringLiteral("[автоматическое чтение журнала отключено для теста]"));
    }
}

DeepTelemetryDialog::~DeepTelemetryDialog()
{
    if (errorsWorker_ != nullptr) {
        // Worker owns only its collector. Never block the GUI or destroy a live
        // QThread when the dialog closes; finished already schedules deletion.
        disconnect(errorsWorker_, nullptr, this, nullptr);
        errorsWorker_->requestInterruption();
    }
}

void DeepTelemetryDialog::applyTelemetry(
    const QVector<double>& coreLoads,
    const QVector<double>& coreFrequenciesMhz,
    const double averageFrequencyMhz,
    const double uptimeSeconds,
    const orion::core::SessionPeaks& peaks)
{
    if (localPaused_ || globalPaused_) {
        return;
    }

    const bool uptimeKnown = std::isfinite(uptimeSeconds) && uptimeSeconds >= 0
        && uptimeSeconds < static_cast<double>(std::numeric_limits<qint64>::max());
    const qint64 totalSeconds = uptimeKnown ? static_cast<qint64>(uptimeSeconds) : 0;
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds % 3600) / 60;
    const auto seconds = totalSeconds % 60;
    uptimeLabel_->setText(uptimeKnown ? QStringLiteral("Время сессии: %1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0')) : QStringLiteral("Время сессии: н/д"));
    peaksLabel_->setText(QStringLiteral(
        "Пики сессии: CPU %1 · RAM %2 · GPU %3 · CPU temp %4 · GPU temp %5")
        .arg(peakText(peaks.cpu, QStringLiteral("%")))
        .arg(peakText(peaks.ram, QStringLiteral("%")))
        .arg(peakText(peaks.gpu, QStringLiteral("%")))
        .arg(peakText(peaks.cpuTemperatureC, QStringLiteral("°C")))
        .arg(peakText(peaks.gpuTemperatureC, QStringLiteral("°C"))));

    const qsizetype count = std::max(coreLoads.size(), coreFrequenciesMhz.size());
    QVector<double> loads(count, -1), frequencies(count, -1);
    qsizetype knownFrequencies = 0;
    for (qsizetype index = 0; index < count; ++index) {
        if (index < coreLoads.size() && std::isfinite(coreLoads[index])
            && coreLoads[index] >= 0 && coreLoads[index] <= 100) loads[index] = coreLoads[index];
        if (index < coreFrequenciesMhz.size() && std::isfinite(coreFrequenciesMhz[index])
            && coreFrequenciesMhz[index] >= 0) { frequencies[index] = coreFrequenciesMhz[index]; ++knownFrequencies; }
    }
    const bool critical = std::any_of(loads.cbegin(), loads.cend(), [](double value) { return value >= 95; });
    loadChart_->setValues(loads, critical);
    QVector<double> displayedFrequencies = frequencies;
    const bool aggregate = knownFrequencies == 0 && optionalNumber(averageFrequencyMhz).isDouble();
    QString frequencyQuality = QStringLiteral("Частота логических CPU: доступно %1 из %2; источник ОС. Н/д не означает 0.")
        .arg(knownFrequencies).arg(count);
    if (aggregate) {
        displayedFrequencies.clear();
        displayedFrequencies.append(averageFrequencyMhz);
        frequencyQuality = QStringLiteral(
            "Доступна только общая оценка частоты — показан один фактический ряд, без размножения по ядрам");
    } else if (knownFrequencies == 0) {
        frequencyQuality = QStringLiteral("Частота недоступна у текущего аппаратного источника");
    }
    frequencyChart_->setValues(displayedFrequencies, false, aggregate);
    frequencyQualityLabel_->setText(frequencyQuality);

    QJsonArray rows;
    for (qsizetype index = 0; index < count; ++index) {
        rows.append(QJsonObject {
            {QStringLiteral("core"), static_cast<int>(index)},
            {QStringLiteral("freq_mhz"), optionalNumber(frequencies[index])},
            {QStringLiteral("percent"), optionalNumber(loads[index])},
        });
    }
    lastSnapshot_ = QJsonObject {
        {QStringLiteral("percpu"), rows},
        {QStringLiteral("average_frequency_mhz"), optionalNumber(averageFrequencyMhz)},
        {QStringLiteral("frequency_sampling"), aggregate ? QStringLiteral("aggregate_estimate")
            : knownFrequencies > 0 ? QStringLiteral("per_logical_cpu") : QStringLiteral("unavailable")},
        {QStringLiteral("observed_at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("session_uptime_seconds"), uptimeKnown ? QJsonValue(uptimeSeconds) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("session_peaks"), QJsonObject {
             {QStringLiteral("cpu_percent_max"), optionalPeak(peaks.cpu)},
             {QStringLiteral("ram_percent_max"), optionalPeak(peaks.ram)},
             {QStringLiteral("gpu_percent_max"), optionalPeak(peaks.gpu)},
             {QStringLiteral("cpu_temp_c_max"), optionalPeak(peaks.cpuTemperatureC)},
             {QStringLiteral("gpu_temp_c_max"), optionalPeak(peaks.gpuTemperatureC)},
        }},
    };

    if (critical) {
        statusLabel_->setText(QStringLiteral("⚠ CPU: критическая нагрузка — график обновляется"));
        statusLabel_->setStyleSheet(QStringLiteral("color:#FF3B30;font-weight:700"));
    } else {
        statusLabel_->clear();
        statusLabel_->setStyleSheet({});
    }
}

void DeepTelemetryDialog::setMonitoringPaused(const bool paused)
{
    globalPaused_ = paused;
    pauseButton_->setEnabled(!globalPaused_);
    refreshButton_->setEnabled(!globalPaused_
        && errorsWorker_ == nullptr);
    updatePausePresentation();
    if (!globalPaused_ && pendingErrorsReport_) {
        const auto ready = std::move(*pendingErrorsReport_);
        pendingErrorsReport_.reset();
        applySystemErrors(ready);
    }
}

QJsonObject DeepTelemetryDialog::exportPayload() const
{
    QJsonArray errors;
    for (const auto& value : lastErrorsReport_.value(QStringLiteral("errors")).toArray()) {
        errors.append(value);
    }
    return {
        {QStringLiteral("exported_at"), QDateTime::currentDateTime().toString(Qt::ISODate)},
        {QStringLiteral("session_uptime_seconds"),
             lastSnapshot_.value(QStringLiteral("session_uptime_seconds"))},
        {QStringLiteral("current_telemetry"), lastSnapshot_},
        {QStringLiteral("session_peaks"), lastSnapshot_.value(QStringLiteral("session_peaks"))},
        {QStringLiteral("system_errors"), errors},
        {QStringLiteral("system_errors_data_quality"),
             lastErrorsReport_.value(QStringLiteral("data_quality"))},
        {QStringLiteral("system_errors_source"),
             lastErrorsReport_.value(QStringLiteral("source"))},
        {QStringLiteral("system_errors_report"), lastErrorsReport_},
        {QStringLiteral("system_errors_capture_state"), pendingErrorsReport_ ? "paused_result_pending"
             : errorsWorker_ ? "loading" : "idle"},
        {QStringLiteral("telemetry_capture_state"), globalPaused_ ? "global_pause" : localPaused_ ? "local_pause" : "live"},
    };
}

void DeepTelemetryDialog::toggleLocalPause()
{
    localPaused_ = !localPaused_;
    updatePausePresentation();
}

void DeepTelemetryDialog::updatePausePresentation()
{
    pauseButton_->setText(localPaused_ ? QStringLiteral("Продолжить") : QStringLiteral("Пауза"));
    if (globalPaused_) {
        statusLabel_->setText(QStringLiteral("Мониторинг приостановлен глобально"));
        statusLabel_->setStyleSheet({});
    } else if (localPaused_) {
        statusLabel_->setText(QStringLiteral("Локальная пауза"));
        statusLabel_->setStyleSheet({});
    } else {
        statusLabel_->clear();
        statusLabel_->setStyleSheet({});
    }
}

void DeepTelemetryDialog::refreshSystemErrors()
{
    if (globalPaused_) {
        return;
    }
    if (errorsWorker_ != nullptr) {
        return;
    }
    refreshButton_->setEnabled(false);
    errorsOutput_->setPlainText(QStringLiteral("⏳ Загрузка системного журнала ошибок..."));
    pendingErrorsReport_.reset();
    const auto generation = ++errorsGeneration_;
    errorsQualityLabel_->setText(QStringLiteral("Читаю журнал; экспорт пока содержит предыдущий снимок."));
    errorsWorker_ = new SystemErrorsWorker(nullptr, errorsCollector_);
    connect(errorsWorker_, &SystemErrorsWorker::resultReady,
        this, [this, generation](const QJsonObject& result) {
            if (generation == errorsGeneration_) applySystemErrors(result);
        });
    connect(errorsWorker_, &QThread::finished, this, [this, completed = errorsWorker_] {
        if (errorsWorker_ != completed) return;
        errorsWorker_ = nullptr;
        refreshButton_->setEnabled(!globalPaused_);
    });
    connect(errorsWorker_, &QThread::finished, errorsWorker_, &QObject::deleteLater);
    errorsWorker_->start();
}

void DeepTelemetryDialog::applySystemErrors(const QJsonObject& result)
{
    if (globalPaused_) {
        pendingErrorsReport_ = result;
        errorsQualityLabel_->setText(QStringLiteral("Журнал готов; снимок будет показан после продолжения мониторинга."));
        return;
    }
    lastErrorsReport_ = result;
    updateErrorsPresentation();
}

void DeepTelemetryDialog::updateErrorsPresentation()
{
    const auto& result = lastErrorsReport_;
    const auto quality = result.value("data_quality").toString("unknown");
    const auto qualityLabel = quality == "valid" ? QStringLiteral("доступны")
        : quality == "estimated" ? QStringLiteral("частичные")
        : quality == "permission_denied" ? QStringLiteral("ограничены правами")
        : quality == "cleared" ? QStringLiteral("очищен только вид") : QStringLiteral("недоступны");
    QString qualityText = QStringLiteral("Данные: %1 · записей: %2 · источник: %3 · получены: %4")
        .arg(qualityLabel).arg(result.value("errors").toArray().size())
        .arg(result.value("source").toString(QStringLiteral("н/д")), result.value("captured_at").toString(QStringLiteral("н/д")));
    if (result.value("limit").toInt() > 0) qualityText += QStringLiteral(" · лимит: %1").arg(result.value("limit").toInt());
    errorsQualityLabel_->setText(qualityText);
    QStringList lines;
    for (const auto& value : result.value(QStringLiteral("errors")).toArray()) {
        QString line = value.toString();
        if (value.isObject()) {
            const auto entry = value.toObject();
            line = entry.value("line").toString();
            if (line.isEmpty()) line = QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact));
        }
        lines.append(line.isEmpty() ? QStringLiteral("[запись не удалось отобразить]") : line);
    }
    const auto note = result.value("note").toString();
    if (!note.isEmpty()) lines.prepend(QStringLiteral("[%1]").arg(note));
    if (lines.isEmpty()) lines.append(quality == "valid" ? QStringLiteral("[ошибок не найдено]")
        : QStringLiteral("[нет доступных записей; отсутствие данных не подтверждает отсутствие ошибок]"));
    errorsOutput_->setPlainText(lines.join(QLatin1Char('\n')));
}

void DeepTelemetryDialog::clearSystemErrors()
{
    ++errorsGeneration_;
    pendingErrorsReport_.reset();
    if (errorsWorker_) errorsWorker_->requestInterruption();
    lastErrorsReport_ = QJsonObject {
        {QStringLiteral("errors"), QJsonArray {}},
        {QStringLiteral("data_quality"), QStringLiteral("cleared")},
        {QStringLiteral("source"), QStringLiteral("session_view")},
        {QStringLiteral("note"), QStringLiteral("логи сессии очищены — нажмите «Обновить» для повторного чтения журнала")},
    };
    updateErrorsPresentation();
}

void DeepTelemetryDialog::showBootLog()
{
    const QString path = orion::storage::activeLogFile();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("DeepTelemetryBootLogDialog"));
    dialog.setWindowTitle(QStringLiteral("Журнал приложения — последние записи"));
    dialog.resize(680, 500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* source = new QLabel(path.isEmpty() ? QStringLiteral("Путь журнала ещё не задан") : path, &dialog);
    source->setTextFormat(Qt::PlainText);
    source->setWordWrap(true);
    source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(source);
    auto* details = new QLabel(QStringLiteral("Загрузка ограниченного снимка журнала…"), &dialog);
    details->setObjectName(QStringLiteral("BootLogPreviewDetails"));
    details->setWordWrap(true);
    layout->addWidget(details);
    auto* output = new QPlainTextEdit(&dialog);
    output->setObjectName(QStringLiteral("BootLogPreviewText"));
    output->setReadOnly(true);
    output->setLineWrapMode(QPlainTextEdit::NoWrap);
    output->setPlainText(QStringLiteral("Чтение журнала…"));
    layout->addWidget(output, 1);
    auto* closeButton = new QPushButton(QStringLiteral("Закрыть"), &dialog);
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeButton, 0, Qt::AlignRight);
    QPointer<BootLogWorker> worker = new BootLogWorker(path);
    connect(worker, &BootLogWorker::resultReady, &dialog,
        [output, details, &dialog](const QString& text, const QString& description) {
            output->setPlainText(text);
            details->setText(description);
            dialog.setProperty("previewReady", true);
        });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
    dialog.exec();
    // Closing the viewer must never wait on file I/O. Its receiver disconnects
    // on destruction; the parentless worker owns only the path and snapshot.
    if (worker) worker->requestInterruption();
}

void DeepTelemetryDialog::exportLogs()
{
    // Freeze before the file picker starts a nested event loop. A refresh,
    // Clear or new telemetry during selection cannot silently change this export.
    const auto snapshot = exportPayload();
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (directory.isEmpty()) directory = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString filename = QStringLiteral("orion_telemetry_export_%1.json")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString selection = QFileDialog::getSaveFileName(
        this, QStringLiteral("Экспорт логов"), directory + QLatin1Char('/') + filename,
        QStringLiteral("JSON files (*.json)"), nullptr, QFileDialog::DontConfirmOverwrite);
    const QString path = orion::storage::jsonSnapshotPath(selection);
    if (path.isEmpty()) return;
    if (QFileInfo::exists(path) && QMessageBox::question(this, QStringLiteral("Заменить файл?"),
        QStringLiteral("Файл уже существует:\n%1\nЗаменить его?").arg(path),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QString error;
    if (!orion::storage::saveJsonSnapshot(path, snapshot, &error)) {
        QMessageBox::warning(this, QStringLiteral("Ошибка экспорта"),
            QStringLiteral("Не удалось сохранить файл:\n%1\n%2").arg(path, error));
        return;
    }
    QMessageBox::information(this, QStringLiteral("Экспорт завершён"),
        QStringLiteral("Логи сохранены:\n%1").arg(path));
}

} // namespace orion::app
