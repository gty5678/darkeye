#pragma once
#include "live_editor.h"
#include "wiki_navigation.h"
#include <QFutureWatcher>
#include <QMap>
#include <QPointer>

class Highlighter;

// A complete editor surface with host-controlled document opening and saving.
class MarkdownEditorWidget : public QWidget {
    Q_OBJECT
    NumberedEditor<QPlainTextEdit> *editor=new NumberedEditor<QPlainTextEdit>;
    Highlighter *highlighter=nullptr;
    LiveEditor *preview=new LiveEditor;
    QStackedWidget *pages=new QStackedWidget;
    QAction *previewAction;
    QList<QAction *> commands;
    QTimer timer;
    QUrl baseUrl;
    QString projectRoot;
    QString indexedDirectory;
    bool recursiveIndex=false;
    QStringList markdownFiles;
    QFutureWatcher<QStringList> *markdownScan=nullptr;
    QString pendingWikiReference;
    QPointer<QDialog> wikiPicker;
    QMap<QString,WikiLinkProvider> wikiProviders;
    WikiLinkContext wikiContext() const { return {baseUrl,projectRoot}; }
    void resetWikiNavigation();
    void refreshMarkdownIndex();
    Q_SIGNAL void markdownIndexChanged();
    void followWikiLink(const QString &reference);
    void chooseWikiDestination(const QString &reference,const QList<WikiLinkTarget> &targets,
                               std::function<void(const WikiLinkTarget &)> activate);
    void showWikiPicker(const QString &title,const QString &button,
                        std::function<QList<WikiLinkTarget>(const QString &)> search,
                        std::function<void(const WikiLinkTarget &)> select,bool includeMarkdownIndex=false);
    int baseFontSize=14;
    const QFont defaultUiFont=QApplication::font();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
public:
    explicit MarkdownEditorWidget(QWidget *parent=nullptr);
    QString markdown() const;
    void setMarkdown(const QString &text);
    void setMarkdown(const QString &text,const QUrl &url);
    QTextDocument *document() const;
    void setBaseUrl(const QUrl &url);
    void setThemePalette(const QPalette &palette);
    void setProjectRoot(const QString &root);
    QStringList wikiLinkCandidates(const QString &target) const;
    bool registerWikiLinkProvider(const QString &prefix,WikiLinkProvider provider);
    void insertWikiLink();
    bool isLiveMode() const;
    void setLiveMode(bool enabled);
    QAction *liveModeAction() const { return previewAction; }
    QList<QAction *> editingActions() const { return commands; }
    void applyBaseFontSize(int size);
    QPair<int,int> selection() const;
    void selectSource(int anchor,int position);
    void replaceSource(int start,int end,const QString &text,int anchor,int position);
    void formatSelection(const QString &left,const QString &right);
    void clipboardParagraph(bool cut);
    void indentSelection(bool outdent);
    void showFind(bool replace);
    void render();
signals:
    void fileLinkActivated(const QString &path);
    void textChanged();
    void modificationChanged(bool modified);
    void liveModeChanged(bool enabled);
};
