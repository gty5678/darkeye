#include "WorkspaceSearch.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/WorkRepository.h"
#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QUrl>
#include <QSet>
#include <algorithm>
namespace darkeye {
namespace {
constexpr int actionRole = Qt::UserRole + 1;
}

WorkspaceSearch::WorkspaceSearch(QSqlDatabase database, QMap<QString, QString> pages, QWidget *parent)
    : QFrame(parent), m_database(database), m_pages(std::move(pages)) {
    setObjectName(QStringLiteral("WorkspaceSearch"));
    setProperty("searchPopup", true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("workspaceSearchInput"));
    m_input->setPlaceholderText(QStringLiteral("搜索人物、作品、标签、厂牌、片商、系列或导演"));
    m_input->setClearButtonEnabled(true);
    m_input->setMinimumHeight(48);
    m_results = new QListWidget(this);
    m_results->setObjectName(QStringLiteral("workspaceSearchResults"));
    m_results->hide();
    layout->addWidget(m_input);
    layout->addWidget(m_results);
    m_more = new QPushButton(QStringLiteral("加载更多结果"), this);
    m_more->hide();
    layout->addWidget(m_more);
    connect(m_more, &QPushButton::clicked, this, [this] { m_limit += 100; updateResults(); });
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(180);
    connect(m_input, &QLineEdit::textChanged, this, [this, timer](const QString &text) {
        const auto query = text.trimmed();
        if (query.isEmpty() || m_mode == Mode::Commands) {
            timer->stop();
            updateResults();
        } else {
            timer->start();
        }
    });
    connect(timer, &QTimer::timeout, this, &WorkspaceSearch::updateResults);
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        if (m_resultQuery != m_input->text().trimmed()) updateResults();
        activate(m_results->currentItem());
    });
    connect(m_results, &QListWidget::itemClicked, this, &WorkspaceSearch::activate);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *previous, QWidget *current) {
        if (!m_fullResults && isVisible() && isAncestorOf(previous) && !isAncestorOf(current))
            hide();
    });
}
void WorkspaceSearch::setFullResults() {
    m_fullResults = true;
    setProperty("searchPopup", false);
    m_limit = 100;
    m_input->hide();
    m_results->setMaximumHeight(QWIDGETSIZE_MAX);
    m_results->installEventFilter(this);
    static_cast<QVBoxLayout *>(layout())->setStretch(1, 1);
}
void WorkspaceSearch::setBatchActions(const QList<QAction *> &actions) {
    m_batchActions = actions;
    for (auto *action : m_batchActions) {
        connect(action, &QAction::changed, this, [this] {
            if (m_mode == Mode::Commands && isVisible()) updateResults();
        });
    }
}

void WorkspaceSearch::open(Mode mode, QList<QPair<QString, QString>> recent) {
    m_recent = std::move(recent);
    if (m_mode != mode) {
        m_mode = mode;
        m_input->setPlaceholderText(mode == Mode::Commands
            ? QStringLiteral("搜索页面或批量操作") : QStringLiteral("搜索人物、作品、标签或分类"));
        m_input->clear();
    }
    if (m_input->text().trimmed().isEmpty()) m_results->setCurrentRow(-1);
    updateResults();
    focusSearch();
}

void WorkspaceSearch::positionPopup() {
    auto *anchor = parentWidget();
    const int top = anchor->height() / 4;
    const auto margins = layout()->contentsMargins();
    const int inputHeight = qMax(m_input->minimumHeight(), m_input->sizeHint().height());
    const int fixedHeight = margins.top() + margins.bottom() + 2 * frameWidth()
        + layout()->spacing() + inputHeight;
    m_input->setMinimumWidth(qBound(160, anchor->width() - 48, 720));
    const int rowHeight = qMax(0, m_results->sizeHintForRow(0));
    const int listHeight = rowHeight * m_results->count() + 2 * m_results->frameWidth();
    const int availableHeight = qMax(0, anchor->height() - top - fixedHeight);
    const bool allPages = m_mode == Mode::Commands && m_resultQuery.isEmpty();
    const int heightLimit = allPages ? availableHeight : qMin(availableHeight, 420);
    m_results->setFixedHeight(qMin(listHeight, heightLimit));
    adjustSize();
    move((anchor->width() - width()) / 2, top);
}

