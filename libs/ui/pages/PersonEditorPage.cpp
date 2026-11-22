#include <QMessageBox>
#include <QCloseEvent>
#include "ui/pages/PersonEditorPage.h"
#include "ui/pages/PageScrollArea.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/components/TokenControls.h"
#include "darkeye_ui/components/TokenViews.h"
#include "ui/components/ImageDropWidget.h"
#include "ui/components/WikiTextEdit.h"
#include "ui/layouts/myads/ThemeAdapter.h"
#include "ui/layouts/myads/WorkspaceWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace
{

QHash<QString, darkeye::PersonEditorPage *> pendingLayoutSaves;

QSpinBox *measurementSpin(QWidget *parent, const QString &objectName)
{
    auto *spin = new darkeye::TokenSpinBox(parent);
    spin->setProperty("testId", objectName);
    spin->setRange(0, 300);
    spin->setSpecialValueText(QStringLiteral("未填写"));
    return spin;
}

QComboBox *scoreCombo(QWidget *parent, const QString &objectName, const QStringList &labels)
{
    auto *combo = new darkeye::DesignComboBox(parent);
    combo->setProperty("testId", objectName);
    combo->addItem(QStringLiteral("未填写"), QVariant());
    for (int score = 0; score < labels.size(); ++score)
        combo->addItem(labels.at(score), score);
    return combo;
}

void setOptionalSpin(QSpinBox *spin, const std::optional<int> &value)
{
    spin->setValue(value.value_or(0));
}

} // namespace

namespace darkeye
{

PersonEditorPage::PersonEditorPage(QSqlDatabase database, ThemeService &themes,
                                       QString imageDirectory, QWidget *parent,
                                       QString coverDirectory)
    : QWidget(parent), m_repository(database), m_works(std::move(database)), m_themes(themes),
      m_imageDirectory(std::move(imageDirectory)), m_coverDirectory(std::move(coverDirectory))
{
    buildUi();
}

PersonEditorPage::~PersonEditorPage()
{
    if (m_workspaceSaveTimer && m_workspaceSaveTimer->isActive())
        saveWorkspaceLayout();
}

void PersonEditorPage::enableLayoutPersistence(const QString &path)
{
    if (path.isEmpty() || m_workspaceSaveTimer)
        return;

    m_workspaceLayoutPath = path;
    // Different people can have editor tabs open at the same time.
    if (auto *pending = pendingLayoutSaves.value(path))
        pending->saveWorkspaceLayout();
    restoreWorkspaceLayout();

    m_workspaceSaveTimer = new QTimer(this);
    m_workspaceSaveTimer->setSingleShot(true);
    m_workspaceSaveTimer->setInterval(250);
    connect(m_workspaceSaveTimer, &QTimer::timeout, this, &PersonEditorPage::saveWorkspaceLayout);
    connect(m_workspace, &myads::WorkspaceWidget::layoutChanged,
            this, &PersonEditorPage::scheduleWorkspaceSave);
    connect(m_workspace, &myads::WorkspaceWidget::layoutPreferencesChanged,
            this, &PersonEditorPage::scheduleWorkspaceSave);
}

void PersonEditorPage::restoreWorkspaceLayout()
{
    if (!QFileInfo::exists(m_workspaceLayoutPath))
        return;

    QHash<QString, myads::ContentConfig> panels;
    for (const auto *pane : m_workspace->panes()) {
        for (const QString &id : pane->contentIds()) {
            myads::ContentConfig config(id);
            config.setWindowTitle(pane->contentTitle(id)).setWidget(pane->contentWidget(id)).setCloseable(false);
            panels.insert(id, config);
        }
    }

    QString error;
    const QJsonObject document = myads::loadLayoutFile(m_workspaceLayoutPath, &error);
    if (document.isEmpty()) {
        qWarning() << "Person editor layout restore:" << m_workspaceLayoutPath << error;
        return;
    }
    // Keep the default layout if the saved one would leave any editor panel missing.
    QSet<QString> missing(panels.keyBegin(), panels.keyEnd());
    const auto contents = document.value(QStringLiteral("pane_contents")).toObject();
    for (const auto &pane : contents) {
        for (const auto &tab : pane.toArray())
            missing.remove(tab.toObject().value(QStringLiteral("content_id")).toString());
    }
    if (!missing.isEmpty()) {
        qWarning() << "Person editor layout restore: missing panels in" << m_workspaceLayoutPath;
        return;
    }

    const auto restorePanel = [&panels](const QJsonObject &tab) -> std::optional<myads::ContentConfig> {
        const auto panel = panels.constFind(tab.value(QStringLiteral("content_id")).toString());
        if (panel == panels.cend())
            return std::nullopt;
        return *panel;
    };
    if (!m_workspace->restoreLayout(document, restorePanel, &error))
        qWarning() << "Person editor layout restore:" << m_workspaceLayoutPath << error;
}

void PersonEditorPage::scheduleWorkspaceSave()
{
    if (auto *pending = pendingLayoutSaves.value(m_workspaceLayoutPath); pending && pending != this)
        pending->m_workspaceSaveTimer->stop();
    pendingLayoutSaves.insert(m_workspaceLayoutPath, this);
    m_workspaceSaveTimer->start();
}

void PersonEditorPage::saveWorkspaceLayout()
{
    m_workspaceSaveTimer->stop();
    pendingLayoutSaves.remove(m_workspaceLayoutPath);
    if (!QDir().mkpath(QFileInfo(m_workspaceLayoutPath).absolutePath())) {
        qWarning() << "Person editor layout save: cannot create directory for" << m_workspaceLayoutPath;
        return;
    }

    QString error;
    if (!m_workspace->saveLayout(m_workspaceLayoutPath, [](const myads::PaneWidget *, const QString &id) {
            return QJsonObject{{QStringLiteral("content_id"), id}};
        }, {}, &error))
        qWarning() << "Person editor layout save:" << m_workspaceLayoutPath << error;
}

void PersonEditorPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_workspace = new myads::WorkspaceWidget(this);
    myads::bindDarkeyeTheme(m_workspace, &m_themes);
    root->addWidget(m_workspace);

