#include "deep_telemetry_dialog.h"

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QElapsedTimer>
#include <QEvent>
#include <QPointer>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

bool checkDeepTelemetryFiles(orion::app::DeepTelemetryDialog& dialog);

namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 2500)
{
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    return predicate();
}
struct ControlledLog {
    QSemaphore entered, release;
    QPointer<QThread> thread;
    QJsonObject payload {{"errors", QJsonArray{"fixture-event"}}, {"source", "fixture-only"},
        {"data_quality", "estimated"}, {"note", "fixture-partial-channel"},
        {"collection_limited", true}, {"collection_limits", QJsonObject{{"line_utf16_units", 2048}}},
        {"read_stats", QJsonObject{{"shortened", 1}}}};
    QJsonObject collect()
    {
        thread = QThread::currentThread();
        entered.release();
        release.tryAcquire(1, 3000); // Bound failed-test cleanup; no system log query.
        return payload;
    }
};
}

int main(int argc, char* argv[])
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    orion::app::DeepTelemetryDialog dialog(nullptr, false);

    orion::core::SessionPeaks peaks;
    peaks.cpu = 96.5;
    peaks.ram = 72.0;
    peaks.gpu = 88.0;
    peaks.cpuTemperatureC = 81.5;
    peaks.gpuTemperatureC = 74.0;
    dialog.applyTelemetry(
        QVector<double> {12.0, 96.5, 37.0},
        QVector<double> {3200.0, 4100.0, 3600.0},
        3633.3,
        125.0,
        peaks);

    const auto payload = dialog.exportPayload();
    const auto snapshot = payload.value(QStringLiteral("current_telemetry")).toObject();
    const auto rows = snapshot.value(QStringLiteral("percpu")).toArray();
    const auto exportedPeaks = payload.value(QStringLiteral("session_peaks")).toObject();
    if (rows.size() != 3
        || rows[1].toObject().value(QStringLiteral("percent")).toDouble() != 96.5
        || rows[1].toObject().value(QStringLiteral("freq_mhz")).toDouble() != 4100.0
        || snapshot.value(QStringLiteral("frequency_sampling")).toString()
            != QStringLiteral("per_logical_cpu")
        || payload.value(QStringLiteral("session_uptime_seconds")).toDouble() != 125.0
        || exportedPeaks.value(QStringLiteral("cpu_percent_max")).toDouble() != 96.5) {
        std::cerr << "Deep Telemetry export payload lost runtime data.\n";
        return EXIT_FAILURE;
    }

    const auto* uptime = dialog.findChild<QLabel*>(QStringLiteral("DeepTelemetryUptime"));
    const auto* peaksLabel = dialog.findChild<QLabel*>(QStringLiteral("DeepTelemetryPeaks"));
    auto* pause = dialog.findChild<QPushButton*>(QStringLiteral("DeepTelemetryPauseButton"));
    if (uptime == nullptr || uptime->text() != QStringLiteral("Время сессии: 00:02:05")
        || peaksLabel == nullptr || !peaksLabel->text().contains(QStringLiteral("96.5%"))
        || pause == nullptr) {
        std::cerr << "Deep Telemetry presentation contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    pause->click();
    const auto pausedSnapshot = dialog.exportPayload();
    dialog.applyTelemetry(
        QVector<double> {1.0}, QVector<double> {1000.0}, 1000.0, 200.0, peaks);
    if (dialog.exportPayload().value(QStringLiteral("current_telemetry"))
            != pausedSnapshot.value(QStringLiteral("current_telemetry"))) {
        std::cerr << "Local pause did not freeze Deep Telemetry.\n";
        return EXIT_FAILURE;
    }

    dialog.setMonitoringPaused(true);
    if (pause->isEnabled()) {
        std::cerr << "Global pause did not disable the local pause control.\n";
        return EXIT_FAILURE;
    }

    dialog.setMonitoringPaused(false);
    dialog.applyTelemetry({0}, {2000}, 2000, 200, peaks);
    if (dialog.exportPayload().value("current_telemetry") != pausedSnapshot.value("current_telemetry")) {
        std::cerr << "Global resume erased local pause.\n"; return EXIT_FAILURE;
    }
    pause->click();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    peaks.gpu = inf;
    dialog.applyTelemetry({0, -1, nan, 101, inf}, {3200, -1, nan}, -1, nan, peaks);
    const auto invalid = dialog.exportPayload().value("current_telemetry").toObject();
    const auto invalidRows = invalid.value("percpu").toArray();
    auto* loadChart = dialog.findChild<QWidget*>("DeepTelemetryLoadChart");
    auto* frequencyChart = dialog.findChild<QWidget*>("DeepTelemetryFrequencyChart");
    if (invalidRows.size() != 5 || !invalidRows[1].toObject().value("percent").isNull()
        || !invalidRows[2].toObject().value("freq_mhz").isNull()
        || !invalidRows[3].toObject().value("percent").isNull()
        || invalidRows[0].toObject().value("percent").toDouble(-1) != 0
        || !invalid.value("session_uptime_seconds").isNull()
        || !invalid.value("session_peaks").toObject().value("gpu_percent_max").isNull()
        || !loadChart || !frequencyChart || loadChart->property("knownValueCount").toInt() != 1
        || frequencyChart->property("slotCount").toInt() != 5) {
        std::cerr << "Missing or invalid core metrics became zero/critical or lost aligned slots.\n"; return EXIT_FAILURE;
    }
    dialog.applyTelemetry({5, 0, -1}, {}, 2500, 300, peaks);
    if (!frequencyChart->property("aggregate").toBool() || frequencyChart->property("slotCount").toInt() != 1
        || dialog.exportPayload().value("current_telemetry").toObject().value("frequency_sampling") != "aggregate_estimate") {
        std::cerr << "Aggregate frequency was disguised as per-core values.\n"; return EXIT_FAILURE;
    }
    dialog.applyTelemetry({}, {}, -1, 300, peaks);
    if (dialog.exportPayload().value("current_telemetry").toObject().value("frequency_sampling") != "unavailable"
        || frequencyChart->property("knownValueCount").toInt() != 0) {
        std::cerr << "Unavailable frequency presented as an estimate.\n"; return EXIT_FAILURE;
    }

    auto controlled = std::make_shared<ControlledLog>();
    orion::app::DeepTelemetryDialog logDialog(nullptr, false, [controlled] { return controlled->collect(); });
    auto* refresh = logDialog.findChild<QPushButton*>("DeepTelemetryRefreshButton");
    auto* clear = logDialog.findChild<QPushButton*>("DeepTelemetryClearButton");
    auto* output = logDialog.findChild<QTextEdit*>("DeepTelemetryErrors");
    if (!output || output->lineWrapMode() != QTextEdit::WidgetWidth) {
        std::cerr << "Partial-log warning requires horizontal scrolling.\n"; return EXIT_FAILURE;
    }
    refresh->click();
    if (!controlled->entered.tryAcquire(1, 1000)) return EXIT_FAILURE;
    clear->click();
    controlled->release.release();
    if (!waitFor([&] { return refresh->isEnabled(); })
        || !logDialog.exportPayload().value("system_errors").toArray().isEmpty()
        || logDialog.exportPayload().value("system_errors_data_quality") != "cleared"
        || output->toPlainText().contains("fixture-event")) {
        std::cerr << "Late log result undid Clear.\n"; return EXIT_FAILURE;
    }
    refresh->click();
    if (!controlled->entered.tryAcquire(1, 1000)) return EXIT_FAILURE;
    controlled->release.release();
    if (!controlled->thread || !controlled->thread->wait(1000)) return EXIT_FAILURE;
    // Result is already queued in the GUI, not suppressible by interruption.
    clear->click();
    if (!waitFor([&] { return refresh->isEnabled(); })
        || logDialog.exportPayload().value("system_errors_data_quality") != "cleared") {
        std::cerr << "Already queued result undid Clear.\n"; return EXIT_FAILURE;
    }
    refresh->click();
    if (!controlled->entered.tryAcquire(1, 1000)) return EXIT_FAILURE;
    logDialog.setMonitoringPaused(true);
    controlled->release.release();
    if (!waitFor([&] { return logDialog.exportPayload().value("system_errors_capture_state") == "paused_result_pending"; })
        || !logDialog.exportPayload().value("system_errors").toArray().isEmpty()) {
        std::cerr << "Global pause applied a queued log result.\n"; return EXIT_FAILURE;
    }
    logDialog.setMonitoringPaused(false);
    if (!output->toPlainText().contains("fixture-event") || !output->toPlainText().startsWith("[fixture-partial-channel]")
        || !logDialog.exportPayload().value("system_errors_report").toObject().value("collection_limited").toBool()
        || logDialog.exportPayload().value("system_errors_report").toObject().value("read_stats").toObject().value("shortened").toInt() != 1
        || logDialog.exportPayload().value("system_errors_report").toObject().value("captured_at").toString().isEmpty()) {
        std::cerr << "Resume lost log data, partial-read warning or capture time.\n"; return EXIT_FAILURE;
    }
    if (!waitFor([&] { return refresh->isEnabled(); })) return EXIT_FAILURE;
    refresh->click();
    if (!controlled->entered.tryAcquire(1, 1000)) return EXIT_FAILURE;
    logDialog.setMonitoringPaused(true);
    controlled->release.release();
    if (!waitFor([&] { return logDialog.exportPayload().value("system_errors_capture_state") == "paused_result_pending"; })) return EXIT_FAILURE;
    clear->click();
    logDialog.setMonitoringPaused(false);
    if (!logDialog.exportPayload().value("system_errors").toArray().isEmpty()) {
        std::cerr << "Resume restored a cleared pending snapshot.\n"; return EXIT_FAILURE;
    }
    QJsonObject denied{{"errors", QJsonArray{}}, {"data_quality", "permission_denied"}, {"source", "fixture-only"}};
    QMetaObject::invokeMethod(&logDialog, "applySystemErrors", Qt::DirectConnection, Q_ARG(QJsonObject, denied));
    if (output->toPlainText().contains(QStringLiteral("ошибок не найдено"))
        || !output->toPlainText().contains(QStringLiteral("не подтверждает"))) {
        std::cerr << "Unavailable log was presented as an error-free system.\n"; return EXIT_FAILURE;
    }
    QJsonObject structured{{"errors", QJsonArray{QJsonObject{{"line", "structured-event"}}}}, {"data_quality", "valid"}};
    QMetaObject::invokeMethod(&logDialog, "applySystemErrors", Qt::DirectConnection, Q_ARG(QJsonObject, structured));
    if (!output->toPlainText().contains("structured-event")
        || !logDialog.exportPayload().value("system_errors").toArray().first().isObject()) {
        std::cerr << "Structured log presentation/export lost the entry.\n"; return EXIT_FAILURE;
    }

    auto delayed = std::make_shared<ControlledLog>();
    auto* closing = new orion::app::DeepTelemetryDialog(nullptr, true, [delayed] { return delayed->collect(); });
    if (!delayed->entered.tryAcquire(1, 1000)) return EXIT_FAILURE;
    QElapsedTimer closeTime; closeTime.start();
    delete closing;
    const bool responsiveClose = closeTime.elapsed() < 250;
    delayed->release.release();
    if (!responsiveClose || !waitFor([&] { return delayed->thread.isNull(); })) {
        std::cerr << "Closing dialog blocked on collector or leaked the completed worker.\n"; return EXIT_FAILURE;
    }

    if (!checkDeepTelemetryFiles(logDialog)) return EXIT_FAILURE;

    // Explicitly synthetic screenshots at the minimum supported window width.
    const auto capture = qEnvironmentVariable("ORION_DEEP_UI_CAPTURE");
    if (!capture.isEmpty()) {
        peaks.gpu = 88;
        logDialog.applyTelemetry({12, 96.5, -1, 40}, {3200, 4100, -1, 0}, 3633, 125, peaks);
        QJsonObject fixture{{"errors", QJsonArray{"ТЕСТОВЫЕ ДАННЫЕ — журнал ОС не читался", "2026-09-22 [fixture] EventID=0 test"}},
            {"source", "fixture-only"}, {"data_quality", "estimated"}, {"collection_limited", true},
            {"note", QStringLiteral("УЧЕБНЫЙ ПРИМЕР: журнал прочитан не полностью — достигнут лимит объёма; часть записей сокращена. Отсутствие записи не подтверждает отсутствие события.")}};
        QMetaObject::invokeMethod(&logDialog, "applySystemErrors", Qt::DirectConnection, Q_ARG(QJsonObject, fixture));
        logDialog.resize(780, 760);
        logDialog.show();
        QCoreApplication::processEvents();
        if (logDialog.width() != 780 || !logDialog.grab().save(capture)
            || logDialog.findChild<QLabel*>("DeepTelemetryPeaks")->palette().color(QPalette::WindowText).lightness() < 150) {
            std::cerr << "Minimum-width telemetry capture failed.\n"; return EXIT_FAILURE;
        }
        logDialog.applyTelemetry({0, 10, 20, 30}, {}, 2500, 125, peaks);
        QCoreApplication::processEvents();
        if (!logDialog.grab().save(capture + ".aggregate.png")) return EXIT_FAILURE;
    }

    std::cout << "Deep Telemetry data, pause, clear, close and export contracts passed.\n";
    return EXIT_SUCCESS;
}
