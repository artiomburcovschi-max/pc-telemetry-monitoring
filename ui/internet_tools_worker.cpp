#include "internet_tools_worker.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <QEvent>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <utility>

namespace orion::app {
namespace {

constexpr qint64 publicResponseLimit = 256 * 1024;
constexpr qint64 downloadTestBytes = 10'000'000;
constexpr qint64 uploadTestBytes = 5'000'000;
constexpr double maximumSanePingMs = 2000.0;

struct HttpResult {
    bool ok {false};
    bool cancelled {false};
    bool timedOut {false};
    int statusCode {0};
    QByteArray body;
    qint64 bytesReceived {0};
    qint64 elapsedMs {0};
    QString error;
};

[[nodiscard]] QString timestampNow()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

[[nodiscard]] QJsonObject failureResult(
    const QString& timestampKey,
    const QString& timestamp,
    const QString& error,
    const bool cancelled = false)
{
    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("cancelled"), cancelled},
        {timestampKey, timestamp},
        {QStringLiteral("error"), error.isEmpty() ? QStringLiteral("неизвестная ошибка") : error},
    };
}

[[nodiscard]] QString textField(const QJsonObject& object, const QString& key)
{
    const QString value = object.value(key).toString().simplified();
    return value.isEmpty() ? QStringLiteral("н/д") : value;
}

[[nodiscard]] QNetworkRequest networkRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("O.R.I.O.N.-Native/3.3"));
    request.setRawHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-cache"));
    request.setRawHeader(QByteArrayLiteral("Accept-Encoding"), QByteArrayLiteral("identity"));
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
        QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

[[nodiscard]] HttpResult performRequest(
    QNetworkAccessManager& manager,
    QNetworkRequest request,
    const QByteArray& method,
    const QByteArray& payload,
    const int timeoutMs,
    const qint64 maximumResponseBytes,
    const std::atomic_bool& stopRequested,
    const std::function<void(qint64, qint64)>& transferProgress = {})
{
    request.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = method == QByteArrayLiteral("POST")
        ? manager.post(request, payload) : manager.get(request);
    HttpResult result;
    QElapsedTimer elapsed;
    elapsed.start();
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setInterval(40);
    bool responseTooLarge = false;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        const QByteArray chunk = reply->readAll();
        result.bytesReceived += chunk.size();
        if (result.body.size() < maximumResponseBytes) {
            result.body += chunk.left(static_cast<int>(
                std::min<qint64>(chunk.size(), maximumResponseBytes - result.body.size())));
        }
        if (result.bytesReceived > maximumResponseBytes) {
            responseTooLarge = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
        [&](const qint64 received, const qint64 total) {
            if (transferProgress) transferProgress(received, total);
        });
    QObject::connect(reply, &QNetworkReply::uploadProgress, &loop,
        [&](const qint64 sent, const qint64 total) {
            if (transferProgress) transferProgress(sent, total);
        });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] {
        if (stopRequested.load(std::memory_order_relaxed)) reply->abort();
        if (elapsed.elapsed() >= timeoutMs) {
            result.timedOut = true;
            reply->abort();
        }
    });
    watchdog.start();
    loop.exec();
    watchdog.stop();
    const QByteArray tail = reply->readAll();
    result.bytesReceived += tail.size();
    if (result.body.size() < maximumResponseBytes) {
        result.body += tail.left(static_cast<int>(
            std::min<qint64>(tail.size(), maximumResponseBytes - result.body.size())));
    }
    result.elapsedMs = std::max<qint64>(1, elapsed.elapsed());
    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.cancelled = stopRequested.load(std::memory_order_relaxed);
    result.ok = !result.cancelled && !result.timedOut && !responseTooLarge
        && reply->error() == QNetworkReply::NoError
        && result.statusCode >= 200 && result.statusCode < 300;
    if (result.cancelled) result.error = QStringLiteral("операция отменена");
    else if (result.timedOut) result.error = QStringLiteral("истёк сетевой тайм-аут");
    else if (responseTooLarge) result.error = QStringLiteral("ответ превысил безопасный лимит");
    else if (reply->error() != QNetworkReply::NoError) result.error = reply->errorString();
    else if (!result.ok) result.error = QStringLiteral("HTTP %1").arg(result.statusCode);
    reply->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return result;
}

