#include "project_file_tree.h"

#include <QDir>
#include <QDrag>
#include <QDragMoveEvent>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <utility>

namespace mdedit {

bool pathIsWithin(const QString &path,const QString &directory){
    if(path.isEmpty()||directory.isEmpty())return false;
    const QString relative=QDir(directory).relativeFilePath(path);
    return !QDir::isAbsolutePath(relative)&&relative!=".."&&!relative.startsWith("../");
}

bool canMoveProjectEntry(const QFileInfo &entry,const QString &directory,const QString &root){
    const QString sourceParent=entry.dir().canonicalPath();
    const QString target=QFileInfo(directory).canonicalFilePath();
    return pathIsWithin(sourceParent,root)&&pathIsWithin(target,root)&&target!=sourceParent&&
        (entry.exists()||entry.isSymLink())&&
        !(entry.isDir()&&!entry.isSymLink()&&pathIsWithin(target,entry.canonicalFilePath()));
}

QIcon projectIcon(ProjectIcon iconType, const std::optional<QPalette> &theme){
    const bool folder=iconType==ProjectIcon::Folder||iconType==ProjectIcon::AddFolder;
    const bool entry=iconType==ProjectIcon::Folder||iconType==ProjectIcon::File;
    QPainterPath outline;
    if(folder){
        outline.moveTo(1.5,12.5);outline.lineTo(1.5,3.5);outline.lineTo(5,3.5);
        outline.lineTo(6.5,5);outline.lineTo(14.5,5);outline.lineTo(14.5,12.5);outline.closeSubpath();
    }else if(iconType!=ProjectIcon::Collapse){
        outline.moveTo(3.5,14);outline.lineTo(3.5,2);outline.lineTo(10,2);
        outline.lineTo(13,5);outline.lineTo(13,14);outline.closeSubpath();
        outline.moveTo(10,2);outline.lineTo(10,5);outline.lineTo(13,5);
    }else{
        outline.moveTo(4,3.5);outline.lineTo(8,6.5);outline.lineTo(12,3.5);
        outline.moveTo(4,12.5);outline.lineTo(8,9.5);outline.lineTo(12,12.5);
    }
    if(iconType==ProjectIcon::AddFolder||iconType==ProjectIcon::AddFile){
        outline.moveTo(6,9);outline.lineTo(10,9);
        outline.moveTo(8,7);outline.lineTo(8,11);
    }
    QIcon icon;
    for(int scale:{1,2}){
        QPixmap pixmap(16*scale,16*scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(themeColor(theme,QPalette::Text,QColor(entry?(folder?"#94703d":"#7957a0"):"#644485")),
                            1.25,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        if(iconType==ProjectIcon::Folder)painter.setBrush(themeColor(theme,QPalette::Button,QColor("#f6e8ce")));
        painter.drawPath(outline);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

namespace {
class ProjectFileIcons final : public QFileIconProvider {
public:
    explicit ProjectFileIcons(const std::optional<QPalette> &theme = {})
        : folderIcon(projectIcon(ProjectIcon::Folder, theme)), fileIcon(projectIcon(ProjectIcon::File, theme)) {}
    QIcon icon(const QFileInfo &info) const override {
        return info.isDir()?folderIcon:fileIcon;
    }

private:
    const QIcon folderIcon;
    const QIcon fileIcon;
};

} // namespace

ProjectFileTree::ProjectFileTree(QWidget *parent) : QTreeView(parent)
{
    setObjectName(QStringLiteral("projectFileTree"));
    setIndentation(18);
    setIconSize(QSize(16,16));
    setUniformRowHeights(true);
    setMouseTracking(true);
    setTextElideMode(Qt::ElideMiddle);
    auto *files = new QFileSystemModel(this);
    files->setReadOnly(true);
    files->setIconProvider(new ProjectFileIcons);
    files->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    files->setNameFilters({QStringLiteral("*.md"), QStringLiteral("*.markdown")});
    files->setNameFilterDisables(false);
    setModel(files);
    setHeaderHidden(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int column = 1; column < files->columnCount(); ++column)
        hideColumn(column);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    setStyleSheet(QStringLiteral(R"(
            QTreeView#projectFileTree {
                background: #faf8fc; border: 0; outline: 0;
                selection-color: #4f2f74; show-decoration-selected: 0;
            }
            QTreeView#projectFileTree::item {
                padding: 4px 6px; min-height: 18px;
                margin: 1px 0; border: 1px solid transparent; border-radius: 6px;
            }
            QTreeView#projectFileTree::item:hover { background: #f1ebf7; }
            QTreeView#projectFileTree::item:selected {
                background: #eae0f5; border-color: #e4d6f1; color: #4f2f74;
            }
            QTreeView#projectFileTree QScrollBar:vertical {
                background: #faf8fc; width: 8px;
            }
            QTreeView#projectFileTree QScrollBar::handle:vertical { background: #c8bdd2; }
    )"));
    const auto openFile = [this, files](const QModelIndex &index) {
        if (rootIndex().isValid() && index.isValid() && !files->isDir(index))
            emit documentActivated(files->filePath(index));
    };
    connect(this, &QTreeView::clicked, this, openFile);
}

QFileSystemModel *ProjectFileTree::fileModel() const
{
    return static_cast<QFileSystemModel *>(model());
}

void ProjectFileTree::setThemePalette(const QPalette &palette)
{
    theme = palette;
    setPalette(palette);
    fileModel()->setIconProvider(new ProjectFileIcons(theme));
    setStyleSheet(QStringLiteral(R"(
        QTreeView#projectFileTree {
            background: %1; color: %2; border: 0; outline: 0; show-decoration-selected: 0;
        }
        QTreeView#projectFileTree::item {
            padding: 4px 6px; min-height: 18px;
            margin: 1px 0; border: 1px solid transparent; border-radius: 6px;
        }
        QTreeView#projectFileTree::item:hover { background: %3; }
        QTreeView#projectFileTree::item:selected { background: %4; border-color: %5; color: %6; }
        QTreeView#projectFileTree QScrollBar:vertical { background: %1; width: 8px; }
        QTreeView#projectFileTree QScrollBar::handle:vertical { background: %5; }
    )").arg(palette.color(QPalette::AlternateBase).name(), palette.color(QPalette::Text).name(),
             palette.color(QPalette::Button).name(), palette.color(QPalette::Highlight).name(),
             palette.color(QPalette::Mid).name(), palette.color(QPalette::HighlightedText).name()));
    viewport()->update();
}

