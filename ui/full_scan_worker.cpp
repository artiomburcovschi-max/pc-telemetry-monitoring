#include "full_scan_worker.h"

#include "deep_scan_worker.h"
#include "hardware_inventory_worker.h"
#include "internet_tools_worker.h"
#include "orion/diagnostics/report_contract.h"
#include "scan_report_sections.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace orion::app
{
namespace
{

[[nodiscard]] QJsonValue optionalBool(const std::optional<bool>& value)
{
    return value ? QJsonValue{*value} : QJsonValue{QJsonValue::Null};
}

[[nodiscard]] std::pair<std::optional<bool>, std::optional<bool>> decodeProductState(
    const QJsonValue& value)
{
    bool ok = false;
    const qint64 numeric = value.isDouble() ? static_cast<qint64>(value.toDouble())
                                            : value.toString().toLongLong(&ok);
    if (!value.isDouble() && !ok)
        return {};
    const QString state = QStringLiteral("%1").arg(numeric, 6, 16, QLatin1Char('0')).right(6);
    const QString enabledByte = state.mid(2, 2);
    const QString updatedByte = state.mid(4, 2);
    std::optional<bool> enabled;
    std::optional<bool> updated;
    if (enabledByte == QStringLiteral("00") || enabledByte == QStringLiteral("01"))
        enabled = false;
    else if (enabledByte == QStringLiteral("10") || enabledByte == QStringLiteral("11"))
        enabled = true;
    if (updatedByte == QStringLiteral("00"))
        updated = true;
    else if (updatedByte == QStringLiteral("10"))
        updated = false;
    return {enabled, updated};
}

[[nodiscard]] QString stateText(const QJsonObject& report)
{
    const QString state = report.value(QStringLiteral("scan")).toObject().value(QStringLiteral("state")).toString();
    if (state == QStringLiteral("complete"))
        return QStringLiteral("все этапы выполнены; недоступные источники помечены явно");
    if (state == QStringLiteral("cancelled"))
        return QStringLiteral("остановлено, сохранён частичный отчёт");
    if (state == QStringLiteral("failed"))
        return QStringLiteral("ошибка сборщика, сохранён частичный отчёт");
    return QStringLiteral("часть этапов завершилась досрочно; отчёт неполный");
}

[[nodiscard]] QString boolState(const QJsonValue& value, const QString& yes, const QString& no)
{
    return value.isBool() ? (value.toBool() ? yes : no) : QStringLiteral("н/д");
}

} // namespace

FullScanWorker::FullScanWorker(QObject* parent) : FullScanWorker(executeNative, parent) {}

FullScanWorker::FullScanWorker(StepFunction execute, QObject* parent)
    : QThread(parent), execute_(std::move(execute))
{
    qRegisterMetaType<QJsonObject>();
}

bool FullScanWorker::startScan(const QJsonObject& snapshot, const QJsonObject& hardwareSeed,
                               const bool confirmed)
{
    if (!confirmed || isRunning() || !execute_)
        return false;
    snapshot_ = snapshot;
    hardwareSeed_ = hardwareSeed;
    cancelled_.store(false, std::memory_order_release);
    start();
    return true;
}

void FullScanWorker::requestStop() { cancelled_.store(true, std::memory_order_release); }

QJsonObject FullScanWorker::antivirusInfoFromJson(const QByteArray& payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || (!document.isArray() && !document.isObject()))
    {
        return {{QStringLiteral("supported"), true},
                {QStringLiteral("data_quality"), QStringLiteral("collector_error")},
                {QStringLiteral("products"), QJsonArray{}},
                {QStringLiteral("source"), QStringLiteral("Windows Security Center 2")},
                {QStringLiteral("note"), QStringLiteral("Центр безопасности вернул некорректные данные.")}};
    }
    QJsonArray rows = document.isArray() ? document.array() : QJsonArray{document.object()};
    QJsonArray products;
    for (const auto& value : rows)
    {
        const auto row = value.toObject();
        if (row.isEmpty())
            continue;
        const auto [enabled, updated] = decodeProductState(row.value(QStringLiteral("productState")));
        QString name = row.value(QStringLiteral("displayName")).toString().simplified();
        if (name.isEmpty())
            name = QStringLiteral("н/д");
        products.append(QJsonObject{{QStringLiteral("name"), name},
                                    {QStringLiteral("real_time_enabled"), optionalBool(enabled)},
                                    {QStringLiteral("up_to_date"), optionalBool(updated)},
                                    {QStringLiteral("product_state"), row.value(QStringLiteral("productState"))}});
    }
    const QString note = products.isEmpty()
        ? QStringLiteral("Центр безопасности Windows не сообщил ни об одном антивирусе. "
                         "Это может означать отсутствие зарегистрированного продукта или недоступность данных.")
        : QString{};
    return {{QStringLiteral("supported"), true},
            {QStringLiteral("data_quality"), QStringLiteral("valid")},
            {QStringLiteral("products"), products},
            {QStringLiteral("source"), QStringLiteral("Windows Security Center 2")},
            {QStringLiteral("note"), note.isEmpty() ? QJsonValue{QJsonValue::Null} : QJsonValue{note}}};
}