    auto *commonForm = new QFormLayout;
    m_imageDrop = new ImageDropWidget(m_imageDirectory, m_workspace);
    m_imageDrop->setObjectName(QStringLiteral("PersonImageDropWidget"));
    m_birthday = new DesignLineEdit(m_workspace);
    m_birthday->setProperty("testId", QStringLiteral("PersonBirthdayInput"));
    m_height = measurementSpin(m_workspace, QStringLiteral("PersonHeightInput"));
    m_needUpdate = new TokenCheckBox(QStringLiteral("需要后续采集更新"), m_workspace);
    m_needUpdate->setProperty("testId", QStringLiteral("PersonNeedUpdateInput"));
    m_notes = new WikiTextEdit(m_workspace);
    m_notes->setProperty("testId", QStringLiteral("PersonNotesInput"));
    m_notes->setMinimumHeight(100);
    m_notes->setCompleterList(m_works.serialSuggestions());
    m_notes->setWorkIdResolver([this](const QString &serial) {
        return m_works.findIdBySerial(serial);
    });
    m_notes->setImageResolver([this](const QString &serial) {
        const auto id = m_works.findIdBySerial(serial);
        if (!id) return QString();
        const auto work = m_works.findById(*id);
        if (!work || work->imageUrl.trimmed().isEmpty()) return QString();
        return QFileInfo(work->imageUrl).isAbsolute()
            ? work->imageUrl : QDir(m_coverDirectory).filePath(work->imageUrl);
    });
    connect(m_notes, &WikiTextEdit::workLinkRequested,
            this, &PersonEditorPage::workLinkRequested);
    connect(m_notes, &WikiTextEdit::actressLinkRequested,
            this, &PersonEditorPage::actressLinkRequested);
    commonForm->addRow(QStringLiteral("生日"), m_birthday);
    commonForm->addRow(QStringLiteral("身高"), m_height);
    commonForm->addRow(QString(), m_needUpdate);
    auto *basePanel = new QWidget(m_workspace);
    auto *baseLayout = new QVBoxLayout(addPageScrollArea(basePanel));
    baseLayout->setContentsMargins(0, 0, 0, 0);
    baseLayout->addLayout(commonForm);

    m_actressFields = new QWidget(m_workspace);
    auto *actressForm = new QFormLayout(m_actressFields);
    m_bust = measurementSpin(m_actressFields, QStringLiteral("PersonBustInput"));
    m_waist = measurementSpin(m_actressFields, QStringLiteral("PersonWaistInput"));
    m_hip = measurementSpin(m_actressFields, QStringLiteral("PersonHipInput"));
    m_cup = new DesignComboBox(m_actressFields);
    m_cup->setProperty("testId", QStringLiteral("PersonCupInput"));
    m_cup->addItems({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                     QStringLiteral("D"), QStringLiteral("E"), QStringLiteral("F"),
                     QStringLiteral("G"), QStringLiteral("H"), QStringLiteral("I"),
                     QStringLiteral("J"), QStringLiteral("K"), QStringLiteral("L"),
                     QStringLiteral("M")});
    m_cup->setCurrentIndex(-1);
    m_debutDate = new DesignLineEdit(m_actressFields);
    m_debutDate->setProperty("testId", QStringLiteral("PersonDebutDateInput"));
    m_minnanoUrl = new DesignLineEdit(m_actressFields);
    m_minnanoUrl->setProperty("testId", QStringLiteral("PersonMinnanoInput"));
    actressForm->addRow(QStringLiteral("胸围"), m_bust);
    actressForm->addRow(QStringLiteral("腰围"), m_waist);
    actressForm->addRow(QStringLiteral("臀围"), m_hip);
    actressForm->addRow(QStringLiteral("罩杯"), m_cup);
    actressForm->addRow(QStringLiteral("出道日期"), m_debutDate);
    actressForm->addRow(QStringLiteral("Minnano ID"), m_minnanoUrl);
    baseLayout->addWidget(m_actressFields);

