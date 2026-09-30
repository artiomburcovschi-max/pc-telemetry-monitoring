#include "main_window.h"
#include "app_monitor_worker.h"
#include "widgets/details_panel.h"
#include "widgets/app_monitor_chart.h"
#include "orion/diagnostics/runtime_diagnostics.h"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QDialog>
#include <QScrollArea>
#include <QTimer>
#include <QTextCursor>
#include <cmath>
#include <iostream>

bool verifyAppMonitorUi(orion::app::MainWindow& window)
{
    using namespace orion::app;
    auto* worker = window.findChild<AppMonitorWorker*>();
    auto* path = window.findChild<QLineEdit*>("AppMonitorExecutable");
    auto* browse = window.findChild<QPushButton*>("AppMonitorBrowse");
    auto* duration = window.findChild<QComboBox*>("AppMonitorDuration");
    auto* hint = window.findChild<QLabel*>("AppMonitorSamplingHint");
    auto* table = window.findChild<QTableWidget*>("AppMonitorSamples");
    auto* details = window.findChild<DetailCard*>("AppMonitorLiveDetails");
    auto* status = window.findChild<QLabel*>("AppMonitorStatus");
    auto* report = window.findChild<QTextEdit*>("AppMonitorReportPreview");
    auto* cpuChart = window.findChild<AppMonitorChart*>("AppMonitorCpuChart");
    auto* memoryChart = window.findChild<AppMonitorChart*>("AppMonitorMemoryChart");
    auto* verdict = window.findChild<DetailCard*>("AppMonitorVerdict");
    auto* openReport = window.findChild<QPushButton*>("AppMonitorOpenReport");
    auto* scroll = window.findChild<QScrollArea*>("AppMonitorScroll");
    QPushButton* start = nullptr;
    for (auto* button : window.findChildren<QPushButton*>())
        if (button->text() == QStringLiteral("Запустить и наблюдать")) start = button;
    if (!worker || !path || !browse || !duration || !hint || !table || !details || !status || !start || !report
        || !cpuChart || !memoryChart || !verdict || !openReport || !scroll) return false;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { passed = false; std::cerr << message << '\n'; }
    };
    check(!start->isEnabled(), "Empty executable path allowed start.");
    path->setText(QCoreApplication::applicationFilePath()); // Never click Start: fixtures use signals only.
    check(start->isEnabled(), "Existing executable did not enable start.");
    duration->setCurrentIndex(duration->findData(480 * 60));
    check(hint->text().contains("5") && duration->currentText() == QStringLiteral("8 ч"), "Long-session hint was lost.");
    QJsonObject sample{{"elapsed", 5}, {"duration", 60}, {"process_count", 2},
        {"cpu_percent", QJsonValue::Null}, {"ram_mb", QJsonValue::Null}, {"private_mb", QJsonValue::Null},
        {"ui_hung", QJsonValue::Null}, {"window_count", 0}};
    worker->sampleReady(sample);
    check(table->item(0, 1)->text() == QStringLiteral("н/д")
        && table->item(0, 2)->text() == QStringLiteral("н/д")
        && table->item(0, 3)->text() == QStringLiteral("н/д")
        && table->item(0, 5)->text() == QStringLiteral("н/д"), "Missing app data was converted to zero/healthy.");
    sample.insert("cpu_percent", 145.5);
    sample.insert("elapsed", 6);
    sample.insert("ram_mb", 123.0);
    sample.insert("private_mb", 0.0);
    sample.insert("thread_count", 8);
    sample.insert("handle_count", 50);
    sample.insert("page_faults_per_sec", 4.5);
    sample.insert("system_commit_used_percent", 65.0);
    sample.insert("gpu_usage_percent", 70.0);
    sample.insert("gpu_temperature_c", 55.0);
    sample.insert("ui_hung", false);
    sample.insert("system_data_quality", "valid");
    worker->sampleReady(sample);
    check(table->item(1, 1)->text() == "145.5%" && table->item(1, 3)->text().startsWith("0.0"),
        "Multicore CPU or real zero private memory was lost.");
    check(details->fieldValue("threads") == "8 / 50" && details->fieldValue("faults") == "4.5"
        && details->fieldValue("gpu").contains("55.0") && details->fieldValue("quality") == QStringLiteral("свежие"),
        "Original live detail fields/freshness status are incomplete.");
    QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
    sample.insert("elapsed", 7);
    worker->sampleReady(sample);
    check(table->rowCount() == 2 && !start->isEnabled() && !browse->isEnabled() && !duration->isEnabled(),
        "Global Pause accepted app samples or left launch controls enabled.");
    check(cpuChart->sampleCount() == 2 && memoryChart->sampleCount() == 2, "Pause accepted graph points.");
    QJsonObject final{{"launch_error", "fixture-launch-error"}, {"verdict", QStringLiteral("не удалось запустить")}};
    final.insert("report_text", orion::diagnostics::appMonitorReportToText(final));
    worker->reportReady(final);
    check(!path->isEnabled() && !duration->isEnabled() && !browse->isEnabled()
        && report->toPlainText().contains("fixture-launch-error"), "Finished report defeated pause or hid launch error.");
    QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
    check(start->isEnabled() && browse->isEnabled(), "Resume failed to restore app controls.");
    worker->sampleReady(sample);
    check(cpuChart->lineSegments().size() == 2 && cpuChart->axisMaximum() > 145.5, "Graph integration lost pause/scale.");
    final = QJsonObject{{"timed_out", true}, {"close_on_timeout", true},
        {"termination", QJsonObject{{"cancelled", true}, {"all_exited", false}}}};
    final.insert("report_text", orion::diagnostics::appMonitorReportToText(final));
    worker->reportReady(final);
    check(status->text().startsWith(QStringLiteral("Автозакрытие отменено")), "Partial closure was presented as success.");
    QJsonArray samples;
    cpuChart->clear(); memoryChart->clear();
    for (int i = 0; i < 80; ++i) {
        sample.insert("elapsed", i * 2); sample.insert("sample_interval", 2); sample.insert("duration", 180);
        sample.insert("cpu_percent", 65.0 + std::sin(i / 7.0) * 40.0 + (i == 45 ? 90 : 0));
        sample.insert("ram_mb", 300.0 + i * 2.0);
        sample.insert("private_mb", i >= 33 && i <= 35 ? QJsonValue(QJsonValue::Null) : QJsonValue(180.0 + i * 2.5));
        worker->sampleReady(sample); samples.append(sample);
    }
    final = QJsonObject{{"exe_path", "<fixture>.exe"}, {"stopped_manually", true},
        {"verdict", QStringLiteral("есть подозрительные признаки")}, {"summary", orion::diagnostics::summarizeAppMonitorSamples(samples)},
        {"verdict_findings", QJsonArray{QJsonObject{{"title", QStringLiteral("ТЕСТОВЫЕ ДАННЫЕ")},
            {"detail", QStringLiteral("ТЕСТОВЫЕ ДАННЫЕ — приложение не запускалось. Пример роста памяти.")}}}},
        {"action_plan", QJsonArray{QJsonObject{{"action", QStringLiteral("Повторить наблюдение с обычной рабочей нагрузкой приложения.")}}}},
        {"coverage", QJsonObject{{"level", "partial"}, {"message", QStringLiteral("Пример неполного покрытия проверки.")}}}};
    final.insert("io_total_scope", "observed_intervals_lower_bound");
    final.insert("exit_code", qint64(0xC0000005LL));
    final.insert("exit_details", orion::diagnostics::decodeWindowsExitCode(final.value("exit_code")));
    final.insert("timing_contract", "pause_transitions_steady_clock_v1");
    final.insert("active_seconds", 158.0);
    final.insert("paused_seconds", 0.125);
    final.insert("observation_wall_seconds", 158.125);
    final.insert("runtime_memory", QJsonObject{{"observation_kind", "timestamped_cache_endpoints"},
        {"data_quality", "unknown"}, {"note", QStringLiteral("ТЕСТОВЫЙ ПРИМЕР: нет свежего снимка после наблюдения.")}});
    worker->reportReady(final);
    check(report->toPlainText().contains(QStringLiteral("нижняя граница")), "UI report omitted I/O bounds.");
    check(report->toPlainText().contains("ACCESS_VIOLATION")
        && report->toPlainText().contains(QStringLiteral("известных переходов: 79"))
        && report->toPlainText().contains(QStringLiteral("158.125 сек")),
        "UI report omitted transition timing, exit details or growth fraction.");
    check(!verdict->isHidden() && openReport->isEnabled() && table->rowCount() == 50 && cpuChart->sampleCount() == 80,
        "Report erased history/card or table unbounded.");
    bool dialogSeen = false;
    QTimer::singleShot(100, [&] {
        auto* dialog = window.findChild<QDialog*>("AppMonitorReportDialog");
        if (!dialog) return;
        auto* full = dialog->findChild<QPlainTextEdit*>("AppMonitorFullReport");
        dialogSeen = full && full->isReadOnly() && full->toPlainText() == report->toPlainText()
            && full->lineWrapMode() == QPlainTextEdit::NoWrap;
        const auto capture = qEnvironmentVariable("ORION_APP_DIALOG_CAPTURE");
        if (!capture.isEmpty()) {
            check(dialog->grab().save(capture), "Report dialog capture failed.");
            if (full) {
                const auto growth = full->document()->find(QStringLiteral("Доля переходов с ростом Working set"));
                check(!growth.isNull(), "Growth section missing from visual report.");
                full->setTextCursor(growth);
                full->ensureCursorVisible();
                QCoreApplication::processEvents();
                check(dialog->grab().save(capture + ".memory.png"), "Memory report capture failed.");
            }
        }
        dialog->accept();
    });
    openReport->click();
    check(dialogSeen, "Detailed report dialog differs from preview.");
    const auto capture = qEnvironmentVariable("ORION_APP_UI_CAPTURE");
    if (!capture.isEmpty()) {
        status->setText(QStringLiteral("ТЕСТОВЫЕ ДАННЫЕ — приложение не запускалось"));
        window.resize(830, 820);
        QCoreApplication::processEvents();
        scroll->ensureWidgetVisible(memoryChart);
        QCoreApplication::processEvents();
        check(window.grab().save(capture), "App-monitor fixture capture failed.");
        scroll->ensureWidgetVisible(verdict);
        QCoreApplication::processEvents();
        check(window.grab().save(capture + ".verdict.png"), "Verdict capture failed.");
    }
    path->clear();
    return passed;
}
