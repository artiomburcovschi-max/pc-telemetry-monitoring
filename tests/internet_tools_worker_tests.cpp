#include "internet_tools_worker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <cstdlib>
#include <functional>
#include <iostream>

namespace {

[[nodiscard]] bool waitUntil(const std::function<bool()>& predicate, const int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return predicate();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    using orion::app::InternetOperation;
    using orion::app::InternetToolsWorker;

    const QString timestamp = QStringLiteral("2026-09-10T17:00:00+03:00");
    const QJsonObject ipInfo = InternetToolsWorker::parseIpInfoResponse(QByteArrayLiteral(
        "{\"ip\":\"203.0.113.10\",\"org\":\"AS64500 Example Fiber\","
        "\"city\":\"Bucharest\",\"country\":\"RO\"}"), timestamp);
    const QJsonObject ipApi = InternetToolsWorker::parseIpApiResponse(QByteArrayLiteral(
        "{\"status\":\"success\",\"query\":\"198.51.100.7\","
        "\"isp\":\"Fallback ISP\",\"city\":\"Iasi\",\"country\":\"Romania\"}"),
        timestamp);
    const QJsonObject rejected = InternetToolsWorker::parseIpApiResponse(QByteArrayLiteral(
        "{\"status\":\"fail\",\"message\":\"private range\"}"), timestamp);
    if (!ipInfo.value(QStringLiteral("ok")).toBool()
        || ipInfo.value(QStringLiteral("provider")).toString() != QStringLiteral("Example Fiber")
        || ipInfo.value(QStringLiteral("location")).toString() != QStringLiteral("Bucharest, RO")
        || ipApi.value(QStringLiteral("public_ip")).toString() != QStringLiteral("198.51.100.7")
        || ipApi.value(QStringLiteral("source")).toString() != QStringLiteral("ip-api.com")
        || rejected.value(QStringLiteral("ok")).toBool()
        || rejected.value(QStringLiteral("error")).toString() != QStringLiteral("private range")) {
        std::cerr << "Public-IP response parsing is incomplete.\n";
        return EXIT_FAILURE;
    }

    const QJsonObject speed = InternetToolsWorker::speedResultFromMeasurements(
        10'000'000, 1000, 5'000'000, 2000, {31.0, 29.0, 30.0}, timestamp);
    const QJsonObject invalidPing = InternetToolsWorker::speedResultFromMeasurements(
        10'000'000, 1000, 5'000'000, 2000, {1'800'000.0}, timestamp);
    if (!speed.value(QStringLiteral("ok")).toBool()
        || speed.value(QStringLiteral("download_mbps")).toDouble() != 80.0
        || speed.value(QStringLiteral("upload_mbps")).toDouble() != 20.0
        || speed.value(QStringLiteral("ping_ms")).toDouble() != 30.0
        || speed.value(QStringLiteral("server")).toObject()
               .value(QStringLiteral("sponsor")).toString() != QStringLiteral("Cloudflare")
        || !invalidPing.value(QStringLiteral("ping_ms")).isNull()
        || invalidPing.value(QStringLiteral("ping_note")).toString().isEmpty()) {
        std::cerr << "Speed result calculation or ping sanity handling changed.\n";
        return EXIT_FAILURE;
    }

    int calls = 0;
    InternetToolsWorker worker(nullptr,
        [&](const InternetOperation operation, const std::atomic_bool&,
            const InternetToolsWorker::ProgressFunction& progress) {
            ++calls;
            progress(50, QStringLiteral("fixture"));
            return QJsonObject {
                {QStringLiteral("ok"), true},
                {QStringLiteral("operation"), operation == InternetOperation::PublicIpLookup
                     ? QStringLiteral("public") : QStringLiteral("speed")},
            };
        });
    QJsonObject result;
    bool ready = false;
    int finalProgress = -1;
    QObject::connect(&worker, &InternetToolsWorker::progressChanged, &application,
        [&](const InternetOperation, const int value, const QString&) { finalProgress = value; });
    QObject::connect(&worker, &InternetToolsWorker::resultReady, &application,
        [&](const InternetOperation, const QJsonObject& value) {
            result = value;
            ready = true;
        });
    if (!worker.startPublicIpLookup() || !waitUntil([&] { return ready; }, 1000)
        || !worker.wait(1000) || calls != 1 || finalProgress != 100
        || result.value(QStringLiteral("operation")).toString() != QStringLiteral("public")) {
        std::cerr << "Injected public-IP operation did not complete deterministically.\n";
        return EXIT_FAILURE;
    }
    ready = false;
    if (!worker.startSpeedTest() || !waitUntil([&] { return ready; }, 1000)
        || !worker.wait(1000) || calls != 2
        || result.value(QStringLiteral("operation")).toString() != QStringLiteral("speed")) {
        std::cerr << "Injected speed operation did not complete deterministically.\n";
        return EXIT_FAILURE;
    }

    InternetToolsWorker cancellable(nullptr,
        [](const InternetOperation, const std::atomic_bool& stopped,
            const InternetToolsWorker::ProgressFunction&) {
            for (int step = 0; step < 100 && !stopped.load(); ++step) QThread::msleep(5);
            return QJsonObject {
                {QStringLiteral("ok"), false},
                {QStringLiteral("cancelled"), stopped.load()},
                {QStringLiteral("error"), QStringLiteral("операция отменена")},
            };
        });
    bool cancelledReady = false;
    QJsonObject cancelledResult;
    QObject::connect(&cancellable, &InternetToolsWorker::resultReady, &application,
        [&](const InternetOperation, const QJsonObject& value) {
            cancelledResult = value;
            cancelledReady = true;
        });
    if (!cancellable.startSpeedTest()) return EXIT_FAILURE;
    QThread::msleep(25);
    cancellable.requestStop();
    if (!waitUntil([&] { return cancelledReady; }, 1000) || !cancellable.wait(1000)
        || !cancelledResult.value(QStringLiteral("cancelled")).toBool()) {
        std::cerr << "Cancellation did not stop the internet worker.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Native public-IP parsing, speed calculation and worker lifecycle passed.\n";
    return EXIT_SUCCESS;
}
