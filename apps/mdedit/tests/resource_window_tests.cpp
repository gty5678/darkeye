#include <QtWidgets>
#include "editor/resources/resource_manager.h"
#include "editor/markdown_editor_widget.h"
#include "latex.h"
#include <iostream>
#define CHECK(x) do { if(!(x)){std::cerr<<"failed line "<<__LINE__<<": "<<#x<<'\n';return 1;} } while(false)
int main(int argc,char **argv){
    QApplication app(argc,argv);
    tex::LaTeX::init(formulaResourcePath().toStdString());
    // Windowed resources do not decode offscreen files, discard late results,
    // and can be requested again after eviction.
    {
        QTemporaryDir dir;CHECK(dir.isValid());
        QImage image(800,400,QImage::Format_RGB32);image.fill(Qt::red);
        const QString path=dir.filePath("window.png");CHECK(image.save(path));
        const QUrl url=QUrl::fromLocalFile(path);
        mdedit::ResourceManager manager;
        manager.setActiveUrls({});
        const auto key=manager.request(url);
        CHECK(!manager.lookup(key));
        CHECK(manager.imageSize(url)==QSize(800,400));
        manager.setActiveUrls({url});manager.request(url);
        manager.setActiveUrls({});
        auto pump=[&](int ms){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){app.processEvents();QThread::msleep(5);}};
        pump(150);CHECK(!manager.lookup(key));
        manager.setActiveUrls({url});manager.request(url);
        pump(200);CHECK(manager.lookup(key));
        manager.setActiveUrls({});CHECK(!manager.lookup(key));

        MarkdownEditorWidget window;window.resize(900,600);
        window.setBaseUrl(QUrl::fromLocalFile(dir.path()+"/"));
        const QString padding=QString("ordinary paragraph\n\n").repeated(100);
        const QString markdown="![first](window.png)\n\n"+padding+"![last](window.png)\n";
        window.setMarkdown(markdown);window.show();pump(400);
        auto *live=window.findChild<QTextBrowser *>("markdownPreview");CHECK(live);
        auto residentBytes=[&]{
            qint64 bytes=0;
            for(auto block=live->document()->begin();block.isValid();block=block.next())
                for(auto it=block.begin();!it.atEnd();++it){
                    const auto format=it.fragment().charFormat();if(!format.isImageFormat())continue;
                    const QUrl name(format.toImageFormat().name());
                    bytes+=qvariant_cast<QImage>(live->document()->resource(QTextDocument::ImageResource,name)).sizeInBytes();
                }
            return bytes;
        };
        CHECK(residentBytes()>1000);
        const auto height=live->document()->size().height();
        live->verticalScrollBar()->setValue(live->verticalScrollBar()->maximum()/2);pump(400);
        CHECK(residentBytes()<1000);
        CHECK(qAbs(live->document()->size().height()-height)<2);
        live->verticalScrollBar()->setValue(live->verticalScrollBar()->maximum());pump(400);
        CHECK(residentBytes()>1000);
        CHECK(qAbs(live->document()->size().height()-height)<2);
        live->verticalScrollBar()->setValue(0);pump(400);CHECK(residentBytes()>1000);
        CHECK(window.markdown()==markdown);
    }
    {
        MarkdownEditorWidget window;window.resize(900,600);window.show();
        const QString text=QString("# Opening\n\n")+QString("A complete paragraph with **formatting**.\n\n").repeated(400)+"Final sentinel";
        QElapsedTimer elapsed;elapsed.start();
        window.setMarkdown(text);
        const auto firstMs=elapsed.elapsed();
        auto *live=window.findChild<QTextBrowser *>("markdownPreview");CHECK(live);
        CHECK(window.markdown()==text);
        CHECK(live->toPlainText().contains("Opening"));
        CHECK(!live->toPlainText().contains("Final sentinel"));
        while(!live->toPlainText().contains("Final sentinel")&&elapsed.elapsed()<5000){
            app.processEvents();QThread::msleep(5);
        }
        CHECK(live->toPlainText().contains("Final sentinel"));
        CHECK(window.markdown()==text);
        CHECK(!window.document()->isModified());
        CHECK(live->verticalScrollBar()->value()==0);
        std::cout<<"opening "<<firstMs<<" ms, complete "<<elapsed.elapsed()<<" ms\n";
        // Switching documents must cancel any pending completion of the old file.
        window.setMarkdown(text);window.setMarkdown("Replacement");
        QElapsedTimer wait;wait.start();
        while(wait.elapsed()<150){app.processEvents();QThread::msleep(5);}
        CHECK(live->toPlainText()=="Replacement");
    }
    {
        QFile sample(QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../综合测试/综合测试.md"));
        CHECK(sample.open(QIODevice::ReadOnly));
        MarkdownEditorWidget window;window.resize(900,600);window.show();
        const QString text=QString::fromUtf8(sample.readAll())+"\n\nCompletion sentinel";
        QElapsedTimer elapsed;elapsed.start();window.setMarkdown(text);
        const auto opening=elapsed.elapsed();
        auto *live=window.findChild<QTextBrowser *>("markdownPreview");CHECK(live);
        CHECK(live->toPlainText().contains("MdEdit 综合测试"));
        CHECK(!live->toPlainText().contains("Completion sentinel"));
        while(!live->toPlainText().contains("Completion sentinel")&&elapsed.elapsed()<10000){app.processEvents();QThread::msleep(5);}
        CHECK(live->toPlainText().contains("Completion sentinel"));
        CHECK(window.markdown()==text);
        std::cout<<"comprehensive opening "<<opening<<" ms, complete "<<elapsed.elapsed()<<" ms\n";
    }
    return 0;
}
