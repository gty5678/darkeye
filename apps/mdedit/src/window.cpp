#include "window.h"

#include "editor/project_file_tree.h"
#include "editor/project_files_panel.h"
#include <QPainter>
#include <QPainterPath>

using mdedit::ProjectFileTree;
using mdedit::ProjectFilesPanel;
using mdedit::pathIsWithin;
using mdedit::relocatedPath;

namespace {
bool writeDocument(const QString &path,const QByteArray &bytes){
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}

QString applicationFolder(){
    const QString executableDirectory=QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    // Start beside the app bundle when running the packaged application.
    QDir directory(executableDirectory);
    if(directory.dirName()=="MacOS"&&directory.cdUp()&&directory.dirName()=="Contents"&&
       directory.cdUp()&&directory.dirName().endsWith(".app")&&directory.cdUp())
        return directory.absolutePath();
#endif
    return executableDirectory;
}

QIcon projectFolderIcon()
{
    QPainterPath outline;
    outline.moveTo(3, 16);
    outline.lineTo(3, 5);
    outline.quadTo(3, 4, 4, 4);
    outline.lineTo(8, 4);
    outline.lineTo(10, 6);
    outline.lineTo(16, 6);
    outline.quadTo(17, 6, 17, 7);
    outline.lineTo(17, 8.5);
    outline.moveTo(3, 16);
    outline.lineTo(5.2, 9.5);
    outline.quadTo(5.5, 8.5, 6.5, 8.5);
    outline.lineTo(18.5, 8.5);
    outline.lineTo(16.5, 15.5);
    outline.quadTo(16.2, 16.5, 15.2, 16.5);
    outline.lineTo(4, 16.5);
    outline.quadTo(3, 16.5, 3, 16);

    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pixmap(20 * scale, 20 * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(outline);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}
}


Window::Window(){
        resize(980,780);updateTitle();
        connect(editingSurface,&MarkdownEditorWidget::fileLinkActivated,this,[this](const QString &file){
            if(!QFileInfo(file).isFile()){
                showStatusMessage("链接文件不存在："+file,5000);return;
            }
            load(file);
        });
        auto *welcome=new QWidget;
        auto *layout=new QVBoxLayout(welcome);
        auto *actions=new QWidget(welcome);
        auto *actionsLayout=new QVBoxLayout(actions);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->setSpacing(12);
        layout->addStretch();
        layout->addWidget(actions,0,Qt::AlignHCenter);
        layout->addStretch();

        auto *openFolderButton=new QPushButton("打开项目文件夹…");
        openFolderButton->setObjectName("openFolderButton");
        openFolderButton->setIcon(projectFolderIcon());
        openFolderButton->setIconSize(QSize(20, 20));
        openFolderButton->setMinimumWidth(220);
        openFolderButton->setMinimumHeight(52);
        openFolderButton->setCursor(Qt::PointingHandCursor);
        openFolderButton->setStyleSheet(R"(
            QPushButton#openFolderButton {
                background: #684396;
                color: #ffffff;
                border: 2px solid transparent;
                border-radius: 14px;
                padding: 6px 18px;
                font-weight: 600;
            }
            QPushButton#openFolderButton:hover { background: #583481; }
            QPushButton#openFolderButton:pressed { background: #482775; }
            QPushButton#openFolderButton:focus { border-color: #cbb1eb; }
        )");
        actionsLayout->addWidget(openFolderButton);