[[nodiscard]] QJsonObject runPublicIpLookup(
    const std::atomic_bool& stopRequested,
    const InternetToolsWorker::ProgressFunction& progress)
{
    const QString checkedAt = timestampNow();
    QNetworkAccessManager manager;
    progress(10, QStringLiteral("Запрос публичного IP через ipinfo.io…"));
    const HttpResult primary = performRequest(manager,
        networkRequest(QUrl(QStringLiteral("https://ipinfo.io/json"))),
        QByteArrayLiteral("GET"), {}, 6000, publicResponseLimit, stopRequested);
    if (primary.cancelled) {
        return failureResult(QStringLiteral("checked_at"), checkedAt,
            QStringLiteral("операция отменена"), true);
    }
    if (primary.ok) {
        QJsonObject parsed = InternetToolsWorker::parseIpInfoResponse(primary.body, checkedAt);
        if (parsed.value(QStringLiteral("ok")).toBool()) return parsed;
    }

    progress(55, QStringLiteral("Основной источник недоступен; проверка ip-api.com…"));
    const HttpResult fallback = performRequest(manager,
        networkRequest(QUrl(QStringLiteral("http://ip-api.com/json/"))),
        QByteArrayLiteral("GET"), {}, 6000, publicResponseLimit, stopRequested);
    if (fallback.cancelled) {
        return failureResult(QStringLiteral("checked_at"), checkedAt,
            QStringLiteral("операция отменена"), true);
    }
    if (fallback.ok) {
        QJsonObject parsed = InternetToolsWorker::parseIpApiResponse(fallback.body, checkedAt);
        if (parsed.value(QStringLiteral("ok")).toBool()) return parsed;
        return parsed;
    }
    return failureResult(QStringLiteral("checked_at"), checkedAt,
        fallback.error.isEmpty() ? primary.error : fallback.error);
}

