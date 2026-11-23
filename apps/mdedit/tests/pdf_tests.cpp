#include <QtWidgets>
#include <QPdfWriter>
#include <QPdfView>
#include <QPdfPageNavigator>
#include "editor/live_editor.h"
#define CHECK(x) do {if(!(x)){qWarning()<<"PDF test failed"<<__LINE__<<#x;return 1;}}while(false)
int runPdfTests(QApplication &app){
    QTemporaryDir dir;CHECK(dir.isValid());
    const QString path=dir.filePath("示例 file.pdf");
    {QPdfWriter writer(path);writer.setResolution(72);QPainter p(&writer);p.fillRect(50,50,200,200,Qt::red);p.drawText(50,300,"First page");CHECK(writer.newPage());p.fillRect(50,50,200,200,Qt::blue);p.drawText(50,300,"Second page");}
    mdedit::ResourceManager manager;const auto url=QUrl::fromLocalFile(path);
    auto wait=[&](auto condition){QElapsedTimer timer;timer.start();while(!condition()&&timer.elapsed()<5000){app.processEvents();QThread::msleep(1);}return condition();};
    const QString first=manager.request(url),second=manager.request(url,1),last=manager.request(url,999);
    QFile bad(dir.filePath("bad.pdf"));CHECK(bad.open(QIODevice::WriteOnly));bad.write("not a pdf");bad.close();const auto invalid=manager.request(QUrl::fromLocalFile(bad.fileName()));
    CHECK(first!=second);CHECK(wait([&]{return manager.lookup(first)&&manager.lookup(second)&&manager.lookup(last)&&manager.lookup(invalid);}));
    CHECK(manager.lookup(first)->error.isEmpty());CHECK(manager.lookup(first)->pageCount==2);CHECK(manager.lookup(second)->page==1);CHECK(manager.lookup(last)->page==1);
    CHECK(!manager.lookup(first)->image.isNull());CHECK(manager.lookup(first)->image!=manager.lookup(second)->image);CHECK(!manager.lookup(invalid)->error.isEmpty());
    LiveEditor editor;editor.resize(900,800);editor.document()->setBaseUrl(QUrl::fromLocalFile(dir.path()+"/"));
    const QString source=QString("# PDF\n\n![Document](<示例 file.pdf>)\n\nAfter preview.\n")+QString("More text.\n\n").repeated(40);editor.setSource(source);editor.show();editor.restoreSourceSelection(0,0);
    auto ready=[&]{for(auto b=editor.document()->begin();b.isValid();b=b.next())for(auto it=b.begin();!it.atEnd();++it)if(it.fragment().charFormat().isImageFormat()&&it.fragment().charFormat().toImageFormat().name().startsWith("pdf-ready:"))return true;return false;};
    CHECK(wait(ready));CHECK(editor.markdown()==source);
    CHECK(wait([&]{return editor.findChild<QPdfView*>("pdfViewer")!=nullptr;}));
    auto *view=editor.findChild<QPdfView*>("pdfViewer");auto *page=editor.findChild<QSpinBox*>("pdfPageNumber");CHECK(page&&page->maximum()==2);
    const int bodyScroll=editor.verticalScrollBar()->value();const auto selection=editor.sourceSelection();
    const int characters=editor.document()->characterCount();const int outerMaximum=editor.verticalScrollBar()->maximum();
    CHECK(wait([&]{return view->verticalScrollBar()->maximum()>0;}));
    auto *bar=view->verticalScrollBar();bar->setSliderDown(true);bar->setSliderPosition(bar->maximum()/2);bar->setSliderDown(false);
    CHECK(bar->value()>0);CHECK(editor.verticalScrollBar()->value()==bodyScroll);
    page->setValue(2);CHECK(wait([&]{return view->pageNavigator()->currentPage()==1;}));
    page->setValue(1);CHECK(wait([&]{return view->pageNavigator()->currentPage()==0;}));
    const QPointF point(100,100);
    QWheelEvent wheel(point,view->viewport()->mapToGlobal(point.toPoint()),QPoint(0,-60),QPoint(),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
    QApplication::sendEvent(view->viewport(),&wheel);CHECK(bar->value()>0);
    CHECK(editor.markdown()==source);CHECK(editor.sourceSelection()==selection);
    CHECK(editor.document()->characterCount()==characters);CHECK(editor.verticalScrollBar()->maximum()==outerMaximum);CHECK(editor.verticalScrollBar()->value()==bodyScroll);
    // Only an event that starts at a document edge transfers to the outer view.
    editor.verticalScrollBar()->setValue(30);bar->setValue(0);
    auto sendWheel=[&](int delta){QWheelEvent e(point,view->viewport()->mapToGlobal(point.toPoint()),QPoint(0,delta),QPoint(),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);QApplication::sendEvent(view->viewport(),&e);};
    const int outerBefore=editor.verticalScrollBar()->value();sendWheel(20);CHECK(editor.verticalScrollBar()->value()<outerBefore);
    editor.verticalScrollBar()->setValue(30);bar->setValue(bar->maximum()-1);sendWheel(-60);
    CHECK(editor.verticalScrollBar()->value()==30);CHECK(bar->value()==bar->maximum());
    sendWheel(-20);CHECK(editor.verticalScrollBar()->value()>30);
    editor.verticalScrollBar()->setValue(0);
    auto followingTop=[&]{for(auto b=editor.document()->begin();b.isValid();b=b.next())if(b.text().contains("After preview"))return editor.document()->documentLayout()->blockBoundingRect(b).top();return qreal(-1);};
    const qreal before=followingTop();
    editor.restoreSourceSelection(source.indexOf("!["),source.indexOf("!["));CHECK(ready());
    CHECK(editor.editResourceSourceAt(source.indexOf("![")));CHECK(editor.toPlainText().contains("![Document]"));CHECK(qAbs(followingTop()-before)<3);
    editor.restoreSourceSelection(0,0);CHECK(wait(ready));CHECK(editor.markdown()==source);
    CHECK(!editor.grab().isNull());editor.grab().save("/tmp/mdedit-pdf-preview.png");
    editor.setSource("Before.\n\n![Missing](missing.pdf)\n\nAfter.\n");
    auto errorPreview=[&]{for(auto b=editor.document()->begin();b.isValid();b=b.next())for(auto it=b.begin();!it.atEnd();++it){const auto f=it.fragment().charFormat();if(f.isImageFormat()&&f.toImageFormat().name().startsWith("pdf-error:"))return true;}return false;};
    CHECK(wait(errorPreview));
    editor.setSource("Before.\n\n![Remote](https://example.com/test.pdf)\n\nAfter.\n");CHECK(wait(errorPreview));
    // Loading a preview while the caret is offscreen must keep the viewport
    // anchored, including the final scrollbar value emitted to the UI.
    LiveEditor scrolling;scrolling.resize(900,500);
    scrolling.document()->setBaseUrl(QUrl::fromLocalFile(dir.path()+"/"));
    scrolling.setSource(QString("Paragraph before preview.\n\n").repeated(60)+
        "![Document](<示例 file.pdf>)\n\nTail.\n");
    scrolling.show();
    scrolling.restoreSourceSelection(0,0);
    scrolling.verticalScrollBar()->setValue(scrolling.verticalScrollBar()->maximum()-100);
    const int scrollBefore=scrolling.verticalScrollBar()->value();
    const auto caretBefore=scrolling.sourceSelection();
    CHECK(wait([&]{for(auto b=scrolling.document()->begin();b.isValid();b=b.next())for(auto it=b.begin();!it.atEnd();++it)if(it.fragment().charFormat().isImageFormat()&&it.fragment().charFormat().toImageFormat().name().startsWith("pdf-ready:"))return true;return false;}));
    CHECK(qAbs(scrolling.verticalScrollBar()->value()-scrollBefore)<3);
    // Document rebuilds may clamp the bar temporarily, but never leave the
    // editor at the offscreen caret after the asynchronous publication.
    CHECK(scrolling.sourceSelection()==caretBefore);
    // Scrolling and releasing the thumb must preserve real layout geometry.
    LiveEditor lazy;lazy.resize(900,500);
    lazy.setSource(QString("A paragraph for deferred scrolling, with enough text to wrap across the editor viewport.\n\n").repeated(200));
    lazy.show();lazy.restoreSourceSelection(0,0);
    lazy.verticalScrollBar()->setSliderDown(true);
    lazy.verticalScrollBar()->setValue(lazy.verticalScrollBar()->maximum()/2);
    const int countBefore=lazy.document()->characterCount();
    QElapsedTimer dragging;dragging.start();
    while(dragging.elapsed()<250){app.processEvents();QThread::msleep(1);}
    CHECK(lazy.document()->characterCount()==countBefore);
    lazy.verticalScrollBar()->setSliderDown(false);
    const int stableMaximum=lazy.verticalScrollBar()->maximum();
    const int stableScroll=lazy.verticalScrollBar()->value();
    QElapsedTimer settled;settled.start();
    while(settled.elapsed()<300){app.processEvents();QThread::msleep(1);}
    CHECK(lazy.document()->characterCount()==countBefore);
    CHECK(lazy.verticalScrollBar()->maximum()==stableMaximum);
    CHECK(lazy.verticalScrollBar()->value()==stableScroll);
    // Wheel input must only move the viewport, including after its idle delay.
    for(int step=0;step<8;++step){
        const QPointF point(200,200);
        QWheelEvent wheel(point,lazy.viewport()->mapToGlobal(point.toPoint()),
                          QPoint(0,-17),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,
                          Qt::ScrollUpdate,false);
        QApplication::sendEvent(lazy.viewport(),&wheel);
    }
    const int wheelScroll=lazy.verticalScrollBar()->value();
    CHECK(wheelScroll>stableScroll);
    settled.restart();
    while(settled.elapsed()<300){app.processEvents();QThread::msleep(1);}
    CHECK(lazy.verticalScrollBar()->value()==wheelScroll);
    CHECK(lazy.verticalScrollBar()->maximum()==stableMaximum);
    CHECK(lazy.document()->characterCount()==countBefore);

    CHECK(lazy.sourceSelection()==qMakePair(0,0));
    return 0;
}