        auto *open=new QPushButton("打开 Markdown 文件…");
        open->setObjectName("openDocumentButton");
        open->setMinimumWidth(220);
        open->setMinimumHeight(48);
        open->setCursor(Qt::PointingHandCursor);
        open->setStyleSheet(R"(
            QPushButton#openDocumentButton {
                background: #f2ebfb;
                color: #482775;
                border: 1px solid #e2d4f3;
                border-radius: 14px;
                padding: 5px 18px;
                font-weight: 600;
            }
            QPushButton#openDocumentButton:hover { background: #eae0f7; }
            QPushButton#openDocumentButton:pressed { background: #dfd0f1; }
            QPushButton#openDocumentButton:focus { border-color: #9877c2; }
        )");
        actionsLayout->addWidget(open);
        auto *openShortcut=new QShortcut(QKeySequence::Open,welcome);
        openShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(openShortcut,&QShortcut::activated,open,&QPushButton::click);
        screens->addWidget(welcome);screens->addWidget(editingSurface);setCentralWidget(screens);
        floatingStatus=new QLabel(screens);
        floatingStatus->setObjectName("floatingStatus");
        floatingStatus->setTextFormat(Qt::PlainText);
        floatingStatus->setAttribute(Qt::WA_TransparentForMouseEvents);
        floatingStatus->setAccessibleDescription("统计 Markdown 源码的字符数和词数，并显示当前字号与操作提示。");
        floatingStatus->setStyleSheet(R"(
            QLabel#floatingStatus {
                background: #f8f4fc; color: #59496a;
                border: 1px solid #e4daee; border-radius: 13px;
                padding: 5px 11px; font-size: 12px;
            }
        )");
        floatingStatus->hide();
        screens->installEventFilter(this);
        statusTimer.setSingleShot(true);
        connect(&statusTimer,&QTimer::timeout,this,[this]{showStatusMessage({});});
        setStyleSheet(R"(QMainWindow,QWidget{background:#ffffff;color:#26232d;}QToolBar{background:#ffffff;border:0;border-bottom:1px solid #dedbe4;padding:8px;spacing:8px;}QToolButton{padding:6px 16px;border:0;border-radius:5px;}QToolButton:hover,QToolButton:checked{background:#eae2f7;color:#482775;}QPlainTextEdit,QTextBrowser{border:0;selection-background-color:#e3d7f5;selection-color:#26232d;}QScrollBar:vertical{background:#ffffff;width:10px;}QScrollBar::handle:vertical{background:#a9a2b2;min-height:28px;border-radius:4px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}QToolTip{color:#26232d;background:#f2eef7;})");
        autosave.setSingleShot(true);autosave.setInterval(600);
        statsTimer.setSingleShot(true);statsTimer.setInterval(250);
        connect(&statsTimer,&QTimer::timeout,this,[this]{updateDocumentStats();});
        connect(editingSurface,&MarkdownEditorWidget::textChanged,this,[this]{statsTimer.start();if(!loading&&documentOpen){
            if(!path.isEmpty())autosave.start();
        }});
        connect(editingSurface,&MarkdownEditorWidget::modificationChanged,this,[this]{updateTitle();});
        connect(&autosave,&QTimer::timeout,this,[this]{save();});
        connect(open,&QPushButton::clicked,this,[this]{
            auto *dialog=new QFileDialog(this,"打开 Markdown 文件",path,"Markdown (*.md *.markdown *.txt);;所有文件 (*)");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
#ifdef Q_OS_MACOS
            // Avoid Cocoa's nested native-panel modal loop (also under the debugger).
            dialog->setOption(QFileDialog::DontUseNativeDialog);
#endif
            dialog->setFileMode(QFileDialog::ExistingFile);
            connect(dialog,&QFileDialog::fileSelected,this,[this](const QString &file){load(file,true);});
            dialog->open();
        });
        auto chooseFolder=[this]{
            auto *dialog=new QFileDialog(this,"打开项目文件夹",applicationFolder());
            dialog->setObjectName("projectFolderDialog");
            dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setFileMode(QFileDialog::Directory);
            dialog->setOption(QFileDialog::ShowDirsOnly);
#ifdef Q_OS_MACOS
            dialog->setOption(QFileDialog::DontUseNativeDialog);
#endif
            connect(dialog,&QFileDialog::fileSelected,this,[this](const QString &folder){openFolder(folder);});dialog->open();
        };
        connect(openFolderButton,&QPushButton::clicked,this,chooseFolder);
        filesDock=new QDockWidget("文件",this);filesDock->setObjectName("filesDock");
        filesDock->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);
        filesDock->setStyleSheet(R"(
            QDockWidget#filesDock::title {
                background: #f1edf5; color: #51415f;
                text-align: left; padding: 7px 10px;
            }
            QWidget#projectFilesPanel, QWidget#projectFileActions { background: #faf8fc; }
        )");
        auto *filesPanel = new ProjectFilesPanel(filesDock);
        fileTree = filesPanel->tree();
        fileModel = fileTree->fileModel();
        connect(fileTree, &ProjectFileTree::documentActivated, this, [this](const QString &file) {
            load(file);
        });
        connect(filesPanel, &ProjectFilesPanel::createFileRequested, this, &Window::createProjectFile);
        filesPanel->setBeforeEntryChange([this](const QString &entry) {
            return !pathIsWithin(path, entry) || save();
        });
        connect(filesPanel, &ProjectFilesPanel::entryChanged, this,
                [this](const QString &source, const QString &destination) {
            updateRecentPaths(source, destination);
            if (!pathIsWithin(path, source)) return;
            if (destination.isEmpty()) {
                openFolder(projectRoot);
                return;
            }
            path = relocatedPath(path, source, destination);
            autosave.stop();
            editingSurface->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath() + "/"));
            updateTitle();
            refreshFileTree();
        });
        connect(fileModel,&QFileSystemModel::directoryLoaded,this,[this]{
            const auto index=fileModel->index(path);if(index.isValid())fileTree->setCurrentIndex(index);
        });
        connect(fileModel,&QFileSystemModel::rowsInserted,this,[this](const QModelIndex &parent,int first,int last){
            if(path.isEmpty()||fileModel->filePath(parent)!=QFileInfo(path).absolutePath())return;
            for(int row=first;row<=last;++row){
                const auto index=fileModel->index(row,0,parent);
                if(fileModel->filePath(index)!=path)continue;
                if(parent!=fileTree->rootIndex())fileTree->expand(parent);
                fileTree->setCurrentIndex(index);
                fileTree->scrollTo(index);
                break;
            }
        });
        filesDock->setWidget(filesPanel);addDockWidget(Qt::LeftDockWidgetArea,filesDock);
        refreshFileTree();
        auto *fileMenu=menuBar()->addMenu("文件");
        auto *folderAction=fileMenu->addAction("打开文件夹…");folderAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
        connect(folderAction,&QAction::triggered,this,chooseFolder);
        auto *closeFolder=fileMenu->addAction("关闭项目文件夹");
        connect(closeFolder,&QAction::triggered,this,[this]{projectRoot.clear();editingSurface->setProjectRoot({});refreshFileTree();});
        auto *newAction=fileMenu->addAction("新建");newAction->setShortcut(QKeySequence::New);
        connect(newAction,&QAction::triggered,this,[this]{newDocument();});
        auto *openAction=fileMenu->addAction("打开…");openAction->setShortcut(QKeySequence::Open);
        connect(openAction,&QAction::triggered,open,&QPushButton::click);
        // The window action also works after leaving the welcome screen.
        openShortcut->setEnabled(false);
        auto *saveAction=fileMenu->addAction("保存");saveAction->setShortcut(QKeySequence::Save);
        connect(saveAction,&QAction::triggered,this,[this]{save();});
        auto *saveAsAction=fileMenu->addAction("另存为…");saveAsAction->setShortcut(QKeySequence::SaveAs);
        connect(saveAsAction,&QAction::triggered,this,[this]{chooseSaveAs();});
        recentMenu=fileMenu->addMenu("最近文件");refreshRecentFiles();
        auto *editMenu=menuBar()->addMenu("编辑");
        for(auto *action:editingSurface->editingActions()){
            editMenu->addAction(action);
            if(action->objectName()=="insertWikiLinkAction"){
                const auto update=[this,action]{action->setEnabled(screens->currentWidget()==editingSurface);};
                connect(screens,&QStackedWidget::currentChanged,action,update);
                update();
            }
        }
        auto *viewMenu=menuBar()->addMenu("视图");
        viewMenu->addAction(editingSurface->liveModeAction());
        viewMenu->addAction(filesDock->toggleViewAction());
        auto *settingsAction=new QAction("设置",this);
        settingsAction->setObjectName("settingsAction");settingsAction->setShortcut(QKeySequence("Ctrl+,"));
        settingsAction->setMenuRole(QAction::PreferencesRole);
        fileMenu->addSeparator();fileMenu->addAction(settingsAction);
        connect(settingsAction,&QAction::triggered,this,[this]{showSettings();});
        applyBaseFontSize(QSettings().value("appearance/baseFontSize",14).toInt());
        editingSurface->document()->setModified(false);
        render();open->setFocus();
    }

