#pragma once

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QTableWidget;

namespace darkeye
{

class PathBrowseDelegate;
class TokenTableWidget;

class SinglePathManagement final : public QWidget
{
    Q_OBJECT

public:
    explicit SinglePathManagement(const QString &labelText = QStringLiteral("路径管理："),
                                  QWidget *parent = nullptr);

    [[nodiscard]] QString path() const;
    void loadPath(const QString &path);

private:
    QLineEdit *m_filePath = nullptr;
};

class MultiplePathManagement final : public QWidget
{
    Q_OBJECT

public:
    explicit MultiplePathManagement(const QString &labelText = QStringLiteral("路径管理："),
                                    QWidget *parent = nullptr);

    [[nodiscard]] QStringList paths() const;
    void loadPaths(const QStringList &paths);
    [[nodiscard]] QTableWidget *table() const;

signals:
    void pathsChanged();

public slots:
    void addRow();
    void deleteSelectedRows();

private slots:
    void handleCellDoubleClicked(int row, int column);
    void browseRow(int row);

private:
    TokenTableWidget *m_table = nullptr;
    PathBrowseDelegate *m_delegate = nullptr;
};

} // namespace darkeye