    m_actorFields = new QWidget(m_workspace);
    auto *actorForm = new QFormLayout(m_actorFields);
    m_handsome = scoreCombo(m_actorFields, QStringLiteral("PersonHandsomeInput"),
                            {QStringLiteral("丑"), QStringLiteral("普通"), QStringLiteral("帅")});
    m_fat = scoreCombo(m_actorFields, QStringLiteral("PersonFatInput"),
                       {QStringLiteral("胖"), QStringLiteral("普通"), QStringLiteral("瘦")});
    actorForm->addRow(QStringLiteral("外貌"), m_handsome);
    actorForm->addRow(QStringLiteral("体型"), m_fat);
    baseLayout->addWidget(m_actorFields);
    baseLayout->addStretch();

    auto *nameTools = new QHBoxLayout;
    const auto nameToolButton = [this](const QString &icon, const QString &toolTip) {
        auto *button = new IconButton(icon, &m_themes, m_workspace);
        button->setToolTip(toolTip);
        return button;
    };
    auto *addName = nameToolButton(QStringLiteral("list_plus"), QStringLiteral("添加姓名"));
    addName->setProperty("testId", QStringLiteral("PersonAddNameButton"));
    auto *removeName = nameToolButton(QStringLiteral("list_x"), QStringLiteral("删除选中姓名"));
    removeName->setProperty("testId", QStringLiteral("PersonRemoveNameButton"));
    auto *moveNameUp = nameToolButton(QStringLiteral("arrow_up"), QStringLiteral("上移"));
    moveNameUp->setProperty("testId", QStringLiteral("PersonMoveNameUpButton"));
    auto *moveNameDown = nameToolButton(QStringLiteral("arrow_down"), QStringLiteral("下移"));
    moveNameDown->setProperty("testId", QStringLiteral("PersonMoveNameDownButton"));
    nameTools->addWidget(addName);
    nameTools->addWidget(removeName);
    nameTools->addWidget(moveNameUp);
    nameTools->addWidget(moveNameDown);
    nameTools->addStretch();
    auto *namePanel = new QWidget(m_workspace);
    auto *nameLayout = new QVBoxLayout(addPageScrollArea(namePanel));
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->addLayout(nameTools);
    m_names = new TokenTableWidget(0, 4, m_workspace);
    m_names->setProperty("testId", QStringLiteral("PersonNameEditor"));
    m_names->setHorizontalHeaderLabels({QStringLiteral("中文"), QStringLiteral("日文"),
                                        QStringLiteral("英文"), QStringLiteral("假名")});
    m_names->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_names->setSelectionBehavior(QAbstractItemView::SelectRows);
    nameLayout->addWidget(m_names, 1);

    auto *notesPanel = new QWidget(m_workspace);
    auto *notesLayout = new QVBoxLayout(addPageScrollArea(notesPanel));
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->addWidget(m_notes);

    auto *actionsPanel = new QWidget(m_workspace);
    auto *actionsLayout = new QVBoxLayout(addPageScrollArea(actionsPanel));
    m_actionsLayout = actionsLayout;
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    m_saveButton = new DesignButton(QStringLiteral("提交修改"), actionsPanel);
    m_saveButton->setProperty("testId", QStringLiteral("PersonSaveButton"));
    m_cancelButton = new DesignButton(QStringLiteral("取消"), actionsPanel);
    m_cancelButton->setProperty("testId", QStringLiteral("PersonCancelButton"));
    actionsLayout->addWidget(m_saveButton);
    actionsLayout->addWidget(m_cancelButton);
    actionsLayout->addStretch();