void Window::applyBaseFontSize(int size){
        baseFontSize=qBound(9,size,30);
        QFont ui=defaultUiFont;
        ui.setPointSizeF(qMax(9.0,(ui.pointSizeF()>0?ui.pointSizeF():13)+baseFontSize-13));
        setFont(ui);menuBar()->setFont(ui);
        editingSurface->applyBaseFontSize(baseFontSize);
        updateFloatingStatus();
    }

void Window::showSettings(){
        if(auto *existing=findChild<QDialog *>("settingsDialog")){existing->show();existing->raise();existing->activateWindow();return;}
        auto *dialog=new QDialog(this);dialog->setObjectName("settingsDialog");
        dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("设置");
        auto *layout=new QFormLayout(dialog);
        auto *size=new QSpinBox;size->setObjectName("baseFontSize");size->setRange(9,30);size->setSuffix(" pt");size->setValue(baseFontSize);
        layout->addRow("基础字号",size);
        auto *hint=new QLabel("调整正文、源码和界面字号；标题按比例缩放。修改即时生效并自动保存。");
        hint->setWordWrap(true);layout->addRow(hint);
        connect(size,&QSpinBox::valueChanged,this,[this](int value){
            applyBaseFontSize(value);QSettings settings;settings.setValue("appearance/baseFontSize",baseFontSize);settings.sync();
            if(settings.status()!=QSettings::NoError)showStatusMessage("字号已生效，但设置保存失败");
        });
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::RestoreDefaults|QDialogButtonBox::Close);layout->addRow(buttons);
        connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,dialog,[size]{size->setValue(14);});
        connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
        dialog->open();size->setFocus();
    }

