#include "ui/pages/management/TagManagementWidget.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/components/TokenControls.h"
#include "darkeye_ui/components/TokenViews.h"
#include "ui/components/WorkTagSelector.h"

#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace darkeye
{
namespace
{
QList<TagOption> toTagOptions(const QList<TagRecord> &records)
{
    QList<TagOption> options;
    options.reserve(records.size());
    for (const TagRecord &record : records)
    {
        TagOption option;
        option.id = record.id;
        option.name = record.name;
        option.typeName = record.typeName;
        option.color = record.color;
        option.detail = record.detail;
        option.mutexGroup = record.groupId.has_value() ? QString::number(*record.groupId) : QString();
        option.aliases = record.aliases;
        options.append(option);
    }
    return options;
}
} // namespace

TagManagementWidget::TagManagementWidget(QSqlDatabase database, ThemeService &themes,
                                         QWidget *parent)
    : QWidget(parent), m_database(std::move(database)), m_repository(m_database), m_themes(themes)
{
    buildUi();
    refreshTagTypes();
    refresh();
}

void TagManagementWidget::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *editorColumn = new QWidget(this);
    editorColumn->setMaximumWidth(400);
    auto *editorColumnLayout = new QVBoxLayout(editorColumn);
    editorColumnLayout->setContentsMargins(0, 0, 0, 0);
    auto *previewGroup = new TokenGroupBox(QStringLiteral("标签展示"), editorColumn);
    auto *previewRow = new QHBoxLayout(previewGroup);
    m_preview = new TagDisplayPreview(&m_themes, previewGroup);
    previewRow->addWidget(m_preview);
    editorColumnLayout->addWidget(previewGroup);

    auto *editor = new TokenGroupBox(QStringLiteral("标签详情"), editorColumn);
    auto *form = new QFormLayout(editor);
    m_name = new DesignLineEdit(editor);
    m_type = new DesignComboBox(editor);
    m_color = new ColorPicker(QColor(QStringLiteral("#cccccc")), true, ColorPicker::Shape::Circle,
                              editor);
    m_color->setMaximumHeight(40);
    m_detail = new DesignLineEdit(editor);
    m_detail->setObjectName(QStringLiteral("TagDetailInput"));
    auto *aliasEditor = new QWidget(editor);
    auto *aliasLayout = new QVBoxLayout(aliasEditor);
    aliasLayout->setContentsMargins(0, 0, 0, 0);
    auto *aliasButtons = new QHBoxLayout;
    auto *addAlias = new IconButton(QStringLiteral("list_plus"), &m_themes, aliasEditor);
    addAlias->setToolTip(QStringLiteral("添加重定向别名"));
    auto *removeAlias = new IconButton(QStringLiteral("list_x"), &m_themes, aliasEditor);
    removeAlias->setToolTip(QStringLiteral("删除选中的别名"));
    removeAlias->setEnabled(false);
    aliasButtons->addWidget(addAlias);
    aliasButtons->addWidget(removeAlias);
    aliasButtons->addStretch();
    aliasLayout->addLayout(aliasButtons);
    m_aliases = new TokenTableWidget(aliasEditor);
    m_aliases->setObjectName(QStringLiteral("TagAliasTable"));
    m_aliases->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_aliases->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_aliases->setSelectionMode(QAbstractItemView::SingleSelection);
    m_aliases->setEditTriggers(QAbstractItemView::DoubleClicked
                              | QAbstractItemView::EditKeyPressed
                              | QAbstractItemView::AnyKeyPressed);
    aliasLayout->addWidget(m_aliases);
    const auto appendAlias = [this]
    {
        const int row = m_aliases->rowCount();
        m_aliases->insertRow(row);
        if (m_aliases->columnCount() == 0)
            return;
        auto *item = new QTableWidgetItem;
        m_aliases->setItem(row, 0, item);
        m_aliases->setCurrentItem(item);
        m_aliases->scrollToBottom();
        m_aliases->editItem(item);
    };
    const auto deleteAlias = [this]
    {
        if (!m_aliases->selectedItems().isEmpty())
            m_aliases->removeRow(m_aliases->currentRow());
    };
    connect(addAlias, &QPushButton::clicked, this, appendAlias);
    connect(removeAlias, &QPushButton::clicked, this, deleteAlias);
    connect(m_aliases, &TokenTableWidget::addRequested, this, appendAlias);
    connect(m_aliases, &TokenTableWidget::deleteRequested, this, deleteAlias);
    connect(m_aliases, &QTableWidget::itemSelectionChanged, removeAlias,
            [this, removeAlias]
            { removeAlias->setEnabled(!m_aliases->selectedItems().isEmpty()); });
    form->addRow(QStringLiteral("标签名字"), m_name);
    form->addRow(QStringLiteral("标签类型"), m_type);
    form->addRow(QStringLiteral("颜色"), m_color);
    form->addRow(QStringLiteral("细节"), m_detail);
    form->addRow(QStringLiteral("重定向"), aliasEditor);
    editorColumnLayout->addWidget(editor);

    auto *saveButton = new DesignButton(QStringLiteral("提交"), editorColumn);
    saveButton->setVariant(QStringLiteral("primary"));
    auto *removeButton = new DesignButton(QStringLiteral("删除"), editorColumn);
    removeButton->setVariant(QStringLiteral("danger"));
    auto *tagTypeButton = new DesignButton(QStringLiteral("修改标签类型"), editorColumn);
    editorColumnLayout->addWidget(saveButton);
    editorColumnLayout->addWidget(removeButton);
    editorColumnLayout->addWidget(tagTypeButton);
    root->addWidget(editorColumn);

    m_tagSelector = new WorkTagSelector({}, &m_themes, this);
    // Python narrows the view while retaining the original 154px left container.
    m_tagSelector->setSelectedColumnWidth(116, 154);
    m_tagSelector->setAvailablePanelWidth(400);
    m_tagSelector->setMutexCheckEnabled(false);
    m_tagSelector->setAvailablePanelExpanded(true);
    m_tagSelector->setLoader(
        [this]
        {
            QString errorMessage;
            m_records = m_repository.listTags(&errorMessage);
            if (!errorMessage.isEmpty())
            {
                Toast::showError(window(), errorMessage, &m_themes);
                return QList<TagOption>{};
            }
            return toTagOptions(m_records);
        });
    root->addWidget(m_tagSelector, 1, Qt::AlignLeft);

    m_multiSelectGroup = new TokenGroupBox(QStringLiteral("多选操作"), this);
    m_multiSelectGroup->setMaximumWidth(240);
    auto *multiSelectLayout = new QVBoxLayout(m_multiSelectGroup);
    auto *colorGroup = new TokenGroupBox(QStringLiteral("改多个标签颜色"), m_multiSelectGroup);
    auto *colorLayout = new QVBoxLayout(colorGroup);
    m_multiSelectColor = new ColorPicker(QColor(QStringLiteral("#cccccc")), true,
                                         ColorPicker::Shape::Circle, colorGroup);
    auto *changeColorButton = new DesignButton(QStringLiteral("批量改变标签颜色"), colorGroup);
    colorLayout->addWidget(m_multiSelectColor);
    colorLayout->addWidget(changeColorButton);
    auto *redirectButton = new DesignButton(QStringLiteral("标签重定向（仅两个标签）"),
                                            m_multiSelectGroup);
    redirectButton->setToolTip(QStringLiteral("选择两个标签后，指定保留的标签。"));
    multiSelectLayout->addWidget(colorGroup);
    multiSelectLayout->addWidget(redirectButton);
    multiSelectLayout->addStretch();
    root->addWidget(m_multiSelectGroup);
    m_multiSelectGroup->setEnabled(false);

    connect(m_tagSelector, &WorkTagSelector::selectionChanged, this,
            &TagManagementWidget::updateSelectionState);
    connect(saveButton, &QPushButton::clicked, this, &TagManagementWidget::saveCurrent);
    connect(removeButton, &QPushButton::clicked, this, &TagManagementWidget::removeCurrent);
    connect(tagTypeButton, &QPushButton::clicked, this, &TagManagementWidget::openTagTypeManager);
    connect(changeColorButton, &QPushButton::clicked, this,
            &TagManagementWidget::changeSelectedColors);
    connect(redirectButton, &QPushButton::clicked, this,
            &TagManagementWidget::redirectSelectedTags);
    connect(m_name, &QLineEdit::textChanged, this, &TagManagementWidget::updatePreview);
    connect(m_color, &ColorPicker::colorChanged, this, &TagManagementWidget::updatePreview);
    connect(m_detail, &QLineEdit::textChanged, this, &TagManagementWidget::updatePreview);
}