    // Match the Python ModifyActor/ModifyActress workspace: avatar and
    // information on the left; names, notes, and actions in the right panes.
    auto *avatarPane = m_workspace->rootPane();
    auto *rightPane = m_workspace->split(avatarPane, myads::Placement::Right, 68);
    auto *notesPane = m_workspace->split(rightPane, myads::Placement::Right, 40);
    auto *basePane = m_workspace->split(avatarPane, myads::Placement::Bottom, 60);
    auto *actionsPane = m_workspace->split(rightPane, myads::Placement::Bottom, 22);
    const auto fill = [this](myads::PaneWidget *pane, const QString &id, const QString &title, QWidget *widget) {
        auto config = m_workspace->createContentConfig(id);
        config.setWindowTitle(title).setWidget(widget).setCloseable(false);
        m_workspace->fillPane(pane, config);
    };
    fill(avatarPane, QStringLiteral("avatar"), QStringLiteral("头像"), m_imageDrop);
    fill(basePane, QStringLiteral("basic"), QStringLiteral("基础信息"), basePanel);
    fill(rightPane, QStringLiteral("names"), QStringLiteral("名字表"), namePanel);
    fill(notesPane, QStringLiteral("notes"), QStringLiteral("自由记录"), notesPanel);
    fill(actionsPane, QStringLiteral("actions"), QStringLiteral("操作"), actionsPanel);
    connect(addName, &QPushButton::clicked, this, [this] { addNameRow(); });
    connect(removeName, &QPushButton::clicked, this, &PersonEditorPage::removeSelectedNameRows);
    connect(moveNameUp, &QPushButton::clicked, this, [this] { moveSelectedNameRow(-1); });
    connect(moveNameDown, &QPushButton::clicked, this, [this] { moveSelectedNameRow(1); });
    connect(m_imageDrop, &ImageDropWidget::imageRejected, this,
            [this](const QString &message) { Toast::showError(this, message, &m_themes); });
    connect(m_imageDrop, &ImageDropWidget::imageChanged,
            this, [this](const QString &) { updateDirtyState(); });
    const auto updateOnChange = [this] { updateDirtyState(); };
    connect(m_birthday, &QLineEdit::textChanged, this, updateOnChange);
    connect(m_cup, &QComboBox::currentTextChanged, this, updateOnChange);
    connect(m_debutDate, &QLineEdit::textChanged, this, updateOnChange);
    connect(m_minnanoUrl, &QLineEdit::textChanged, this, updateOnChange);
    connect(m_height, QOverload<int>::of(&QSpinBox::valueChanged), this, updateOnChange);
    connect(m_bust, QOverload<int>::of(&QSpinBox::valueChanged), this, updateOnChange);
    connect(m_waist, QOverload<int>::of(&QSpinBox::valueChanged), this, updateOnChange);
    connect(m_hip, QOverload<int>::of(&QSpinBox::valueChanged), this, updateOnChange);
    connect(m_handsome, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateOnChange);
    connect(m_fat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateOnChange);
    connect(m_needUpdate, &QCheckBox::toggled, this, updateOnChange);
    connect(m_notes, &QTextEdit::textChanged, this, updateOnChange);
    connect(m_names, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        updateDirtyState();
        if (!m_populating)
            emit namesChanged();
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this] { savePerson(); });
    connect(m_cancelButton, &QPushButton::clicked, this, &PersonEditorPage::closeRequested);
    resetDirtyState();
}

void PersonEditorPage::addActressExternalLinksPanel(QWidget *panel)
{
    auto *notesPane = m_workspace->findPaneByContentId(QStringLiteral("notes"));
    if (!panel || !notesPane)
        return;
    auto *linksPane = m_workspace->split(notesPane, myads::Placement::Top, 42);
    auto config = m_workspace->createContentConfig(QStringLiteral("external_links"));
    config.setWindowTitle(QStringLiteral("外部链接")).setWidget(panel).setCloseable(false);
    m_workspace->fillPane(linksPane, config);
}