void WorkspaceSearch::focusSearch() {
    if (!m_fullResults && !isVisible()) {
        // Keep the editor in the main window so Cocoa can route IME input to it.
        setFrameShape(QFrame::StyledPanel);
        layout()->setSizeConstraint(QLayout::SetFixedSize);
        ensurePolished();
        positionPopup();
        raise();
        show();
    }
    m_input->setFocus(Qt::ShortcutFocusReason);
    m_input->selectAll();
}

void WorkspaceSearch::showEvent(QShowEvent *event) {
    QFrame::showEvent(event);
    if (!m_fullResults) qApp->installEventFilter(this);
}

void WorkspaceSearch::hideEvent(QHideEvent *event) {
    if (!m_fullResults) qApp->removeEventFilter(this);
    QFrame::hideEvent(event);
}

void WorkspaceSearch::search(const QString &text) {
    m_input->setText(text);
    updateResults();
    if (!m_fullResults) focusSearch();
}
void WorkspaceSearch::activate(QListWidgetItem *item) {
    if (!item || item->data(Qt::UserRole).toString().isEmpty()) return;
    if (auto *action = item->data(actionRole).value<QAction *>()) {
        if (!action->isEnabled()) return;
        hide();
        m_input->clear();
        action->trigger();
        return;
    }
    const auto route = item->data(Qt::UserRole).toString();
    // Resolve the destination while the originating pane is still active.
    emit openRequested(route);
    if (!m_fullResults) {
        hide();
        m_input->clear();
    }
}
bool WorkspaceSearch::eventFilter(QObject *object, QEvent *event) {
    if (!m_fullResults) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (object->isWidgetType() && !rect().contains(mapFromGlobal(mouse->globalPosition()).toPoint()))
                hide();
        } else if (event->type() == QEvent::WindowDeactivate && object == window()) {
            hide();
        } else if (event->type() == QEvent::Resize && object == parentWidget()) {
            positionPopup();
        }
    }
    if (event->type() == QEvent::KeyPress && (object == m_input || object == m_results)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            if (!m_fullResults) {
                hide();
            } else {
                for (auto *timer : findChildren<QTimer *>()) timer->stop();
                m_results->hide();
            }
            return true;
        }
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            if (!m_results->isVisible() || m_resultQuery != m_input->text().trimmed()) updateResults();
            if (m_results->count() == 0) return true;
            const int delta = key->key() == Qt::Key_Down ? 1 : -1;
            for (int row = m_results->currentRow() + delta; row >= 0 && row < m_results->count(); row += delta) {
                if (m_results->item(row)->flags().testFlag(Qt::ItemIsEnabled)) {
                    m_results->setCurrentRow(row);
                    break;
                }
            }
            return true;
        }
        if (object == m_results && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            activate(m_results->currentItem()); return true;
        }
    }
    return QFrame::eventFilter(object, event);
}
void WorkspaceSearch::updateResults() {
    const auto previousRoute = m_resultQuery == m_input->text().trimmed() && m_results->currentItem()
        ? m_results->currentItem()->data(Qt::UserRole).toString() : QString();
    m_results->clear();
    m_more->hide();
    const auto text = m_input->text().trimmed();
    m_resultQuery = text;
    auto add = [this](const QString &title, const QString &route) {
        auto *item = new QListWidgetItem(title, m_results);
        item->setData(Qt::UserRole, route);
    };
    if (m_mode == Mode::Commands) {
        QSet<QString> addedPages;
        if (text.isEmpty()) {
            for (const auto &[route, title] : m_recent) {
                add(QStringLiteral("最近 · ") + title, route);
                addedPages.insert(route);
            }
        }
        const QMap<QString, QString> aliases{
            {QStringLiteral("女优"), QStringLiteral("actress")},
            {QStringLiteral("女优库"), QStringLiteral("actress")},
            {QStringLiteral("男优"), QStringLiteral("actor")},
            {QStringLiteral("男优库"), QStringLiteral("actor")},
            {QStringLiteral("作品库"), QStringLiteral("mutiwork")},
        };
        const auto exactRoute = aliases.value(text);
        for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
            if (it.value() == text || it.key() == exactRoute) {
                add(QStringLiteral("打开 · ") + it.value(), it.key());
                addedPages.insert(it.key());
            }
        }
        for (const bool management : {false, true}) {
            for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
                if (addedPages.contains(it.key()) || it.key().startsWith(QStringLiteral("manage_")) != management)
                    continue;
                const auto label = management ? QStringLiteral("管理 · ") : QStringLiteral("打开 · ");
                const auto title = label + it.value();
                if (text.isEmpty() || title.contains(text, Qt::CaseInsensitive)
                    || it.key().contains(text, Qt::CaseInsensitive)) {
                    add(title, it.key());
                }
            }
        }
        for (auto *action : m_batchActions) {
            auto title = QStringLiteral("批量操作 · ") + action->text();
            if (!title.contains(text, Qt::CaseInsensitive) &&
                !action->toolTip().contains(text, Qt::CaseInsensitive))
                continue;
            if (!action->statusTip().isEmpty()) title += QStringLiteral(" · ") + action->statusTip();
            add(title, action->objectName());
            auto *item = m_results->item(m_results->count() - 1);
            item->setData(actionRole, QVariant::fromValue(action));
            item->setToolTip(action->toolTip());
            if (!action->isEnabled()) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        if (m_results->count() == 0) add(QStringLiteral("没有匹配的命令"), {});
    } else if (text.isEmpty()) {
        for (const auto &[route, title] : m_recent)
            add(title, route);
    } else {
        if (m_database.isOpen()) {
            PersonRepository people(m_database);
            for (auto kind : {PersonKind::Actress, PersonKind::Actor}) {
                PersonSearch query; query.kind = kind; query.name = text; query.limit = m_fullResults ? m_limit : 12;
                const auto persons = people.search(query);
                if (m_fullResults && persons.size() == m_limit) m_more->show();
                for (const auto &person : persons)
                    add((kind == PersonKind::Actress ? QStringLiteral("女优 · ") : QStringLiteral("男优 · ")) + person.name,
                        (kind == PersonKind::Actress ? QStringLiteral("actress_detail/") : QStringLiteral("actor_works/")) + QString::number(person.id));
            }
            WorkRepository works(m_database);
            const auto matches = [&text](const QString &name) {
                return name.contains(text, Qt::CaseInsensitive);
            };
            for (const auto &director : works.directorSuggestions()) {
                if (matches(director))
                    add(QStringLiteral("导演 · ") + director,
                        QStringLiteral("director/") + QString::fromLatin1(QUrl::toPercentEncoding(director)));
            }
            WorkSearch query; query.keyword = text; query.limit = m_limit;
            const auto foundWorks = works.search(query);
            if (m_fullResults && foundWorks.size() == m_limit) m_more->show();
            for (const auto &work : foundWorks)
                add(QStringLiteral("作品 · %1  %2").arg(work.serialNumber, work.chineseTitle), QStringLiteral("work_detail/") + QString::number(work.id));
            for (const auto &tag : works.tagOptions())
                if ((tag.name + tag.aliases.join(' ')).contains(text, Qt::CaseInsensitive))
                    add(QStringLiteral("标签 · ") + tag.name, QStringLiteral("tag/") + QString::number(tag.id));
            const auto addReferences = [&](const QString &type, const QString &route,
                                           const QList<NamedIdOption> &options) {
                for (const auto &value : options) {
                    if (matches(value.name) || matches(value.chineseName) || matches(value.japaneseName)
                        || std::any_of(value.aliases.cbegin(), value.aliases.cend(), matches))
                        add(type + QStringLiteral(" · ") + value.name, route + '/' + QString::number(value.id));
                }
            };
            addReferences(QStringLiteral("厂牌"), QStringLiteral("label"), works.labelOptions());
            addReferences(QStringLiteral("片商"), QStringLiteral("maker"), works.makerOptions());
            addReferences(QStringLiteral("系列"), QStringLiteral("series"), works.seriesOptions());
        }
        if (!m_fullResults) add(QStringLiteral("查看全部搜索结果 · ") + text, QStringLiteral("search/") + QString::fromLatin1(QUrl::toPercentEncoding(text)));
        if (m_results->count() == 0) add(QStringLiteral("没有匹配结果"), {});
    }
    int selectedRow = -1;
    for (int row = 0; row < m_results->count(); ++row) {
        const auto *item = m_results->item(row);
        if (!item->flags().testFlag(Qt::ItemIsEnabled)) continue;
        if (selectedRow < 0) selectedRow = row;
        if (!previousRoute.isEmpty() && item->data(Qt::UserRole).toString() == previousRoute) {
            selectedRow = row;
            break;
        }
    }
    m_results->setCurrentRow(selectedRow);
    m_results->setVisible(m_results->count() > 0);
    if (!m_fullResults && isVisible()) positionPopup();
}
}