[[nodiscard]] QJsonObject runSpeedTest(
    const std::atomic_bool& stopRequested,
    const InternetToolsWorker::ProgressFunction& progress)
{
    const QString testedAt = timestampNow();
    QNetworkAccessManager manager;
    QVector<double> pingSamples;
    progress(5, QStringLiteral("Проверка задержки до Cloudflare Edge…"));
    for (int attempt = 0; attempt < 3; ++attempt) {
        QUrl url(QStringLiteral("https://speed.cloudflare.com/__down"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("bytes"), QStringLiteral("0"));
        query.addQueryItem(QStringLiteral("orion"), QString::number(
            QRandomGenerator::global()->generate64()));
        url.setQuery(query);
        const HttpResult latency = performRequest(manager, networkRequest(url),
            QByteArrayLiteral("GET"), {}, 5000, 64 * 1024, stopRequested);
        if (latency.cancelled) {
            return failureResult(QStringLiteral("tested_at"), testedAt,
                QStringLiteral("операция отменена"), true);
        }
        if (latency.ok) pingSamples.push_back(static_cast<double>(latency.elapsedMs));
        progress(5 + (attempt + 1) * 5,
            QStringLiteral("Задержка: попытка %1/3").arg(attempt + 1));
    }
    if (pingSamples.isEmpty()) {
        return failureResult(QStringLiteral("tested_at"), testedAt,
            QStringLiteral("тестовый сервер не ответил на проверку задержки"));
    }

    progress(25, QStringLiteral("Загрузка контрольных 10 МБ…"));
    QUrl downloadUrl(QStringLiteral("https://speed.cloudflare.com/__down"));
    QUrlQuery downloadQuery;
    downloadQuery.addQueryItem(QStringLiteral("bytes"), QString::number(downloadTestBytes));
    downloadQuery.addQueryItem(QStringLiteral("orion"), QString::number(
        QRandomGenerator::global()->generate64()));
    downloadUrl.setQuery(downloadQuery);
    const HttpResult download = performRequest(manager, networkRequest(downloadUrl),
        QByteArrayLiteral("GET"), {}, 30000, downloadTestBytes + 64 * 1024,
        stopRequested, [&](const qint64 received, const qint64 total) {
            const qint64 expected = total > 0 ? total : downloadTestBytes;
            const int value = 25 + static_cast<int>(35.0
                * std::clamp<qint64>(received, 0, expected) / expected);
            progress(value, QStringLiteral("Загрузка: %1/%2 МБ")
                .arg(received / 1'000'000.0, 0, 'f', 1)
                .arg(expected / 1'000'000.0, 0, 'f', 1));
        });
    if (!download.ok || download.bytesReceived < downloadTestBytes * 9 / 10) {
        return failureResult(QStringLiteral("tested_at"), testedAt,
            download.cancelled ? QStringLiteral("операция отменена")
                               : download.error.isEmpty()
                    ? QStringLiteral("сервер вернул неполный объём загрузки") : download.error,
            download.cancelled);
    }

    progress(65, QStringLiteral("Отправка контрольных 5 МБ…"));
    QNetworkRequest uploadRequest = networkRequest(
        QUrl(QStringLiteral("https://speed.cloudflare.com/__up")));
    uploadRequest.setHeader(QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/octet-stream"));
    const QByteArray uploadPayload(static_cast<int>(uploadTestBytes), 'O');
    const HttpResult upload = performRequest(manager, uploadRequest,
        QByteArrayLiteral("POST"), uploadPayload, 30000, 256 * 1024,
        stopRequested, [&](const qint64 sent, const qint64 total) {
            const qint64 expected = total > 0 ? total : uploadTestBytes;
            const int value = 65 + static_cast<int>(30.0
                * std::clamp<qint64>(sent, 0, expected) / expected);
            progress(value, QStringLiteral("Отправка: %1/%2 МБ")
                .arg(sent / 1'000'000.0, 0, 'f', 1)
                .arg(expected / 1'000'000.0, 0, 'f', 1));
        });
    if (!upload.ok) {
        return failureResult(QStringLiteral("tested_at"), testedAt,
            upload.cancelled ? QStringLiteral("операция отменена") : upload.error,
            upload.cancelled);
    }
    progress(98, QStringLiteral("Расчёт результата…"));
    return InternetToolsWorker::speedResultFromMeasurements(
        download.bytesReceived, download.elapsedMs,
        uploadTestBytes, upload.elapsedMs, pingSamples, testedAt);
}

} // namespace

InternetToolsWorker::InternetToolsWorker(QObject* parent, OperationFunction operation)
    : QThread(parent)
    , operation_(std::move(operation))
{
    qRegisterMetaType<InternetOperation>();
}

InternetToolsWorker::~InternetToolsWorker()
{
    requestStop();
    wait(3000);
}

bool InternetToolsWorker::startPublicIpLookup()
{
    return startOperation(InternetOperation::PublicIpLookup);
}

bool InternetToolsWorker::startSpeedTest()
{
    return startOperation(InternetOperation::SpeedTest);
}

bool InternetToolsWorker::startOperation(const InternetOperation operation)
{
    if (isRunning()) return false;
    {
        const QMutexLocker lock(&mutex_);
        pendingOperation_ = operation;
    }
    stopRequested_.store(false, std::memory_order_relaxed);
    start();
    return true;
}

void InternetToolsWorker::requestStop()
{
    stopRequested_.store(true, std::memory_order_relaxed);
}

QJsonObject InternetToolsWorker::parseIpInfoResponse(
    const QByteArray& payload, const QString& checkedAt)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return failureResult(QStringLiteral("checked_at"), checkedAt,
            QStringLiteral("ipinfo.io вернул некорректный JSON"));
    }
    const QJsonObject object = document.object();
    QString provider = object.value(QStringLiteral("org")).toString().simplified();
    const qsizetype separator = provider.indexOf(QLatin1Char(' '));
    if (separator > 0 && provider.left(separator).startsWith(QStringLiteral("AS"))) {
        provider = provider.mid(separator + 1).simplified();
    }
    if (provider.isEmpty()) provider = QStringLiteral("н/д");
    const QString city = object.value(QStringLiteral("city")).toString().simplified();
    const QString country = object.value(QStringLiteral("country")).toString().simplified();
    QStringList locationParts;
    if (!city.isEmpty()) locationParts.push_back(city);
    if (!country.isEmpty()) locationParts.push_back(country);
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("public_ip"), textField(object, QStringLiteral("ip"))},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("location"), locationParts.isEmpty()
             ? QStringLiteral("н/д") : locationParts.join(QStringLiteral(", "))},
        {QStringLiteral("source"), QStringLiteral("ipinfo.io")},
        {QStringLiteral("checked_at"), checkedAt},
        {QStringLiteral("error"), QJsonValue::Null},
    };
}

QJsonObject InternetToolsWorker::parseIpApiResponse(
    const QByteArray& payload, const QString& checkedAt)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return failureResult(QStringLiteral("checked_at"), checkedAt,
            QStringLiteral("ip-api.com вернул некорректный JSON"));
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("status")).toString() != QStringLiteral("success")) {
        return failureResult(QStringLiteral("checked_at"), checkedAt,
            object.value(QStringLiteral("message")).toString(
                QStringLiteral("ip-api.com вернул ошибку")));
    }
    const QString city = object.value(QStringLiteral("city")).toString().simplified();
    const QString country = object.value(QStringLiteral("country")).toString().simplified();
    QStringList locationParts;
    if (!city.isEmpty()) locationParts.push_back(city);
    if (!country.isEmpty()) locationParts.push_back(country);
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("public_ip"), textField(object, QStringLiteral("query"))},
        {QStringLiteral("provider"), textField(object, QStringLiteral("isp"))},
        {QStringLiteral("location"), locationParts.isEmpty()
             ? QStringLiteral("н/д") : locationParts.join(QStringLiteral(", "))},
        {QStringLiteral("source"), QStringLiteral("ip-api.com")},
        {QStringLiteral("checked_at"), checkedAt},
        {QStringLiteral("error"), QJsonValue::Null},
    };
}

