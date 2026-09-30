#include "server_monitor_widget.h"

#include "../server_status_checker.h"

#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <utility>

namespace orion::app {
namespace {

constexpr int labelColumn = 0;
constexpr int statusColumn = 3;
constexpr int latencyColumn = 4;
constexpr int checkedColumn = 5;

} // namespace

ServerMonitorWidget::ServerMonitorWidget(QString databasePath, QWidget* parent)
    : QFrame(parent)
    , store_(std::move(databasePath))
{
    setObjectName(QStringLiteral("ServerMonitorWidget"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(createProfileForm(
        QStringLiteral("server"), QStringLiteral("Новый сервер (SSH / TCP)"), serverForm_));
    leftLayout->addWidget(createProfileForm(
        QStringLiteral("database"), QStringLiteral("Новая база данных"), databaseForm_));
    auto* secretWarning = new QLabel(QStringLiteral(
        "Важно: для совместимости с Python-версией пароль или путь к ключу хранится "
        "локально в server_profiles.db без шифрования. Не сохраняйте здесь боевые секреты."), left);
    secretWarning->setObjectName(QStringLiteral("ServerSecretWarning"));
    secretWarning->setWordWrap(true);
    leftLayout->addWidget(secretWarning);
    leftLayout->addStretch(1);

    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("Сохранённые профили"), right);
    title->setObjectName(QStringLiteral("Header"));
    header->addWidget(title);
    header->addStretch(1);
    checkSelectedButton_ = new QPushButton(QStringLiteral("Проверить выбранный"), right);
    checkSelectedButton_->setObjectName(QStringLiteral("ServerCheckSelected"));
    checkAllButton_ = new QPushButton(QStringLiteral("Проверить все"), right);
    checkAllButton_->setObjectName(QStringLiteral("ServerCheckAll"));
    deleteButton_ = new QPushButton(QStringLiteral("Удалить"), right);
    deleteButton_->setObjectName(QStringLiteral("ServerDelete"));
    header->addWidget(checkSelectedButton_);
    header->addWidget(checkAllButton_);
    header->addWidget(deleteButton_);
    rightLayout->addLayout(header);

    table_ = new QTableWidget(right);
    table_->setObjectName(QStringLiteral("ServerProfileTable"));
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({QStringLiteral("Профиль"), QStringLiteral("Тип"),
        QStringLiteral("Хост:Порт"), QStringLiteral("Статус"), QStringLiteral("Latency"),
        QStringLiteral("Проверено")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(28);
    auto* columns = table_->horizontalHeader();
    columns->setSectionResizeMode(labelColumn, QHeaderView::Stretch);
    for (int column = 1; column < 6; ++column) {
        columns->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    rightLayout->addWidget(table_, 1);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setSizes({330, 700});
    outer->addWidget(splitter, 1);

    connect(checkSelectedButton_, &QPushButton::clicked, this, &ServerMonitorWidget::checkSelected);
    connect(checkAllButton_, &QPushButton::clicked, this, &ServerMonitorWidget::checkAll);
    connect(deleteButton_, &QPushButton::clicked, this, &ServerMonitorWidget::deleteSelected);
    connect(table_, &QTableWidget::itemDoubleClicked, this,
        [this](QTableWidgetItem*) { checkSelected(); });

    QString error;
    if (!store_.ensureSchema(&error)) {
        QMessageBox::warning(this, QStringLiteral("Профили серверов"), error);
    }
    reloadProfiles();
}

ServerMonitorWidget::~ServerMonitorWidget()
{
    shutdown();
}

QWidget* ServerMonitorWidget::createProfileForm(
    const QString& kind,
    const QString& title,
    ProfileForm& form)
{
    auto* group = new QGroupBox(title, this);
    auto* outer = new QVBoxLayout(group);
    auto* fields = new QFormLayout;
    form.label = new QLineEdit(group);
    form.label->setPlaceholderText(QStringLiteral("например: Прод-сервер"));
    fields->addRow(QStringLiteral("Название профиля:"), form.label);
    if (kind == QStringLiteral("database")) {
        form.databaseType = new QComboBox(group);
        form.databaseType->addItems({QStringLiteral("PostgreSQL"), QStringLiteral("MySQL"),
            QStringLiteral("SQLite")});
        fields->addRow(QStringLiteral("СУБД:"), form.databaseType);
    }
    form.host = new QLineEdit(group);
    form.host->setPlaceholderText(QStringLiteral("192.168.1.10 или db.example.com"));
    fields->addRow(QStringLiteral("IP / Хост:"), form.host);
    form.port = new QSpinBox(group);
    form.port->setRange(1, 65535);
    form.port->setValue(kind == QStringLiteral("database") ? 5432 : 22);
    fields->addRow(QStringLiteral("Порт:"), form.port);
    form.login = new QLineEdit(group);
    fields->addRow(QStringLiteral("Логин:"), form.login);
    form.secret = new QLineEdit(group);
    form.secret->setEchoMode(QLineEdit::Password);
    form.secret->setPlaceholderText(QStringLiteral("пароль или путь к SSH-ключу"));
    fields->addRow(QStringLiteral("Пароль / Ключ:"), form.secret);
    outer->addLayout(fields);
    form.save = new QPushButton(QStringLiteral("Сохранить профиль"), group);
    form.save->setObjectName(kind == QStringLiteral("server")
        ? QStringLiteral("SaveServerProfile") : QStringLiteral("SaveDatabaseProfile"));
    connect(form.save, &QPushButton::clicked, this,
        [this, kind, &form] { saveProfile(kind, form); });
    outer->addWidget(form.save);
    return group;
}

void ServerMonitorWidget::saveProfile(const QString& kind, ProfileForm& form)
{
    if (form.label->text().trimmed().isEmpty() || form.host->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Не хватает данных"),
            QStringLiteral("Укажите название профиля и хост."));
        return;
    }
    orion::storage::ServerProfile profile;
    profile.kind = kind;
    profile.label = form.label->text().trimmed();
    profile.host = form.host->text().trimmed();
    profile.port = form.port->value();
    profile.login = form.login->text().trimmed();
    profile.secret = form.secret->text();
    if (form.databaseType != nullptr) {
        profile.databaseType = form.databaseType->currentText();
    }
    QString error;
    if (!store_.saveProfile(profile, &error)) {
        QMessageBox::warning(this, QStringLiteral("Не удалось сохранить профиль"), error);
        return;
    }
    form.label->clear();
    form.host->clear();
    form.login->clear();
    form.secret->clear();
    reloadProfiles();
}

void ServerMonitorWidget::reloadProfiles()
{
    QString error;
    const auto profiles = store_.listProfiles({}, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Не удалось прочитать профили"), error);
        return;
    }
    profiles_.clear();
    rows_.clear();
    table_->setRowCount(0);
    for (const auto& profile : profiles) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        const QString kind = profile.kind == QStringLiteral("server")
            ? QStringLiteral("сервер")
            : (profile.databaseType.isEmpty() ? QStringLiteral("БД") : profile.databaseType);
        const QStringList values {profile.label, kind,
            QStringLiteral("%1:%2").arg(profile.host).arg(profile.port),
            QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—")};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setData(Qt::UserRole, profile.id);
            table_->setItem(row, column, item);
        }
        profiles_.insert(profile.id, profile);
        rows_.insert(profile.id, row);
    }
}

qint64 ServerMonitorWidget::selectedProfileId() const
{
    const int row = table_->currentRow();
    const auto* item = row >= 0 ? table_->item(row, labelColumn) : nullptr;
    return item != nullptr ? item->data(Qt::UserRole).toLongLong() : -1;
}

void ServerMonitorWidget::checkProfile(const qint64 profileId)
{
    if (paused_ || shuttingDown_ || activeChecks_.contains(profileId)
        || !profiles_.contains(profileId)) return;
    const auto profile = profiles_.value(profileId);
    const int row = rows_.value(profileId, -1);
    if (row >= 0 && table_->item(row, statusColumn) != nullptr) {
        table_->item(row, statusColumn)->setText(QStringLiteral("Проверяется…"));
        table_->item(row, latencyColumn)->setText(QStringLiteral("—"));
    }
    auto* checker = new ServerStatusChecker(profile.id, profile.host,
        static_cast<quint16>(profile.port), 2000, this);
    activeChecks_.insert(profileId, checker);
    connect(checker, &ServerStatusChecker::resultReady,
        this, &ServerMonitorWidget::applyCheckResult);
    connect(checker, &QThread::finished, this,
        [this, profileId, checker] { finishCheck(profileId, checker); });
    checker->start();
}

void ServerMonitorWidget::applyCheckResult(
    const qint64 profileId,
    const bool online,
    const double latencyMs,
    const QString& message)
{
    const int row = rows_.value(profileId, -1);
    if (row < 0 || row >= table_->rowCount()) return;
    table_->item(row, statusColumn)->setText(
        QStringLiteral("%1 %2").arg(online ? QStringLiteral("●") : QStringLiteral("○"), message));
    table_->item(row, latencyColumn)->setText(
        online ? QStringLiteral("%1 мс").arg(latencyMs, 0, 'f', 1) : QStringLiteral("—"));
    table_->item(row, checkedColumn)->setText(
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
}

void ServerMonitorWidget::checkSelected()
{
    const qint64 profileId = selectedProfileId();
    if (profileId < 0) {
        QMessageBox::information(this, QStringLiteral("Ничего не выбрано"),
            QStringLiteral("Выберите профиль из таблицы."));
        return;
    }
    checkProfile(profileId);
}

void ServerMonitorWidget::checkAll()
{
    for (auto iterator = profiles_.cbegin(); iterator != profiles_.cend(); ++iterator) {
        checkProfile(iterator.key());
    }
}

void ServerMonitorWidget::deleteSelected()
{
    const qint64 profileId = selectedProfileId();
    if (profileId < 0) {
        QMessageBox::information(this, QStringLiteral("Ничего не выбрано"),
            QStringLiteral("Выберите профиль из таблицы."));
        return;
    }
    if (auto* checker = activeChecks_.value(profileId, nullptr); checker != nullptr) {
        checker->requestStop();
        checker->wait(2500);
    }
    QString error;
    if (!store_.deleteProfile(profileId, &error)) {
        QMessageBox::warning(this, QStringLiteral("Не удалось удалить профиль"), error);
        return;
    }
    reloadProfiles();
}

void ServerMonitorWidget::finishCheck(
    const qint64 profileId,
    ServerStatusChecker* checker)
{
    if (activeChecks_.value(profileId) == checker) {
        activeChecks_.remove(profileId);
    }
    checker->deleteLater();
}

void ServerMonitorWidget::setGlobalPaused(const bool paused)
{
    paused_ = paused;
    checkSelectedButton_->setEnabled(!paused);
    checkAllButton_->setEnabled(!paused);
    if (paused) {
        for (auto* checker : std::as_const(activeChecks_)) {
            checker->requestStop();
        }
    }
}

void ServerMonitorWidget::shutdown()
{
    if (shuttingDown_) return;
    shuttingDown_ = true;
    checkSelectedButton_->setEnabled(false);
    checkAllButton_->setEnabled(false);
    const auto checks = activeChecks_.values();
    for (auto* checker : checks) checker->requestStop();
    for (auto* checker : checks) checker->wait(2500);
    activeChecks_.clear();
}

} // namespace orion::app