void TagManagementWidget::refresh()
{
    QString errorMessage;
    m_records = m_repository.listTags(&errorMessage);
    if (!errorMessage.isEmpty())
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    m_tagSelector->reloadTags();
    if (m_currentId > 0)
    {
        const auto found = std::find_if(
            m_records.cbegin(), m_records.cend(),
            [this](const TagRecord &record) { return record.id == m_currentId; });
        if (found != m_records.cend())
        {
            m_tagSelector->setSelectedIds({m_currentId});
            selectTag(m_currentId);
            return;
        }
    }
    beginNewTag();
}

void TagManagementWidget::refreshTagTypes()
{
    QComboBox *const typeCombo = m_type;
    if (typeCombo == nullptr)
        return;

    const std::optional<qint64> selected =
        typeCombo->currentData().isValid()
            ? std::optional<qint64>(typeCombo->currentData().toLongLong())
            : std::nullopt;
    QString errorMessage;
    const QList<TagTypeRecord> types = m_repository.listTagTypes(&errorMessage);
    if (!errorMessage.isEmpty())
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    typeCombo->clear();
    typeCombo->addItem(QStringLiteral("未分类"), QVariant());
    for (const TagTypeRecord &type : types)
        typeCombo->addItem(type.name, type.id);
    if (selected.has_value())
    {
        const int index = typeCombo->findData(*selected);
        typeCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
}

void TagManagementWidget::selectTag(qint64 tagId)
{
    const auto found = std::find_if(
        m_records.cbegin(), m_records.cend(),
        [tagId](const TagRecord &record) { return record.id == tagId; });
    if (found == m_records.cend())
        return;
    const TagRecord &record = *found;
    m_currentId = record.id;
    m_name->setText(record.name);
    const int typeIndex = record.typeId.has_value() ? m_type->findData(*record.typeId) : 0;
    m_type->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
    m_color->setColor(record.color);
    m_detail->setText(record.detail);
    m_aliases->setRowCount(0);
    m_aliases->setColumnCount(record.aliases.isEmpty() ? 0 : 1);
    if (!record.aliases.isEmpty())
        m_aliases->setHorizontalHeaderLabels({QStringLiteral("tag_name")});
    for (const QString &alias : record.aliases)
    {
        const int row = m_aliases->rowCount();
        m_aliases->insertRow(row);
        m_aliases->setItem(row, 0, new QTableWidgetItem(alias));
    }
    updatePreview();
}

void TagManagementWidget::beginNewTag()
{
    m_currentId = 0;
    m_tagSelector->clearSelection();
    m_name->clear();
    m_type->setCurrentIndex(0);
    m_color->setColor(QStringLiteral("#cccccc"));
    m_detail->clear();
    m_aliases->setRowCount(0);
    m_aliases->setColumnCount(0);
    updatePreview();
}

void TagManagementWidget::saveCurrent()
{
    TagRecord record = editorRecord();
    QString errorMessage;
    if (record.id == 0)
    {
        const auto id = m_repository.createTag(record, &errorMessage);
        if (!id.has_value())
        {
            Toast::showError(window(), errorMessage, &m_themes);
            return;
        }
        m_currentId = *id;
    }
    else if (!m_repository.updateTag(record, &errorMessage))
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    refresh();
    emit tagsChanged();
    Toast::showSuccess(window(), QStringLiteral("标签已保存"), &m_themes);
}

void TagManagementWidget::removeCurrent()
{
    if (m_currentId <= 0)
        return;
    if (QMessageBox::question(this, QStringLiteral("确认删除"),
                              QStringLiteral("确定删除当前标签及其别名？已被作品使用时会拒绝。")) !=
        QMessageBox::Yes)
        return;
    QString errorMessage;
    if (!m_repository.removeTag(m_currentId, &errorMessage))
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    m_currentId = 0;
    refresh();
    emit tagsChanged();
}

QList<qint64> TagManagementWidget::selectedTagIds() const
{
    return m_tagSelector->selectedIds();
}

void TagManagementWidget::updateSelectionState()
{
    const QList<qint64> ids = selectedTagIds();
    m_multiSelectGroup->setEnabled(ids.size() > 1);
    if (ids.size() == 1)
    {
        selectTag(ids.constFirst());
        return;
    }
    if (ids.isEmpty())
        beginNewTag();
}

void TagManagementWidget::changeSelectedColors()
{
    const QList<qint64> ids = selectedTagIds();
    if (ids.size() < 2)
    {
        Toast::showWarning(window(), QStringLiteral("请至少选择两个标签"), &m_themes);
        return;
    }
    QString errorMessage;
    if (!m_repository.updateTagColors(ids, m_multiSelectColor->color(), &errorMessage))
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    refresh();
    emit tagsChanged();
    Toast::showSuccess(window(), QStringLiteral("标签颜色已更新"), &m_themes);
}

void TagManagementWidget::redirectSelectedTags()
{
    const QList<qint64> ids = selectedTagIds();
    if (ids.size() != 2)
    {
        Toast::showWarning(window(), QStringLiteral("请选择两个标签进行重定向"), &m_themes);
        return;
    }
    const auto findRecord = [this](qint64 id) -> const TagRecord *
    {
        const auto found = std::find_if(m_records.cbegin(), m_records.cend(),
                                        [id](const TagRecord &record) { return record.id == id; });
        return found == m_records.cend() ? nullptr : &*found;
    };
    const TagRecord *first = findRecord(ids.at(0));
    const TagRecord *second = findRecord(ids.at(1));
    if (first == nullptr || second == nullptr)
        return;

    QMessageBox choice(this);
    choice.setWindowTitle(QStringLiteral("选择重定向方向"));
    choice.setText(QStringLiteral("请选择要保留的标签："));
    auto *keepFirst = choice.addButton(QStringLiteral("保留 %1").arg(first->name),
                                       QMessageBox::ActionRole);
    auto *keepSecond = choice.addButton(QStringLiteral("保留 %1").arg(second->name),
                                        QMessageBox::ActionRole);
    choice.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    choice.exec();
    const qint64 targetId = choice.clickedButton() == keepFirst ? first->id
                          : choice.clickedButton() == keepSecond ? second->id
                                                                 : 0;
    if (targetId == 0)
        return;
    const qint64 sourceId = targetId == first->id ? second->id : first->id;
    if (QMessageBox::question(
            this, QStringLiteral("确认重定向"),
            QStringLiteral("来源标签及其别名会指向保留标签，作品关联会自动合并。")) !=
        QMessageBox::Yes)
        return;
    QString errorMessage;
    if (!m_repository.redirectTag(sourceId, targetId, &errorMessage))
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    m_currentId = targetId;
    refresh();
    emit tagsChanged();
    Toast::showSuccess(window(), QStringLiteral("标签重定向完成"), &m_themes);
}

void TagManagementWidget::openTagTypeManager()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("修改标签类型"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *manager = new TagTypeManagementWidget(m_database, m_themes, &dialog);
    layout->addWidget(manager);
    dialog.resize(600, 420);
    connect(manager, &TagTypeManagementWidget::typesChanged, this,
            [this]
            {
                refreshTagTypes();
                refresh();
                emit tagTypesChanged();
            });
    dialog.exec();
}

void TagManagementWidget::updatePreview()
{
    m_preview->setTag(m_name->text(), m_color->color(), m_detail->text());
}

TagRecord TagManagementWidget::editorRecord() const
{
    TagRecord record;
    for (const TagRecord &candidate : m_records)
    {
        if (candidate.id == m_currentId)
        {
            record = candidate;
            break;
        }
    }
    record.id = m_currentId;
    record.name = m_name->text();
    if (m_type->currentData().isValid())
        record.typeId = m_type->currentData().toLongLong();
    else
        record.typeId.reset();
    record.color = m_color->color();
    record.detail = m_detail->text();
    record.aliases.clear();
    for (int row = 0; row < m_aliases->rowCount(); ++row)
    {
        const auto *item = m_aliases->item(row, 0);
        if (item != nullptr && !item->text().trimmed().isEmpty())
            record.aliases.append(item->text().trimmed());
    }
    return record;
}

TagTypeManagementWidget::TagTypeManagementWidget(QSqlDatabase database, ThemeService &themes,
                                                 QWidget *parent)
    : QWidget(parent), m_repository(std::move(database)), m_themes(themes)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    setObjectName(QStringLiteral("TagTypeManagementWidget"));
    auto *reorderableTable = new ReorderableTokenTableWidget(this);
    m_table = reorderableTable;
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("标签类型"), QStringLiteral("顺序")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(reorderableTable, &ReorderableTokenTableWidget::rowsReordered, this,
            &TagTypeManagementWidget::moveRow);
    root->addWidget(m_table, 1);
    auto *form = new QFormLayout;
    m_name = new DesignLineEdit(this);
    form->addRow(QStringLiteral("类型名称"), m_name);
    root->addLayout(form);
    auto *buttons = new QHBoxLayout;
    const auto iconButton = [this](const QString &icon, const QString &toolTip) {
        auto *button = new IconButton(icon, &m_themes, this);
        button->setToolTip(toolTip);
        return button;
    };
    auto *newButton = iconButton(QStringLiteral("list_plus"), QStringLiteral("新建"));
    auto *saveButton = iconButton(QStringLiteral("save"), QStringLiteral("保存"));
    auto *removeButton = iconButton(QStringLiteral("list_x"), QStringLiteral("删除"));
    auto *upButton = iconButton(QStringLiteral("arrow_up"), QStringLiteral("上移"));
    auto *downButton = iconButton(QStringLiteral("arrow_down"), QStringLiteral("下移"));
    for (QPushButton *button : {newButton, saveButton, removeButton, upButton, downButton})
        buttons->addWidget(button);
    buttons->addStretch();
    root->addLayout(buttons);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) { selectRow(row); });
    const auto addType = [this] {
        beginNewType();
        m_name->setFocus();
    };
    connect(newButton, &QPushButton::clicked, this, addType);
    connect(saveButton, &QPushButton::clicked, this, &TagTypeManagementWidget::saveCurrent);
    connect(removeButton, &QPushButton::clicked, this, &TagTypeManagementWidget::removeCurrent);
    connect(upButton, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
    connect(downButton, &QPushButton::clicked, this, [this] { moveCurrent(1); });
    connect(m_table, &TokenTableWidget::addRequested, this, addType);
    connect(m_table, &TokenTableWidget::deleteRequested, this,
            &TagTypeManagementWidget::removeCurrent);
    connect(m_table, &TokenTableWidget::submitRequested, this,
            &TagTypeManagementWidget::saveCurrent);
    refresh();
}

