#include "project_files_panel.h"
#include "project_file_tree.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace mdedit {
namespace {
bool validProjectName(const QString &name){
    static const QRegularExpression invalidCharacters(R"([\\/:*?"<>|\x00-\x1f])");
    return !name.isEmpty()&&!name.startsWith('.')&&!name.contains(invalidCharacters);
}

QInputDialog *nameDialog(QWidget *parent, const QString &id, const QString &title,
                         const QString &label, const QString &initialName)
{
    auto *dialog = new QInputDialog(parent);
    dialog->setObjectName(id);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    dialog->setTextValue(initialName);
    return dialog;
}
} // namespace

QString relocatedPath(const QString &path,const QString &source,const QString &destination){
    if(path==source)return destination;
    return destination+"/"+QDir(source).relativeFilePath(path);
}

ProjectFilesPanel::ProjectFilesPanel(QWidget *parent)
    : QWidget(parent), m_tree(new ProjectFileTree(this))
{
    setObjectName("projectFilesPanel");
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("QWidget#projectFilesPanel, QWidget#projectFileActions { background: #faf8fc; }"));
    auto *filesLayout=new QVBoxLayout(this);
    filesLayout->setContentsMargins(8,6,8,6);
    filesLayout->setSpacing(4);

    auto *fileActions=new QWidget(this);
    m_fileActions=fileActions;
    fileActions->setObjectName("projectFileActions");
    fileActions->setAttribute(Qt::WA_StyledBackground);
    auto *fileActionsLayout=new QHBoxLayout(fileActions);
    fileActionsLayout->setContentsMargins(1,0,1,4);
    fileActionsLayout->setSpacing(5);
    fileActions->setStyleSheet(R"(
        QToolButton {
            background: #f3edf8; border: 1px solid #ebe2f2;
            border-radius: 6px; padding: 4px;
        }
        QToolButton:hover:enabled { background: #eae2f7; }
        QToolButton:pressed { background: #ded0ef; }
        QToolButton:focus { border-color: #9877c2; }
        QToolButton:disabled { background: transparent; }
    )");
    auto addFileAction=[&](const QString &text,const QString &id,ProjectIcon icon){
        auto *button=new QToolButton(fileActions);
        button->setObjectName(id);
        button->setToolTip(text);
        button->setAccessibleName(text);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setIconSize(QSize(16,16));
        button->setIcon(projectIcon(icon));
        m_actionIcons.append({button, icon});
        fileActionsLayout->addWidget(button);
        return button;
    };
    auto *addFolder = addFileAction("添加文件夹", "addProjectFolderButton", ProjectIcon::AddFolder);
    auto *addFile = addFileAction("新建 MD 文件", "newProjectFileButton", ProjectIcon::AddFile);
    auto *collapse = addFileAction("全部折叠", "collapseFoldersButton", ProjectIcon::Collapse);
    fileActionsLayout->addStretch();
    filesLayout->addWidget(fileActions, 0, Qt::AlignTop);
    filesLayout->addWidget(m_tree, 1);

    connect(addFolder, &QToolButton::clicked, this, [this] { createFolder(selectedDirectory()); });
    connect(addFile, &QToolButton::clicked, this, [this] { requestFile(selectedDirectory()); });
    connect(collapse, &QToolButton::clicked, m_tree, &QTreeView::collapseAll);
    m_tree->setMoveHandler([this](const QString &source, const QString &destination) {
        moveEntry(source, destination);
    });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, [this](const QPoint &position) {
        const auto index = m_tree->indexAt(position);
        const QString directory = directoryFor(index);
        if (directory.isEmpty()) return;
        QMenu menu(m_tree);
        auto *create = menu.addAction("新建 Markdown 文件…");
        auto *folder = menu.addAction("添加文件夹…");
        QAction *rename = nullptr;
        QAction *remove = nullptr;
        const QString entry = m_tree->fileModel()->filePath(index);
        if (containsEntry(entry)) {
            menu.addSeparator();
            rename = menu.addAction("重命名…");
            remove = menu.addAction("删除…");
        }
        auto *chosen = menu.exec(m_tree->viewport()->mapToGlobal(position));
        if (chosen == create) requestFile(directory);
        else if (chosen == folder) createFolder(directory);
        else if (chosen && chosen == rename) renameEntry(entry);
        else if (chosen && chosen == remove) trashEntry(entry);
    });
}

QSize ProjectFilesPanel::sizeHint() const
{
    QSize size = QWidget::sizeHint();
    size.setWidth(200);
    return size;
}

void ProjectFilesPanel::setThemePalette(const QPalette &palette)
{
    setPalette(palette);
    setStyleSheet(QStringLiteral("QWidget#projectFilesPanel, QWidget#projectFileActions { background: %1; }")
                      .arg(palette.color(QPalette::AlternateBase).name()));
    m_fileActions->setStyleSheet(QStringLiteral(R"(
        QWidget#projectFileActions { border-bottom: 1px solid %2; }
        QToolButton { background: %1; border: 1px solid %2; padding: 4px; }
        QToolButton:hover:enabled { background: %3; }
        QToolButton:pressed { background: %3; border-color: %4; }
        QToolButton:focus { border-color: %4; }
        QToolButton:disabled { background: transparent; }
    )").arg(palette.color(QPalette::Button).name(), palette.color(QPalette::Mid).name(),
             palette.color(QPalette::Highlight).name(), palette.color(QPalette::Link).name()));
    for (const auto &[button, icon] : m_actionIcons)
        button->setIcon(projectIcon(icon, palette));
    m_tree->setThemePalette(palette);
}

