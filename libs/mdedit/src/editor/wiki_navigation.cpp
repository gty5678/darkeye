#include "markdown_editor_widget.h"
#include <QPromise>
#include <QShortcut>
#include <QThreadPool>
#include <memory>
#include <utility>

namespace {

QString markdownDirectory(const WikiLinkContext &context)
{
    if (!context.projectRoot.isEmpty()) return QDir(context.projectRoot).absolutePath();
    return context.baseUrl.isLocalFile() ? QDir(context.baseUrl.toLocalFile()).absolutePath() : QString{};
}

bool isWithinDirectory(const QString &canonicalRoot, const QFileInfo &file)
{
    const QString path = file.canonicalFilePath();
    if (canonicalRoot.isEmpty() || path.isEmpty() || !file.isFile()) return false;
    const QString relative = QDir(canonicalRoot).relativeFilePath(path);
    return relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative);
}

QStringList scanMarkdownFiles(const QString &directory, bool recursive)
{
    if (directory.isEmpty()) return {};
    const QDir root(directory);
    const QString canonicalRoot = QFileInfo(directory).canonicalFilePath();
    QStringList files;
    QDirIterator it(directory, QDir::Files | QDir::NoDotAndDotDot,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QFileInfo file(it.next());
        const QString suffix = file.suffix().toLower();
        if ((suffix == "md" || suffix == "markdown") && isWithinDirectory(canonicalRoot, file))
            files.append(root.relativeFilePath(file.absoluteFilePath()));
    }
    files.sort();
    return files;
}

QStringList resolveMarkdown(const QString &reference, const QStringList &files)
{
    if (reference.trimmed().isEmpty()) return {};
    const bool explicitRelative = reference.startsWith("./");
    const QString name = QDir::cleanPath(reference.trimmed());
    if (explicitRelative && (name.endsWith(".md", Qt::CaseInsensitive) ||
                             name.endsWith(".markdown", Qt::CaseInsensitive)))
        return files.contains(name) ? QStringList{name} : QStringList{};
    QStringList exact, matches;
    for (const QString &path : files) {
        const QFileInfo file(path);
        const QString stem = path.left(path.size() - file.suffix().size() - 1);
        if (path == name || stem == name) exact.append(path);
        else if (!explicitRelative && !name.contains('/') &&
                 (file.fileName() == name || file.completeBaseName() == name))
            matches.append(path);
    }
    return exact.isEmpty() ? matches : exact;
}

bool fitsWikiLink(const QString &value)
{
    static const QRegularExpression delimiters(R"([\[\]|\r\n\x{2028}\x{2029}])");
    return !value.trimmed().isEmpty() && !value.contains(delimiters);
}

WikiLinkTarget markdownTarget(const QString &path)
{
    return {"./" + path, QFileInfo(path).completeBaseName(), path, "Markdown"};
}

bool matchesQuery(const WikiLinkTarget &target, const QString &query)
{
    return target.title.contains(query, Qt::CaseInsensitive) ||
           target.detail.contains(query, Qt::CaseInsensitive) ||
           target.reference.contains(query, Qt::CaseInsensitive) ||
           target.type.contains(query, Qt::CaseInsensitive);
}

QString providerPrefix(const QString &reference)
{
    const qsizetype colon = reference.indexOf(':');
    return colon > 0 ? reference.left(colon) : QString{};
}

}

bool MarkdownEditorWidget::registerWikiLinkProvider(const QString &prefix, WikiLinkProvider provider)
{
    static const QRegularExpression validPrefix(R"(\A[A-Za-z][A-Za-z0-9_-]*\z)");
    if (!validPrefix.match(prefix).hasMatch() || !provider.search || !provider.resolve || !provider.activate)
        return false;
    if (wikiPicker) wikiPicker->reject();
    pendingWikiReference.clear();
    wikiProviders.insert(prefix, std::move(provider));
    return true;
}

void MarkdownEditorWidget::setProjectRoot(const QString &root)
{
    if (projectRoot == root) return;
    projectRoot = root;
    resetWikiNavigation();
}

