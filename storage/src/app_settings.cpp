#include "orion/storage/app_settings.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>

namespace orion::storage {
namespace {

template<typename T>
void readIf(const QJsonObject& object, const QString& key, T& target);

template<>
void readIf<bool>(const QJsonObject& object, const QString& key, bool& target)
{
    if (object.value(key).isBool()) {
        target = object.value(key).toBool();
    }
}

template<>
void readIf<int>(const QJsonObject& object, const QString& key, int& target)
{
    if (object.value(key).isDouble()) {
        target = object.value(key).toInt(target);
    }
}

template<>
void readIf<double>(const QJsonObject& object, const QString& key, double& target)
{
    if (object.value(key).isDouble()) {
        target = object.value(key).toDouble(target);
    }
}

template<>
void readIf<QString>(const QJsonObject& object, const QString& key, QString& target)
{
    if (object.value(key).isString()) {
        target = object.value(key).toString();
    }
}

} // namespace

QJsonObject AppSettings::toJson() const
{
    return {
        {QStringLiteral("theme_key"), themeKey},
        {QStringLiteral("MIN_WIDTH"), minimumWidth},
        {QStringLiteral("MIN_HEIGHT"), minimumHeight},
        {QStringLiteral("MAX_WIDTH"), maximumWidth},
        {QStringLiteral("MAX_HEIGHT"), maximumHeight},
        {QStringLiteral("terminal_enabled"), terminalEnabled},
        {QStringLiteral("server_tab_enabled"), serverTabEnabled},
        {QStringLiteral("window_opacity"), windowOpacity},
        {QStringLiteral("always_on_top"), alwaysOnTop},
        {QStringLiteral("free_form_resize"), freeFormResize},
        {QStringLiteral("alert_freeze_disabled"), alertFreezeDisabled},
        {QStringLiteral("tray_icon_enabled"), trayIconEnabled},
        {QStringLiteral("tray_notifications_enabled"), trayNotificationsEnabled},
        {QStringLiteral("ping_target"), pingTarget},
        {QStringLiteral("card_cpu_visible"), cardCpuVisible},
        {QStringLiteral("card_gpu_visible"), cardGpuVisible},
        {QStringLiteral("card_ram_visible"), cardRamVisible},
        {QStringLiteral("card_disk_visible"), cardDiskVisible},
        {QStringLiteral("card_net_visible"), cardNetVisible},
        {QStringLiteral("card_dragdrop_enabled"), cardDragDropEnabled},
        {QStringLiteral("card_dock_state"), cardDockState},
        {QStringLiteral("diag_hub_dock_state"), diagnosticHubDockState},
        {QStringLiteral("cpu_core_chart_mode"), cpuCoreChartMode},
        {QStringLiteral("custom_theme_colors"), customThemeColors},
    };
}

AppSettings AppSettings::fromJson(const QJsonObject& object)
{
    AppSettings settings;
    readIf(object, QStringLiteral("theme_key"), settings.themeKey);
    readIf(object, QStringLiteral("MIN_WIDTH"), settings.minimumWidth);
    readIf(object, QStringLiteral("MIN_HEIGHT"), settings.minimumHeight);
    readIf(object, QStringLiteral("MAX_WIDTH"), settings.maximumWidth);
    readIf(object, QStringLiteral("MAX_HEIGHT"), settings.maximumHeight);
    readIf(object, QStringLiteral("terminal_enabled"), settings.terminalEnabled);
    readIf(object, QStringLiteral("server_tab_enabled"), settings.serverTabEnabled);
    readIf(object, QStringLiteral("window_opacity"), settings.windowOpacity);
    readIf(object, QStringLiteral("always_on_top"), settings.alwaysOnTop);
    readIf(object, QStringLiteral("free_form_resize"), settings.freeFormResize);
    readIf(object, QStringLiteral("alert_freeze_disabled"), settings.alertFreezeDisabled);
    readIf(object, QStringLiteral("tray_icon_enabled"), settings.trayIconEnabled);
    readIf(object, QStringLiteral("tray_notifications_enabled"), settings.trayNotificationsEnabled);
    readIf(object, QStringLiteral("ping_target"), settings.pingTarget);
    readIf(object, QStringLiteral("card_cpu_visible"), settings.cardCpuVisible);
    readIf(object, QStringLiteral("card_gpu_visible"), settings.cardGpuVisible);
    readIf(object, QStringLiteral("card_ram_visible"), settings.cardRamVisible);
    readIf(object, QStringLiteral("card_disk_visible"), settings.cardDiskVisible);
    readIf(object, QStringLiteral("card_net_visible"), settings.cardNetVisible);
    readIf(object, QStringLiteral("card_dragdrop_enabled"), settings.cardDragDropEnabled);
    readIf(object, QStringLiteral("card_dock_state"), settings.cardDockState);
    readIf(object, QStringLiteral("diag_hub_dock_state"), settings.diagnosticHubDockState);
    readIf(object, QStringLiteral("cpu_core_chart_mode"), settings.cpuCoreChartMode);
    if (object.value(QStringLiteral("custom_theme_colors")).isObject()) {
        settings.customThemeColors = object.value(QStringLiteral("custom_theme_colors")).toObject();
    }

    const QStringList validThemes {
        QStringLiteral("eclipse"), QStringLiteral("quantum_cyan"),
        QStringLiteral("matrix_terminal"), QStringLiteral("slate_minimal"),
        QStringLiteral("custom")};
    if (settings.themeKey == QStringLiteral("orion_dark")) {
        settings.themeKey = QStringLiteral("eclipse");
    } else if (!validThemes.contains(settings.themeKey)) {
        settings.themeKey = QStringLiteral("eclipse");
    }
    settings.minimumWidth = std::clamp(settings.minimumWidth, 400, 3000);
    settings.minimumHeight = std::clamp(settings.minimumHeight, 300, 3000);
    settings.maximumWidth = std::clamp(settings.maximumWidth, 400, 4000);
    settings.maximumHeight = std::clamp(settings.maximumHeight, 300, 4000);
    settings.maximumWidth = std::max(settings.minimumWidth, settings.maximumWidth);
    settings.maximumHeight = std::max(settings.minimumHeight, settings.maximumHeight);
    settings.windowOpacity = std::clamp(settings.windowOpacity, 0.5, 1.0);
    settings.pingTarget = settings.pingTarget.trimmed();
    if (settings.pingTarget.isEmpty()) {
        settings.pingTarget = QStringLiteral("8.8.8.8");
    }
    if (settings.cpuCoreChartMode != QStringLiteral("profile")
        && settings.cpuCoreChartMode != QStringLiteral("bars")
        && settings.cpuCoreChartMode != QStringLiteral("history")) {
        settings.cpuCoreChartMode = QStringLiteral("profile");
    }
    return settings;
}

bool AppSettings::load(
    const QString& path,
    AppSettings& settings,
    QString* error)
{
    if (!QFileInfo::exists(path)) {
        settings = AppSettings {};
        return true;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = parseError.errorString();
        }
        return false;
    }
    settings = fromJson(document.object());
    return true;
}

bool AppSettings::save(const QString& path, QString* error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    const auto bytes = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace orion::storage
