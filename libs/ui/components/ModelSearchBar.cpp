#include "ui/components/ModelSearchBar.h"

#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"

#include <QAbstractItemModel>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QTableView>

namespace darkeye {
ModelSearchBar::ModelSearchBar(QTableView *table, ThemeService &themes, QWidget *parent)
    : QWidget(parent), m_table(table)
{
    auto *layout = new QHBoxLayout(this);
    m_input = new DesignLineEdit(this);
    m_input->setFixedWidth(200);
    m_input->setClearButtonEnabled(true);
    m_input->setPlaceholderText(QStringLiteral("搜索"));
    m_input->setAccessibleName(QStringLiteral("搜索表格"));
    m_input->installEventFilter(this);
    m_result = new DesignLabel(QStringLiteral("无搜索结果"), this);
    m_result->setFixedWidth(70);
    m_previous = new IconButton(QStringLiteral("arrow_up"), &themes, this);
    m_next = new IconButton(QStringLiteral("arrow_down"), &themes, this);
    m_previous->setWhatsThis(QStringLiteral("向前搜索"));
    m_next->setWhatsThis(QStringLiteral("向后搜索"));
    m_previous->setToolTip(QStringLiteral("向前搜索(Shift+Enter)"));
    m_next->setToolTip(QStringLiteral("向后搜索(Enter)"));
    m_previous->setAccessibleName(QStringLiteral("上一个搜索结果"));
    m_next->setAccessibleName(QStringLiteral("下一个搜索结果"));
    m_previous->setEnabled(false);
    m_next->setEnabled(false);
    layout->addWidget(m_input);
    layout->addWidget(m_result);
    layout->addWidget(m_previous);
    layout->addWidget(m_next);
    layout->addStretch();
    connect(m_input, &QLineEdit::textChanged, this, [this] { search(); });
    connect(m_previous, &QPushButton::clicked, this, [this] { navigate(-1); });
    connect(m_next, &QPushButton::clicked, this, [this] { navigate(1); });
    auto *model = m_table->model();
    connect(model, &QAbstractItemModel::modelReset, this, [this] { search(); });
    connect(model, &QAbstractItemModel::rowsInserted, this, [this] { search(); });
    connect(model, &QAbstractItemModel::rowsRemoved, this, [this] { search(); });
    connect(model, &QAbstractItemModel::dataChanged, this, [this] { search(); });
    connect(model, &QAbstractItemModel::layoutChanged, this, [this] { search(); });
}

void ModelSearchBar::search()
{
    m_matches.clear();
    m_current = -1;
    const QString needle = m_input->text().trimmed();
    const auto *model = m_table->model();
    if (!needle.isEmpty())
        for (int row = 0; row < model->rowCount(); ++row)
            for (int column = 0; column < model->columnCount(); ++column)
            {
                const auto index = model->index(row, column);
                if (index.data().toString().contains(needle, Qt::CaseInsensitive))
                {
                    m_matches.append(QPersistentModelIndex(index));
                    break;
                }
            }
    m_previous->setEnabled(!m_matches.isEmpty());
    m_next->setEnabled(!m_matches.isEmpty());
    m_result->setText(needle.isEmpty() ? QStringLiteral("无搜索结果")
                                     : QStringLiteral("无匹配结果"));
    navigate(1);
}

void ModelSearchBar::navigate(int direction)
{
    if (m_matches.isEmpty()) return;
    m_current = (m_current + direction + m_matches.size()) % m_matches.size();
    const auto index = m_matches.at(m_current);
    if (!index.isValid()) { search(); return; }
    m_table->selectRow(index.row());
    m_table->scrollTo(m_table->model()->index(index.row(), 0),
                      QAbstractItemView::PositionAtCenter);
    m_result->setText(QStringLiteral("结果 %1/%2").arg(m_current + 1).arg(m_matches.size()));
}

bool ModelSearchBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        {
            if (key->modifiers() == Qt::NoModifier) { navigate(1); return true; }
            if (key->modifiers() == Qt::ShiftModifier) { navigate(-1); return true; }
        }
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace darkeye
