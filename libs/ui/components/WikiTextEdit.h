#pragma once

#include "darkeye_ui/components/DesignInput.h"

#include <functional>
#include <optional>

class QCompleter;
class QStringListModel;
class WikiImagePreviewWindow;

namespace darkeye {

class WikiHighlighter;

class WikiTextEdit final : public DesignTextEdit
{
    Q_OBJECT

public:
    using CompletionLoader = std::function<QStringList()>;
    using ImageResolver = std::function<QString(const QString &)>;
    using WorkIdResolver = std::function<std::optional<qint64>(const QString &)>;

    explicit WikiTextEdit(QWidget *parent = nullptr);

    void setCompleterList(const QStringList &words);
    [[nodiscard]] QStringList completerList() const;
    void setCompleterLoader(CompletionLoader loader);
    void reloadCompleter();
    void setImageResolver(ImageResolver resolver);
    void setWorkIdResolver(WorkIdResolver resolver);

    [[nodiscard]] QString completionPrefix() const;
    [[nodiscard]] static QString linkTargetAt(const QString &text, qsizetype position);
    void insertCompletion(const QString &completion);

signals:
    void linkActivated(const QString &target);
    void workLinkRequested(qint64 workId);
    void actressLinkRequested(qint64 actressId);
    void completerWordsLoaded(int sequence, const QStringList &words);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setupCompleterStyle();
    void updateCompleterPopup();
    void updatePreview(const QString &text);
    void handleLinkClick(const QString &target);

    WikiHighlighter *m_highlighter = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_model = nullptr;
    WikiImagePreviewWindow *m_preview = nullptr;
    CompletionLoader m_loader;
    ImageResolver m_imageResolver;
    WorkIdResolver m_workIdResolver;
    QString m_currentSelectedText;
    int m_reloadSequence = 0;
};

} // namespace darkeye