void ProjectFilesPanel::setBeforeEntryChange(std::function<bool(const QString &)> handler)
{
    m_beforeEntryChange = std::move(handler);
}

QString ProjectFilesPanel::directoryFor(const QModelIndex &index) const
{
    const auto *model = m_tree->fileModel();
    if (!m_tree->rootIndex().isValid()) return {};
    if (!index.isValid()) return model->filePath(m_tree->rootIndex());
    return model->filePath(model->isDir(index) ? index : index.parent());
}

QString ProjectFilesPanel::selectedDirectory() const
{
    const auto index = m_tree->currentIndex();
    if (m_tree->selectionModel()->isSelected(index) && m_tree->visualRect(index).isValid())
        return directoryFor(index);
    return directoryFor({});
}

bool ProjectFilesPanel::containsEntry(const QString &path) const
{
    const QString root = directoryFor({});
    return !path.isEmpty() && !root.isEmpty()
        && pathIsWithin(QFileInfo(path).dir().canonicalPath(), QDir(root).canonicalPath());
}

void ProjectFilesPanel::createFolder(const QString &directory)
{
    if (directory.isEmpty()) return;
    auto *dialog = nameDialog(this, "newProjectFolderDialog", "添加文件夹",
                              QString("在 %1 中新建文件夹").arg(QDir::toNativeSeparators(directory)),
                              "新建文件夹");
    connect(dialog, &QInputDialog::textValueSelected, this, [this, directory](const QString &input) {
        const QString name = input.trimmed();
        if (!validProjectName(name) || !containsEntry(QDir(directory).filePath(name))
            || !QDir(directory).mkdir(name)) {
            QMessageBox::warning(this, "无法添加文件夹", "请检查名称是否有效、是否已被使用以及目录是否可写。");
            return;
        }
        const auto parent = m_tree->fileModel()->index(directory);
        if (parent != m_tree->rootIndex()) m_tree->expand(parent);
    });
    dialog->open();
}

void ProjectFilesPanel::requestFile(const QString &directory)
{
    if (directory.isEmpty()) return;
    auto *dialog = nameDialog(this, "newMarkdownFileDialog", "新建 Markdown 文件",
                              QString("在 %1 中新建文件").arg(QDir::toNativeSeparators(directory)),
                              "未命名.md");
    connect(dialog, &QInputDialog::textValueSelected, this, [this, directory](const QString &input) {
        QString name = input.trimmed();
        if (!validProjectName(name)) {
            QMessageBox::warning(this, "无法新建文件", "请输入有效的文件名。");
            return;
        }
        if (!name.endsWith(".md", Qt::CaseInsensitive)
            && !name.endsWith(".markdown", Qt::CaseInsensitive)) name += ".md";
        const QString destination = QDir(directory).filePath(name);
        if (!containsEntry(destination)) {
            QMessageBox::warning(this, "无法新建文件", "目标必须位于当前项目。");
            return;
        }
        emit createFileRequested(destination);
    });
    dialog->open();
}

bool ProjectFilesPanel::relocateEntry(const QString &source, const QString &destination, const QString &title)
{
    if (source == destination || !containsEntry(source) || !containsEntry(destination)) return false;
    const QFileInfo existing(destination);
    if (existing.exists() || existing.isSymLink()) {
        QMessageBox::warning(this, title, "目标位置已有同名文件或文件夹。");
        return false;
    }
    if (m_beforeEntryChange && !m_beforeEntryChange(source)) return false;
    if (!QDir().rename(source, destination)) {
        QMessageBox::warning(this, title, "请检查名称和文件夹权限。");
        return false;
    }
    emit entryChanged(source, destination);
    return true;
}

void ProjectFilesPanel::moveEntry(const QString &source, const QString &directory)
{
    if (!containsEntry(source)) return;
    const QFileInfo entry(source);
    if (!canMoveProjectEntry(entry, directory, QDir(directoryFor({})).canonicalPath())) return;
    if (!relocateEntry(source, QDir(directory).filePath(entry.fileName()), "无法移动")) return;
    const auto target = m_tree->fileModel()->index(directory);
    if (target != m_tree->rootIndex()) m_tree->expand(target);
}

void ProjectFilesPanel::renameEntry(const QString &entry)
{
    if (!containsEntry(entry)) return;
    const QFileInfo info(entry);
    auto *dialog = nameDialog(this, "renameProjectEntryDialog", "重命名",
                              info.isDir() ? "新文件夹名称：" : "新名称（包含文件扩展名）：",
                              info.fileName());
    connect(dialog, &QInputDialog::textValueSelected, this, [this, entry](const QString &input) {
        const QString name = input.trimmed();
        if (!validProjectName(name)) {
            QMessageBox::warning(this, "无法重命名", "请输入有效的名称。");
            return;
        }
        const QString destination = QFileInfo(entry).dir().filePath(name);
        if (relocateEntry(entry, destination, "无法重命名")) m_tree->selectFile(destination);
    });
    dialog->open();
}

void ProjectFilesPanel::trashEntry(const QString &entry)
{
    if (!containsEntry(entry)) return;
    const QFileInfo info(entry);
    const QString question = info.isDir() && !info.isSymLink()
        ? QString("将文件夹「%1」及其全部内容移到废纸篓？").arg(info.fileName())
        : QString("将「%1」移到废纸篓？").arg(info.fileName());
    if (QMessageBox::question(this, "删除", question, QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (!containsEntry(entry) || (m_beforeEntryChange && !m_beforeEntryChange(entry))) return;
    QFile file(entry);
    if (!file.moveToTrash()) {
        QMessageBox::warning(this, "无法移到废纸篓", file.errorString());
        return;
    }
    emit entryChanged(entry, {});
}

} // namespace mdedit