QJsonObject InternetToolsWorker::speedResultFromMeasurements(
    const qint64 downloadBytes,
    const qint64 downloadElapsedMs,
    const qint64 uploadBytes,
    const qint64 uploadElapsedMs,
    const QVector<double>& pingSamplesMs,
    const QString& testedAt)
{
    if (downloadBytes <= 0 || uploadBytes <= 0
        || downloadElapsedMs <= 0 || uploadElapsedMs <= 0) {
        return failureResult(QStringLiteral("tested_at"), testedAt,
            QStringLiteral("недостаточно данных для расчёта скорости"));
    }
    const auto roundedTenth = [](const double value) {
        return std::round(value * 10.0) / 10.0;
    };
    const double downloadMbps = roundedTenth(
        static_cast<double>(downloadBytes) * 8.0 / downloadElapsedMs / 1000.0);
    const double uploadMbps = roundedTenth(
        static_cast<double>(uploadBytes) * 8.0 / uploadElapsedMs / 1000.0);
    QVector<double> sortedPing = pingSamplesMs;
    std::sort(sortedPing.begin(), sortedPing.end());
    const double rawPing = sortedPing.isEmpty() ? -1.0
        : sortedPing.at(sortedPing.size() / 2);
    const bool pingValid = rawPing > 0.0 && rawPing <= maximumSanePingMs;
    const QString pingNote = pingValid ? QString() : QStringLiteral(
        "не удалось надёжно измерить задержку; скорость загрузки и отдачи рассчитана отдельно");
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("download_mbps"), downloadMbps},
        {QStringLiteral("upload_mbps"), uploadMbps},
        {QStringLiteral("ping_ms"), pingValid ? QJsonValue {roundedTenth(rawPing)}
             : QJsonValue {QJsonValue::Null}},
        {QStringLiteral("ping_note"), pingNote.isEmpty()
             ? QJsonValue {QJsonValue::Null} : QJsonValue {pingNote}},
        {QStringLiteral("server"), QJsonObject {
             {QStringLiteral("name"), QStringLiteral("Cloudflare Edge")},
             {QStringLiteral("sponsor"), QStringLiteral("Cloudflare")},
             {QStringLiteral("country"), QStringLiteral("автовыбор")},
         }},
        {QStringLiteral("method"), QStringLiteral("однопоточная оценка Cloudflare")},
        {QStringLiteral("download_bytes"), downloadBytes},
        {QStringLiteral("upload_bytes"), uploadBytes},
        {QStringLiteral("tested_at"), testedAt},
        {QStringLiteral("cancelled"), false},
        {QStringLiteral("error"), QJsonValue::Null},
    };
}

void InternetToolsWorker::run()
{
    InternetOperation operation;
    {
        const QMutexLocker lock(&mutex_);
        operation = pendingOperation_;
    }
    const ProgressFunction progress = [this, operation](const int percent,
                                          const QString& status) {
        emit progressChanged(operation, percent, status);
    };
    QJsonObject result;
    if (operation_) result = operation_(operation, stopRequested_, progress);
    else if (operation == InternetOperation::PublicIpLookup) {
        result = runPublicIpLookup(stopRequested_, progress);
    } else {
        result = runSpeedTest(stopRequested_, progress);
    }
    if (!result.contains(QStringLiteral("ok"))) {
        result = failureResult(operation == InternetOperation::PublicIpLookup
                ? QStringLiteral("checked_at") : QStringLiteral("tested_at"),
            timestampNow(), QStringLiteral("операция вернула неверный результат"));
    }
    emit progressChanged(operation, 100,
        result.value(QStringLiteral("ok")).toBool()
            ? QStringLiteral("Проверка завершена")
            : result.value(QStringLiteral("cancelled")).toBool()
                ? QStringLiteral("Проверка отменена") : QStringLiteral("Проверка не удалась"));
    emit resultReady(operation, result);
}

} // namespace orion::app