QJsonObject FullScanWorker::collectAntivirusInfo(const Cancel& cancelled)
{
#ifndef Q_OS_WIN
    Q_UNUSED(cancelled);
    return {{QStringLiteral("supported"), false},
            {QStringLiteral("data_quality"), QStringLiteral("unsupported")},
            {QStringLiteral("products"), QJsonArray{}},
            {QStringLiteral("source"), QStringLiteral("Windows Security Center 2")},
            {QStringLiteral("note"), QStringLiteral("Эта проверка доступна только через Центр безопасности Windows.")}};
#else
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (executable.isEmpty())
        return {{QStringLiteral("supported"), true},
                {QStringLiteral("data_quality"), QStringLiteral("collector_error")},
                {QStringLiteral("products"), QJsonArray{}},
                {QStringLiteral("source"), QStringLiteral("Windows Security Center 2")},
                {QStringLiteral("note"), QStringLiteral("PowerShell не найден; сведения антивируса не прочитаны.")}};
    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8;"
        "$rows=@(Get-CimInstance -Namespace 'root/SecurityCenter2' -ClassName 'AntiVirusProduct' "
        "| Select-Object displayName,productState,pathToSignedProductExe);"
        "ConvertTo-Json -InputObject $rows -Compress -Depth 4");
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(executable,
                  {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
                   QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
                   QStringLiteral("Bypass"), QStringLiteral("-Command"), script});
    if (!process.waitForStarted(3000))
        return {{QStringLiteral("supported"), true},
                {QStringLiteral("data_quality"), QStringLiteral("collector_error")},
                {QStringLiteral("products"), QJsonArray{}},
                {QStringLiteral("source"), QStringLiteral("Windows Security Center 2")},
                {QStringLiteral("note"), QStringLiteral("Не удалось запустить чтение Центра безопасности.")}};
    while (!process.waitForFinished(50))
    {
        if (cancelled())
        {
            process.terminate();
            if (!process.waitForFinished(1000))
                process.kill();
            return {{QStringLiteral("supported"), true},
                    {QStringLiteral("data_quality"), QStringLiteral("cancelled")},
                    {QStringLiteral("products"), QJsonArray{}},
                    {QStringLiteral("source"), QStringLiteral("Windows Security Center 2")},
                    {QStringLiteral("note"), QStringLiteral("Проверка антивируса отменена.")}};
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString error = QString::fromUtf8(process.readAllStandardError()).simplified();
        return {{QStringLiteral("supported"), true},
                {QStringLiteral("data_quality"), QStringLiteral("collector_error")},
                {QStringLiteral("products"), QJsonArray{}},
                {QStringLiteral("source"), QStringLiteral("Windows Security Center 2")},
                {QStringLiteral("note"), error.isEmpty()
                     ? QStringLiteral("Центр безопасности Windows недоступен.") : error}};
    }
    return antivirusInfoFromJson(process.readAllStandardOutput());
#endif
}