void MarkdownEditorWidget::resetWikiNavigation()
{
    if (wikiPicker) wikiPicker->reject();
    pendingWikiReference.clear();
    const QString directory = markdownDirectory(wikiContext());
    const bool recursive = !projectRoot.isEmpty();
    if (directory == indexedDirectory && recursive == recursiveIndex) return;

    indexedDirectory = directory;
    recursiveIndex = recursive;
    markdownFiles.clear();
    if (markdownScan) {
        markdownScan->disconnect(this);
        markdownScan->deleteLater();
        markdownScan = nullptr;
    }
    refreshMarkdownIndex();
}

void MarkdownEditorWidget::refreshMarkdownIndex()
{
    if (indexedDirectory.isEmpty() || markdownScan) return;
    auto *scan = new QFutureWatcher<QStringList>(this);
    markdownScan = scan;
    connect(scan, &QFutureWatcher<QStringList>::finished, this, [this, scan] {
        if (markdownScan != scan) return;
        markdownFiles = scan->result();
        markdownScan = nullptr;
        scan->deleteLater();
        emit markdownIndexChanged();

        const QString reference = std::exchange(pendingWikiReference, {});
        if (reference.isEmpty()) return;
        QList<WikiLinkTarget> targets;
        for (const QString &path : resolveMarkdown(reference, markdownFiles))
            targets.append(markdownTarget(path));
        const QString directory = indexedDirectory;
        chooseWikiDestination(reference, targets, [this, directory](const WikiLinkTarget &target) {
            const QFileInfo file(QDir(directory).absoluteFilePath(target.detail));
            if (isWithinDirectory(QFileInfo(directory).canonicalFilePath(), file))
                emit fileLinkActivated(file.absoluteFilePath());
            else
                chooseWikiDestination(target.reference, {}, {});
        });
    });
    QPromise<QStringList> promise;
    promise.start();
    scan->setFuture(promise.future());
    QThreadPool::globalInstance()->start([promise = std::move(promise), directory = indexedDirectory,
                                         recursive = recursiveIndex]() mutable {
        promise.addResult(scanMarkdownFiles(directory, recursive));
        promise.finish();
    });
}

QStringList MarkdownEditorWidget::wikiLinkCandidates(const QString &target) const
{
    const QString directory = markdownDirectory(wikiContext());
    auto matches = resolveMarkdown(target, scanMarkdownFiles(directory, !projectRoot.isEmpty()));
    for (QString &path : matches) path = QDir(directory).absoluteFilePath(path);
    return matches;
}

void MarkdownEditorWidget::followWikiLink(const QString &reference)
{
    pendingWikiReference.clear();
    const auto provider = wikiProviders.constFind(providerPrefix(reference));
    if (provider != wikiProviders.cend()) {
        const auto context = wikiContext();
        const auto callbacks = *provider;
        chooseWikiDestination(reference, callbacks.resolve(reference, context),
                             [callbacks, context](const WikiLinkTarget &target) { callbacks.activate(target, context); });
    } else if (indexedDirectory.isEmpty()) {
        chooseWikiDestination(reference, {}, {});
    } else {
        pendingWikiReference = reference;
        refreshMarkdownIndex();
    }
}

void MarkdownEditorWidget::chooseWikiDestination(const QString &reference, const QList<WikiLinkTarget> &targets,
                                                std::function<void(const WikiLinkTarget &)> activate)
{
    if (targets.isEmpty()) {
        auto *message = new QMessageBox(QMessageBox::Information, "无法跳转", QString{}, QMessageBox::Ok, this);
        message->setTextFormat(Qt::PlainText);
        message->setText("未找到链接目标：" + reference);
        message->setAttribute(Qt::WA_DeleteOnClose);
        message->open();
    } else if (targets.size() == 1) {
        activate(targets.first());
    } else {
        showWikiPicker("选择链接目标", "打开", [targets](const QString &query) {
            QList<WikiLinkTarget> matches;
            for (const auto &target : targets)
                if (matchesQuery(target, query)) matches.append(target);
            return matches;
        }, std::move(activate));
    }
}