void TagTypeManagementWidget::refresh()
{
    QString errorMessage;
    m_records = m_repository.listTagTypes(&errorMessage);
    if (!errorMessage.isEmpty())
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    m_table->setRowCount(m_records.size());
    for (qsizetype row = 0; row < m_records.size(); ++row)
    {
        const TagTypeRecord &record = m_records.at(row);
        m_table->setItem(row, 0, new QTableWidgetItem(QString::number(record.id)));
        m_table->setItem(row, 1, new QTableWidgetItem(record.name));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::number(record.order)));
    }
    if (m_currentId > 0)
    {
        for (qsizetype row = 0; row < m_records.size(); ++row)
        {
            if (m_records.at(row).id == m_currentId)
            {
                m_table->selectRow(row);
                selectRow(row);
                return;
            }
        }
    }
    beginNewType();
}

void TagTypeManagementWidget::selectRow(int row)
{
    if (row < 0 || row >= m_records.size())
        return;
    m_currentId = m_records.at(row).id;
    m_name->setText(m_records.at(row).name);
}

void TagTypeManagementWidget::beginNewType()
{
    m_currentId = 0;
    m_table->clearSelection();
    m_name->clear();
}

void TagTypeManagementWidget::saveCurrent()
{
    QString errorMessage;
    if (m_currentId == 0)
    {
        const auto id =
            m_repository.createTagType(m_name->text(), m_records.size() + 1, &errorMessage);
        if (!id.has_value())
        {
            Toast::showError(window(), errorMessage, &m_themes);
            return;
        }
        m_currentId = *id;
    }
    else
    {
        const auto found =
            std::find_if(m_records.cbegin(), m_records.cend(),
                         [this](const TagTypeRecord &record) { return record.id == m_currentId; });
        const int order = found == m_records.cend() ? m_records.size() + 1 : found->order;
        if (!m_repository.updateTagType({m_currentId, m_name->text(), order}, &errorMessage))
        {
            Toast::showError(window(), errorMessage, &m_themes);
            return;
        }
    }
    refresh();
    emit typesChanged();
    Toast::showSuccess(window(), QStringLiteral("标签类型已保存"), &m_themes);
}

void TagTypeManagementWidget::removeCurrent()
{
    if (m_currentId <= 0)
        return;
    if (QMessageBox::question(this, QStringLiteral("确认删除"),
                              QStringLiteral("确定删除当前标签类型？仍有标签使用时会拒绝。")) !=
        QMessageBox::Yes)
        return;
    QString errorMessage;
    if (!m_repository.removeTagType(m_currentId, &errorMessage))
    {
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    m_currentId = 0;
    refresh();
    emit typesChanged();
}

void TagTypeManagementWidget::moveCurrent(int offset)
{
    if (m_currentId <= 0)
        return;
    QString errorMessage;
    if (!m_repository.moveTagType(m_currentId, offset, &errorMessage))
    {
        Toast::showWarning(window(), errorMessage, &m_themes);
        return;
    }
    refresh();
    emit typesChanged();
}

void TagTypeManagementWidget::moveRow(int sourceRow, int destinationRow)
{
    if (sourceRow < 0 || sourceRow >= m_records.size() || destinationRow < 0
        || destinationRow >= m_records.size())
        return;
    m_currentId = m_records.at(sourceRow).id;
    moveCurrent(destinationRow - sourceRow);
}

} // namespace darkeye
