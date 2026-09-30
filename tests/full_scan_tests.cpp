#include "full_scan_dialog.h"
#include "full_scan_worker.h"
#include "orion/diagnostics/report_contract.h"

#include <QApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace orion::app;

namespace
{

QJsonObject fixtureDeepReport(const QString& state = QStringLiteral("complete"))
{
    const QJsonObject hardware{
        {QStringLiteral("cpu"), QJsonObject{{QStringLiteral("physical_cores"), 8},
                                             {QStringLiteral("model"), QStringLiteral("Fixture CPU")}}},
        {QStringLiteral("ram"), QJsonObject{{QStringLiteral("total_gb"), 32.0}}},
        {QStringLiteral("gpu"), QJsonArray{QJsonObject{
             {QStringLiteral("model"), QStringLiteral("NVIDIA GeForce RTX Fixture")},
             {QStringLiteral("memory_mb"), 8192.0}}}}};
    const QJsonObject temperature{
        {QStringLiteral("cpu"), QJsonObject{{QStringLiteral("level"), QStringLiteral("normal")},
                                             {QStringLiteral("current_value_c"), 63.0},
                                             {QStringLiteral("data_quality"), QStringLiteral("valid")}}},
        {QStringLiteral("gpu"), QJsonObject{{QStringLiteral("level"), QStringLiteral("unknown")},
                                             {QStringLiteral("data_quality"), QStringLiteral("unsupported")}}}};
    const QJsonObject sensors{
        {QStringLiteral("captured_at"), QStringLiteral("2026-09-14T12:00:00.000Z")},
        {QStringLiteral("data_quality"), QStringLiteral("valid")},
        {QStringLiteral("sources"), QJsonArray{QStringLiteral("fixture-sensor")}},
        {QStringLiteral("temperatures"), QJsonArray{QJsonObject{
             {QStringLiteral("component"), QStringLiteral("cpu")},
             {QStringLiteral("label"), QStringLiteral("CPU Package")},
             {QStringLiteral("value_c"), 63.0},
             {QStringLiteral("source"), QStringLiteral("fixture-sensor")}}}},
        {QStringLiteral("fans"), QJsonArray{}}};
    const QJsonObject diagnostics{
        {QStringLiteral("smart"), QJsonObject{{QStringLiteral("available"), false},
                                               {QStringLiteral("data_quality"), QStringLiteral("unsupported")},
                                               {QStringLiteral("note"), QStringLiteral("Fixture SMART unavailable")}}},
        {QStringLiteral("log_errors"), QJsonObject{{QStringLiteral("data_quality"), QStringLiteral("valid")},
                                                    {QStringLiteral("errors"), QJsonArray{}}}},
        {QStringLiteral("temperature"), temperature},
        {QStringLiteral("sensors"), sensors}};
    const QJsonObject runtime{
        {QStringLiteral("data_quality"), QStringLiteral("estimated")},
        {QStringLiteral("ram_used_percent"), 48.0},
        {QStringLiteral("before"), QJsonObject{{QStringLiteral("data_quality"), QStringLiteral("estimated")},
                                                {QStringLiteral("ram_used_percent"), 44.0}}}};
    const QJsonObject tray{{QStringLiteral("icon_enabled"), true},
                           {QStringLiteral("notifications_enabled"), true},
                           {QStringLiteral("system_available"), true},
                           {QStringLiteral("visible"), true},
                           {QStringLiteral("monitoring_paused"), false}};
    QJsonObject snapshot{
        {QStringLiteral("hardware"), hardware},
        {QStringLiteral("diagnostics"), diagnostics},
        {QStringLiteral("stress_test"), QJsonObject{{QStringLiteral("run_cpu"), true},
                                                     {QStringLiteral("run_gpu"), true},
                                                     {QStringLiteral("run_disk"), true},
                                                     {QStringLiteral("stopped_early"),
                                                      state != QStringLiteral("complete")}}},
        {QStringLiteral("autostart_entries"), QJsonArray{QJsonObject{
             {QStringLiteral("name"), QStringLiteral("Fixture app")},
             {QStringLiteral("command"), QStringLiteral("fixture.exe")},
             {QStringLiteral("source"), QStringLiteral("fixture")}}}},
        {QStringLiteral("autostart_collection"),
         QJsonObject{{QStringLiteral("data_quality"), QStringLiteral("valid")}}},
        {QStringLiteral("runtime"), runtime},
        {QStringLiteral("tray"), tray}};
    auto report = orion::diagnostics::buildReport(snapshot);
    report.insert(QStringLiteral("hardware"), snapshot.value(QStringLiteral("hardware")));
    report.insert(QStringLiteral("stress_test"), snapshot.value(QStringLiteral("stress_test")));
    report.insert(QStringLiteral("autostart_entries"), snapshot.value(QStringLiteral("autostart_entries")));
    report.insert(QStringLiteral("autostart_collection"), snapshot.value(QStringLiteral("autostart_collection")));
    report.insert(QStringLiteral("tray"), snapshot.value(QStringLiteral("tray")));
    report.insert(QStringLiteral("scan"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("deep_local")},
                                                       {QStringLiteral("state"), state}});
    return report;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    bool ok = true;
    const auto check = [&](const bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ok = false;
        }
    };

    const auto av = FullScanWorker::antivirusInfoFromJson(QByteArrayLiteral(
        "[{\"displayName\":\"Fixture AV\",\"productState\":397568},"
        "{\"displayName\":\"Unknown-state AV\",\"productState\":1193046}]"));
    const auto products = av.value(QStringLiteral("products")).toArray();
    check(av.value(QStringLiteral("data_quality")) == QStringLiteral("valid") && products.size() == 2
          && products.at(0).toObject().value(QStringLiteral("real_time_enabled")).isBool()
          && products.at(1).toObject().value(QStringLiteral("real_time_enabled")).isNull(),
          "Antivirus productState decoding lost explicit unknown states.");
    check(FullScanWorker::antivirusInfoFromJson(QByteArrayLiteral("not-json"))
              .value(QStringLiteral("data_quality")) == QStringLiteral("collector_error"),
          "Invalid Security Center payload became valid data.");
    check(FullScanWorker::rateInternet({{QStringLiteral("ok"), true},
                                        {QStringLiteral("download_mbps"), 60.0},
                                        {QStringLiteral("upload_mbps"), 15.0},
                                        {QStringLiteral("ping_ms"), 30.0}})
              .value(QStringLiteral("key")) == QStringLiteral("good")
          && FullScanWorker::rateInternet({{QStringLiteral("ok"), true},
                                           {QStringLiteral("download_mbps"), 20.0},
                                           {QStringLiteral("upload_mbps"), 4.0},
                                           {QStringLiteral("ping_ms"), 60.0}})
                 .value(QStringLiteral("key")) == QStringLiteral("average")
          && FullScanWorker::rateInternet({{QStringLiteral("ok"), false}})
                 .value(QStringLiteral("key")) == QStringLiteral("unavailable"),
          "Internet rating thresholds differ from the source contract.");

    QVector<FullScanStep> order;
    std::atomic_bool release{false};
    FullScanWorker worker([&](const FullScanStep step, const QJsonObject&,
                              const FullScanWorker::Cancel& cancel,
                              const FullScanWorker::Progress& progress) -> QJsonObject
    {
        while (!release.load() && !cancel())
            QThread::msleep(1);
        order.append(step);
        progress(75, QStringLiteral("ТЕСТОВЫЕ ДАННЫЕ — нагрузка и сеть не запускались"));
        progress(10, QStringLiteral("Проверка монотонности"));
        switch (step)
        {
        case FullScanStep::DeepLocal:
            return fixtureDeepReport();
        case FullScanStep::Antivirus:
            return av;
        case FullScanStep::SpeedTest:
            return {{QStringLiteral("ok"), true},
                    {QStringLiteral("download_mbps"), 75.0},
                    {QStringLiteral("upload_mbps"), 20.0},
                    {QStringLiteral("ping_ms"), 25.0},
                    {QStringLiteral("method"), QStringLiteral("fixture")}};
        case FullScanStep::PublicIp:
            return {{QStringLiteral("ok"), true},
                    {QStringLiteral("public_ip"), QStringLiteral("198.51.100.42")},
                    {QStringLiteral("provider"), QStringLiteral("Fixture ISP")},
                    {QStringLiteral("location"), QStringLiteral("Bucharest, RO")}};
        }
        return {};
    });
    QJsonObject result;
    int lastProgress = 0;
    bool monotonic = true;
    QObject::connect(&worker, &FullScanWorker::progressChanged, &application,
                     [&](const int value, const QString&)
                     {
                         monotonic &= value >= lastProgress && value >= 0 && value <= 100;
                         lastProgress = value;
                     });
    QObject::connect(&worker, &FullScanWorker::reportReady, &application,
                     [&](const QJsonObject& value) { result = value; });
    const auto awaitWorker = [&](FullScanWorker& target)
    {
        QEventLoop loop;
        QObject::connect(&target, &QThread::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        if (target.isRunning())
            loop.exec();
        if (target.isRunning())
        {
            target.requestStop();
            target.wait();
            ok = false;
        }
        QCoreApplication::processEvents();
    };

    check(!worker.startScan({}, {}, false) && order.isEmpty(), "Unconfirmed full scan executed.");
    QWidget parent;
    parent.setStyleSheet(QStringLiteral(
        "QWidget { background:#0A0A0C; color:#E8E8EC; font-family:Consolas; font-size:13px; }"
        "QLabel#FullScanVerdict { background:#141417; border-left:4px solid #F5C518; padding:8px; }"
        "QPlainTextEdit { background:#141417; border:1px solid #333338; }"));
    QPointer<FullScanDialog> dialog = new FullScanDialog(&worker, &parent);
    dialog->show();
    check(!dialog->findChild<QPushButton*>(QStringLiteral("FullScanCopy"))->isEnabled(),
          "Full report copy was enabled before completion.");
    const QJsonObject cached{{QStringLiteral("speed_test"), QJsonObject{{QStringLiteral("old"), true}}},
                             {QStringLiteral("public_ip"), QJsonObject{{QStringLiteral("old"), true}}}};
    check(worker.startScan(cached, {}, true), "Confirmed full scan fixture did not start.");
    check(!worker.startScan({}, {}, true), "Concurrent full scan was accepted.");
    release.store(true);
    awaitWorker(worker);
    check(order == QVector<FullScanStep>{FullScanStep::DeepLocal, FullScanStep::Antivirus,
                                         FullScanStep::SpeedTest, FullScanStep::PublicIp},
          "Full scan stage order changed.");
    check(monotonic && lastProgress == 100, "Full scan progress regressed or did not finish.");
    check(orion::diagnostics::isReportV3(result)
          && result.value(QStringLiteral("scan")).toObject().value(QStringLiteral("kind")) == QStringLiteral("full")
          && result.value(QStringLiteral("scan")).toObject().value(QStringLiteral("state")) == QStringLiteral("complete")
          && result.value(QStringLiteral("speed_test")).toObject().value(QStringLiteral("download_mbps")) == 75.0
          && !result.value(QStringLiteral("speed_test")).toObject().contains(QStringLiteral("old")),
          "Full report did not retain fresh report-v3 results.");
    check(result.value(QStringLiteral("pc_rating")).toObject().value(QStringLiteral("label")) == QStringLiteral("игровой")
          && result.value(QStringLiteral("internet_rating")).toObject().value(QStringLiteral("label")) == QStringLiteral("хороший")
          && result.value(QStringLiteral("antivirus")).toObject().value(QStringLiteral("products")).toArray().size() == 2,
          "Full scan classifications or antivirus inventory are missing.");
    const QString text = FullScanWorker::reportText(result);
    check(text.contains(QStringLiteral("ПОЛНАЯ ПРОВЕРКА"))
          && text.contains(QStringLiteral("Fixture AV"))
          && text.contains(QStringLiteral("198.51.100.42"))
          && text.contains(QStringLiteral("Оценка ПК"))
          && text.contains(QStringLiteral("Датчики после нагрузки"))
          && text.contains(QStringLiteral("RAM занято: 44.0% → 48.0%"))
          && text.contains(QStringLiteral("Трей и уведомления"))
          && !text.contains(QStringLiteral("\"physical_cores\"")),
          "Human-readable full report dropped a required section.");
    check(dialog->findChild<QPushButton*>(QStringLiteral("FullScanCopy"))->isEnabled()
          && !dialog->findChild<QLabel*>(QStringLiteral("FullScanVerdict"))->isHidden()
          && dialog->findChild<QPlainTextEdit*>(QStringLiteral("FullScanLog"))->maximumBlockCount() == 2500,
          "Full scan dialog did not expose its completed result.");

    const auto args = application.arguments();
    const int shot = args.indexOf(QStringLiteral("--screenshot"));
    if (shot >= 0 && shot + 1 < args.size())
    {
        dialog->setWindowTitle(QStringLiteral("Полная проверка — ТЕСТОВЫЕ ДАННЫЕ"));
        dialog->findChild<QLabel*>(QStringLiteral("FullScanStatus"))
            ->setText(QStringLiteral("ТЕСТОВЫЕ ДАННЫЕ · реальная нагрузка и сеть не запускались"));
        QCoreApplication::processEvents();
        check(dialog->grab().save(args.at(shot + 1)), "Full scan fixture screenshot failed.");
    }
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    std::atomic_bool enteredSpeed{false};
    QVector<FullScanStep> cancelledOrder;
    FullScanWorker cancelled([&](const FullScanStep step, const QJsonObject&,
                                  const FullScanWorker::Cancel& cancel,
                                  const FullScanWorker::Progress&) -> QJsonObject
    {
        cancelledOrder.append(step);
        if (step == FullScanStep::DeepLocal)
            return fixtureDeepReport();
        if (step == FullScanStep::Antivirus)
            return av;
        if (step == FullScanStep::SpeedTest)
        {
            enteredSpeed.store(true);
            while (!cancel())
                QThread::msleep(2);
            return {{QStringLiteral("ok"), false}, {QStringLiteral("cancelled"), true}};
        }
        return {};
    });
    QJsonObject cancelledResult;
    QObject::connect(&cancelled, &FullScanWorker::reportReady, &application,
                     [&](const QJsonObject& value) { cancelledResult = value; });
    cancelled.startScan({}, {}, true);
    QTimer cancelTimer;
    QObject::connect(&cancelTimer, &QTimer::timeout, &application, [&]
    {
        if (enteredSpeed.load())
        {
            cancelTimer.stop();
            cancelled.requestStop();
        }
    });
    cancelTimer.start(5);
    awaitWorker(cancelled);
    check(cancelledResult.value(QStringLiteral("scan")).toObject().value(QStringLiteral("state"))
              == QStringLiteral("cancelled")
          && !cancelledOrder.contains(FullScanStep::PublicIp)
          && !cancelledResult.value(QStringLiteral("scan")).toObject()
                  .value(QStringLiteral("completed_steps")).toArray()
                  .contains(QStringLiteral("Публичный IP и провайдер")),
          "Cancelling the full scan continued to a later network stage.");

    FullScanWorker partial([](const FullScanStep step, const QJsonObject&,
                              const FullScanWorker::Cancel&,
                              const FullScanWorker::Progress&) -> QJsonObject
    {
        if (step == FullScanStep::DeepLocal)
        {
            auto report = fixtureDeepReport(QStringLiteral("partial"));
            auto diagnostics = report.value("diagnostics").toObject();
            diagnostics.remove("temperature");
            diagnostics.insert("sensors", QJsonObject{{"data_quality", "not_collected"}});
            report.insert("diagnostics", diagnostics);
            return report;
        }
        if (step == FullScanStep::Antivirus)
            return {{QStringLiteral("supported"), false}, {QStringLiteral("products"), QJsonArray{}}};
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("fixture")}};
    });
    QJsonObject partialResult;
    QObject::connect(&partial, &FullScanWorker::reportReady, &application,
                     [&](const QJsonObject& value) { partialResult = value; });
    partial.startScan({}, {}, true);
    awaitWorker(partial);
    check(partialResult.value(QStringLiteral("scan")).toObject().value(QStringLiteral("state"))
              == QStringLiteral("partial")
          && FullScanWorker::reportText(partialResult).contains(QStringLiteral("отчёт неполный"))
          && FullScanWorker::reportText(partialResult).contains(QStringLiteral("Снимок после нагрузки не собран"))
          && partialResult.value("diagnostics").toObject().value("sensors").toObject()
                 .value("data_quality") == "not_collected",
          "A partial deep phase was promoted to a complete full scan.");

    FullScanWorker failed([](FullScanStep, const QJsonObject&, const FullScanWorker::Cancel&,
                             const FullScanWorker::Progress&) -> QJsonObject
                          { throw std::runtime_error("fixture full failure"); });
    QJsonObject failedResult;
    QObject::connect(&failed, &FullScanWorker::reportReady, &application,
                     [&](const QJsonObject& value) { failedResult = value; });
    failed.startScan({}, {}, true);
    awaitWorker(failed);
    check(failedResult.value(QStringLiteral("scan")).toObject().value(QStringLiteral("state"))
              == QStringLiteral("failed")
          && FullScanWorker::reportText(failedResult).contains(QStringLiteral("fixture full failure")),
          "Full scan collector exception escaped or was hidden.");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
