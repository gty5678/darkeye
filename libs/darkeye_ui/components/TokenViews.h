#pragma once

#include <QDateTimeEdit>
#include <QKeySequenceEdit>
#include <QListView>
#include <QListWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTreeView>

class QDropEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QKeyEvent;
class QMouseEvent;

namespace darkeye {

class TokenTableView final : public QTableView
{
public:
    explicit TokenTableView(QWidget *parent = nullptr);
};

class TokenTableWidget : public QTableWidget
{
    Q_OBJECT

public:
    explicit TokenTableWidget(QWidget *parent = nullptr);
    TokenTableWidget(int rows, int columns, QWidget *parent = nullptr);

signals:
    void addRequested();
    void deleteRequested();
    void submitRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
};

class ReorderableTokenTableWidget final : public TokenTableWidget
{
    Q_OBJECT

public:
    explicit ReorderableTokenTableWidget(QWidget *parent = nullptr);

signals:
    // Destination is the final row index after the source row is removed.
    void rowsReordered(int sourceRow, int destinationRow);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    int m_draggedRow = -1;
};

class TreeView : public QTreeView
{
public:
    explicit TreeView(QWidget *parent = nullptr);
};
using TokenTreeView = TreeView;

class TokenListView final : public QListView
{
public:
    explicit TokenListView(QWidget *parent = nullptr);
};

class TokenListWidget final : public QListWidget
{
public:
    explicit TokenListWidget(QWidget *parent = nullptr);
};

class TokenDateTimeEdit final : public QDateTimeEdit
{
public:
    explicit TokenDateTimeEdit(QWidget *parent = nullptr);
};

class TokenKeySequenceEdit final : public QKeySequenceEdit
{
public:
    explicit TokenKeySequenceEdit(QWidget *parent = nullptr);
};

} // namespace darkeye
