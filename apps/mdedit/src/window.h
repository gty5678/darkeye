#pragma once
#include "editor/markdown_editor_widget.h"
#include <QFileSystemModel>
#include <QDockWidget>

namespace mdedit { class ProjectFileTree; }

class Window : public QMainWindow {
    MarkdownEditorWidget *editingSurface=new MarkdownEditorWidget;
    QStackedWidget *screens=new QStackedWidget;
    QTimer autosave,statsTimer;
    bool loading=false;
    QString path;
    QString projectRoot;
    QFileSystemModel *fileModel=nullptr;
    mdedit::ProjectFileTree *fileTree=nullptr;
    QDockWidget *filesDock=nullptr;
    void refreshFileTree();
    void updateRecentPaths(const QString &source,const QString &destination);
    void createProjectFile(const QString &destination);
    void showDocument(const QString &file,const QString &markdown);
    bool documentOpen=false;
    QMenu *recentMenu=nullptr;
    void prepareSwitch(std::function<void()> next);
    void chooseSaveAs(std::function<void()> next={});
    void refreshRecentFiles();
    void rememberFile();
    QLabel *floatingStatus=nullptr;
    QString documentStats;
    QString statusMessage;
    QTimer statusTimer;
    void showStatusMessage(const QString &message,int timeout=0);
    void updateFloatingStatus();
    int baseFontSize=14;
    const QFont defaultUiFont=QApplication::font();
public:
    Window();
    void applyBaseFontSize(int size);
    void showSettings();
    QPair<int,int> selection() const;
    void selectSource(int anchor,int position);
    void replaceSource(int start,int end,const QString &text,int anchor,int position);
    void formatSelection(const QString &left,const QString &right);
    void clipboardParagraph(bool cut);
    void indentSelection(bool outdent);
    void showFind(bool replace);
    void updateDocumentStats();
    void updateTitle();
    void render();
    void newDocument();
    bool saveAs(const QString &target);
    bool save();
    void openFolder(const QString &folder);
    void load(const QString &file,bool standalone=false);
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
    void closeEvent(QCloseEvent *e)override;
};

