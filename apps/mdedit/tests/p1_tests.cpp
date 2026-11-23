#include "window.h"

#include <QTreeView>
bool readFile(const QString &path,QByteArray *bytes){QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;*bytes=file.readAll();return file.error()==QFileDevice::NoError;}
#define CHECK(condition) do{if(!(condition)){qCritical()<<"Failed at"<<__LINE__<<#condition;return 1;}}while(false)

int main(int argc,char **argv){
    QApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("MdEditTests");QCoreApplication::setApplicationName(QUuid::createUuid().toString(QUuid::Id128));
    const QString data=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    struct Cleanup{QString path;~Cleanup(){QDir(path).removeRecursively();}} cleanup{data};
    QTemporaryDir dir;CHECK(dir.isValid());QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    const QString text="# 第一章\n\nbody one\n\n## 子章节\n\nchild body\n\n# 第二章\n\nlast body\n";
    const QString first=dir.filePath("first.md"),second=dir.filePath("second.md");
    {
        Window window;window.newDocument();CHECK(window.windowTitle().contains("未命名"));
        auto *editor=window.findChild<MarkdownEditorWidget *>();CHECK(editor);
        window.replaceSource(0,0,text,0,0);CHECK(editor->document()->isModified());
        window.newDocument();auto *prompt=window.findChild<QMessageBox *>("unsavedDocumentDialog");CHECK(prompt);
        prompt->button(QMessageBox::Cancel)->click();CHECK(editor->markdown()==text);app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
        CHECK(window.saveAs(first));CHECK(!editor->document()->isModified());QByteArray bytes;CHECK(readFile(first,&bytes)&&bytes==text.toUtf8());
        window.replaceSource(text.size(),text.size(),"extra",0,0);CHECK(window.saveAs(second));
        CHECK(readFile(first,&bytes)&&bytes==text.toUtf8());CHECK(readFile(second,&bytes)&&bytes.endsWith("extra"));
        CHECK(QSettings().value("files/recent").toStringList()==QStringList({second,first}));
        window.newDocument();CHECK(editor->markdown().isEmpty());CHECK(window.windowTitle().contains("未命名"));
        window.replaceSource(0,0,"save before new",0,0);window.newDocument();
        prompt=window.findChild<QMessageBox *>("unsavedDocumentDialog");CHECK(prompt);prompt->button(QMessageBox::Save)->click();
        auto *picker=window.findChild<QFileDialog *>("saveAsDialog");CHECK(picker);
        const QString third=dir.filePath("third.md");picker->selectFile(third);QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);
        CHECK(readFile(third,&bytes)&&bytes=="save before new");CHECK(editor->markdown().isEmpty());
        app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
        window.replaceSource(0,0,"discard this",0,0);window.newDocument();
        prompt=window.findChild<QMessageBox *>("unsavedDocumentDialog");CHECK(prompt);prompt->button(QMessageBox::Discard)->click();CHECK(editor->markdown().isEmpty());
    }
    {
        Window window;QMenu *recent=nullptr;
        for(auto *menu:window.findChildren<QMenu *>())if(menu->title()=="最近文件")recent=menu;
        CHECK(recent);CHECK(recent->actions().first()->text()==dir.filePath("third.md"));
        for(auto *action:recent->actions())if(action->text()==second){action->trigger();break;}
        CHECK(window.windowTitle().startsWith("second.md"));
        CHECK(QFile::remove(first));
        for(auto *action:recent->actions())if(action->text()==first){action->trigger();break;}
        CHECK(!QSettings().value("files/recent").toStringList().contains(first));
    }
    {
        Window window;window.newDocument();window.replaceSource(0,0,"unsaved",0,0);
        CHECK(!window.saveAs(dir.filePath("missing/fail.md")));
        CHECK(window.findChild<MarkdownEditorWidget *>()->document()->isModified());
    }
    {
        QTemporaryDir project;CHECK(project.isValid());
        QDir root(project.path());CHECK(root.mkpath("notes/deep"));CHECK(root.mkpath("other"));
        auto write=[](const QString &name){QFile file(name);return file.open(QIODevice::WriteOnly)&&file.write("hello")==5;};
        const QString home=root.filePath("home.md"),nested=root.filePath("notes/deep/target.md");
        CHECK(write(home));CHECK(write(nested));CHECK(write(root.filePath("other/target.markdown")));
        CHECK(write(root.filePath("notes/deep/sibling.MD")));CHECK(write(root.filePath("ignored.txt")));
        Window window;window.show();
        auto *dock=window.findChild<QDockWidget *>("filesDock");CHECK(dock);
        CHECK(!dock->isVisible()&&!dock->toggleViewAction()->isEnabled());
        window.load(home);
        CHECK(dock->isVisible()&&dock->toggleViewAction()->isEnabled());
        auto *tree=window.findChild<QTreeView *>("projectFileTree");CHECK(tree);
        auto *model=qobject_cast<QFileSystemModel *>(tree->model());CHECK(model);
        CHECK(model->filePath(tree->rootIndex())==project.path());
        window.openFolder(project.path());window.load(home);
        CHECK(dock->isVisible()&&dock->toggleViewAction()->isEnabled());
        auto *editor=window.findChild<MarkdownEditorWidget *>();CHECK(editor);
        CHECK(editor->wikiLinkCandidates("target").size()==2);
        CHECK(editor->wikiLinkCandidates("notes/deep/target")==QStringList{nested});
        CHECK(editor->wikiLinkCandidates("ignored.txt").isEmpty());
        CHECK(editor->wikiLinkCandidates("../home").isEmpty());
        window.load(nested);
        CHECK(editor->wikiLinkCandidates("home")==QStringList{home});
        CHECK(model->filePath(tree->rootIndex())==project.path());
        window.load(nested,true);
        CHECK(editor->wikiLinkCandidates("home").isEmpty());
        CHECK(editor->wikiLinkCandidates("target")==QStringList{nested});
        CHECK(editor->wikiLinkCandidates("sibling")==QStringList{root.filePath("notes/deep/sibling.MD")});
        CHECK(dock->isVisible()&&dock->toggleViewAction()->isEnabled());
        CHECK(model->filePath(tree->rootIndex())==QFileInfo(nested).absolutePath());
        CHECK(model->filePath(tree->currentIndex())==nested);
        window.openFolder(project.path());CHECK(editor->markdown().isEmpty());
        CHECK(editor->wikiLinkCandidates("home")==QStringList{home});

        // New files should be selected once the filesystem model notices them.
        QElapsedTimer timer;
        const auto waitFor=[&](const auto &condition){
            timer.restart();
            while(!condition()&&timer.elapsed()<3000)app.processEvents(QEventLoop::WaitForMoreEvents,10);
            return condition();
        };
        CHECK(waitFor([&]{ return tree->width() >= 280; }));
        const auto clickWikiLink=[&](MarkdownEditorWidget *surface,const QString &label){
            auto *view=surface->findChild<QTextBrowser *>("markdownPreview");
            auto cursor=view->document()->find(label);
            if(cursor.isNull())return false;
            cursor.setPosition(cursor.selectionStart()+1);
            const QPoint point=view->cursorRect(cursor).center();
            for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
                QMouseEvent event(type,point,view->viewport()->mapToGlobal(point),Qt::LeftButton,
                                  type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view->viewport(),&event);
            }
            return true;
        };
        const auto pickerFor=[](MarkdownEditorWidget *surface)->QDialog *{
            for(auto *dialog:surface->findChildren<QDialog *>("wikiLinkPicker"))
                if (dialog->isVisible()) return dialog;
            return nullptr;
        };
        const auto selectTarget=[](QDialog *picker){
            auto *query=picker->findChild<QLineEdit *>();
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(query,&enter);
        };
        window.load(home);
        auto *insertLink=editor->findChild<QAction *>("insertWikiLinkAction");CHECK(insertLink&&insertLink->isEnabled());
        editor->setLiveMode(false);
        editor->selectSource(0,5);
        insertLink->trigger();
        auto *picker=pickerFor(editor);CHECK(picker);
        auto *results=picker->findChild<QListWidget *>();CHECK(results);
        CHECK(waitFor([&]{return results->count()==4;}));
        picker->findChild<QLineEdit *>()->setText("deep/TARGET");
        CHECK(results->count()==1&&results->item(0)->text().contains("notes/deep/target.md"));
        selectTarget(picker);
        CHECK(editor->markdown()=="[[./notes/deep/target.md|hello]]");
        editor->document()->undo();
        CHECK(editor->markdown()=="hello");
        editor->setLiveMode(true);
        editor->setMarkdown("intro\n\n[[./notes/deep/target.md|打开文档]]\n\nend");
        CHECK(clickWikiLink(editor,"打开文档"));
        CHECK(waitFor([&]{return window.windowTitle().startsWith("target.md ");}));
        editor->setMarkdown("intro\n\n[[target|选择文档]]\n\nend");
        CHECK(clickWikiLink(editor,"选择文档"));
        CHECK(waitFor([&]{return pickerFor(editor)!=nullptr;}));
        picker=pickerFor(editor);
        results=picker->findChild<QListWidget *>();
        CHECK(results->count()==2);
        picker->findChild<QLineEdit *>()->setText("other/");
        CHECK(results->count()==1);
        selectTarget(picker);
        CHECK(window.windowTitle().startsWith("target.markdown"));
        {
            MarkdownEditorWidget embedded;
            embedded.resize(640,360);
            embedded.show();
            const WikiLinkTarget issue{"issue:4821","修复登录超时","客户端","工单"};
            QString opened;
            CHECK(embedded.registerWikiLinkProvider("issue",{
                [issue](const QString &query,const WikiLinkContext &){
                    return issue.title.contains(query)?QList<WikiLinkTarget>{issue}:QList<WikiLinkTarget>{};
                },
                [issue](const QString &ref,const WikiLinkContext &){
                    return ref==issue.reference?QList<WikiLinkTarget>{issue}:QList<WikiLinkTarget>{};
                },
                [&opened](const WikiLinkTarget &target,const WikiLinkContext &){opened=target.reference;}
            }));
            embedded.setMarkdown("intro\n\n\n\nend");
            embedded.selectSource(7,7);
            embedded.insertWikiLink();
            picker=pickerFor(&embedded);CHECK(picker);
            picker->findChild<QLineEdit *>()->setText("不匹配");
            CHECK(picker->findChild<QListWidget *>()->count()==0);
            picker->findChild<QLineEdit *>()->setText("登录");
            CHECK(picker->findChild<QListWidget *>()->count()==1);
            selectTarget(picker);
            CHECK(embedded.markdown()=="intro\n\n[[issue:4821|修复登录超时]]\n\nend");
            embedded.selectSource(0,0);
            CHECK(clickWikiLink(&embedded,issue.title));
            CHECK(opened=="issue:4821");
        }
        window.openFolder(project.path());
        const auto folder=model->index(root.filePath("notes"));
        CHECK(waitFor([&]{return tree->visualRect(folder).isValid();}));
        const auto showNewFileDialog=[&](const QPoint &position){
            QTimer::singleShot(0,&window,[&]{
                auto *menu=qobject_cast<QMenu *>(app.activePopupWidget());
                if(!menu)return;
                menu->setActiveAction(menu->actions().first());
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                QApplication::sendEvent(menu,&enter);
                menu->close();
            });
            QTimer::singleShot(1000,&window,[&app]{
                if(auto *popup=app.activePopupWidget())popup->close();
            });
            tree->customContextMenuRequested(position);
            return window.findChild<QInputDialog *>("newMarkdownFileDialog");
        };
        auto *newFile=showNewFileDialog(tree->visualRect(folder).center());CHECK(newFile);
        newFile->setTextValue("计划");newFile->accept();
        const QString created=root.filePath("notes/计划.md");
        QByteArray bytes;
        CHECK(readFile(created,&bytes)&&bytes.isEmpty());
        CHECK(window.windowTitle().startsWith("计划.md")&&editor->markdown().isEmpty());
        CHECK(waitFor([&]{return model->filePath(tree->currentIndex())==created&&tree->visualRect(tree->currentIndex()).isValid();}));
        window.replaceSource(0,0,"content",0,0);
        CHECK(window.save()&&readFile(created,&bytes)&&bytes=="content");
        const auto renameEntry=[&](const QString &entry,const QString &name){
            const auto index=model->index(entry);
            tree->scrollTo(index);
            app.processEvents();
            QTimer::singleShot(0,&window,[&]{
                auto *menu=qobject_cast<QMenu *>(app.activePopupWidget());
                if(!menu)return;
                for(auto *action:menu->actions())if(action->text()=="重命名…"){
                    menu->setActiveAction(action);
                    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                    QApplication::sendEvent(menu,&enter);
                    break;
                }
                menu->close();
            });
            tree->customContextMenuRequested(tree->visualRect(index).center());
            auto *dialog=window.findChild<QInputDialog *>("renameProjectEntryDialog");
            if(!dialog)return false;
            dialog->setTextValue(name);dialog->accept();
            app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
            return true;
        };
        window.replaceSource(7,7," updated",0,0);
        CHECK(renameEntry(created,"改名.md"));
        const QString renamed=root.filePath("notes/改名.md");
        CHECK(!QFileInfo::exists(created));
        CHECK(readFile(renamed,&bytes)&&bytes=="content updated");
        CHECK(window.windowTitle().startsWith("改名.md"));
        CHECK(renameEntry(root.filePath("notes"),"新目录"));
        const QString relocated=root.filePath("新目录/改名.md");
        window.replaceSource(15,15,"!",0,0);
        CHECK(window.save());
        CHECK(readFile(relocated,&bytes)&&bytes=="content updated!");
        CHECK(!QFileInfo::exists(renamed));
        CHECK(QSettings().value("files/recent").toStringList().contains(relocated));
        QAction *closeProject=nullptr;
        for(auto *menu:window.menuBar()->findChildren<QMenu *>())
            for(auto *action:menu->actions())
                if(action->text()=="关闭项目文件夹")closeProject=action;
        CHECK(closeProject);
        closeProject->trigger();
        CHECK(!dock->isVisible()&&!dock->toggleViewAction()->isEnabled());
        CHECK(editor->markdown()=="content updated!");
    }
    return 0;
}
