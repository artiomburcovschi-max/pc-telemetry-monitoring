#include "deep_telemetry_dialog.h"
#include "orion/storage/logging.h"
#include "orion/storage/telemetry_files.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

bool checkDeepTelemetryFiles(orion::app::DeepTelemetryDialog& dialog)
{
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { passed = false; std::cerr << message << '\n'; }
    };
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    auto read = [&](const QString& path) {
        QFile file(path);
        check(file.open(QIODevice::ReadOnly), "Exported fixture not readable");
        return file.readAll();
    };
    auto* button = dialog.findChild<QPushButton*>("DeepTelemetryExportButton");
    auto* clear = dialog.findChild<QPushButton*>("DeepTelemetryClearButton");
    if (!button || !clear) return false;
    const auto runExport = [&](const QString& path, bool acceptReplacement, bool mutate,
                               int expectedQuestions, int expectedSaved) {
        QTimer driver;
        QElapsedTimer elapsed; elapsed.start();
        bool picked = false;
        int questions = 0, saved = 0;
        QObject::connect(&driver, &QTimer::timeout, &dialog, [&] {
            auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (elapsed.elapsed() > 3000) {
                check(false, "Export modal flow timed out");
                if (modal) modal->reject();
                return;
            }
            if (auto* picker = qobject_cast<QFileDialog*>(modal)) {
                if (!picked) {
                    picked = true;
                    check(picker->testOption(QFileDialog::DontConfirmOverwrite), "Duplicate platform overwrite confirmation enabled");
                    if (mutate) {
                        clear->click();
                        dialog.applyTelemetry({25}, {1234}, 1234, 987, {});
                    }
                    if (path.isEmpty()) picker->reject();
                    else {
                        picker->setDirectory(QFileInfo(path).absolutePath());
                        picker->selectFile(QFileInfo(path).fileName());
                        // A visible Qt picker retains its existing filename edit;
                        // exercise the same edit used by the user, then validate it.
                        if (auto* filename = picker->findChild<QLineEdit*>("fileNameEdit"))
                            filename->setText(QFileInfo(path).fileName());
                    }
                } else {
                    const auto selected = picker->selectedFiles();
                    const bool safe = selected.size() == 1 && QDir::cleanPath(selected.first()) == QDir::cleanPath(path);
                    if (!safe) std::cerr << "Picker selected: " << selected.join(" | ").toStdString()
                        << "; expected: " << path.toStdString() << '\n';
                    check(safe, "File picker did not select the requested temporary fixture path");
                    if (!safe) { picker->reject(); return; }
                    QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
                }
            } else if (auto* message = qobject_cast<QMessageBox*>(modal)) {
                if (message->icon() == QMessageBox::Question) {
                    ++questions;
                    check(message->defaultButton() == message->button(QMessageBox::No), "Overwrite default is not No");
                    check(message->text().contains(orion::storage::jsonSnapshotPath(path)), "Confirmation does not show final target");
                    message->button(acceptReplacement ? QMessageBox::Yes : QMessageBox::No)->click();
                } else {
                    check(message->icon() == QMessageBox::Information, "Export reported unexpected failure");
                    if (message->icon() == QMessageBox::Information) ++saved;
                    message->accept();
                }
            }
        });
        driver.start(10);
        button->click();
        driver.stop();
        check(picked && questions == expectedQuestions && saved == expectedSaved, "Export confirmation/result count wrong");
    };
    runExport({}, false, false, 0, 0);
    check(QDir(temporary.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
        "Cancel created a file");
    const auto expected = dialog.exportPayload();
    const auto selection = temporary.filePath(QStringLiteral("снимок без расширения"));
    const auto resolved = selection + ".json";
    runExport(selection, false, true, 0, 1);
    const auto original = read(resolved);
    const auto exported = QJsonDocument::fromJson(original).object();
    check(exported.value("current_telemetry") == expected.value("current_telemetry")
        && exported.value("system_errors_report") == expected.value("system_errors_report")
        && exported.value("telemetry_capture_state") == expected.value("telemetry_capture_state"),
        "Nested file picker changed the snapshot being exported");
    runExport(selection, false, false, 1, 0);
    check(read(resolved) == original, "No after extension collision replaced a file");
    runExport(resolved, false, false, 1, 0);
    check(read(resolved) == original, "No on exact target replaced a file");
    runExport(resolved, true, false, 1, 1);
    check(QJsonDocument::fromJson(read(resolved)).object().value("current_telemetry")
        == dialog.exportPayload().value("current_telemetry"), "Confirmed replacement lost current snapshot");

    const auto logPath = temporary.filePath(QStringLiteral("учебный журнал.log"));
    {
        QFile file(logPath);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(QByteArray(4 * 1024 * 1024, 'x'));
        file.write(QStringLiteral("\nУЧЕБНЫЙ ЖУРНАЛ — это не результат проверки ПК\n"
            "2026-09-24 [fixture] Инициализация тестовых данных\n"
            "2026-09-24 [fixture] <b>Текст не интерпретируется как HTML</b>\n").toUtf8());
    }
    QString error;
    if (!orion::storage::initializeFileLogging(logPath, &error)) return false;
    QTimer driver;
    QElapsedTimer elapsed; elapsed.start();
    bool viewed = false;
    QObject::connect(&driver, &QTimer::timeout, &dialog, [&] {
        auto* viewer = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!viewer) return;
        if (elapsed.elapsed() > 3000) {
            check(false, "Boot log background read timed out");
            viewer->reject();
            return;
        }
        if (!viewer->property("previewReady").toBool()) return;
        // The queued text change invalidates wrapping; settle the layout before
        // checking geometry or capturing the very first frame after collection.
        viewer->layout()->activate();
        viewed = true;
        const auto* text = viewer->findChild<QPlainTextEdit*>("BootLogPreviewText");
        const auto* details = viewer->findChild<QLabel*>("BootLogPreviewDetails");
        check(text && text->toPlainText().contains(QStringLiteral("УЧЕБНЫЙ ЖУРНАЛ"))
            && text->toPlainText().size() < orion::storage::kLogPreviewBytes
            && text->isReadOnly() && details && details->text().contains(QStringLiteral("Ограниченный просмотр")),
            "Bounded boot-log view lost text, read-only state or disclosure");
        check(details && details->height() >= details->heightForWidth(details->width()),
            "Boot-log disclosure is clipped after wrapping");
        const auto capture = qEnvironmentVariable("ORION_BOOT_UI_CAPTURE");
        if (!capture.isEmpty()) check(viewer->grab().save(capture), "Boot log screenshot failed");
        viewer->accept();
    });
    driver.start(10);
    QMetaObject::invokeMethod(&dialog, "showBootLog", Qt::DirectConnection);
    driver.stop();
    orion::storage::shutdownFileLogging();
    check(viewed, "Boot viewer never completed");
    check(QFileInfo(logPath).size() > 4 * 1024 * 1024, "Boot preview truncated the source file");
    // Close immediately, before the queued worker result can be applied.
    QTimer::singleShot(0, &dialog, [] {
        if (auto* viewer = qobject_cast<QDialog*>(QApplication::activeModalWidget())) viewer->reject();
    });
    elapsed.restart();
    QMetaObject::invokeMethod(&dialog, "showBootLog", Qt::DirectConnection);
    check(elapsed.elapsed() < 500, "Immediate viewer close blocked on I/O");
    return passed;
}
