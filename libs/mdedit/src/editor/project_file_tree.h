#pragma once

#include <QTreeView>

#include <functional>
#include "theme_palette.h"

class QFileInfo;
class QFileSystemModel;

namespace mdedit {

enum class ProjectIcon { Folder, File, AddFolder, AddFile, Collapse };
QIcon projectIcon(ProjectIcon iconType, const std::optional<QPalette> &theme = {});
bool pathIsWithin(const QString &path, const QString &directory);
bool canMoveProjectEntry(const QFileInfo &entry, const QString &directory, const QString &root);

class ProjectFileTree final : public QTreeView
{
    Q_OBJECT

public:
    explicit ProjectFileTree(QWidget *parent = nullptr);
    QFileSystemModel *fileModel() const;
    void setDirectory(const QString &directory);
    void selectFile(const QString &path);
    void setThemePalette(const QPalette &palette);
    void setMoveHandler(std::function<void(const QString &, const QString &)> handler);

signals:
    void documentActivated(const QString &path);

protected:
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override;
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr auto entryMimeType = "application/x-mdedit-project-entry";
    QString dropDestination(const QDropEvent *event, const QPoint &position) const;
    std::function<void(const QString &, const QString &)> moveEntry;
    QRect dropHighlight;
    std::optional<QPalette> theme;
};

} // namespace mdedit
