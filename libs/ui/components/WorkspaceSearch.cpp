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

WorkspaceSearch::WorkspaceSearch(QSqlDatabase database, QMap<QString, QString> pages, QWidget *parent,
                                 Presentation presentation)
    : QFrame(parent), m_database(database), m_entries(std::move(pages)),
      m_fullResults(presentation == Presentation::ResultsPage) {
    setObjectName(QStringLiteral("WorkspaceSearch"));
    setProperty("searchPopup", !m_fullResults);
    if (!m_fullResults) buildUi();
}

void WorkspaceSearch::buildUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    m_results = new QListWidget(this);
    m_results->setObjectName(QStringLiteral("workspaceSearchResults"));
    m_results->hide();
    connect(m_results, &QListWidget::itemClicked, this, &WorkspaceSearch::activate);

    if (m_fullResults) {
        m_limit = 100;
        layout->addWidget(m_results, 1);
        m_results->installEventFilter(this);
        m_more = new QPushButton(QStringLiteral("加载更多结果"), this);
        m_more->hide();
        layout->addWidget(m_more);
        connect(m_more, &QPushButton::clicked, this, [this] { m_limit += 100; updateResults(); });
        return;
    }

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("workspaceSearchInput"));
    m_input->setPlaceholderText(QStringLiteral("搜索人物、作品、标签、厂牌、片商、系列或导演"));
    m_input->setClearButtonEnabled(true);
    layout->addWidget(m_input);
    layout->addWidget(m_results);
    setTabOrder(m_input, m_results);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(180);
    connect(m_input, &QLineEdit::textChanged, this, [this, timer](const QString &text) {
        if (text.trimmed().isEmpty() || m_mode == Mode::Commands) {
            timer->stop();
            updateResults();
        } else {
            timer->start();
        }
    });
    connect(timer, &QTimer::timeout, this, &WorkspaceSearch::updateResults);
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        if (m_resultQuery != queryText()) updateResults();
        activate(m_results->currentItem());
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *previous, QWidget *current) {
        if (isVisible() && isAncestorOf(previous) && !isAncestorOf(current))
            hide();
    });
}

void WorkspaceSearch::addCommands(const QMap<QString, QString> &commands) {
    m_entries.insert(commands);
    for (auto it = commands.keyBegin(); it != commands.keyEnd(); ++it)
        m_commandRoutes.insert(*it);
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
    if (m_fullResults) return;
    m_recent = std::move(recent);
    const bool modeChanged = m_mode != mode;
    if (modeChanged) {
        m_mode = mode;
        switch (mode) {
        case Mode::Commands:
            m_input->setPlaceholderText(QStringLiteral("搜索页面或命令"));
            break;
        case Mode::IndependentActress:
            m_input->setPlaceholderText(QStringLiteral("输入女演员姓名，在独立页打开详情"));
            break;
        case Mode::EditActress:
            m_input->setPlaceholderText(QStringLiteral("输入要编辑的女演员姓名"));
            break;
        case Mode::EditActor:
            m_input->setPlaceholderText(QStringLiteral("输入要编辑的男演员姓名"));
            break;
        case Mode::EditWork:
            m_input->setPlaceholderText(QStringLiteral("输入要编辑的作品番号或标题"));
            break;
        case Mode::Entities:
            m_input->setPlaceholderText(QStringLiteral("搜索人物、作品、标签或分类"));
            break;
        }
    }
    if (m_mode == Mode::Commands || modeChanged) m_input->clear();
    if (queryText().isEmpty()) m_results->setCurrentRow(-1);
    updateResults();
    focusSearch();
}

