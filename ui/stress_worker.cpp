#include "stress_worker.h"

#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/platform/system_backend.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryFile>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <ranges>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace orion::app {
namespace {

constexpr double cpuSafetyC = 100.0;
constexpr double gpuSafetyC = 92.0;

[[nodiscard]] double average(const std::vector<double>& values)
{
    return values.empty() ? 0.0
                          : std::accumulate(values.begin(), values.end(), 0.0)
            / static_cast<double>(values.size());
}

[[nodiscard]] QByteArray workloadBlock(const qsizetype size)
{
    QByteArray block(size, Qt::Uninitialized);
    std::uint32_t state = 0x12345678U;
    for (qsizetype index = 0; index < size; ++index) {
        state = state * 1664525U + 1013904223U;
        block[index] = static_cast<char>(state >> 24U);
    }
    return block;
}

#ifdef _WIN32
template<typename T>
class ComHandle {
public:
    ~ComHandle() { reset(); }
    ComHandle() = default;
    ComHandle(const ComHandle&) = delete;
    ComHandle& operator=(const ComHandle&) = delete;
    [[nodiscard]] T* get() const noexcept { return value_; }
    [[nodiscard]] T** put() noexcept { reset(); return &value_; }
    [[nodiscard]] T* operator->() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
    void reset(T* value = nullptr) noexcept
    {
        if (value_ != nullptr) value_->Release();
        value_ = value;
    }
private:
    T* value_ {nullptr};
};
#endif

} // namespace

StressWorker::StressWorker(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<QJsonObject>();
}

void StressWorker::startTest(const StressOptions& options)
{
    if (isRunning()) return;
    {
        const QMutexLocker locker(&mutex_);
        options_ = options;
        options_.durationSeconds = std::clamp(options_.durationSeconds, 1, 300);
        options_.diskSizeMiB = std::clamp(options_.diskSizeMiB, 1, 500);
    }
    safetyReason_.clear();
    stopRequested_.store(false, std::memory_order_release);
    userStopRequested_.store(false, std::memory_order_release);
    start();
}

void StressWorker::requestStop()
{
    userStopRequested_.store(true, std::memory_order_release);
    stopRequested_.store(true, std::memory_order_release);
}

void StressWorker::emitPhaseProgress(
    const int phase,
    const int phases,
    const double fraction,
    const QString& status)
{
    const double overall = (static_cast<double>(phase) + std::clamp(fraction, 0.0, 1.0))
        / static_cast<double>(std::max(phases, 1));
    emit progressChanged(qRound(overall * 100.0), status);
}