QPair<int,int> Window::selection() const{return editingSurface->selection();}
void Window::selectSource(int a,int b){editingSurface->selectSource(a,b);}
void Window::replaceSource(int a,int b,const QString &text,int anchor,int position){editingSurface->replaceSource(a,b,text,anchor,position);}
void Window::formatSelection(const QString &left,const QString &right){editingSurface->formatSelection(left,right);}
void Window::clipboardParagraph(bool cut){editingSurface->clipboardParagraph(cut);}
void Window::indentSelection(bool outdent){editingSurface->indentSelection(outdent);}
void Window::showFind(bool replace){editingSurface->showFind(replace);}

void Window::updateDocumentStats(){
        const QString text=editingSurface->markdown();
        qsizetype characters=0,words=0;
        QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme,text);
        while(graphemes.toNextBoundary()!=-1)++characters;
        QTextBoundaryFinder boundaries(QTextBoundaryFinder::Word,text);
        do{
            if(boundaries.boundaryReasons()&QTextBoundaryFinder::StartOfItem)++words;
        }while(boundaries.toNextBoundary()!=-1);
        documentStats=QString("%1 字符  ·  %2 词").arg(characters).arg(words);
        updateFloatingStatus();
    }

void Window::showStatusMessage(const QString &message,int timeout){
    statusTimer.stop();
    statusMessage=message;
    if(timeout>0)statusTimer.start(timeout);
    updateFloatingStatus();
}

void Window::updateFloatingStatus(){
    const QString separator="  ·  ";
    QString summary;
    if(documentOpen)
        summary=documentStats+separator+QString("字号 %1 pt").arg(baseFontSize);
    if(statusMessage.isEmpty()&&summary.isEmpty()){
        floatingStatus->hide();
        return;
    }

    constexpr int rightInset=18,bottomInset=12,leftInset=12,horizontalChrome=24;
    const int availableWidth=qMax(0,screens->width()-rightInset-leftInset-horizontalChrome);
    floatingStatus->ensurePolished();
    const QFontMetrics metrics(floatingStatus->font());
    QString display;
    if(statusMessage.isEmpty()||summary.isEmpty()){
        display=metrics.elidedText(statusMessage+summary,Qt::ElideRight,availableWidth);
    }else{
        const int messageWidth=availableWidth-metrics.horizontalAdvance(separator+summary);
        display=metrics.elidedText(statusMessage,Qt::ElideRight,availableWidth);
        if(messageWidth>=metrics.horizontalAdvance("……")){
            display=metrics.elidedText(statusMessage,Qt::ElideRight,messageWidth)+separator+summary;
        }
    }
    floatingStatus->setText(display);
    floatingStatus->setAccessibleName(statusMessage.isEmpty()||summary.isEmpty()
        ?statusMessage+summary:statusMessage+separator+summary);
    floatingStatus->resize(floatingStatus->sizeHint());
    floatingStatus->move(screens->width()-rightInset-floatingStatus->width(),
                         screens->height()-bottomInset-floatingStatus->height());
    floatingStatus->setVisible(!display.isEmpty());
    floatingStatus->raise();
}

bool Window::eventFilter(QObject *watched,QEvent *event){
    if(watched==screens&&event->type()==QEvent::Resize)updateFloatingStatus();
    return QMainWindow::eventFilter(watched,event);
}

