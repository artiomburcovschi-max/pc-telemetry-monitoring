#include "orion/storage/app_paths.h"
#include "orion/storage/app_settings.h"
#include "orion/storage/logging.h"
#include "orion/storage/server_profile_store.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary settings directory is available");

    const orion::storage::AppPaths paths(temporary.filePath(QStringLiteral("ORION")));
    QString error;
    check(paths.ensureCreated(&error), "application data directories are created");
    check(QFileInfo::exists(paths.logsDirectory()), "logs directory exists");
    check(QFileInfo::exists(paths.reportsDirectory()), "reports directory exists");

    orion::storage::AppSettings saved;
    saved.themeKey = QStringLiteral("custom");
    saved.windowOpacity = 0.75;
    saved.pingTarget = QStringLiteral("1.1.1.1");
    saved.cpuCoreChartMode = QStringLiteral("history");
    saved.minimumWidth = 640;
    saved.minimumHeight = 480;
    saved.maximumWidth = 1920;
    saved.maximumHeight = 1200;
    saved.alwaysOnTop = true;
    saved.freeFormResize = true;
    saved.alertFreezeDisabled = true;
    saved.trayIconEnabled = false;
    saved.trayNotificationsEnabled = false;
    saved.terminalEnabled = true;
    saved.serverTabEnabled = true;
    saved.cardGpuVisible = false;
    saved.cardDragDropEnabled = false;
    saved.cardDockState = QStringLiteral("dock-state");
    saved.diagnosticHubDockState = QStringLiteral("diagnostic-dock-state");
    check(saved.save(paths.settingsFile(), &error), "settings are saved atomically");

    orion::storage::AppSettings loaded;
    check(orion::storage::AppSettings::load(paths.settingsFile(), loaded, &error),
          "settings JSON is loaded");
    check(loaded.themeKey == saved.themeKey, "theme setting survives a round trip");
    check(loaded.windowOpacity == 0.75, "opacity setting survives a round trip");
    check(loaded.pingTarget == QStringLiteral("1.1.1.1"),
          "ping target survives a round trip");
    check(loaded.cpuCoreChartMode == QStringLiteral("history"),
          "CPU chart mode survives a round trip");
    check(loaded.minimumWidth == 640 && loaded.minimumHeight == 480
            && loaded.maximumWidth == 1920 && loaded.maximumHeight == 1200,
          "window constraints survive a round trip");
    check(loaded.alwaysOnTop && loaded.freeFormResize && loaded.alertFreezeDisabled,
          "window and alert behaviour survives a round trip");
    check(!loaded.trayIconEnabled && !loaded.trayNotificationsEnabled,
          "independent tray options survive a round trip");
    check(loaded.terminalEnabled && loaded.serverTabEnabled,
          "optional native tabs survive a settings round trip");
    check(!loaded.cardGpuVisible && !loaded.cardDragDropEnabled
            && loaded.cardDockState == QStringLiteral("dock-state")
            && loaded.diagnosticHubDockState == QStringLiteral("diagnostic-dock-state"),
          "overview and diagnostic dock settings survive a round trip");

    const auto normalized = orion::storage::AppSettings::fromJson(QJsonObject {
        {QStringLiteral("theme_key"), QStringLiteral("unknown")},
        {QStringLiteral("MIN_WIDTH"), -5},
        {QStringLiteral("MAX_WIDTH"), 10},
        {QStringLiteral("window_opacity"), 4.0},
        {QStringLiteral("ping_target"), QStringLiteral("   ")},
    });
    check(normalized.themeKey == QStringLiteral("eclipse"),
          "unknown themes fall back to Eclipse");
    check(normalized.minimumWidth == 400 && normalized.maximumWidth == 400,
          "window constraints are normalized to UI-safe ranges");
    check(normalized.windowOpacity == 1.0
            && normalized.pingTarget == QStringLiteral("8.8.8.8"),
          "opacity and ping target are normalized");

    check(orion::storage::initializeFileLogging(paths.logFile(), &error),
          "file logging initializes");
    qInfo().noquote() << QStringLiteral("stage1 logging smoke");
    orion::storage::shutdownFileLogging();
    QFile log(paths.logFile());
    check(log.open(QIODevice::ReadOnly), "log file can be read after shutdown");
    check(log.readAll().contains("stage1 logging smoke"), "Qt message reaches UTF-8 file log");

    orion::storage::ServerProfileStore profileStore(
        paths.dataFile(QStringLiteral("server_profiles.db")));
    check(profileStore.ensureSchema(&error), "server profile SQLite schema is created");
    orion::storage::ServerProfile profile;
    profile.kind = QStringLiteral("server");
    profile.label = QStringLiteral("Local fixture");
    profile.host = QStringLiteral("127.0.0.1");
    profile.port = 22;
    profile.login = QStringLiteral("operator");
    profile.secret = QStringLiteral("test-only-secret");
    check(profileStore.saveProfile(profile, &error) && profile.id >= 0,
          "a server profile is inserted and receives an id");
    auto profiles = profileStore.listProfiles({}, &error);
    check(profiles.size() == 1 && profiles.front().label == profile.label
            && profiles.front().secret == profile.secret,
          "all server profile fields survive a SQLite round trip");
    profile.label = QStringLiteral("Updated fixture");
    check(profileStore.saveProfile(profile, &error), "a server profile is updated");
    profiles = profileStore.listProfiles(QStringLiteral("server"), &error);
    check(profiles.size() == 1 && profiles.front().label == profile.label,
          "profile filtering and update preserve the native contract");
    check(profileStore.deleteProfile(profile.id, &error)
            && profileStore.listProfiles({}, &error).isEmpty(),
          "a server profile can be deleted without residue");

    if (failures != 0) {
        std::cerr << failures << " storage test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All ORION storage tests passed.\n";
    return EXIT_SUCCESS;
}