void WorkspaceSearch::positionPopup() {
    auto *anchor = parentWidget();
    const int edgeMargin = anchor->height() / 6;
    const auto margins = layout()->contentsMargins();
    const int inputHeight = qMax(m_input->minimumHeight(), m_input->sizeHint().height());
    const int fixedHeight = margins.top() + margins.bottom() + 2 * frameWidth()
        + layout()->spacing() + inputHeight;
    m_input->setMinimumWidth(qBound(160, anchor->width() - 48, 720));
    const int rowHeight = qMax(0, m_results->sizeHintForRow(0));
    const int listHeight = rowHeight * m_results->count() + 2 * m_results->frameWidth();
    const int availableHeight = qMax(0, anchor->height() - 2 * edgeMargin - fixedHeight);
    m_results->setFixedHeight(qMin(listHeight, availableHeight));
    adjustSize();
    move((anchor->width() - width()) / 2, edgeMargin);
}

void WorkspaceSearch::focusSearch() {
    if (!m_fullResults && !isVisible()) {
        // Keep the editor in the main window so Cocoa can route IME input to it.
        setFrameShape(QFrame::StyledPanel);
        layout()->setContentsMargins(12, 12, 12, 12);
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
    if (!m_fullResults) {
        qApp->installEventFilter(this);
    } else if (!m_results) {
        buildUi();
        updateResults();
    }
}

void WorkspaceSearch::hideEvent(QHideEvent *event) {
    if (!m_fullResults) qApp->removeEventFilter(this);
    QFrame::hideEvent(event);
}

void WorkspaceSearch::search(const QString &text) {
    if (m_fullResults) {
        m_pageQuery = text;
        if (m_results) updateResults();
        return;
    }
    m_input->setText(text);
    updateResults();
    focusSearch();
}
void WorkspaceSearch::activate(QListWidgetItem *item) {
    if (!item || item->data(Qt::UserRole).toString().isEmpty()) return;
    if (auto *action = item->data(actionRole).value<QAction *>()) {
        if (!action->isEnabled()) return;
        hide();
        if (m_input) m_input->clear();
        action->trigger();
        return;
    }
    const auto route = item->data(Qt::UserRole).toString();
    if (m_mode == Mode::Commands && route == independentActressCommand) {
        open(Mode::IndependentActress, {});
        return;
    }
    // Resolve the destination while the originating pane is still active.
    if (m_mode == Mode::IndependentActress)
        emit independentActressRequested(route.section(u'/', 1).toLongLong());
    else
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
                m_results->hide();
            }
            return true;
        }
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            if (!m_results->isVisible() || m_resultQuery != queryText()) updateResults();
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
QString WorkspaceSearch::queryText() const {
    return (m_fullResults ? m_pageQuery : m_input->text()).trimmed();
}