QJsonObject FullScanWorker::rateInternet(const QJsonObject& speed)
{
    if (!speed.value(QStringLiteral("ok")).toBool())
        return {{QStringLiteral("key"), QStringLiteral("unavailable")},
                {QStringLiteral("label"), QStringLiteral("н/д")},
                {QStringLiteral("explanation"),
                 QStringLiteral("Тест скорости завершился без надёжного результата; оценивать линию не из чего.")}};
    const double download = speed.value(QStringLiteral("download_mbps")).toDouble(-1.0);
    const double upload = speed.value(QStringLiteral("upload_mbps")).toDouble(-1.0);
    const bool pingKnown = speed.value(QStringLiteral("ping_ms")).isDouble();
    const double ping = pingKnown ? speed.value(QStringLiteral("ping_ms")).toDouble() : 0.0;
    QString key;
    QString label;
    if (download >= 50.0 && upload >= 10.0 && ping <= 40.0)
    {
        key = QStringLiteral("good");
        label = QStringLiteral("хороший");
    }
    else if (download >= 15.0 && upload >= 3.0 && ping <= 100.0)
    {
        key = QStringLiteral("average");
        label = QStringLiteral("средний");
    }
    else
    {
        key = QStringLiteral("weak");
        label = QStringLiteral("слабый");
    }
    const QString pingText = pingKnown ? QStringLiteral("%1 мс").arg(ping, 0, 'f', 1)
                                       : QStringLiteral("не определён");
    QString explanation = QStringLiteral("Скачивание %1 Мбит/с, отдача %2 Мбит/с, пинг %3.")
                              .arg(download, 0, 'f', 1).arg(upload, 0, 'f', 1).arg(pingText);
    if (!pingKnown)
        explanation += QStringLiteral(" Оценка построена только по скачиванию и отдаче.");
    return {{QStringLiteral("key"), key}, {QStringLiteral("label"), label},
            {QStringLiteral("explanation"), explanation}};
}

QJsonObject FullScanWorker::executeNative(const FullScanStep step, const QJsonObject& seed,
                                          const Cancel& cancelled, const Progress& progress)
{
    if (step == FullScanStep::Antivirus)
        return collectAntivirusInfo(cancelled);
    if (step == FullScanStep::DeepLocal)
    {
        DeepScanWorker worker;
        QJsonObject report;
        QObject::connect(&worker, &DeepScanWorker::progressChanged, &worker, progress,
                         Qt::DirectConnection);
        QObject::connect(&worker, &DeepScanWorker::logLine, &worker,
                         [&](const QString& line) { progress(-1, line); }, Qt::DirectConnection);
        QObject::connect(&worker, &DeepScanWorker::reportReady, &worker,
                         [&](const QJsonObject& value) { report = value; }, Qt::DirectConnection);
        worker.startScan(seed.value(QStringLiteral("snapshot")).toObject(),
                         seed.value(QStringLiteral("hardware_seed")).toObject(), true);
        while (!worker.wait(50))
        {
            if (cancelled())
                worker.requestStop();
        }
        return report;
    }

    InternetToolsWorker worker;
    QJsonObject result;
    const InternetOperation operation = step == FullScanStep::SpeedTest
        ? InternetOperation::SpeedTest : InternetOperation::PublicIpLookup;
    QObject::connect(&worker, &InternetToolsWorker::progressChanged, &worker,
                     [&](InternetOperation, const int value, const QString& text)
                     { progress(value, text); }, Qt::DirectConnection);
    QObject::connect(&worker, &InternetToolsWorker::resultReady, &worker,
                     [&](InternetOperation, const QJsonObject& value) { result = value; },
                     Qt::DirectConnection);
    if (operation == InternetOperation::SpeedTest)
        worker.startSpeedTest();
    else
        worker.startPublicIpLookup();
    while (!worker.wait(50))
    {
        if (cancelled())
            worker.requestStop();
    }
    return result;
}

