#include "ui/components/PersonTransferSelector.h"

#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/TokenViews.h"

#include "darkeye_ui/components/OctImage.h"

#include <QHelpEvent>
#include <QMenu>
#include <QScreen>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QSet>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>

namespace darkeye
{

PersonTransferSelector::PersonTransferSelector(QString personLabel, QWidget *parent,
                                               ThemeService *themes)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *searchRow = new QHBoxLayout;
    m_search = new DesignLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("搜索中文名或日文名"));
    m_search->setClearButtonEnabled(true);
    auto *add = new IconButton(QStringLiteral("circle_plus"), themes, this);
    add->setToolTip(QStringLiteral("添加%1并选择").arg(personLabel));
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(add);
    layout->addLayout(searchRow);

    m_available = new TokenListWidget(this);
    m_available->setProperty("personSelectorList", true);
    layout->addWidget(m_available, 1);
    auto *actions = new QHBoxLayout;
    auto *choose = new IconButton(QStringLiteral("arrow_down"), themes, this);
    choose->setToolTip(QStringLiteral("选择参演%1").arg(personLabel));
    auto *label = new DesignLabel(QStringLiteral("参演%1").arg(personLabel), this);
    label->setAlignment(Qt::AlignCenter);
    auto *remove = new IconButton(QStringLiteral("arrow_up"), themes, this);
    remove->setToolTip(QStringLiteral("移除参演%1").arg(personLabel));
    actions->addWidget(choose);
    actions->addWidget(label, 1);
    actions->addWidget(remove);
    layout->addLayout(actions);
    m_selected = new TokenListWidget(this);
    m_selected->setProperty("personSelectedList", true);
    m_selected->setProperty("personSelectorList", true);
    layout->addWidget(m_selected, 1);

    m_portrait = new OctImage({}, {}, 150, true, this);
    m_portrait->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    m_portrait->setAttribute(Qt::WA_ShowWithoutActivating);
    m_portrait->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_portrait->setObjectName(QStringLiteral("PersonSelectorPortrait"));
    for (auto *list : {m_available, m_selected}) {
        list->viewport()->setMouseTracking(true);
        list->viewport()->installEventFilter(this);
        connect(list->verticalScrollBar(), &QScrollBar::valueChanged,
                m_portrait, &QWidget::hide);
    }

    for (auto *list : {m_available, m_selected}) {
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QWidget::customContextMenuRequested, this, [this, list](const QPoint &pos) {
            auto *item = list->itemAt(pos);
            if (!item) return;
            const qint64 id = item->data(Qt::UserRole).toLongLong();
            list->setCurrentItem(item);
            m_portrait->hide();
            QMenu menu(this);
            auto *detail = menu.addAction(QStringLiteral("查看详情"));
            auto *edit = menu.addAction(QStringLiteral("编辑资料"));
            const auto *action = menu.exec(list->viewport()->mapToGlobal(pos));
            if (action == detail) emit detailRequested(id);
            else if (action == edit) emit editRequested(id);
        });
    }

    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuildAvailable(); });
    connect(choose, &QPushButton::clicked, this, &PersonTransferSelector::chooseCurrent);
    connect(remove, &QPushButton::clicked, this, &PersonTransferSelector::removeCurrent);
    connect(add, &QPushButton::clicked, this, &PersonTransferSelector::addRequested);
    connect(m_available, &QListWidget::itemSelectionChanged, this, [this] {
        if (!m_available->selectedItems().isEmpty()) m_selected->clearSelection();
    });
    connect(m_selected, &QListWidget::itemSelectionChanged, this, [this] {
        if (!m_selected->selectedItems().isEmpty()) m_available->clearSelection();
    });
}

void PersonTransferSelector::setOptions(const QList<IdLabelOption> &options)
{
    m_options = options;
    QSet<qint64> available;
    for (const auto &option : m_options) available.insert(option.id);
    for (auto it = m_selectedIds.begin(); it != m_selectedIds.end();) {
        if (!available.contains(*it)) it = m_selectedIds.erase(it); else ++it;
    }
    rebuildAvailable();
    rebuildSelected();
}

QList<qint64> PersonTransferSelector::selectedIds() const { return m_selectedIds; }

void PersonTransferSelector::setModified(bool modified)
{
    // Python highlights only the receive (selected people) list.
    m_selected->setProperty("addWorkModified", modified);
    m_selected->style()->unpolish(m_selected);
    m_selected->style()->polish(m_selected);
    m_selected->update();
}

