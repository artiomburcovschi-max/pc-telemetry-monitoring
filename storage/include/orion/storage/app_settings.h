#pragma once

#include <QJsonObject>
#include <QString>

namespace orion::storage {

struct AppSettings {
    QString themeKey {QStringLiteral("eclipse")};
    int minimumWidth {730};
    int minimumHeight {800};
    int maximumWidth {830};
    int maximumHeight {900};
    bool terminalEnabled {false};
    bool serverTabEnabled {false};
    double windowOpacity {1.0};
    bool alwaysOnTop {false};
    bool freeFormResize {false};
    bool alertFreezeDisabled {false};
    bool trayIconEnabled {true};
    bool trayNotificationsEnabled {true};
    QString pingTarget {QStringLiteral("8.8.8.8")};
    bool cardCpuVisible {true};
    bool cardGpuVisible {true};
    bool cardRamVisible {true};
    bool cardDiskVisible {true};
    bool cardNetVisible {true};
    bool cardDragDropEnabled {true};
    QString cardDockState;
    QString diagnosticHubDockState;
    QString cpuCoreChartMode {QStringLiteral("profile")};
    QJsonObject customThemeColors {
        {QStringLiteral("bg"), QStringLiteral("#121212")},
        {QStringLiteral("text"), QStringLiteral("#F2F2F7")},
        {QStringLiteral("accent"), QStringLiteral("#FF9F0A")},
        {QStringLiteral("graph"), QStringLiteral("#FF9F0A")},
    };

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static AppSettings fromJson(const QJsonObject& object);
    [[nodiscard]] static bool load(
        const QString& path,
        AppSettings& settings,
        QString* error = nullptr);
    [[nodiscard]] bool save(const QString& path, QString* error = nullptr) const;
};

} // namespace orion::storage