void Window::updateTitle(){
        const bool modified=editingSurface->document()->isModified();
        if(path.isEmpty()){setWindowTitle(documentOpen?QString("未命名")+(modified?" *":"")+" — MdEdit":"MdEdit");return;}
        const QString name=QFileInfo(path).fileName();
        setWindowTitle(name+(modified?" *":"")+" — MdEdit");
    }

void Window::render(){editingSurface->render();}

bool Window::save(){
        if(path.isEmpty()){
            if(documentOpen){chooseSaveAs();return false;}
            return true;
        }
        if(!editingSurface->document()->isModified())return true;
        const auto bytes=editingSurface->markdown().toUtf8();
        if(!writeDocument(path,bytes)){
            showStatusMessage("自动保存失败，请检查文件权限和磁盘空间");return false;
        }
        editingSurface->document()->setModified(false);
        showStatusMessage({});
        return true;
    }

void Window::load(const QString &file,bool standalone){
        if(QFileInfo(file).isDir()){openFolder(file);return;}
        prepareSwitch([this,file,standalone]{
        QFile f(file);
        if(!f.open(QIODevice::ReadOnly)){QMessageBox::warning(this,"打开失败",f.errorString());return;}
        const auto bytes=f.readAll();
        if(f.error()!=QFileDevice::NoError){QMessageBox::warning(this,"打开失败",f.errorString());return;}
        if(standalone||projectRoot.isEmpty())projectRoot=QFileInfo(file).absolutePath();
        showDocument(file,QString::fromUtf8(bytes));
        });
    }

void Window::showDocument(const QString &file,const QString &markdown){
    editingSurface->setProjectRoot(projectRoot);
    loading=true;documentOpen=true;
    path=QFileInfo(file).absoluteFilePath();
    editingSurface->setMarkdown(markdown,QUrl::fromLocalFile(QFileInfo(path).absolutePath()+"/"));
    editingSurface->document()->setModified(false);
    autosave.stop();
    screens->setCurrentWidget(editingSurface);
    updateDocumentStats();
    showStatusMessage({});
    editingSurface->setFocus();
    updateTitle();loading=false;rememberFile();refreshFileTree();
}

void Window::updateRecentPaths(const QString &source,const QString &destination){
    QSettings settings;
    QStringList recent;
    for(const auto &file:settings.value("files/recent").toStringList()){
        if(!pathIsWithin(file,source))recent.append(file);
        else if(!destination.isEmpty())recent.append(relocatedPath(file,source,destination));
    }
    recent.removeDuplicates();
    settings.setValue("files/recent",recent);
    refreshRecentFiles();
}

void Window::createProjectFile(const QString &destination)
{
    const bool modifiedUntitled = path.isEmpty() && editingSurface->document()->isModified();
    prepareSwitch([this, destination, modifiedUntitled] {
        QFile file(destination);
        if (!pathIsWithin(QFileInfo(destination).dir().canonicalPath(),
                          QFileInfo(projectRoot).canonicalFilePath())
            || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (modifiedUntitled && path.isEmpty()) editingSurface->document()->setModified(true);
            QMessageBox::warning(this, "无法新建文件",
                                 file.error() == QFileDevice::NoError ? "目标必须位于当前项目。" : file.errorString());
            return;
        }
        file.close();
        showDocument(destination, {});
    });
}

void Window::closeEvent(QCloseEvent *event){
    if(path.isEmpty()&&documentOpen&&editingSurface->document()->isModified()){
        event->ignore();prepareSwitch([this]{close();});return;
    }
    if(path.isEmpty()||save()){event->accept();return;}
    event->ignore();
    QMessageBox::warning(this,"无法关闭文档",statusMessage);
}