void StressWorker::run()
{
    StressOptions options;
    {
        const QMutexLocker locker(&mutex_);
        options = options_;
    }
    const int phases = static_cast<int>(options.runCpu)
        + static_cast<int>(options.runGpu) + static_cast<int>(options.runDisk);
    QJsonObject report {
        {QStringLiteral("schema_version"), 3},
        {QStringLiteral("run_cpu"), options.runCpu},
        {QStringLiteral("run_gpu"), options.runGpu},
        {QStringLiteral("run_disk"), options.runDisk},
        {QStringLiteral("cpu_worker_failures"), 0},
        {QStringLiteral("cpu_throttling_suspected"), false},
        {QStringLiteral("gpu_throttling_suspected"), false},
        {QStringLiteral("disk_target_is_memory_fs"), false},
        {QStringLiteral("new_system_errors"), QJsonArray {}},
        {QStringLiteral("system_errors_checked_after"), false},
    };
    int phase = 0;
    const QJsonObject systemErrorsBefore = phases > 0
        ? orion::diagnostics::collectSystemErrorReport(100)
        : QJsonObject {};
    report.insert(QStringLiteral("system_errors_before_note"),
        systemErrorsBefore.value(QStringLiteral("note")));
    if (options.runCpu && !stopRequested_.load(std::memory_order_acquire)) {
        runCpu(report, phase++, phases);
    }
    if (options.runGpu && !stopRequested_.load(std::memory_order_acquire)) {
        runGpu(report, phase++, phases);
    }
    if (options.runDisk && !stopRequested_.load(std::memory_order_acquire)) {
        runDisk(report, phase++, phases);
    }
    if (phases > 0) {
        const auto systemErrorsAfter = orion::diagnostics::collectSystemErrorReport(100);
        const auto delta = orion::diagnostics::diffSystemErrorReports(
            systemErrorsBefore, systemErrorsAfter);
        report.insert(QStringLiteral("system_errors_after_note"),
            systemErrorsAfter.value(QStringLiteral("note")));
        report.insert(QStringLiteral("system_errors_comparison_quality"),
            delta.value(QStringLiteral("data_quality")));
        report.insert(QStringLiteral("new_system_errors"),
            delta.value(QStringLiteral("errors")));
        report.insert(QStringLiteral("system_errors_checked_after"),
            delta.value(QStringLiteral("comparison_supported")).toBool());
    }
    const bool stopped = stopRequested_.load(std::memory_order_acquire);
    report.insert(QStringLiteral("stopped_early"), stopped);
    if (!safetyReason_.isEmpty()) {
        report.insert(QStringLiteral("stop_reason"), QStringLiteral("safety"));
        report.insert(QStringLiteral("safety_stop_reason"), safetyReason_);
    } else if (userStopRequested_.load(std::memory_order_acquire)) {
        report.insert(QStringLiteral("stop_reason"), QStringLiteral("user"));
    } else {
        report.insert(QStringLiteral("stop_reason"), QJsonValue {QJsonValue::Null});
        emit progressChanged(100, QStringLiteral("Тест завершён"));
    }
    emit testCompleted(report);
}