void PersonTransferSelector::setSelectedIds(const QList<qint64> &ids)
{
    QSet<qint64> available;
    for (const auto &option : m_options) available.insert(option.id);
    QList<qint64> selected;
    for (qint64 id : ids) if (available.contains(id) && !selected.contains(id)) selected.append(id);
    m_selectedIds = selected;
    rebuildAvailable();
    rebuildSelected();
}

void PersonTransferSelector::selectId(qint64 id)
{
    if (id <= 0 || m_selectedIds.contains(id)) return;
    const auto found = std::find_if(m_options.cbegin(), m_options.cend(),
                                   [id](const IdLabelOption &item) { return item.id == id; });
    if (found == m_options.cend()) return;
    m_selectedIds.append(id);
    rebuildAvailable();
    rebuildSelected();
    emit selectionChanged();
}

void PersonTransferSelector::clearSelection()
{
    if (m_selectedIds.isEmpty()) return;
    m_selectedIds.clear();
    rebuildAvailable();
    rebuildSelected();
    emit selectionChanged();
}

void PersonTransferSelector::rebuildAvailable()
{
    const QString query = m_search->text().trimmed();
    m_portrait->hide();
    m_available->clear();
    for (const auto &option : m_options) {
        if (m_selectedIds.contains(option.id) ||
            (!query.isEmpty() && !optionText(option).contains(query, Qt::CaseInsensitive))) continue;
        auto *item = new QListWidgetItem(optionText(option), m_available);
        item->setData(Qt::UserRole, option.id);
    }
}

void PersonTransferSelector::rebuildSelected()
{
    m_portrait->hide();
    m_selected->clear();
    for (qint64 id : m_selectedIds) {
        const auto found = std::find_if(m_options.cbegin(), m_options.cend(),
                                        [id](const IdLabelOption &item) { return item.id == id; });
        if (found == m_options.cend()) continue;
        auto *item = new QListWidgetItem(optionText(*found), m_selected);
        item->setData(Qt::UserRole, id);
    }
}

void PersonTransferSelector::chooseCurrent()
{
    const auto items = m_available->selectedItems();
    if (items.isEmpty()) return;
    selectId(items.constFirst()->data(Qt::UserRole).toLongLong());
}

void PersonTransferSelector::removeCurrent()
{
    const auto items = m_selected->selectedItems();
    if (items.isEmpty()) return;
    m_selectedIds.removeAll(items.constFirst()->data(Qt::UserRole).toLongLong());
    rebuildAvailable();
    rebuildSelected();
    emit selectionChanged();
}

bool PersonTransferSelector::eventFilter(QObject *watched, QEvent *event)
{
    auto *list = watched == m_available->viewport() ? m_available : m_selected;
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        auto *item = list->itemAt(help->pos());
        if (!item) { m_portrait->hide(); return true; }
        const qint64 id = item->data(Qt::UserRole).toLongLong();
        const auto found = std::find_if(m_options.cbegin(), m_options.cend(),
            [id](const IdLabelOption &option) { return option.id == id; });
        if (found == m_options.cend()) return true;
        m_portrait->updateImage(found->imagePath);
        const QRect rowRect = list->visualItemRect(item);
        const QRect anchor(list->viewport()->mapToGlobal(rowRect.topLeft()), rowRect.size());
        constexpr int gap = 8;
        QPoint position(anchor.right() + 1 + gap,
                        anchor.center().y() - m_portrait->height() / 2);
        if (auto *screen = list->screen()) {
            const QRect bounds = screen->availableGeometry();
            if (position.x() + m_portrait->width() > bounds.right() + 1)
                position.setX(anchor.left() - gap - m_portrait->width());
            position.setX(qBound(bounds.left(), position.x(),
                                qMax(bounds.left(), bounds.right() - m_portrait->width() + 1)));
            position.setY(qBound(bounds.top(), position.y(), qMax(bounds.top(), bounds.bottom() - m_portrait->height() + 1)));
        }
        m_portrait->move(position);
        m_portrait->show();
        return true;
    }
    if (event->type() == QEvent::Leave || event->type() == QEvent::MouseMove
        || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Hide)
        m_portrait->hide();
    return QWidget::eventFilter(watched, event);
}

QString PersonTransferSelector::optionText(const IdLabelOption &option) const
{
    return option.group.isEmpty() ? option.label
                                  : QStringLiteral("%1（%2）").arg(option.label, option.group);
}

} // namespace darkeye
