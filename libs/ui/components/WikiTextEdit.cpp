#include "ui/components/WikiTextEdit.h"

#include "ui/components/WikiHighlighter.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QEvent>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextCursor>
#include <QThreadPool>
#include <QVBoxLayout>

class WikiImagePreviewWindow final : public QWidget
{
public:
    explicit WikiImagePreviewWindow(QWidget *parent = nullptr)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(2, 2, 2, 2);
        m_image = new QLabel(this);
        m_image->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_image);
        setStyleSheet(QStringLiteral(
            "background-color:#333333;border:1px solid #555555;border-radius:4px;"));
    }

    void showImage(const QString &path)
    {
        if (path.isEmpty()) {
            hide();
            return;
        }
        if (m_currentPath == path && isVisible()) return;
        m_currentPath = path;
        QImage image(path);
        if (image.isNull()) {
            hide();
            return;
        }
        const int cropWidth = qRound(image.height() * 0.7);
        image = image.copy(qMax(0, image.width() - cropWidth), 0,
                           qMin(cropWidth, image.width()), image.height());
        image = image.scaled(QSize(140, 200), Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
        const QPixmap pixmap = QPixmap::fromImage(image);
        m_image->setPixmap(pixmap);
        resize(pixmap.size() + QSize(4, 4));
        show();
    }

private:
    QLabel *m_image = nullptr;
    QString m_currentPath;
};

namespace darkeye {

WikiTextEdit::WikiTextEdit(QWidget *parent)
    : DesignTextEdit(parent)
{
    m_highlighter = new WikiHighlighter(document());
    setMouseTracking(true);

    m_model = new QStringListModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    setupCompleterStyle();

    m_preview = new WikiImagePreviewWindow(this);
    auto *popup = m_completer->popup();
    popup->setMouseTracking(true);
    popup->installEventFilter(this);
    popup->viewport()->setMouseTracking(true);
    popup->viewport()->installEventFilter(this);

    connect(m_completer, qOverload<const QString &>(&QCompleter::activated),
            this, &WikiTextEdit::insertCompletion);
    connect(this, &QTextEdit::textChanged, this, &WikiTextEdit::updateCompleterPopup);
    connect(popup->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current) {
        if (!current.isValid()) return;
        m_currentSelectedText = current.data().toString();
        updatePreview(m_currentSelectedText);
    });
    connect(this, &WikiTextEdit::completerWordsLoaded, this,
            [this](int sequence, const QStringList &words) {
        if (sequence == m_reloadSequence) setCompleterList(words);
    });
}

void WikiTextEdit::setCompleterList(const QStringList &words)
{
    m_model->setStringList(words);
}

QStringList WikiTextEdit::completerList() const
{
    return m_model->stringList();
}

void WikiTextEdit::setCompleterLoader(CompletionLoader loader)
{
    m_loader = std::move(loader);
    reloadCompleter();
}

void WikiTextEdit::reloadCompleter()
{
    if (!m_loader) return;
    const int sequence = ++m_reloadSequence;
    const CompletionLoader loader = m_loader;
    QPointer<WikiTextEdit> guard(this);
    QThreadPool::globalInstance()->start([guard, loader, sequence] {
        QStringList words;
        try { words = loader(); } catch (...) { words.clear(); }
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, sequence, words] {
            if (guard) emit guard->completerWordsLoaded(sequence, words);
        }, Qt::QueuedConnection);
    });
}

void WikiTextEdit::setImageResolver(ImageResolver resolver)
{
    m_imageResolver = std::move(resolver);
}

void WikiTextEdit::setWorkIdResolver(WorkIdResolver resolver)
{
    m_workIdResolver = std::move(resolver);
}

QString WikiTextEdit::completionPrefix() const
{
    const QTextCursor cursor = textCursor();
    const QString before = cursor.block().text().left(cursor.positionInBlock());
    const qsizetype brackets = before.lastIndexOf(QStringLiteral("[["));
    if (brackets < 0) return {};
    const QString between = before.mid(brackets + 2);
    if (between.contains(QStringLiteral("]]"))) return {};
    return between;
}

QString WikiTextEdit::linkTargetAt(const QString &text, qsizetype position)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\[\[(.*?)\]\])"));
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const qsizetype start = match.capturedStart();
        const qsizetype end = match.capturedEnd();
        if (start <= position && position <= end) {
            return match.captured(1).section(QLatin1Char('|'), 0, 0).trimmed();
        }
    }
    return {};
}

void WikiTextEdit::insertCompletion(const QString &completion)
{
    if (m_completer->widget() != this) return;
    QTextCursor cursor = textCursor();
    const QString activePrefix = m_completer->completionPrefix().isEmpty()
        ? completionPrefix() : m_completer->completionPrefix();
    const int prefixLength = activePrefix.size();
    cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, prefixLength);
    cursor.insertText(completion);
    cursor.insertText(QStringLiteral("]]"));
    setTextCursor(cursor);
}