void WorkspaceSearch::updateResults() {
    const auto text = queryText();
    const auto previousRoute = m_resultQuery == text && m_results->currentItem()
        ? m_results->currentItem()->data(Qt::UserRole).toString() : QString();
    m_results->clear();
    if (m_more) m_more->hide();
    m_resultQuery = text;
    auto add = [this](const QString &title, const QString &route) {
        auto *item = new QListWidgetItem(title, m_results);
        item->setData(Qt::UserRole, route);
    };
    const auto addPeople = [&](PersonKind kind, const QString &route) {
        if (!m_database.isOpen()) return;
        PersonSearch query;
        query.kind = kind;
        query.name = text;
        query.limit = m_fullResults ? m_limit : 12;
        const auto persons = PersonRepository(m_database).search(query);
        if (m_fullResults && persons.size() == m_limit) m_more->show();
        for (const auto &person : persons)
            add((kind == PersonKind::Actress ? QStringLiteral("女演员 · ") : QStringLiteral("男演员 · ")) + person.name,
                route + u'/' + QString::number(person.id));
    };
    const auto addWorks = [&](const QString &route) {
        if (!m_database.isOpen()) return;
        WorkSearch query;
        query.keyword = text;
        query.limit = m_limit;
        const auto works = WorkRepository(m_database).search(query);
        if (m_fullResults && works.size() == m_limit) m_more->show();
        for (const auto &work : works)
            add(QStringLiteral("作品 · %1  %2").arg(work.serialNumber, work.chineseTitle),
                route + u'/' + QString::number(work.id));
    };
    if (m_mode == Mode::Commands) {
        const auto pagePrefix = QStringLiteral("页面 · ");
        const auto commandPrefix = QStringLiteral("命令 · ");
        const auto entryTitle = [&](const QString &route, const QString &title) {
            return (m_commandRoutes.contains(route) ? commandPrefix : pagePrefix) + title;
        };
        QSet<QString> addedRoutes;
        static const QMap<QString, QString> aliases{
            {QStringLiteral("女优"), QStringLiteral("actress")},
            {QStringLiteral("女优库"), QStringLiteral("actress")},
            {QStringLiteral("男优"), QStringLiteral("actor")},
            {QStringLiteral("男优库"), QStringLiteral("actor")},
            {QStringLiteral("商店"), QStringLiteral("home")},
            {QStringLiteral("作品"), QStringLiteral("mutiwork")},
            {QStringLiteral("书架"), QStringLiteral("shelf")},
            {QStringLiteral("作品过滤器"), QStringLiteral("work_filters")},
            {QStringLiteral("女演员详情"), QStringLiteral("actress_detail")},
            {QStringLiteral("时间轴"), QStringLiteral("timeline")},
            {QStringLiteral("暗黑界"), QStringLiteral("av")},
            {QStringLiteral("通知"), QStringLiteral("inbox")},
            {QStringLiteral("汇总查询"), QStringLiteral("manage_summary")},
            {QStringLiteral("软删除"), QStringLiteral("manage_deleted")},
        };
        const auto exactRoute = aliases.value(text);
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            if (it.value() == text || it.key() == exactRoute) {
                add(entryTitle(it.key(), it.value()), it.key());
                addedRoutes.insert(it.key());
            }
        }
        const auto addMatches = [&](bool commands, bool management) {
            for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
                const bool isCommand = m_commandRoutes.contains(it.key());
                const bool isManagementPage = !isCommand && it.key().startsWith(QStringLiteral("manage_"));
                if (isCommand != commands || isManagementPage != management || addedRoutes.contains(it.key()))
                    continue;
                const auto title = (isCommand ? commandPrefix : pagePrefix) + it.value();
                if (title.contains(text, Qt::CaseInsensitive)
                    || (management && QStringLiteral("管理").contains(text))
                    || it.key().contains(text, Qt::CaseInsensitive)) {
                    add(title, it.key());
                }
            }
        };
        addMatches(false, false);
        addMatches(false, true);
        addMatches(true, false);
        for (auto *action : m_batchActions) {
            auto title = commandPrefix + QStringLiteral("批量操作 · ") + action->text();
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
    } else if (m_mode == Mode::IndependentActress) {
        if (!text.isEmpty()) {
            addPeople(PersonKind::Actress, QStringLiteral("actress_detail"));
            if (m_results->count() == 0) add(QStringLiteral("没有匹配的女演员"), {});
        }
    } else if (m_mode != Mode::Entities) {
        if (!text.isEmpty()) {
            if (m_mode == Mode::EditActress)
                addPeople(PersonKind::Actress, QStringLiteral("modify_actress"));
            else if (m_mode == Mode::EditActor)
                addPeople(PersonKind::Actor, QStringLiteral("modify_actor"));
            else
                addWorks(QStringLiteral("work_edit"));
            if (m_results->count() == 0) add(QStringLiteral("没有匹配结果"), {});
        }
    } else if (text.isEmpty()) {
        for (const auto &[route, title] : m_recent)
            add(title, route);
    } else {
        if (m_database.isOpen()) {
            addPeople(PersonKind::Actress, QStringLiteral("actress_detail"));
            addPeople(PersonKind::Actor, QStringLiteral("actor_works"));
            WorkRepository works(m_database);
            const auto matches = [&text](const QString &name) {
                return name.contains(text, Qt::CaseInsensitive);
            };
            for (const auto &director : works.directorSuggestions()) {
                if (matches(director))
                    add(QStringLiteral("导演 · ") + director,
                        QStringLiteral("director/") + QString::fromLatin1(QUrl::toPercentEncoding(director)));
            }
            addWorks(QStringLiteral("work_detail"));
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
