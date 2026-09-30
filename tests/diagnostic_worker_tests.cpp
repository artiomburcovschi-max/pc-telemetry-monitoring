#include "diagnostic_worker.h"

#include "orion/diagnostics/report_contract.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QTimer>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QJsonObject snapshot {
        {QStringLiteral("autostart_collected"), true},
        {QStringLiteral("autostart_entries"), QJsonArray {}},
        {QStringLiteral("diagnostics"), QJsonObject {
             {QStringLiteral("temperature"), QJsonObject {
                  {QStringLiteral("cpu"), QJsonObject {{QStringLiteral("level"), QStringLiteral("ok")}}},
                  {QStringLiteral("gpu"), QJsonObject {{QStringLiteral("level"), QStringLiteral("ok")}}},
              }},
             {QStringLiteral("current"), QJsonObject {
                  {QStringLiteral("ram_usage_percent"), 97.0},
                  {QStringLiteral("gpu_temperature_c"), 62.0},
              }},
             {QStringLiteral("smart"), QJsonObject {
                  {QStringLiteral("available"), true},
                  {QStringLiteral("disks"), QJsonArray {}},
              }},
             {QStringLiteral("log_errors"), QJsonObject {
                  {QStringLiteral("errors"), QJsonArray {}},
                  {QStringLiteral("data_quality"), QStringLiteral("valid")},
                  {QStringLiteral("source"), QStringLiteral("test fixture")},
              }},
         }},
        {QStringLiteral("hardware"), QJsonObject {
             {QStringLiteral("ram"), QJsonObject {{QStringLiteral("total_gb"), 16.0}}},
             {QStringLiteral("disks"), QJsonArray {}},
         }},
        {QStringLiteral("runtime"), QJsonObject {}},
        {QStringLiteral("stress_test"), QJsonObject {}},
    };

    orion::app::DiagnosticWorker worker;
    QEventLoop loop;
    QJsonObject report;
    QObject::connect(&worker, &orion::app::DiagnosticWorker::reportReady,
        [&](const QJsonObject& value) {
            report = value;
            loop.quit();
        });
    worker.scan(snapshot);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    if (worker.isRunning()) {
        worker.stop();
        worker.wait(1000);
    }
    if (!orion::diagnostics::isReportV3(report)
        || report.value(QStringLiteral("findings")).toArray().isEmpty()
        || !orion::diagnostics::reportToText(report).contains(QStringLiteral("План действий"))) {
        std::cerr << "Background diagnostic report contract failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Background diagnostic report contract passed.\n";
    return EXIT_SUCCESS;
}