void StressWorker::runCpu(QJsonObject& report, const int phase, const int phases)
{
    StressOptions options;
    { const QMutexLocker locker(&mutex_); options = options_; }
    const unsigned workerCount = std::max(1U, std::thread::hardware_concurrency());
    std::vector<std::atomic<std::uint64_t>> batches(workerCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    std::atomic_bool localStop {false};
    for (unsigned index = 0; index < workerCount; ++index) {
        workers.emplace_back([&, index] {
            std::uint32_t state = 0x12345678U + index;
            while (!localStop.load(std::memory_order_relaxed)
                && !stopRequested_.load(std::memory_order_relaxed)) {
                for (int iteration = 0; iteration < 200000; ++iteration) {
                    state = state * 1664525U + 1013904223U;
                    state ^= state >> 13U;
                }
                batches[index].fetch_add(1, std::memory_order_relaxed);
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        });
    }

    auto backend = orion::platform::makeSystemBackend();
    QElapsedTimer timer;
    timer.start();
    std::vector<double> loads;
    std::vector<double> frequencies;
    std::vector<double> temperatures;
    int hotSamples = 0;
    while (!stopRequested_.load(std::memory_order_acquire)
        && timer.elapsed() < options.durationSeconds * 1000LL) {
        const auto data = backend->sample(std::chrono::milliseconds {120});
        if (data.cpuUsagePercent.value.has_value()) loads.push_back(*data.cpuUsagePercent.value);
        if (data.cpuFrequencyMhz.value.has_value()) frequencies.push_back(*data.cpuFrequencyMhz.value);
        if (data.cpuTemperatureC.value.has_value()) {
            temperatures.push_back(*data.cpuTemperatureC.value);
            hotSamples = *data.cpuTemperatureC.value >= cpuSafetyC ? hotSamples + 1 : 0;
            if (hotSamples >= 2) {
                safetyReason_ = QStringLiteral("CPU достиг %1°C в двух последовательных замерах; нагрузка остановлена защитой.")
                    .arg(*data.cpuTemperatureC.value, 0, 'f', 1);
                report.insert(QStringLiteral("cpu_safety_stop_reason"), safetyReason_);
                stopRequested_.store(true, std::memory_order_release);
            }
        }
        emitPhaseProgress(phase, phases,
            static_cast<double>(timer.elapsed()) / (options.durationSeconds * 1000.0),
            QStringLiteral("CPU: %1 / %2 сек · нагрузка %3% · %4")
                .arg(timer.elapsed() / 1000.0, 0, 'f', 1)
                .arg(options.durationSeconds)
                .arg(data.cpuUsagePercent.value.value_or(-1.0), 0, 'f', 0)
                .arg(data.cpuTemperatureC.value.has_value()
                    ? QStringLiteral("%1°C").arg(*data.cpuTemperatureC.value, 0, 'f', 1)
                    : QStringLiteral("температура н/д")));
    }
    localStop.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    const auto actualSeconds = timer.elapsed() / 1000.0;
    std::uint64_t totalBatches = 0;
    for (const auto& counter : batches) totalBatches += counter.load();
    report.insert(QStringLiteral("cpu_workers"), static_cast<int>(workerCount));
    report.insert(QStringLiteral("cpu_actual_seconds"), actualSeconds);
    report.insert(QStringLiteral("cpu_load_sample_count"), static_cast<int>(loads.size()));
    report.insert(QStringLiteral("cpu_load_avg_percent"), loads.empty() ? QJsonValue {QJsonValue::Null} : QJsonValue {average(loads)});
    report.insert(QStringLiteral("cpu_load_peak_percent"), loads.empty() ? QJsonValue {QJsonValue::Null} : QJsonValue {*std::ranges::max_element(loads)});
    report.insert(QStringLiteral("cpu_avg_usage_percent"), report.value(QStringLiteral("cpu_load_avg_percent")));
    report.insert(QStringLiteral("cpu_peak_usage_percent"), report.value(QStringLiteral("cpu_load_peak_percent")));
    report.insert(QStringLiteral("cpu_frequency_sample_count"), static_cast<int>(frequencies.size()));
    report.insert(QStringLiteral("cpu_avg_freq_mhz"), frequencies.empty() ? QJsonValue {QJsonValue::Null} : QJsonValue {average(frequencies)});
    report.insert(QStringLiteral("cpu_min_freq_mhz"), frequencies.empty() ? QJsonValue {QJsonValue::Null} : QJsonValue {*std::ranges::min_element(frequencies)});
    report.insert(QStringLiteral("cpu_max_freq_mhz"), frequencies.empty() ? QJsonValue {QJsonValue::Null} : QJsonValue {*std::ranges::max_element(frequencies)});
    report.insert(QStringLiteral("cpu_temperature_sample_count"), static_cast<int>(temperatures.size()));
    report.insert(QStringLiteral("cpu_max_temp_c"), temperatures.empty() ? QJsonValue {QJsonValue::Null} : QJsonValue {*std::ranges::max_element(temperatures)});
    report.insert(QStringLiteral("cpu_workload_sample_count"), static_cast<int>(loads.size()));
    report.insert(QStringLiteral("cpu_workload_batches_per_sec"), actualSeconds > 0.0
        ? QJsonValue {static_cast<double>(totalBatches) / actualSeconds} : QJsonValue {QJsonValue::Null});
}

void StressWorker::runDisk(QJsonObject& report, const int phase, const int phases)
{
    StressOptions options;
    { const QMutexLocker locker(&mutex_); options = options_; }
    QString directory = options.diskDirectory;
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/stress");
    }
    QDir().mkpath(directory);
    QTemporaryFile file(QDir(directory).filePath(QStringLiteral("orion_stress_XXXXXX.tmp")));
    file.setAutoRemove(true);
    if (!file.open()) {
        report.insert(QStringLiteral("disk_data_verified"), QJsonValue {QJsonValue::Null});
        report.insert(QStringLiteral("disk_error"), file.errorString());
        return;
    }
    const QStorageInfo storage(file.fileName());
    report.insert(QStringLiteral("disk_test_directory"), directory);
    report.insert(QStringLiteral("disk_target_mountpoint"), storage.rootPath());
    report.insert(QStringLiteral("disk_mountpoint"), storage.rootPath());
    report.insert(QStringLiteral("disk_target_device"), QString::fromUtf8(storage.device()));
    report.insert(QStringLiteral("disk_target_fstype"), QString::fromUtf8(storage.fileSystemType()));
    const qint64 totalBytes = static_cast<qint64>(options.diskSizeMiB) * 1024 * 1024;
    const QByteArray block = workloadBlock(std::min<qint64>(8 * 1024 * 1024, totalBytes));
    QCryptographicHash writeHash(QCryptographicHash::Sha256);
    QCryptographicHash readHash(QCryptographicHash::Sha256);
    qint64 written = 0;
    QElapsedTimer timer;
    timer.start();
    while (written < totalBytes && !stopRequested_.load(std::memory_order_acquire)) {
        const qint64 amount = std::min<qint64>(block.size(), totalBytes - written);
        const qint64 result = file.write(block.constData(), amount);
        if (result != amount) break;
        writeHash.addData(QByteArrayView(block.constData(), amount));
        written += amount;
        emitPhaseProgress(phase, phases, 0.5 * static_cast<double>(written) / totalBytes,
            QStringLiteral("Диск: запись %1 / %2 МБ")
                .arg(written / (1024 * 1024)).arg(options.diskSizeMiB));
    }
    file.flush();
#ifdef _WIN32
    const bool flushed = FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()))) != FALSE;
