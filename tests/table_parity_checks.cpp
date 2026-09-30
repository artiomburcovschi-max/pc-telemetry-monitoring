#include "main_window.h"
#include "process_worker.h"
#include "process_action_worker.h"
#include "autostart_worker.h"
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <iostream>

bool verifyTableParity(orion::app::MainWindow& window)
{
    using namespace orion::app;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    auto* worker = window.findChild<ProcessWorker*>();
    auto* autostartWorker = window.findChild<AutostartWorker*>();
    auto* table = window.findChild<QTableWidget*>("ProcessTable");
    auto* search = window.findChild<QLineEdit*>("ProcessSearch");
    auto* terminate = window.findChild<QPushButton*>("ProcessTerminate");
    auto* refresh = window.findChild<QPushButton*>("ProcessRefresh");
    auto* action = window.findChild<ProcessActionWorker*>();
    auto* startup = window.findChild<QTableWidget*>("AutostartTable");
    auto* startupSearch = window.findChild<QLineEdit*>("AutostartSearch");
    auto* category = window.findChild<QComboBox*>("AutostartCategory");
    if (!worker || !autostartWorker || !table || !search || !terminate || !refresh
        || !action || !startup || !startupSearch || !category) return false;
    worker->stop();
    if (!worker->wait(3000) || !autostartWorker->wait(3000)) return false;
    QCoreApplication::processEvents(); // Drain pending real snapshots before fixture injection.
    search->clear();
    QVector<ProcessTelemetry> rows;
    for (int i = 0; i < 20; ++i)
        rows.append({static_cast<quint32>(12345 + i), 0, QStringLiteral("fixture-%1").arg(i),
            2, static_cast<double>(96 - i), static_cast<double>(2 + i * 10), -1.0, -1.0,
            static_cast<quint64>(333 + i)});
    const auto publish = [&] { worker->processesReady(rows, 4, QStringLiteral("fixture")); };
    publish();
    check(table->rowCount() == 15, "Fixture TOP-15 limit failed.");
    search->setText("12364");
    check(table->rowCount() == 1 && table->item(0, 0)->text() == "fixture-19",
        "PID search did not scan beyond TOP-15.");
    check(table->item(0, 2)->foreground().color() == QColor("#F4C542"),
        "Process warning color diverged from common threshold.");
    check(table->item(0, 4)->text() == QStringLiteral("н/д")
        && table->item(0, 5)->text() == QStringLiteral("н/д"), "Unavailable metrics became zero.");
    search->clear();
    table->sortItems(3, Qt::AscendingOrder);
    table->selectRow(0);
    const auto selectedPid = table->item(0, 1)->data(Qt::DisplayRole).toUInt();
    rows[0].workingSetMiB = 999.0;
    publish();
    auto selected = table->selectionModel()->selectedRows();
    check(table->horizontalHeader()->sortIndicatorSection() == 3
        && table->horizontalHeader()->sortIndicatorOrder() == Qt::AscendingOrder
        && table->item(0, 3)->data(Qt::DisplayRole).toDouble() == 12.0,
        "Process refresh lost numeric user sorting.");
    check(selected.size() == 1
        && table->item(selected.first().row(), 1)->data(Qt::DisplayRole).toUInt() == selectedPid,
        "Process refresh lost stable selection.");
    rows[0].creationIdentity++;
    publish();
    check(table->selectionModel()->selectedRows().isEmpty() && !terminate->isEnabled(),
        "Reused PID inherited another process's selection/action.");
    table->selectRow(0);
    rows.clear();
    publish();
    check(table->rowCount() == 0 && table->selectionModel()->selectedRows().isEmpty()
        && !terminate->isEnabled(), "Empty process inventory retained actionable selection.");
    rows.append({12345, 0, QStringLiteral("fixture-confirm"), 1, 0, 2, 1, 0, 333});
    publish();
    table->selectRow(0);
    if (ProcessActionWorker::supported()) {
        check(terminate->isEnabled(), "Verified selection did not enable termination.");
        QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
        check(!terminate->isEnabled() && !refresh->isEnabled(), "Pause left process actions enabled.");
        QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
        bool confirmationSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) return;
            confirmationSeen = true;
            check(box->standardButton(box->defaultButton()) == QMessageBox::No
                && box->text().contains("fixture-confirm") && box->text().contains("12345"),
                "Process confirmation did not default to No or lost target identity.");
            // A new snapshot during confirmation must not authorize the replacement.
            rows[0].creationIdentity++;
            publish();
            box->done(QMessageBox::No);
        });
        terminate->click();
        check(confirmationSeen && !action->isRunning() && !terminate->isEnabled(),
            "Cancelled confirmation started an action or retained stale selection.");
    }
    rows[0].pid = static_cast<quint32>(QCoreApplication::applicationPid());
    publish();
    table->selectRow(0);
    check(!terminate->isEnabled(), "O.R.I.O.N. allowed terminating itself.");

    QVector<AutostartTelemetry> entries {
        {"same", "A-command", "registry", 1, "user"},
        {"same", "Z-command", "registry", 0, "user"},
        {"service", "M-command", "services", -1, "system"}};
    const auto publishStartup = [&] { autostartWorker->entriesReady(entries, "fixture"); };
    publishStartup();
    startup->sortItems(4, Qt::DescendingOrder);
    startup->selectRow(0);
    entries[0].command = "ZZ-command";
    publishStartup();
    selected = startup->selectionModel()->selectedRows();
    check(startup->horizontalHeader()->sortIndicatorSection() == 4
        && startup->horizontalHeader()->sortIndicatorOrder() == Qt::DescendingOrder
        && startup->item(0, 4)->text() == "ZZ-command", "Autostart refresh lost user sorting.");
    check(selected.size() == 1 && startup->item(selected.first().row(), 4)->text() == "Z-command",
        "Duplicate autostart names broke stable selection.");
    entries.removeAt(1);
    publishStartup();
    check(startup->selectionModel()->selectedRows().isEmpty(), "Removed autostart entry left selection.");
    startupSearch->setText("services");
    check(startup->rowCount() == 1 && startup->item(0, 3)->text() == QStringLiteral("—")
        && startup->item(0, 4)->toolTip() == "M-command", "Autostart source/status/tooltip contract failed.");
    category->setCurrentIndex(category->findData("user"));
    check(startup->rowCount() == 0, "Autostart category and search filters did not combine.");
    startupSearch->setText("ZZ-command");
    check(startup->rowCount() == 1, "Autostart command search failed.");
    startupSearch->clear();
    category->setCurrentIndex(0);
    check(startup->editTriggers() == QAbstractItemView::NoEditTriggers, "Autostart became editable.");
    return passed;
}