void FullScanWorker::run()
{
    const Cancel cancelled = [this] { return cancelled_.load(std::memory_order_acquire); };
    QString failure;
    QStringList completed;
    int progressValue = 0;
    QJsonObject report;
    const auto execute = [&](const FullScanStep step, const int begin, const int end,
                             const QString& label, const QJsonObject& seed = QJsonObject{})
    {
        if (cancelled())
            return QJsonObject{};
        emit logLine(label);
        emit progressChanged(begin, label);
        const auto value = execute_(step, seed, cancelled,
                                    [&](const int percent, const QString& text)
                                    {
                                        if (percent >= 0)
                                        {
                                            progressValue = std::max(progressValue,
                                                begin + std::clamp(percent, 0, 100) * (end - begin) / 100);
                                            emit progressChanged(progressValue, text);
                                        }
                                        if (!text.isEmpty())
                                            emit logLine(text);
                                    });
        if (!cancelled())
        {
            progressValue = end;
            completed.append(label);
            emit progressChanged(end, label);
        }
        return value;
    };

    emit logLine(QStringLiteral("Полная проверка — свежая локальная диагностика, затем подтверждённые интернет-этапы."));
    try
    {
        report = execute(FullScanStep::DeepLocal, 0, 65,
                         QStringLiteral("Глубокая локальная диагностика"),
                         {{QStringLiteral("snapshot"), snapshot_},
                          {QStringLiteral("hardware_seed"), hardwareSeed_}});
        if (!cancelled())
            report.insert(QStringLiteral("antivirus"),
                          execute(FullScanStep::Antivirus, 65, 72,
                                  QStringLiteral("Антивирус Windows")));
        if (!cancelled())
            report.insert(QStringLiteral("speed_test"),
                          execute(FullScanStep::SpeedTest, 72, 94,
                                  QStringLiteral("Тест скорости интернета: 10 МБ ↓ / 5 МБ ↑")));
        if (!cancelled())
        {
            const auto publicIp = execute(FullScanStep::PublicIp, 94, 98,
                                          QStringLiteral("Публичный IP и провайдер"));
            report.insert(QStringLiteral("public_ip"), publicIp);
            auto network = report.value(QStringLiteral("network")).toObject();
            network.insert(QStringLiteral("public_ip"), publicIp);
            report.insert(QStringLiteral("network"), network);
        }
    }
    catch (const std::exception& error)
    {
        failure = QString::fromUtf8(error.what());
    }
    catch (...)
    {
        failure = QStringLiteral("Неизвестная ошибка сборщика");
    }

    report.insert(QStringLiteral("pc_rating"),
                  HardwareInventoryWorker::ratePc(report.value(QStringLiteral("hardware")).toObject()));
    report.insert(QStringLiteral("internet_rating"),
                  rateInternet(report.value(QStringLiteral("speed_test")).toObject()));
    const QString deepState = report.value(QStringLiteral("scan")).toObject().value(QStringLiteral("state")).toString();
    const bool interrupted = cancelled();
    const QString state = interrupted ? QStringLiteral("cancelled")
        : !failure.isEmpty() ? QStringLiteral("failed")
        : deepState == QStringLiteral("complete") ? QStringLiteral("complete")
                                                   : QStringLiteral("partial");
    report.insert(QStringLiteral("scan"),
                  QJsonObject{{QStringLiteral("kind"), QStringLiteral("full")},
                              {QStringLiteral("state"), state},
                              {QStringLiteral("error"), failure},
                              {QStringLiteral("completed_steps"), QJsonArray::fromStringList(completed)},
                              {QStringLiteral("includes_internet"), true},
                              {QStringLiteral("download_test_bytes"), 10'000'000},
                              {QStringLiteral("upload_test_bytes"), 5'000'000},
                              {QStringLiteral("fresh_results"), true},
                              {QStringLiteral("deep_state"), deepState}});
    if (!failure.isEmpty())
        emit logLine(QStringLiteral("Ошибка: %1").arg(failure));
    if (!interrupted && failure.isEmpty())
        emit progressChanged(100, state == QStringLiteral("complete")
            ? QStringLiteral("Полный отчёт сформирован")
            : QStringLiteral("Проверка завершена частично; причины указаны в отчёте"));
    emit reportReady(report);
}

