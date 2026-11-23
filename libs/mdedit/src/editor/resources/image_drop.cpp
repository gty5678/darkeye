#include "../markdown_editor_widget.h"

#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QMimeData>
#include <QUrl>

namespace {

QStringList localImages(const QMimeData &mime)
{
    QStringList paths;
    for (const auto &url : mime.urls()) {
        if (url.isLocalFile() && mdedit::ResourceResolver::isImageResource(url)
            && QFileInfo(url.toLocalFile()).isFile())
            paths.append(url.toLocalFile());
    }
    return paths;
}

QString storeImage(const QDir &resources, const QString &source, const QImage &image)
{
    const QFileInfo original(source);
    if (!source.isEmpty() && !original.isSymLink()
        && original.dir().canonicalPath() == resources.canonicalPath())
        return original.fileName();

    const QString stem = source.isEmpty() ? QStringLiteral("image") : original.completeBaseName();
    const QString extension = source.isEmpty() ? QStringLiteral("png") : original.suffix();
    for (int number = 1; ; ++number) {
        const QString name = (number == 1 ? stem : stem + '-' + QString::number(number))
            + '.' + extension;
        const QString destination = resources.filePath(name);
        const QFileInfo existing(destination);
        if (existing.exists() || existing.isSymLink()) continue;
        if (!source.isEmpty())
            return QFile::copy(source, destination) ? name : QString{};

        QFile output(destination);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return {};
        const bool saved = image.save(&output, "PNG") && output.flush();
        output.close();
        if (saved && output.error() == QFileDevice::NoError) return name;
        output.remove();
        return {};
    }
}

void showImportError(QWidget *parent, const QString &message)
{
    auto *dialog = new QMessageBox(QMessageBox::Warning, QStringLiteral("无法导入图片"),
                                   message, QMessageBox::Ok, parent);
    dialog->setTextFormat(Qt::PlainText);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

} // namespace

bool MarkdownEditorWidget::eventFilter(QObject *watched, QEvent *event)
{
    const bool live = watched == preview->viewport();
    if (!live && watched != editor->viewport()) return QWidget::eventFilter(watched, event);

    const auto type = event->type();
    if (type != QEvent::DragEnter && type != QEvent::DragMove && type != QEvent::Drop)
        return QWidget::eventFilter(watched, event);

    auto *drop = static_cast<QDropEvent *>(event);
    const auto &mime = *drop->mimeData();
    const auto paths = localImages(mime);
    if ((paths.isEmpty() && !mime.hasImage()) || !drop->possibleActions().testFlag(Qt::CopyAction))
        return QWidget::eventFilter(watched, event);

    drop->setDropAction(Qt::CopyAction);
    drop->accept();
    if (type != QEvent::Drop) return true;

    if (!baseUrl.isLocalFile() || !QFileInfo(baseUrl.toLocalFile()).isDir()) {
        showImportError(this, QStringLiteral("请先保存文档或打开项目文件夹，再拖入图片。"));
        return true;
    }
    const QDir directory(baseUrl.toLocalFile());
    if (!directory.mkpath(QStringLiteral("resources"))) {
        showImportError(this, QStringLiteral("无法创建 resources 文件夹。请检查目录权限。"));
        return true;
    }
    const QDir resources(directory.filePath(QStringLiteral("resources")));
    QStringList links;
    QStringList failures;
    const auto appendImage = [&](const QString &source, const QImage &image) {
        const QString name = storeImage(resources, source, image);
        if (name.isEmpty())
            failures.append(source.isEmpty() ? QStringLiteral("拖入的图片") : QFileInfo(source).fileName());
        else
            links.append(QStringLiteral("![](resources/%1)").arg(QString::fromLatin1(QUrl::toPercentEncoding(name))));
    };
    for (const auto &path : paths)
        appendImage(path, {});
    if (paths.isEmpty())
        appendImage({}, qvariant_cast<QImage>(mime.imageData()));

    if (!links.isEmpty()) {
        const QPoint point = drop->position().toPoint();
        const QString source = markdown();
        const int offset = qBound(0, live ? preview->sourcePositionAt(point)
                                         : editor->cursorForPosition(point).position(), int(source.size()));
        QString insertion = links.join(QStringLiteral("\n\n"));
        if (offset > 0 && source.at(offset - 1) != '\n') insertion.prepend(QStringLiteral("\n\n"));
        if (offset < source.size() && source.at(offset) != '\n') insertion.append(QStringLiteral("\n\n"));
        replaceSource(offset, offset, insertion, offset + insertion.size(), offset + insertion.size());
    }
    if (!failures.isEmpty())
        showImportError(this, QStringLiteral("以下图片无法保存到 resources：\n%1").arg(failures.join('\n')));
    return true;
}
