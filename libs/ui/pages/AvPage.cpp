#include "ui/pages/AvPage.h"

#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/theme/ChamferPath.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "editor/markdown_editor_widget.h"
#include "editor/project_file_tree.h"
#include "editor/project_files_panel.h"
#include "settings/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QLabel>
#include <QPainter>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace darkeye
{
namespace
{
constexpr int wikiCorner = 6;

class WikiDocumentPanel final : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(palette().color(QPalette::Mid));
        painter.setBrush(palette().color(QPalette::Base));
        painter.drawPath(chamferPath(QRectF(rect()).adjusted(.5, .5, -.5, -.5), wikiCorner));
    }
};

QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
} // namespace

AvPage::AvPage(QWidget *parent) : AvPage({}, parent)
{
}

AvPage::AvPage(QString wikiDirectory, QWidget *parent)
    : LazyWidget(parent), m_requestedWikiDirectory(std::move(wikiDirectory))
{
}

AvPage::AvPage(QString wikiDirectory, ThemeService &themes, QWidget *parent)
    : AvPage(std::move(wikiDirectory), parent)
{
    m_themes = &themes;
}

void AvPage::lazyLoad()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 8, 8);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("AvWikiSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(9);

    m_filesPanel = new mdedit::ProjectFilesPanel(splitter);
    m_tree = m_filesPanel->tree();

    m_documentPanel = new WikiDocumentPanel(splitter);
    auto *contentLayout = new QVBoxLayout(m_documentPanel);
    // Keep the scrolling viewport inside the frame's diagonals.
    contentLayout->setContentsMargins(wikiCorner, wikiCorner, wikiCorner, wikiCorner);

    m_editor = new MarkdownEditorWidget(m_documentPanel);
    m_editor->setObjectName(QStringLiteral("AvWikiEditor"));
    m_editor->applyBaseFontSize(13);
    m_editor->setLiveMode(true);
    m_editor->liveModeAction()->setEnabled(false);
    m_editor->hide();
    contentLayout->addWidget(m_editor, 1);

    m_emptyMessage = new QLabel(m_documentPanel);
    m_emptyMessage->setAlignment(Qt::AlignCenter);
    m_emptyMessage->setWordWrap(true);
    contentLayout->addWidget(m_emptyMessage, 1);

    splitter->addWidget(m_filesPanel);
    splitter->addWidget(m_documentPanel);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter);

    connect(m_tree, &mdedit::ProjectFileTree::documentActivated, this, &AvPage::openDocument);
    connect(m_filesPanel, &mdedit::ProjectFilesPanel::createFileRequested, this, &AvPage::createDocument);
    m_filesPanel->setBeforeEntryChange([this](const QString &entry) {
        return !mdedit::pathIsWithin(m_currentPath, entry) || autoSave();
    });
    connect(m_filesPanel, &mdedit::ProjectFilesPanel::entryChanged, this,
            [this](const QString &source, const QString &destination) {
        if (!mdedit::pathIsWithin(m_currentPath, source)) return;
        if (destination.isEmpty())
        {
            closeDocument();
            return;
        }
        m_currentPath = mdedit::relocatedPath(m_currentPath, source, destination);
        m_editor->setBaseUrl(QUrl::fromLocalFile(QFileInfo(m_currentPath).absolutePath() + '/'));
        m_tree->selectFile(m_currentPath);
    });
    connect(m_editor, &MarkdownEditorWidget::fileLinkActivated, this, &AvPage::openDocument);
    connect(m_tree->fileModel(), &QFileSystemModel::rowsInserted, this, [this] {
        if (!m_currentPath.isEmpty() && !m_tree->currentIndex().isValid())
            m_tree->selectFile(m_currentPath);
    });
    connect(m_tree->fileModel(), &QFileSystemModel::rowsRemoved, this, [this] {
        if (m_currentPath.isEmpty() || QFileInfo::exists(m_currentPath)
            || m_editor->document()->isModified()) return;
        closeDocument();
    }, Qt::QueuedConnection);

    connect(m_editor, &MarkdownEditorWidget::textChanged, this, [this] {
        m_saveTimer->start(1500);
    });

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    connect(m_saveTimer, &QTimer::timeout, this, &AvPage::autoSave);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &AvPage::autoSave);

    m_wikiDirectory = resolveWikiDirectory();
    m_editor->setProjectRoot(m_wikiDirectory);
    const bool exists = QFileInfo(m_wikiDirectory).isDir();
    if (exists)
        m_tree->setDirectory(m_wikiDirectory);
    m_filesPanel->setVisible(exists);
    m_emptyMessage->setTextFormat(Qt::PlainText);
    m_emptyMessage->setText(exists ? QStringLiteral("请选择一篇知识库文档")
                                  : QStringLiteral("未找到知识库目录：%1").arg(m_wikiDirectory));
    if (m_themes)
    {
        connect(m_themes, &ThemeService::themeChanged, this, &AvPage::applyTheme);
        applyTheme();
    }
}