QString FullScanWorker::reportText(const QJsonObject& report)
{
    const auto scan = report.value(QStringLiteral("scan")).toObject();
    QString text = QStringLiteral("O.R.I.O.N. — ПОЛНАЯ ПРОВЕРКА\nСформирован: %1\nСостояние: %2\n"
                                  "Интернет-этапы: выполнены только после явного подтверждения пользователя.\n\n")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), stateText(report));
    if (!scan.value(QStringLiteral("error")).toString().isEmpty())
        text += QStringLiteral("Ошибка: %1\n").arg(scan.value(QStringLiteral("error")).toString());
    text += orion::diagnostics::reportToText(report);

    const auto pc = report.value(QStringLiteral("pc_rating")).toObject();
    text += QStringLiteral("\n=== Оценка ПК (ориентировочная) ===\nСтатус: %1\n")
                .arg(pc.value(QStringLiteral("label")).toString(QStringLiteral("недостаточно данных")));
    for (const auto& value : pc.value(QStringLiteral("reasons")).toArray())
        text += QStringLiteral("  - %1\n").arg(value.toString());

    const auto internet = report.value(QStringLiteral("internet_rating")).toObject();
    const auto speed = report.value(QStringLiteral("speed_test")).toObject();
    const auto ip = report.value(QStringLiteral("public_ip")).toObject();
    text += QStringLiteral("\n=== Интернет ===\nСтатус: %1\n%2\n")
                .arg(internet.value(QStringLiteral("label")).toString(QStringLiteral("н/д")),
                     internet.value(QStringLiteral("explanation")).toString());
    if (speed.value(QStringLiteral("ok")).toBool())
    {
        const QString ping = speed.value(QStringLiteral("ping_ms")).isDouble()
            ? QStringLiteral("%1 мс").arg(speed.value(QStringLiteral("ping_ms")).toDouble(), 0, 'f', 1)
            : QStringLiteral("н/д");
        text += QStringLiteral("Скорость: ↓ %1 / ↑ %2 Мбит/с, пинг %3.\n")
                    .arg(speed.value(QStringLiteral("download_mbps")).toDouble(), 0, 'f', 1)
                    .arg(speed.value(QStringLiteral("upload_mbps")).toDouble(), 0, 'f', 1)
                    .arg(ping);
    }
    else
        text += QStringLiteral("Тест скорости: %1\n")
                    .arg(speed.value(QStringLiteral("error")).toString(QStringLiteral("нет данных")));
    if (ip.value(QStringLiteral("ok")).toBool())
        text += QStringLiteral("Публичный IP: %1 — %2 (%3).\n")
                    .arg(ip.value(QStringLiteral("public_ip")).toString(QStringLiteral("н/д")),
                         ip.value(QStringLiteral("provider")).toString(QStringLiteral("н/д")),
                         ip.value(QStringLiteral("location")).toString(QStringLiteral("н/д")));
    else
        text += QStringLiteral("Публичный IP: %1\n")
                    .arg(ip.value(QStringLiteral("error")).toString(QStringLiteral("нет данных")));

    const auto antivirus = report.value(QStringLiteral("antivirus")).toObject();
    text += QStringLiteral("\n=== Антивирус ===\nИсточник: %1 · качество: %2\n")
                .arg(antivirus.value(QStringLiteral("source")).toString(QStringLiteral("н/д")),
                     antivirus.value(QStringLiteral("data_quality")).toString(QStringLiteral("unknown")));
    const auto products = antivirus.value(QStringLiteral("products")).toArray();
    if (products.isEmpty())
        text += antivirus.value(QStringLiteral("note")).toString(QStringLiteral("Нет надёжных данных.")) + QLatin1Char('\n');
    for (const auto& value : products)
    {
        const auto product = value.toObject();
        text += QStringLiteral("  %1 — защита: %2, базы: %3\n")
                    .arg(product.value(QStringLiteral("name")).toString(QStringLiteral("н/д")),
                         boolState(product.value(QStringLiteral("real_time_enabled")),
                                   QStringLiteral("включена"), QStringLiteral("выключена")),
                         boolState(product.value(QStringLiteral("up_to_date")),
                                   QStringLiteral("актуальны"), QStringLiteral("устарели")));
    }
    text += formatExtendedScanSections(report);
    return text;
}

} // namespace orion::app
