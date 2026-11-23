#pragma once

#include <QWidget>

#include <functional>

class QModelIndex;
class QToolButton;

namespace mdedit {

class ProjectFileTree;
enum class ProjectIcon;

class ProjectFilesPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectFilesPanel(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    void setThemePalette(const QPalette &palette);
    ProjectFileTree *tree() const { return m_tree; }
    void setBeforeEntryChange(std::function<bool(const QString &)> handler);

signals:
    void createFileRequested(const QString &path);
    void entryChanged(const QString &source, const QString &destination);

private:
    QString directoryFor(const QModelIndex &index) const;
    QString selectedDirectory() const;
    bool containsEntry(const QString &path) const;
    void createFolder(const QString &directory);
    void requestFile(const QString &directory);
    void moveEntry(const QString &source, const QString &directory);
    void renameEntry(const QString &entry);
    void trashEntry(const QString &entry);
    bool relocateEntry(const QString &source, const QString &destination, const QString &title);

    ProjectFileTree *m_tree;
    QWidget *m_fileActions = nullptr;
    QList<QPair<QToolButton *, ProjectIcon>> m_actionIcons;
    std::function<bool(const QString &)> m_beforeEntryChange;
};

QString relocatedPath(const QString &path, const QString &source, const QString &destination);

} // namespace mdedit