bool WikiTextEdit::eventFilter(QObject *watched, QEvent *event)
{
    auto *popup = m_completer->popup();
    if (watched == popup || watched == popup->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            QPoint position = mouse->position().toPoint();
            if (watched == popup) position = popup->viewport()->mapFromParent(position);
            const QModelIndex index = popup->indexAt(position);
            updatePreview(index.isValid() ? index.data().toString() : m_currentSelectedText);
        } else if (event->type() == QEvent::Leave) {
            updatePreview(m_currentSelectedText);
        } else if (event->type() == QEvent::Hide) {
            m_preview->hide();
            m_currentSelectedText.clear();
        }
    }
    return DesignTextEdit::eventFilter(watched, event);
}

void WikiTextEdit::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QTextCursor cursor = cursorForPosition(event->position().toPoint());
        const QRect rect = cursorRect(cursor);
        const QPoint point = event->position().toPoint();
        if (qAbs(point.x() - rect.x()) <= fontMetrics().averageCharWidth()
            && point.y() >= rect.y() - rect.height() * 0.5
            && point.y() <= rect.y() + rect.height() * 1.5) {
            const QString target = linkTargetAt(toPlainText(), cursor.position());
            if (!target.isEmpty()) {
                handleLinkClick(target);
                return;
            }
        }
    }
    DesignTextEdit::mousePressEvent(event);
}

void WikiTextEdit::mouseMoveEvent(QMouseEvent *event)
{
    const QTextCursor cursor = cursorForPosition(event->position().toPoint());
    const QRect rect = cursorRect(cursor);
    const QPoint point = event->position().toPoint();
    const bool nearText = qAbs(point.x() - rect.x()) <= fontMetrics().averageCharWidth() * 2
        && point.y() >= rect.y() - rect.height() * 0.5
        && point.y() <= rect.y() + rect.height() * 1.5;
    const QString target = nearText ? linkTargetAt(toPlainText(), cursor.position()) : QString();
    viewport()->setCursor(target.isEmpty() ? Qt::IBeamCursor : Qt::PointingHandCursor);
    DesignTextEdit::mouseMoveEvent(event);
}

void WikiTextEdit::keyPressEvent(QKeyEvent *event)
{
    if (m_completer->popup()->isVisible()
        && (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Escape || event->key() == Qt::Key_Tab
            || event->key() == Qt::Key_Backtab)) {
        event->ignore();
        return;
    }
    DesignTextEdit::keyPressEvent(event);
    if (event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_E)
        updateCompleterPopup();
}

void WikiTextEdit::setupCompleterStyle()
{
    m_completer->popup()->setStyleSheet(QStringLiteral(R"(
        QListView { background-color:#FFFFFF; color:#111111; border:1px solid #454545;
                    border-radius:4px; padding:2px; outline:0; }
        QListView::item { padding:4px 10px; margin:2px 0; border-radius:4px; }
        QListView::item:selected { background-color:#d5d5d5; color:#ffffff; }
        QListView::item:hover:!selected { background-color:#d5d5d5; }
        QScrollBar:vertical { border:none; background:#e5e5e5; width:8px; margin:0; }
        QScrollBar::handle:vertical { background:#cccccc; min-height:20px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
    )"));
}

void WikiTextEdit::updateCompleterPopup()
{
    const QTextCursor cursor = textCursor();
    const QString before = cursor.block().text().left(cursor.positionInBlock());
    const qsizetype brackets = before.lastIndexOf(QStringLiteral("[["));
    if (brackets < 0 || before.mid(brackets + 2).contains(QStringLiteral("]]"))) {
        m_completer->popup()->hide();
        return;
    }
    const QString prefix = before.mid(brackets + 2);
    if (prefix != m_completer->completionPrefix()) {
        m_completer->setCompletionPrefix(prefix);
        m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    }
    QRect rect = cursorRect();
    rect.setWidth(qMax(150, m_completer->popup()->sizeHintForColumn(0)
                              + m_completer->popup()->verticalScrollBar()->sizeHint().width()));
    m_completer->complete(rect);
}

void WikiTextEdit::updatePreview(const QString &text)
{
    if (text.isEmpty() || !m_imageResolver) {
        m_preview->hide();
        return;
    }
    const QString path = m_imageResolver(text);
    if (path.isEmpty()) {
        m_preview->hide();
        return;
    }
    m_preview->showImage(path);
    auto *popup = m_completer->popup();
    const QPoint origin = popup->mapToGlobal(popup->rect().topLeft());
    int x = origin.x() - m_preview->width() - 5;
    if (x < 0) x = origin.x() + popup->width() + 5;
    m_preview->move(x, origin.y());
}

void WikiTextEdit::handleLinkClick(const QString &target)
{
    if (target.startsWith(QLatin1Char('w')) && target.mid(1).toLongLong() > 0) {
        emit workLinkRequested(target.mid(1).toLongLong());
        return;
    }
    if (target.startsWith(QLatin1Char('a')) && target.mid(1).toLongLong() > 0) {
        emit actressLinkRequested(target.mid(1).toLongLong());
        return;
    }
    if (m_workIdResolver) {
        const std::optional<qint64> workId = m_workIdResolver(target);
        if (workId.has_value()) {
            emit workLinkRequested(*workId);
            return;
        }
    }
    emit linkActivated(target);
}

} // namespace darkeye