void PersonEditorPage::configureAvatar(const QString &purpose, const QString &placeholder)
{
    // Python 的 ActressAvatarDropWidget / ActorAvatarDropWidget 会在头像窗格中
    // 以 1:1 比例居中，不给外层附加固定最小尺寸。
    m_imageDrop->setPurpose(purpose, placeholder);
    m_imageDrop->setPreviewAspectRatio(1.0);
    m_imageDrop->setMinimumSize(0, 0);
    m_imageDrop->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PersonEditorPage::setAvatarImagePath(const QString &path)
{
    m_imageDrop->setImagePath(path);
    updateDirtyState();
}

void PersonEditorPage::addActionButton(QWidget *widget)
{
    if (widget == nullptr || m_actionsLayout == nullptr)
        return;
    m_actionsLayout->insertWidget(m_actionsLayout->count() - 1, widget);
}

void PersonEditorPage::setCancelButtonVisible(bool visible)
{
    if (m_cancelButton != nullptr)
        m_cancelButton->setVisible(visible);
}

bool PersonEditorPage::deleteCurrentPerson(QString *errorMessage)
{
    if (!m_original.has_value())
    {
        if (errorMessage != nullptr) *errorMessage = QStringLiteral("当前没有正在编辑的人物");
        return false;
    }
    const PersonDetails details = *m_original;
    if (!m_repository.deletePerson(details.kind, details.id, errorMessage))
        return false;
    const QFileInfo image(details.imagePath);
    const QString imagePath = image.isAbsolute() ? image.absoluteFilePath()
                                                  : QDir(m_imageDirectory).filePath(details.imagePath);
    if (!details.imagePath.trimmed().isEmpty()) QFile::remove(imagePath);
    m_original.reset();
    return true;
}

void PersonEditorPage::applyActressCaptureFields(const QJsonObject &capture)
{
    if (!m_original.has_value() || m_original->kind != PersonKind::Actress)
        return;

    const auto number = [&capture](const QString &key)
    {
        bool ok = false;
        const int value = capture.value(key).toVariant().toInt(&ok);
        return ok ? value : 0;
    };
    const auto text = [&capture](const QString &key)
    { return capture.value(key).toString().trimmed(); };

    m_populating = true;
    m_height->setValue(number(QStringLiteral("身高")));
    m_bust->setValue(number(QStringLiteral("胸围")));
    m_waist->setValue(number(QStringLiteral("腰围")));
    m_hip->setValue(number(QStringLiteral("臀围")));
    m_cup->setCurrentIndex(m_cup->findText(text(QStringLiteral("罩杯"))));
    m_birthday->setText(text(QStringLiteral("出生日期")));
    m_debutDate->setText(text(QStringLiteral("出道日期")));
    const QString minnanoId = text(QStringLiteral("minnano_actress_id"));
    if (!minnanoId.isEmpty())
        m_minnanoUrl->setText(minnanoId);
    m_needUpdate->setChecked(false);

    QList<PersonName> names = editorDetails().names;
    if (names.isEmpty())
        names.append(PersonName{});
    const QString japanese = text(QStringLiteral("日文名"));
    const QString kana = text(QStringLiteral("假名"));
    const QString english = text(QStringLiteral("英文名"));
    if (!japanese.isEmpty())
        names[0].japanese = japanese;
    if (!kana.isEmpty())
        names[0].kana = kana;
    if (!english.isEmpty())
        names[0].english = english;
    const QJsonArray aliases = capture.value(QStringLiteral("alias_chain")).toArray();
    for (qsizetype index = 0; index < aliases.size(); ++index)
    {
        const QJsonObject alias = aliases.at(index).toObject();
        const qsizetype row = index + 1;
        if (row >= names.size())
            names.append(PersonName{});
        const QString aliasJapanese = alias.value(QStringLiteral("jp")).toString().trimmed();
        const QString aliasKana = alias.value(QStringLiteral("kana")).toString().trimmed();
        const QString aliasEnglish = alias.value(QStringLiteral("en")).toString().trimmed();
        if (!aliasJapanese.isEmpty()) names[row].japanese = aliasJapanese;
        if (!aliasKana.isEmpty()) names[row].kana = aliasKana;
        if (!aliasEnglish.isEmpty()) names[row].english = aliasEnglish;
    }
    m_names->setRowCount(0);
    for (const PersonName &name : names)
        addNameRow(name);
    m_populating = false;
    updateDirtyState();
}

QString PersonEditorPage::primaryJapaneseName() const
{
    if (m_names->rowCount() == 0 || m_names->item(0, 1) == nullptr)
        return {};
    return m_names->item(0, 1)->text().trimmed();
}

QString PersonEditorPage::primaryChineseName() const
{
    if (m_names->rowCount() == 0 || m_names->item(0, 0) == nullptr)
        return {};
    return m_names->item(0, 0)->text().trimmed();
}

QList<QString> PersonEditorPage::japaneseNames() const
{
    QList<QString> result;
    result.reserve(m_names->rowCount());
    for (int row = 0; row < m_names->rowCount(); ++row)
        result.append(m_names->item(row, 1) == nullptr ? QString()
                                                        : m_names->item(row, 1)->text().trimmed());
    return result;
}

QList<QString> PersonEditorPage::chineseNames() const
{
    QList<QString> result;
    result.reserve(m_names->rowCount());
    for (int row = 0; row < m_names->rowCount(); ++row)
        result.append(m_names->item(row, 0) == nullptr ? QString()
                                                        : m_names->item(row, 0)->text().trimmed());
    return result;
}

void PersonEditorPage::replaceChineseNames(const QList<QString> &translations, bool overwrite)
{
    bool changed = false;
    m_populating = true;
    for (int row = 0; row < m_names->rowCount() && row < translations.size(); ++row)
    {
        const QString translation = translations.at(row).trimmed();
        if (translation.isEmpty())
            continue;
        auto *item = m_names->item(row, 0);
        if (item == nullptr)
        {
            item = new QTableWidgetItem;
            m_names->setItem(row, 0, item);
        }
        if ((overwrite || item->text().trimmed().isEmpty()) && item->text() != translation)
        {
            item->setText(translation);
            changed = true;
        }
    }
    m_populating = false;
    updateDirtyState();
    // Programmatic table updates intentionally suppress itemChanged while filling, but the
    // editor's consumers still need the same notification as a user edit.  In particular this
    // keeps the translated names and the enabled submit action in the active edit session.
    if (changed)
        emit namesChanged();
}

bool PersonEditorPage::loadPerson(PersonKind kind, qint64 personId)
{
    QString errorMessage;
    const auto details = m_repository.findDetails(kind, personId, &errorMessage);
    if (!details.has_value())
    {
        Toast::showError(parentWidget(),
                         errorMessage.isEmpty() ? QStringLiteral("人物不存在") : errorMessage,
                         &m_themes);
        return false;
    }
    m_original = details;
    setWindowTitle(kind == PersonKind::Actress ? QStringLiteral("修改女演员")
                                               : QStringLiteral("修改男演员"));
    m_populating = true;
    populate(*details);
    m_populating = false;
    resetDirtyState();
    return true;
}

bool PersonEditorPage::savePerson()
{
    if (!m_original.has_value())
        return false;
    QString errorMessage;
    PersonDetails details = editorDetails();
    const PersonName primary = details.names.isEmpty() ? PersonName{} : details.names.first();
    QString fileNamePart = primary.japanese.trimmed();
    if (fileNamePart.isEmpty())
        fileNamePart = primary.chinese.trimmed();
    if (fileNamePart.isEmpty())
        fileNamePart = primary.english.trimmed();
    if (fileNamePart.isEmpty())
        fileNamePart = QStringLiteral("person");
    QString storedImagePath;
    if (!m_imageDrop->persistAsJpeg(QStringLiteral("%1-%2.jpg").arg(details.id).arg(fileNamePart),
                                    &storedImagePath, &errorMessage))
    {
        Toast::showError(this, errorMessage, &m_themes);
        return false;
    }
    details.imagePath = storedImagePath;
    if (!m_repository.updateDetails(details, &errorMessage))
    {
        Toast::showError(this, errorMessage, &m_themes);
        return false;
    }
    m_original = m_repository.findDetails(details.kind, details.id);
    if (m_original.has_value())
    {
        m_populating = true;
        populate(*m_original);
        m_populating = false;
    }
    resetDirtyState();
    emit personSaved(details.kind, details.id);
    return true;
}

qint64 PersonEditorPage::personId() const noexcept
{
    return m_original.has_value() ? m_original->id : 0;
}

void PersonEditorPage::populate(const PersonDetails &details)
{
    m_imageDrop->setImagePath(details.imagePath);
    m_birthday->setText(details.birthday);
    setOptionalSpin(m_height, details.height);
    m_needUpdate->setChecked(details.needUpdate);
    m_notes->setPlainText(details.notes);
    m_actressFields->setVisible(details.kind == PersonKind::Actress);
    m_actorFields->setVisible(details.kind == PersonKind::Actor);
    setOptionalSpin(m_bust, details.bust);
    setOptionalSpin(m_waist, details.waist);
    setOptionalSpin(m_hip, details.hip);
    m_cup->setCurrentIndex(m_cup->findText(details.cup));
    m_debutDate->setText(details.debutDate);
    m_minnanoUrl->setText(details.minnanoUrl);
    const auto setScore = [](QComboBox *combo, const std::optional<int> &score)
    { combo->setCurrentIndex(score.has_value() ? combo->findData(*score) : 0); };
    setScore(m_handsome, details.handsome);
    setScore(m_fat, details.fat);
    m_names->setRowCount(0);
    for (const PersonName &name : details.names)
        addNameRow(name);
    if (details.names.isEmpty())
        addNameRow();
}

PersonDetails PersonEditorPage::editorDetails() const
{
    PersonDetails details = *m_original;
    details.imagePath = m_imageDrop->imagePath();
    details.birthday = m_birthday->text();
    details.height = optionalSpinValue(m_height);
    details.needUpdate = m_needUpdate->isChecked();
    details.notes = m_notes->toPlainText();
    details.names.clear();
    for (int row = 0; row < m_names->rowCount(); ++row)
    {
        const auto text = [this, row](int column)
        {
            const auto *item = m_names->item(row, column);
            return item == nullptr ? QString() : item->text().trimmed();
        };
        // Keep the database identity with its table row.  The Python editor sends
        // actress_name_id back to its update routine; losing it here used to make
        // every edit look like a full replacement of the name chain.
        const auto *firstItem = m_names->item(row, 0);
        const qint64 nameId = firstItem == nullptr ? 0
                                                    : firstItem->data(Qt::UserRole).toLongLong();
        details.names.append({nameId, text(0), text(1), text(2), text(3)});
    }
    if (details.kind == PersonKind::Actress)
    {
        details.bust = optionalSpinValue(m_bust);
        details.waist = optionalSpinValue(m_waist);
        details.hip = optionalSpinValue(m_hip);
        details.cup = m_cup->currentText();
        details.debutDate = m_debutDate->text();
        details.minnanoUrl = m_minnanoUrl->text();
    }
    else
    {
        const auto score = [](const QComboBox *combo) -> std::optional<int>
        {
            return combo->currentIndex() == 0 ? std::nullopt
                                              : std::optional<int>(combo->currentData().toInt());
        };
        details.handsome = score(m_handsome);
        details.fat = score(m_fat);
    }
    return details;
}

void PersonEditorPage::updateDirtyState()
{
    if (m_populating || !m_original.has_value())
        return;

    const PersonDetails current = editorDetails();
    const PersonDetails &original = *m_original;
    const bool imageDirty = current.imagePath != original.imagePath;
    const bool birthdayDirty = current.birthday != original.birthday;
    const bool heightDirty = current.height != original.height;
    const bool needUpdateDirty = current.needUpdate != original.needUpdate;
    const bool notesDirty = current.notes != original.notes;
    const bool namesDirty = !sameNames(current.names, original.names);

    m_imageDrop->setDirty(imageDirty);
    setDirtyStyle(m_birthday, birthdayDirty, QStringLiteral("QLineEdit#DesignInput"));
    setDirtyStyle(m_height, heightDirty, QStringLiteral("QSpinBox#DesignSpinBox"));
    setDirtyStyle(m_needUpdate, needUpdateDirty, QStringLiteral("QCheckBox#DesignCheckBox"));
    setDirtyStyle(m_notes, notesDirty, QStringLiteral("QTextEdit#DesignTextEdit"));
    setDirtyStyle(m_names, namesDirty, QStringLiteral("QTableWidget#DesignTableWidget"));

    bool hasChanges = imageDirty || birthdayDirty || heightDirty || needUpdateDirty || notesDirty ||
                      namesDirty;
    if (original.kind == PersonKind::Actress)
    {
        const bool bustDirty = current.bust != original.bust;
        const bool waistDirty = current.waist != original.waist;
        const bool hipDirty = current.hip != original.hip;
        const bool cupDirty = current.cup != original.cup;
        const bool debutDateDirty = current.debutDate != original.debutDate;
        const bool minnanoDirty = current.minnanoUrl != original.minnanoUrl;
        setDirtyStyle(m_bust, bustDirty, QStringLiteral("QSpinBox#DesignSpinBox"));
        setDirtyStyle(m_waist, waistDirty, QStringLiteral("QSpinBox#DesignSpinBox"));
        setDirtyStyle(m_hip, hipDirty, QStringLiteral("QSpinBox#DesignSpinBox"));
        setDirtyStyle(m_cup, cupDirty, QStringLiteral("QComboBox#DesignComboBox"));
        setDirtyStyle(m_debutDate, debutDateDirty, QStringLiteral("QLineEdit#DesignInput"));
        setDirtyStyle(m_minnanoUrl, minnanoDirty, QStringLiteral("QLineEdit#DesignInput"));
        hasChanges = hasChanges || bustDirty || waistDirty || hipDirty || cupDirty ||
                     debutDateDirty || minnanoDirty;
    }
    else
    {
        const bool handsomeDirty = current.handsome != original.handsome;
        const bool fatDirty = current.fat != original.fat;
        setDirtyStyle(m_handsome, handsomeDirty, QStringLiteral("QComboBox#DesignComboBox"));
        setDirtyStyle(m_fat, fatDirty, QStringLiteral("QComboBox#DesignComboBox"));
        hasChanges = hasChanges || handsomeDirty || fatDirty;
    }

    m_saveButton->setEnabled(hasChanges);
    m_saveButton->setStyleSheet(hasChanges
        ? QStringLiteral("QPushButton#DesignButton { background-color: #FFA500; color: white; "
                         "border-radius: 5px; padding: 6px; }")
        : QStringLiteral("QPushButton#DesignButton { background-color: #999999; color: #CCCCCC; "
                         "border-radius: 5px; padding: 6px; }"));
}

void PersonEditorPage::resetDirtyState()
{
    m_imageDrop->setDirty(false);
    setDirtyStyle(m_birthday, false, QStringLiteral("QLineEdit#DesignInput"));
    setDirtyStyle(m_height, false, QStringLiteral("QSpinBox#DesignSpinBox"));
    setDirtyStyle(m_bust, false, QStringLiteral("QSpinBox#DesignSpinBox"));
    setDirtyStyle(m_waist, false, QStringLiteral("QSpinBox#DesignSpinBox"));
    setDirtyStyle(m_hip, false, QStringLiteral("QSpinBox#DesignSpinBox"));
    setDirtyStyle(m_cup, false, QStringLiteral("QComboBox#DesignComboBox"));
    setDirtyStyle(m_debutDate, false, QStringLiteral("QLineEdit#DesignInput"));
    setDirtyStyle(m_minnanoUrl, false, QStringLiteral("QLineEdit#DesignInput"));
    setDirtyStyle(m_handsome, false, QStringLiteral("QComboBox#DesignComboBox"));
    setDirtyStyle(m_fat, false, QStringLiteral("QComboBox#DesignComboBox"));
    setDirtyStyle(m_needUpdate, false, QStringLiteral("QCheckBox#DesignCheckBox"));
    setDirtyStyle(m_notes, false, QStringLiteral("QTextEdit#DesignTextEdit"));
    setDirtyStyle(m_names, false, QStringLiteral("QTableWidget#DesignTableWidget"));
    m_saveButton->setEnabled(false);
    m_saveButton->setStyleSheet(QStringLiteral(
        "QPushButton#DesignButton { background-color: #999999; color: #CCCCCC; "
        "border-radius: 5px; padding: 6px; }"));
}

void PersonEditorPage::setDirtyStyle(QWidget *widget, bool dirty, const QString &selector)
{
    widget->setStyleSheet(dirty ? QStringLiteral("%1 { border: 2px solid #FFA500; }").arg(selector)
                                : QString());
}

bool PersonEditorPage::sameNames(const QList<PersonName> &left, const QList<PersonName> &right)
{
    if (left.size() != right.size())
        return false;
    for (qsizetype index = 0; index < left.size(); ++index)
    {
        const PersonName &a = left.at(index);
        const PersonName &b = right.at(index);
        if (a.chinese != b.chinese || a.japanese != b.japanese || a.english != b.english ||
            a.kana != b.kana)
            return false;
    }
    return true;
}

void PersonEditorPage::addNameRow(const PersonName &name)
{
    const int row = m_names->rowCount();
    m_names->insertRow(row);
    const QStringList values{name.chinese, name.japanese, name.english, name.kana};
    for (int column = 0; column < values.size(); ++column)
    {
        auto *item = new QTableWidgetItem(values.at(column));
        // Storing it on the first cell makes row moves automatically retain the
        // identity while keeping the table's visible columns unchanged.
        if (column == 0)
            item->setData(Qt::UserRole, name.id);
        m_names->setItem(row, column, item);
    }
    updateDirtyState();
}

void PersonEditorPage::removeSelectedNameRows()
{
    QList<int> rows;
    for (const QModelIndex &index : m_names->selectionModel()->selectedRows())
        rows.append(index.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (const int row : rows)
        m_names->removeRow(row);
    if (m_names->rowCount() == 0)
        addNameRow();
    updateDirtyState();
    emit namesChanged();
}

void PersonEditorPage::moveSelectedNameRow(int offset)
{
    const QModelIndexList selectedRows = m_names->selectionModel()->selectedRows();
    if (selectedRows.size() != 1)
        return;
    const int sourceRow = selectedRows.first().row();
    const int targetRow = sourceRow + offset;
    if (targetRow < 0 || targetRow >= m_names->rowCount())
        return;
    for (int column = 0; column < m_names->columnCount(); ++column)
    {
        QTableWidgetItem *source = m_names->takeItem(sourceRow, column);
        QTableWidgetItem *target = m_names->takeItem(targetRow, column);
        m_names->setItem(sourceRow, column, target);
        m_names->setItem(targetRow, column, source);
    }
    m_names->selectRow(targetRow);
    updateDirtyState();
    emit namesChanged();
}

std::optional<int> PersonEditorPage::optionalSpinValue(const QSpinBox *spinBox)
{
    return spinBox->value() == 0 ? std::nullopt : std::optional<int>(spinBox->value());
}

} // namespace darkeye

namespace darkeye {
void PersonEditorPage::closeEvent(QCloseEvent *event) {
    updateDirtyState();
    if (!m_saveButton->isEnabled()) { event->accept(); return; }
    const auto choice = QMessageBox::question(this, QStringLiteral("未保存的修改"),
        QStringLiteral("关闭前保存人物资料的修改？"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel || (choice == QMessageBox::Save && !savePerson())) event->ignore();
    else event->accept();
}
}