#else
    const bool flushed = ::fsync(file.handle()) == 0;
#endif
    const double writeSeconds = timer.elapsed() / 1000.0;
    const double writeMiBps = writeSeconds > 0.0
        ? static_cast<double>(written) / (1024.0 * 1024.0) / writeSeconds : 0.0;
    qint64 read = 0;
    double readSeconds = 0.0;
    if (!stopRequested_.load(std::memory_order_acquire) && file.seek(0)) {
        timer.restart();
        while (read < written && !stopRequested_.load(std::memory_order_acquire)) {
            const QByteArray data = file.read(std::min<qint64>(block.size(), written - read));
            if (data.isEmpty()) break;
            readHash.addData(data);
            read += data.size();
            emitPhaseProgress(phase, phases, 0.5 + 0.5 * static_cast<double>(read) / written,
                QStringLiteral("Диск: чтение и проверка %1 / %2 МБ")
                    .arg(read / (1024 * 1024)).arg(written / (1024 * 1024)));
        }
        readSeconds = timer.elapsed() / 1000.0;
    }
    file.close();
    report.insert(QStringLiteral("disk_actual_mb"), static_cast<double>(written) / (1024.0 * 1024.0));
    report.insert(QStringLiteral("disk_write_mbps"), writeMiBps);
    report.insert(QStringLiteral("disk_read_mbps"), readSeconds > 0.0
        ? QJsonValue {static_cast<double>(read) / (1024.0 * 1024.0) / readSeconds}
        : QJsonValue {QJsonValue::Null});
    report.insert(QStringLiteral("disk_fsync_applied"), flushed);
    report.insert(QStringLiteral("disk_fsync_performed"), flushed);
    report.insert(QStringLiteral("disk_read_cache_possible"), true);
    report.insert(QStringLiteral("disk_data_verified"),
        read > 0 ? QJsonValue {read == written && writeHash.result() == readHash.result()}
                 : QJsonValue {QJsonValue::Null});
}