void ProjectFileTree::setDirectory(const QString &directory)
{
    setRootIndex(directory.isEmpty() ? QModelIndex{} : fileModel()->setRootPath(directory));
}

void ProjectFileTree::selectFile(const QString &path)
{
    const auto index = path.isEmpty() ? QModelIndex{} : fileModel()->index(path);
    if (index.isValid() && !pathIsWithin(path, fileModel()->filePath(rootIndex())))
        return;

    for (auto parent = index.parent(); parent.isValid() && parent != rootIndex(); parent = parent.parent())
        expand(parent);
    setCurrentIndex(index);
    if (index.isValid())
        scrollTo(index);
}

void ProjectFileTree::setMoveHandler(std::function<void(const QString &, const QString &)> handler)
{
    moveEntry = std::move(handler);
    const bool enabled = bool(moveEntry);
    setDragDropMode(enabled ? DragDrop : NoDragDrop);
    setDragEnabled(enabled);
    setDefaultDropAction(Qt::MoveAction);
    setAcceptDrops(enabled);
    viewport()->setAcceptDrops(enabled);
}

void ProjectFileTree::drawBranches(QPainter *painter,const QRect &rect,const QModelIndex &index) const {
    painter->fillRect(rect,themeColor(theme,QPalette::AlternateBase,QColor("#faf8fc")));
    if(!model()->hasChildren(index))return;
    const QPointF center(rect.right()-indentation()/2.0,rect.center().y());
    QPainterPath chevron;
    if(isExpanded(index)){
        chevron.moveTo(center+QPointF(-3,-1.5));
        chevron.lineTo(center+QPointF(0,1.5));
        chevron.lineTo(center+QPointF(3,-1.5));
    }else{
        chevron.moveTo(center+QPointF(-1.5,-3));
        chevron.lineTo(center+QPointF(1.5,0));
        chevron.lineTo(center+QPointF(-1.5,3));
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(themeColor(theme,QPalette::Text,QColor("#807488")),1.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    painter->drawPath(chevron);
    painter->restore();
}

void ProjectFileTree::startDrag(Qt::DropActions) {
    const auto index=currentIndex();
    if(!moveEntry||!index.isValid()||index==rootIndex())return;
    auto *files=fileModel();
    auto *mime=new QMimeData;
    mime->setData(entryMimeType,files->filePath(index).toUtf8());
    auto *drag=new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(files->fileIcon(index).pixmap(24,24));
    drag->exec(Qt::MoveAction);
    stopAutoScroll();
    dropHighlight={};
    viewport()->update();
}

void ProjectFileTree::dragEnterEvent(QDragEnterEvent *event) {
    if(moveEntry&&event->source()==this&&event->mimeData()->hasFormat(entryMimeType))event->acceptProposedAction();
    else event->ignore();
}

void ProjectFileTree::dragMoveEvent(QDragMoveEvent *event) {
    const QPoint position=event->position().toPoint();
    const auto destination=dropDestination(event,position);
    if(hasAutoScroll()&&(position.y()<autoScrollMargin()||position.y()>=viewport()->height()-autoScrollMargin()))
        startAutoScroll();
    else stopAutoScroll();
    if(destination.isEmpty())dropHighlight={};
    else if(destination==fileModel()->filePath(rootIndex()))
        dropHighlight=QRect(0,0,viewport()->width(),3);
    else dropHighlight=visualRect(indexAt(position));
    viewport()->update();
    if(destination.isEmpty())event->ignore();
    else event->acceptProposedAction();
}

void ProjectFileTree::dragLeaveEvent(QDragLeaveEvent *event) {
    dropHighlight={};
    viewport()->update();
    QTreeView::dragLeaveEvent(event);
}

void ProjectFileTree::dropEvent(QDropEvent *event) {
    stopAutoScroll();
    dropHighlight={};
    viewport()->update();
    const auto destination=dropDestination(event,event->position().toPoint());
    if(destination.isEmpty()){event->ignore();return;}
    event->acceptProposedAction();
    const QString source=QString::fromUtf8(event->mimeData()->data(entryMimeType));
    // Finish the drag before showing any filesystem error dialog.
    QTimer::singleShot(0,this,[this,source,destination]{if(moveEntry)moveEntry(source,destination);});
}

void ProjectFileTree::paintEvent(QPaintEvent *event) {
    QTreeView::paintEvent(event);
    if(dropHighlight.isEmpty())return;
    QPainter painter(viewport());
    painter.setPen(themeColor(theme,QPalette::Link,QColor("#9877c2")));
    painter.drawRect(dropHighlight.adjusted(0,0,-1,-1));
}

QString ProjectFileTree::dropDestination(const QDropEvent *event,const QPoint &position) const {
    if(!moveEntry||event->source()!=this||!rootIndex().isValid())return {};
    auto *files=fileModel();
    const auto index=indexAt(position);
    auto target=index.isValid()?(files->isDir(index)?index:index.parent()):rootIndex();
    // The upper edge of a top-level row is also a root drop target.
    if(index.isValid()&&index.parent()==rootIndex()&&position.y()-visualRect(index).top()<4)
        target=rootIndex();
    const QString source=QString::fromUtf8(event->mimeData()->data(entryMimeType));
    const QString directory=files->filePath(target);
    const QString root=QFileInfo(files->filePath(rootIndex())).canonicalFilePath();
    return canMoveProjectEntry(QFileInfo(source),directory,root)?directory:QString();
}

} // namespace mdedit