void AvPage::applyTheme()
{
    const auto tokens = m_themes->currentTokens();
    const QColor background(tokens.background);
    const QColor text(tokens.text);
    const QColor primary(tokens.primary);
    const auto mix = [](const QColor &from, const QColor &to, double weight) {
        return QColor::fromRgbF(from.redF() * (1 - weight) + to.redF() * weight,
                                from.greenF() * (1 - weight) + to.greenF() * weight,
                                from.blueF() * (1 - weight) + to.blueF() * weight);
    };
    QPalette themed = palette();
    themed.setColor(QPalette::Window, QColor(tokens.pageBackground));
    themed.setColor(QPalette::WindowText, text);
    themed.setColor(QPalette::Base, background);
    themed.setColor(QPalette::AlternateBase, QColor(tokens.pageBackground));
    themed.setColor(QPalette::Button, QColor(tokens.inputBackground));
    themed.setColor(QPalette::ButtonText, text);
    themed.setColor(QPalette::Text, text);
    themed.setColor(QPalette::Mid, QColor(tokens.border));
    themed.setColor(QPalette::Link, mix(primary, text, 0.45));
    themed.setColor(QPalette::Highlight, mix(background, primary, 0.32));
    themed.setColor(QPalette::HighlightedText, text);
    themed.setColor(QPalette::Light, mix(background, QColor(tokens.warning), 0.26));
    m_documentPanel->setPalette(themed);
    m_filesPanel->setThemePalette(themed);
    m_editor->setThemePalette(themed);
}

QString AvPage::resolveWikiDirectory() const
{
    if (!m_requestedWikiDirectory.trimmed().isEmpty())
        return normalizedPath(m_requestedWikiDirectory);

    return normalizedPath(settings::Paths().wikiDirectory());
}

void AvPage::openDocument(const QString &filePath)
{
    const QString path = normalizedPath(filePath);
    if (path == m_currentPath) return;

    const QFileInfo file(path);
    const QString suffix = file.suffix();
    const QString canonicalRoot = QFileInfo(m_wikiDirectory).canonicalFilePath();
    if (!file.isFile() || !mdedit::pathIsWithin(file.canonicalFilePath(), canonicalRoot)
        || (suffix.compare(QStringLiteral("md"), Qt::CaseInsensitive) != 0
            && suffix.compare(QStringLiteral("markdown"), Qt::CaseInsensitive) != 0)
        || !autoSave() || !loadEditor(path))
    {
        m_tree->selectFile(m_currentPath);
        return;
    }

    m_currentPath = path;
    m_emptyMessage->hide();
    m_editor->show();
    m_tree->selectFile(path);
}

void AvPage::createDocument(const QString &filePath)
{
    if (!autoSave()) return;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        Toast::showError(window(), QStringLiteral("无法新建文件：%1").arg(file.errorString()));
        return;
    }
    file.close();
    openDocument(filePath);
}

void AvPage::closeDocument()
{
    m_currentPath.clear();
    m_tree->selectFile({});
    m_editor->hide();
    m_emptyMessage->show();
}

bool AvPage::loadEditor(const QString &filePath)
{
    QFile file(filePath);
    QByteArray contents;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        contents = file.readAll();
    if (file.error() != QFileDevice::NoError)
    {
        Toast::showError(window(), QStringLiteral("读取失败：%1").arg(file.errorString()));
        return false;
    }
    const QSignalBlocker blocker(m_editor);
    m_editor->setMarkdown(QString::fromUtf8(contents),
                          QUrl::fromLocalFile(QFileInfo(filePath).absolutePath() + '/'));
    return true;
}

bool AvPage::autoSave()
{
    if (m_currentPath.isEmpty() || !m_editor->document()->isModified()) return true;
    m_saveTimer->stop();
    const auto contents = m_editor->markdown().toUtf8();
    QSaveFile file(m_currentPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
        || file.write(contents) != contents.size() || !file.commit())
    {
        Toast::showError(window(), QStringLiteral("自动保存失败：%1").arg(file.errorString()));
        return false;
    }
    m_editor->document()->setModified(false);
    return true;
}

} // namespace darkeye