void StressWorker::runGpu(QJsonObject& report, const int phase, const int phases)
{
#ifndef _WIN32
    Q_UNUSED(phase)
    Q_UNUSED(phases)
    report.insert(QStringLiteral("gpu_supported"), false);
    report.insert(QStringLiteral("gpu_backend"), QStringLiteral("unsupported"));
    report.insert(QStringLiteral("gpu_worker_error"),
        QStringLiteral("Нативный GPU stress backend этого Linux checkpoint ещё не реализован"));
#else
    StressOptions options;
    { const QMutexLocker locker(&mutex_); options = options_; }
    ComHandle<ID3D11Device> device;
    ComHandle<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel {};
    const auto created = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, device.put(), &featureLevel, context.put());
    if (FAILED(created)) {
        report.insert(QStringLiteral("gpu_supported"), false);
        report.insert(QStringLiteral("gpu_backend"), QStringLiteral("Direct3D 11"));
        report.insert(QStringLiteral("gpu_worker_error"), QStringLiteral("Не удалось создать аппаратное D3D11-устройство"));
        return;
    }
    constexpr char shaderSource[] = R"(
        RWStructuredBuffer<float4> values : register(u0);
        [numthreads(256, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID) {
            float4 v = values[id.x];
            [loop] for (uint i = 0; i < 96; ++i) {
                v = frac(v * float4(1.6180339, 1.4142135, 1.7320508, 2.2360679)
                    + float4(0.1031, 0.11369, 0.13787, 0.09987));
                v = sqrt(abs(v + v.wxyz * 0.071));
            }
            values[id.x] = v;
        })";
    ComHandle<ID3DBlob> bytecode;
    ComHandle<ID3DBlob> errors;
    const auto compiled = D3DCompile(shaderSource, sizeof(shaderSource) - 1, "orion_stress",
        nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        bytecode.put(), errors.put());
    if (FAILED(compiled)) {
        report.insert(QStringLiteral("gpu_supported"), false);
        report.insert(QStringLiteral("gpu_worker_error"), errors
            ? QString::fromUtf8(static_cast<const char*>(errors->GetBufferPointer()),
                static_cast<qsizetype>(errors->GetBufferSize()))
            : QStringLiteral("Не удалось скомпилировать D3D11 compute shader"));
        return;
    }
    ComHandle<ID3D11ComputeShader> shader;
    if (FAILED(device->CreateComputeShader(bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(), nullptr, shader.put()))) {
        report.insert(QStringLiteral("gpu_supported"), false);
        report.insert(QStringLiteral("gpu_worker_error"), QStringLiteral("D3D11 compute shader недоступен"));
        return;
    }
    constexpr UINT elementCount = 262144;
    struct Value { float values[4]; };
    std::vector<Value> initial(elementCount);
    for (UINT index = 0; index < elementCount; ++index) {
        initial[index] = {{0.1F + index * 0.000001F, 0.2F, 0.3F, 0.4F}};
    }
    D3D11_BUFFER_DESC desc {};
    desc.ByteWidth = static_cast<UINT>(initial.size() * sizeof(Value));
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(Value);
    D3D11_SUBRESOURCE_DATA initData {initial.data(), 0, 0};
    ComHandle<ID3D11Buffer> buffer;
    if (FAILED(device->CreateBuffer(&desc, &initData, buffer.put()))) {
        report.insert(QStringLiteral("gpu_supported"), false);
        report.insert(QStringLiteral("gpu_worker_error"), QStringLiteral("Не удалось создать GPU workload buffer"));
        return;
    }
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = elementCount;
    ComHandle<ID3D11UnorderedAccessView> uav;
    if (FAILED(device->CreateUnorderedAccessView(buffer.get(), &uavDesc, uav.put()))) {
        report.insert(QStringLiteral("gpu_supported"), false);
        report.insert(QStringLiteral("gpu_worker_error"), QStringLiteral("Не удалось создать D3D11 UAV"));
        return;
    }
    context->CSSetShader(shader.get(), nullptr, 0);
    ID3D11UnorderedAccessView* views[] {uav.get()};
    context->CSSetUnorderedAccessViews(0, 1, views, nullptr);
    auto backend = orion::platform::makeSystemBackend();
    QElapsedTimer timer;
    timer.start();
    std::uint64_t dispatches = 0;
    std::vector<double> usages;
    std::vector<double> temperatures;
    int hotSamples = 0;
    while (!stopRequested_.load(std::memory_order_acquire)
        && timer.elapsed() < options.durationSeconds * 1000LL) {
        for (int repeat = 0; repeat < 24; ++repeat) {
            context->Dispatch(elementCount / 256, 1, 1);
            ++dispatches;
        }
        context->Flush();
        const auto data = backend->sample(std::chrono::milliseconds {80});
        if (data.gpuUsagePercent.value.has_value()) usages.push_back(*data.gpuUsagePercent.value);
        if (data.gpuTemperatureC.value.has_value()) {
            temperatures.push_back(*data.gpuTemperatureC.value);
            hotSamples = *data.gpuTemperatureC.value >= gpuSafetyC ? hotSamples + 1 : 0;
            if (hotSamples >= 2) {
                safetyReason_ = QStringLiteral("GPU достиг %1°C в двух последовательных замерах; нагрузка остановлена защитой.")
                    .arg(*data.gpuTemperatureC.value, 0, 'f', 1);
                report.insert(QStringLiteral("gpu_safety_stop_reason"), safetyReason_);
                stopRequested_.store(true, std::memory_order_release);
            }
        }
        emitPhaseProgress(phase, phases,
            static_cast<double>(timer.elapsed()) / (options.durationSeconds * 1000.0),
            QStringLiteral("GPU D3D11: %1 / %2 сек · загрузка %3% · %4")
                .arg(timer.elapsed() / 1000.0, 0, 'f', 1)
                .arg(options.durationSeconds)
                .arg(data.gpuUsagePercent.value.value_or(-1.0), 0, 'f', 0)
                .arg(data.gpuTemperatureC.value.has_value()
                    ? QStringLiteral("%1°C").arg(*data.gpuTemperatureC.value, 0, 'f', 1)
                    : QStringLiteral("температура н/д")));
    }
    ID3D11UnorderedAccessView* nullView[] {nullptr};
    context->CSSetUnorderedAccessViews(0, 1, nullView, nullptr);
    context->CSSetShader(nullptr, nullptr, 0);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    ComHandle<ID3D11Buffer> staging;
    bool verified = false;
    if (SUCCEEDED(device->CreateBuffer(&desc, nullptr, staging.put()))) {
        context->CopyResource(staging.get(), buffer.get());
        D3D11_MAPPED_SUBRESOURCE mapped {};
        if (SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            const auto* output = static_cast<const Value*>(mapped.pData);
            verified = output != nullptr && std::isfinite(output[0].values[0])
                && std::abs(output[0].values[0] - initial[0].values[0]) > 0.00001F;
            context->Unmap(staging.get(), 0);
        }
    }
    const double seconds = timer.elapsed() / 1000.0;
    report.insert(QStringLiteral("gpu_supported"), true);
    report.insert(QStringLiteral("gpu_backend"), QStringLiteral("Direct3D 11 compute shader"));
    report.insert(QStringLiteral("gpu_renderer"), QStringLiteral("D3D11 hardware feature level 0x%1")
        .arg(static_cast<unsigned>(featureLevel), 0, 16));
    report.insert(QStringLiteral("gpu_hardware_renderer"), true);
    report.insert(QStringLiteral("gpu_actual_seconds"), seconds);
    report.insert(QStringLiteral("gpu_frames"), static_cast<double>(dispatches));
    report.insert(QStringLiteral("gpu_fps"), seconds > 0.0 ? dispatches / seconds : 0.0);
    report.insert(QStringLiteral("gpu_output_verified"), verified);
    report.insert(QStringLiteral("gpu_load_sample_count"), static_cast<int>(usages.size()));
    report.insert(QStringLiteral("gpu_usage_sample_count"), static_cast<int>(usages.size()));
    report.insert(QStringLiteral("gpu_usage_avg_percent"), usages.empty() ? QJsonValue {QJsonValue::Null} : QJsonValue {average(usages)});
    report.insert(QStringLiteral("gpu_usage_peak_percent"), usages.empty() ? QJsonValue {QJsonValue::Null} : QJsonValue {*std::ranges::max_element(usages)});
    report.insert(QStringLiteral("gpu_temperature_sample_count"), static_cast<int>(temperatures.size()));
    report.insert(QStringLiteral("gpu_max_temp_c"), temperatures.empty() ? QJsonValue {QJsonValue::Null} : QJsonValue {*std::ranges::max_element(temperatures)});
#endif
}

} // namespace orion::app
