#include "darkeye_ui/components/TokenViews.h"

#include <QLineEdit>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

namespace darkeye {
namespace {

class TableEditorDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem clean(option);
        clean.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(painter, clean, index);
    }

    void updateEditorGeometry(QWidget *editor,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &) const override
    {
        editor->setGeometry(option.rect);
    }
};

template<typename View>
void initializeTable(View *view, const QString &name)
{
    view->setObjectName(name);
    view->setItemDelegate(new TableEditorDelegate(view));
}

} // namespace

TokenTableView::TokenTableView(QWidget *parent) : QTableView(parent)
{
    initializeTable(this, QStringLiteral("DesignTableView"));
}

TokenTableWidget::TokenTableWidget(QWidget *parent) : QTableWidget(parent)
{
    initializeTable(this, QStringLiteral("DesignTableWidget"));
}

TokenTableWidget::TokenTableWidget(int rows, int columns, QWidget *parent)
    : QTableWidget(rows, columns, parent)
{
    initializeTable(this, QStringLiteral("DesignTableWidget"));
}

void TokenTableWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::New))
    {
        emit addRequested();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Delete))
    {
        emit deleteRequested();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Save))
    {
        emit submitRequested();
        event->accept();
        return;
    }
    QTableWidget::keyPressEvent(event);
}

ReorderableTokenTableWidget::ReorderableTokenTableWidget(QWidget *parent)
    : TokenTableWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
}

void ReorderableTokenTableWidget::mousePressEvent(QMouseEvent *event)
{
    m_draggedRow = indexAt(event->pos()).row();
    TokenTableWidget::mousePressEvent(event);
}

void ReorderableTokenTableWidget::dragEnterEvent(QDragEnterEvent *event)
{
    setProperty("rowDragActive", true);
    viewport()->update();
    TokenTableWidget::dragEnterEvent(event);
}

void ReorderableTokenTableWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    setProperty("rowDragActive", false);
    viewport()->update();
    TokenTableWidget::dragLeaveEvent(event);
}

void ReorderableTokenTableWidget::dropEvent(QDropEvent *event)
{
    setProperty("rowDragActive", false);
    viewport()->update();

    const int sourceRow = m_draggedRow;
    m_draggedRow = -1;
    if (sourceRow < 0 || sourceRow >= rowCount())
    {
        event->ignore();
        return;
    }

    const QModelIndex target = indexAt(event->position().toPoint());
    int destinationRow = target.isValid() ? target.row() : rowCount() - 1;
    if (target.isValid() && event->position().y() > visualRect(target).center().y())
        ++destinationRow;
    destinationRow = qBound(0, destinationRow, rowCount());
    if (destinationRow > sourceRow)
        --destinationRow;
    if (destinationRow != sourceRow)
        emit rowsReordered(sourceRow, destinationRow);
    event->acceptProposedAction();
}

TreeView::TreeView(QWidget *parent) : QTreeView(parent)
{
    setObjectName(QStringLiteral("DesignTreeView"));
    setAlternatingRowColors(true);
    setUniformRowHeights(true);
}

TokenListView::TokenListView(QWidget *parent) : QListView(parent)
{
    setObjectName(QStringLiteral("DesignListView"));
}

TokenListWidget::TokenListWidget(QWidget *parent) : QListWidget(parent)
{
    setObjectName(QStringLiteral("DesignListWidget"));
}

TokenDateTimeEdit::TokenDateTimeEdit(QWidget *parent) : QDateTimeEdit(parent)
{
    setObjectName(QStringLiteral("DesignDateTimeEdit"));
}

TokenKeySequenceEdit::TokenKeySequenceEdit(QWidget *parent)
    : QKeySequenceEdit(parent)
{
    setObjectName(QStringLiteral("DesignKeySequenceEdit"));
    for (auto *lineEdit : findChildren<QLineEdit *>()) {
        lineEdit->setObjectName(QStringLiteral("DesignKeySequenceEditLineEdit"));
    }
}

} // namespace darkeye