void Window::prepareSwitch(std::function<void()> next){
    if(!editingSurface->document()->isModified()){next();return;}
    if(!path.isEmpty()){if(save())next();return;}
    auto *dialog=new QMessageBox(QMessageBox::Question,"保存未命名文档？","当前文档有未保存的修改。",
        QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,this);
    dialog->setObjectName("unsavedDocumentDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setDefaultButton(QMessageBox::Save);
    connect(dialog,&QMessageBox::finished,this,[this,next](int result){
        if(result==QMessageBox::Save)chooseSaveAs(next);
        else if(result==QMessageBox::Discard){
            editingSurface->document()->setModified(false);next();
        }
    });dialog->open();
}

void Window::newDocument(){
    prepareSwitch([this]{
        autosave.stop();loading=true;path.clear();documentOpen=true;
        editingSurface->setMarkdown({},projectRoot.isEmpty()?QUrl():QUrl::fromLocalFile(projectRoot+"/"));loading=false;
        screens->setCurrentWidget(editingSurface);
        updateDocumentStats();showStatusMessage({});updateTitle();editingSurface->setFocus();
    });
}

void Window::chooseSaveAs(std::function<void()> next){
    if(!documentOpen)return;
    if(auto *existing=findChild<QFileDialog *>("saveAsDialog")){existing->raise();return;}
    auto *dialog=new QFileDialog(this,"另存为",path.isEmpty()?QDir::homePath()+"/未命名.md":path,"Markdown (*.md *.markdown);;所有文件 (*)");
    dialog->setObjectName("saveAsDialog");dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAcceptMode(QFileDialog::AcceptSave);dialog->setDefaultSuffix("md");
#ifdef Q_OS_MACOS
    dialog->setOption(QFileDialog::DontUseNativeDialog);
#endif
    connect(dialog,&QFileDialog::fileSelected,this,[this,next](const QString &file){if(saveAs(file)&&next)next();});
    dialog->open();
}

bool Window::saveAs(const QString &target){
    if(!documentOpen||target.isEmpty())return false;
    const QString destination=QFileInfo(target).absoluteFilePath();
    if(destination==path||(!QFileInfo(target).canonicalFilePath().isEmpty()&&QFileInfo(target).canonicalFilePath()==QFileInfo(path).canonicalFilePath()))return save();
    const auto bytes=editingSurface->markdown().toUtf8();
    if(!writeDocument(destination,bytes)){showStatusMessage("另存为失败，请检查文件权限和磁盘空间");return false;}
    path=destination;autosave.stop();
    editingSurface->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()+"/"));
    editingSurface->document()->setModified(false);rememberFile();updateTitle();refreshFileTree();
    showStatusMessage("已保存："+path,5000);return true;
}

void Window::rememberFile(){
    if(path.isEmpty())return;
    QSettings settings;auto recent=settings.value("files/recent").toStringList();
    recent.removeAll(path);recent.prepend(path);while(recent.size()>10)recent.removeLast();
    settings.setValue("files/recent",recent);refreshRecentFiles();
}
void Window::refreshRecentFiles(){
    recentMenu->clear();const auto recent=QSettings().value("files/recent").toStringList();
    for(const auto &file:recent){
        auto *action=recentMenu->addAction(file);action->setToolTip(file);
        connect(action,&QAction::triggered,this,[this,file]{
            if(!QFile::exists(file)){
                auto entries=QSettings().value("files/recent").toStringList();entries.removeAll(file);QSettings().setValue("files/recent",entries);
                refreshRecentFiles();showStatusMessage("文件不存在，已从最近文件中移除："+file);return;
            }
            load(file);
        });
    }
    if(recent.isEmpty()){auto *empty=recentMenu->addAction("暂无最近文件");empty->setEnabled(false);}
    else{
        recentMenu->addSeparator();auto *clear=recentMenu->addAction("清空最近文件");
        connect(clear,&QAction::triggered,this,[this]{QSettings().remove("files/recent");refreshRecentFiles();});
    }
}

void Window::openFolder(const QString &folder){
    const QFileInfo info(folder);if(!info.isDir())return;
    prepareSwitch([this,folder]{
        autosave.stop();projectRoot=QFileInfo(folder).absoluteFilePath();path.clear();documentOpen=false;loading=true;
        editingSurface->setProjectRoot(projectRoot);
        editingSurface->setMarkdown({},QUrl::fromLocalFile(projectRoot+"/"));
        editingSurface->document()->setModified(false);loading=false;
        screens->setCurrentIndex(0);updateTitle();refreshFileTree();
        documentStats.clear();showStatusMessage("已打开项目："+projectRoot,5000);
    });
}
void Window::refreshFileTree(){
    filesDock->toggleViewAction()->setEnabled(!projectRoot.isEmpty());
    if(projectRoot.isEmpty()){
        filesDock->hide();
        fileTree->clearSelection();
        fileTree->setDirectory({});
        filesDock->setWindowTitle("文件");
        return;
    }
    fileTree->setDirectory(projectRoot);
    filesDock->setWindowTitle("项目 · "+QDir(projectRoot).dirName());filesDock->show();
    fileTree->selectFile(path);
}