void MarkdownEditorWidget::insertWikiLink()
{
    pendingWikiReference.clear();
    const auto [anchor, position] = selection();
    const int start = qMin(anchor, position), end = qMax(anchor, position);
    QTextCursor range(document());
    range.setPosition(start);
    range.setPosition(end, QTextCursor::KeepAnchor);
    refreshMarkdownIndex();
    showWikiPicker("插入内部链接", "插入", [this](const QString &query) {
        QList<WikiLinkTarget> targets;
        for (const QString &path : markdownFiles) {
            if (!path.contains(query, Qt::CaseInsensitive) &&
                !QStringLiteral("Markdown").contains(query, Qt::CaseInsensitive)) continue;
            const auto target = markdownTarget(path);
            if (fitsWikiLink(target.reference)) targets.append(target);
        }
        const auto context = wikiContext();
        const auto providers = wikiProviders;
        for (auto provider = providers.cbegin(); provider != providers.cend(); ++provider)
            for (const auto &target : provider->search(query, context))
                if (providerPrefix(target.reference) == provider.key() &&
                    target.reference == target.reference.trimmed() &&
                    target.reference.size() > provider.key().size() + 1 &&
                    fitsWikiLink(target.reference))
                    targets.append(target);
        return targets;
    }, [this, range](const WikiLinkTarget &target) {
        const QString selected = range.selectedText();
        const QString label = fitsWikiLink(selected) ? selected.trimmed() : target.title.trimmed();
        QString link = "[[" + target.reference;
        if (label != target.reference && fitsWikiLink(label)) link += "|" + label;
        link += "]]";
        const int caret = range.selectionStart() + link.size();
        replaceSource(range.selectionStart(), range.selectionEnd(), link, caret, caret);
    }, true);
}

void MarkdownEditorWidget::showWikiPicker(const QString &title, const QString &button,
    std::function<QList<WikiLinkTarget>(const QString &)> search,
    std::function<void(const WikiLinkTarget &)> select, bool includeMarkdownIndex)
{
    if (wikiPicker) wikiPicker->reject();
    auto *dialog = new QDialog(this);
    wikiPicker = dialog;
    dialog->setObjectName("wikiLinkPicker");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(500, 360);

    auto *layout = new QVBoxLayout(dialog);
    auto *query = new QLineEdit(dialog);
    query->setPlaceholderText("搜索链接目标");
    query->setAccessibleName("搜索链接目标");
    auto *results = new QListWidget(dialog);
    results->setAccessibleName("链接目标");
    auto *status = new QLabel(dialog);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
    auto *accept = buttons->addButton(button, QDialogButtonBox::AcceptRole);
    accept->setDefault(true);
    layout->addWidget(query);
    layout->addWidget(results);
    layout->addWidget(status);
    layout->addWidget(buttons);

    auto targets = std::make_shared<QList<WikiLinkTarget>>();
    const auto update = [this, query, results, status, targets, search, includeMarkdownIndex] {
        const int previous = results->currentRow();
        const QString reference = previous >= 0 ? targets->at(previous).reference : QString{};
        *targets = search(query->text().trimmed());
        results->clear();
        int current = 0;
        for (int row = 0; row < targets->size(); ++row) {
            const auto &target = targets->at(row);
            QString label = target.title.isEmpty() ? target.reference : target.title;
            if (!target.type.isEmpty()) label += "  ·  " + target.type;
            if (!target.detail.isEmpty()) label += "  —  " + target.detail;
            results->addItem(label);
            if (target.reference == reference) current = row;
        }
        if (!targets->isEmpty()) results->setCurrentRow(current);
        status->setText(includeMarkdownIndex && markdownScan ? "正在扫描 Markdown 文件…"
            : targets->isEmpty() ? "没有匹配的目标" : QString{});
    };
    connect(query, &QLineEdit::textChanged, dialog, update);
    if (includeMarkdownIndex) connect(this, &MarkdownEditorWidget::markdownIndexChanged, dialog, update);
    connect(results, &QListWidget::currentRowChanged, accept, [accept](int row) { accept->setEnabled(row >= 0); });
    accept->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(results, &QListWidget::itemDoubleClicked, dialog, &QDialog::accept);
    for (const int direction : {-1, 1}) {
        auto *shortcut = new QShortcut(QKeySequence(direction < 0 ? Qt::Key_Up : Qt::Key_Down), query);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, results, [results, direction] {
            if (results->count()) results->setCurrentRow(qBound(0, results->currentRow() + direction, results->count() - 1));
        });
    }
    connect(dialog, &QDialog::finished, this, [this, dialog, results, targets, select](int result) {
        if (wikiPicker == dialog) wikiPicker.clear();
        if (result == QDialog::Accepted && results->currentRow() >= 0)
            select(targets->at(results->currentRow()));
    });
    update();
    dialog->open();
    query->setFocus();
}
