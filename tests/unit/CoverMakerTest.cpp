#include "cover_maker/CoverEditor.h"
#include "cover_maker/UpscaleClient.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QBuffer>
#include <QTimer>
#include <QToolButton>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QWheelEvent>
#include <QUndoStack>
#include <QTreeWidget>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QStackedWidget>
#include "cover_maker/SleeveEditor.h"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace darkeye::cover_maker;
class CoverMakerTest : public QObject {
    Q_OBJECT
private slots:
    void fitOriginalToCoverHalves() {
        CoverEditor editor;CoverDocument d;d.outputSize={1000,600};d.editableImageLayers=true;
        QImage source(200,400,QImage::Format_RGB32);source.fill(Qt::red);
        auto layer=makeImageLayer(source,d.outputSize);layer.crop={.2,.2,.5,.5};layer.z=7;
        d.imageLayers.append(layer);editor.setDocument(d);
        const auto baseline=editor.document();
        auto *left=editor.findChild<QPushButton *>(QStringLiteral("fitImageLeft"));
        auto *right=editor.findChild<QPushButton *>(QStringLiteral("fitImageRight"));
        QVERIFY(left);QVERIFY(right);
        left->click();
        QCOMPARE(editor.document().imageLayers[0].frame,QRectF(0,-1./3,.5,5./3));
        QCOMPARE(editor.document().imageLayers[0].crop,QRectF(0,0,1,1));
        QCOMPARE(editor.document().imageLayers[0].z,baseline.imageLayers[0].z);
        editor.undoStack()->undo();QVERIFY(editor.document()==baseline);
        editor.setGuideLines({.4,.6});
        right->click();
        const auto fitted=editor.document().imageLayers[0];
        QCOMPARE(fitted.frame.x(),.6);QCOMPARE(fitted.frame.width(),.4);
        QCOMPARE(fitted.frame.center().y(),.5);
        const auto pixels=imageLayerRect(fitted,d.outputSize);
        QVERIFY(qAbs(pixels.height()/pixels.width()-2)<1e-9);
        left->click();QCOMPARE(editor.document().imageLayers[0].frame.width(),.4);
    }
    void wheelZoomsViewWithoutEditingImage() {
        CoverEditor editor;CoverDocument d;d.outputSize={600,900};d.editableImageLayers=true;
        QImage source(200,100,QImage::Format_RGB32);source.fill(Qt::red);
        d.imageLayers.append(makeImageLayer(source,d.outputSize));
        editor.setDocument(d);editor.resize(1000,900);editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto *canvas=editor.findChild<QWidget *>(QStringLiteral("coverCanvas"));QVERIFY(canvas);
        const QPoint center=canvas->rect().center();
        QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,center);
        const auto before=editor.document();const auto preview=canvas->grab().toImage();
        const int history=editor.undoStack()->count();
        QWheelEvent wheel(center,canvas->mapToGlobal(center),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(canvas,&wheel);
        QVERIFY(wheel.isAccepted());QVERIFY(editor.document()==before);
        QCOMPARE(editor.undoStack()->count(),history);
        QVERIFY(canvas->grab().toImage()!=preview);
        const auto zoomed=canvas->grab().toImage();
        const QPoint moved=center+QPoint(45,30);
        QTest::mousePress(canvas,Qt::MiddleButton,Qt::NoModifier,center);
        QCOMPARE(canvas->cursor().shape(),Qt::ClosedHandCursor);
        QTest::mouseMove(canvas,moved);
        QTest::mouseRelease(canvas,Qt::MiddleButton,Qt::NoModifier,moved);
        QVERIFY(editor.document()==before);
        QCOMPARE(editor.undoStack()->count(),history);
        const auto panned=canvas->grab().toImage();
        QVERIFY(panned!=zoomed);
        QCOMPARE(panned.pixelColor(moved),zoomed.pixelColor(center));
        QTest::mousePress(canvas,Qt::MiddleButton,Qt::NoModifier,moved);
        QTest::mouseMove(canvas,center);
        QTest::mouseRelease(canvas,Qt::MiddleButton,Qt::NoModifier,center);
        QCOMPARE(canvas->grab().toImage(),zoomed);
    }
    void upscaleLiveService() {
        const auto url=qEnvironmentVariable("DARKEYE_SR_TEST_URL");
        if(url.isEmpty())QSKIP("Set DARKEYE_SR_TEST_URL to test a running SRkit instance");
        UpscaleClient client;QSignalSpy done(&client,&UpscaleClient::completed),failed(&client,&UpscaleClient::failed);
        QImage image(16,12,QImage::Format_ARGB32);image.fill(QColor(100,70,40,97));
        client.start(QUrl(url),qEnvironmentVariable("DARKEYE_SR_TEST_TOKEN"),image,2);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),240000);
        QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed[0][0].toString()));
        QCOMPARE(done.count(),1);const auto result=qvariant_cast<QImage>(done[0][0]);
        QCOMPARE(result.size(),QSize(32,24));QCOMPARE(qAlpha(result.pixel(0,0)),97);
    }
    void upscaleHttpContract_data() {
        QTest::addColumn<int>("status");QTest::addColumn<bool>("wrongSize");
        QTest::newRow("png")<<200<<false;QTest::newRow("bad-size")<<200<<true;QTest::newRow("service-error")<<503<<false;
    }
    void upscaleHttpContract() {
        QFETCH(int,status);QFETCH(bool,wrongSize);
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));QByteArray request;
        QImage result(wrongSize?QSize(3,3):QSize(24,16),QImage::Format_ARGB32);result.fill(Qt::green);
        QByteArray body;QBuffer buffer(&body);buffer.open(QIODevice::WriteOnly);QVERIFY(result.save(&buffer,"PNG"));
        if(status!=200)body=R"({"error":{"code":503,"message":"Engine unavailable"}})";
        connect(&server,&QTcpServer::newConnection,this,[&]{
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,this,[&,socket]{
                request+=socket->readAll();const auto end=request.indexOf("\r\n\r\n");if(end<0)return;
                qsizetype length=-1;for(auto header:request.left(end).split('\n'))if(header.toLower().startsWith("content-length:"))length=header.mid(15).trimmed().toLongLong();
                if(length<0 || request.size()<end+4+length)return;
                socket->write("HTTP/1.1 "+QByteArray::number(status)+" Result\r\nContent-Type: "+(status==200?QByteArray("image/png"):QByteArray("application/json"))+"\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
        });
        UpscaleClient client;QSignalSpy done(&client,&UpscaleClient::completed),failed(&client,&UpscaleClient::failed);
        QImage image(12,8,QImage::Format_ARGB32);image.fill(Qt::red);
        client.start(QUrl(QStringLiteral("http://127.0.0.1:%1/v1/upscale").arg(server.serverPort())),QStringLiteral("test-token"),image,2);
        QVERIFY(client.busy());QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),5000);
        QVERIFY(request.startsWith("POST /v1/upscale "));QVERIFY(request.contains("Authorization: Bearer test-token"));
        QVERIFY(request.contains("name=\"image\""));QVERIFY(request.contains("name=\"scale\""));QVERIFY(request.contains("\r\n\r\n2\r\n"));
        if(status==200 && !wrongSize){QCOMPARE(done.count(),1);QCOMPARE(failed.count(),0);QCOMPARE(qvariant_cast<QImage>(done[0][0]),result);}
        else{QCOMPARE(done.count(),0);QCOMPARE(failed.count(),1);if(status==503)QCOMPARE(failed[0][0].toString(),QStringLiteral("Engine unavailable"));}
    }
    void upscaleEditorPreservesLayoutAndUndo() {
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));QByteArray request;
        QImage enhanced(24,16,QImage::Format_ARGB32);enhanced.fill(Qt::green);QByteArray png;QBuffer output(&png);output.open(QIODevice::WriteOnly);QVERIFY(enhanced.save(&output,"PNG"));
        connect(&server,&QTcpServer::newConnection,this,[&]{
            auto *socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,this,[&,socket]{
                request+=socket->readAll();const auto end=request.indexOf("\r\n\r\n");if(end<0)return;
                qsizetype length=-1;for(auto header:request.left(end).split('\n'))if(header.toLower().startsWith("content-length:"))length=header.mid(15).trimmed().toLongLong();
                if(length<0 || request.size()<end+4+length)return;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "+QByteArray::number(png.size())+"\r\nConnection: close\r\n\r\n"+png);socket->disconnectFromHost();
            });
        });
        const auto previous=qgetenv("DARKEYE_SR_URL");qputenv("DARKEYE_SR_URL",QStringLiteral("http://127.0.0.1:%1/v1/upscale").arg(server.serverPort()).toUtf8());
        CoverEditor editor;if(previous.isNull())qunsetenv("DARKEYE_SR_URL");else qputenv("DARKEYE_SR_URL",previous);
        CoverDocument document;document.editableImageLayers=true;ImageLayer layer;layer.image=QImage(12,8,QImage::Format_ARGB32);layer.image.fill(Qt::red);layer.frame={.2,.3,.4,.5};layer.crop={.1,.2,.7,.6};document.imageLayers.append(layer);editor.setDocument(document);const auto baseline=editor.document();
        QTimer::singleShot(0,[]{if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->accept();});
        editor.findChild<QPushButton *>(QStringLiteral("upscaleImage"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(editor.document().imageLayers[0].image.size(),QSize(24,16),5000);
        QCOMPARE(editor.document().imageLayers[0].frame,layer.frame);QCOMPARE(editor.document().imageLayers[0].crop,layer.crop);
        QCOMPARE(editor.undoStack()->count(),1);editor.undoStack()->undo();QCOMPARE(editor.document(),baseline);
        editor.undoStack()->redo();QCOMPARE(editor.document().imageLayers[0].image,enhanced);
    }
    void upscaleCancellationAndEndpoint() {
        QVERIFY(UpscaleClient::validEndpoint(QUrl("https://example.com/v1/upscale")));
        QVERIFY(!UpscaleClient::validEndpoint(QUrl("http://example.com/v1/upscale")));
        QVERIFY(!UpscaleClient::validEndpoint(QUrl("https://user:pass@example.com/v1/upscale")));
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        UpscaleClient client;QSignalSpy done(&client,&UpscaleClient::completed),failed(&client,&UpscaleClient::failed);
        QImage image(10,10,QImage::Format_RGB32);image.fill(Qt::white);
        client.start(QUrl(QStringLiteral("http://127.0.0.1:%1/v1/upscale").arg(server.serverPort())),{},image,2);
        QVERIFY(client.busy());client.cancel();QTRY_VERIFY(!client.busy());QCOMPARE(done.count(),0);QCOMPARE(failed.count(),0);
    }
    void rotationAcceptsTypedFractionalAngles() {
        CoverEditor editor;CoverDocument d;d.outputSize={600,900};d.editableTextLayers=true;TextLayer text;text.text=QStringLiteral("旋转测试");text.fontSize=60;d.textLayers.append(text);
        editor.setDocument(d);editor.resize(1100,950);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto *rotation=editor.findChild<QDoubleSpinBox *>(QStringLiteral("textRotation"));QVERIFY(rotation);QVERIFY(rotation->isVisible());
        auto *input=rotation->findChild<QLineEdit *>();QVERIFY(input);input->setFocus();input->selectAll();
        QTest::keyClicks(input,"37.25");QTest::keyClick(input,Qt::Key_Return);
        QCOMPARE(editor.document().textLayers[0].rotation,37.25);QCOMPARE(rotation->value(),37.25);
        QCOMPARE(editor.undoStack()->count(),1);editor.undoStack()->undo();QCOMPARE(rotation->value(),0.);editor.undoStack()->redo();QCOMPARE(rotation->value(),37.25);
        rotation->setValue(-12.5);QCOMPARE(editor.document().textLayers[0].rotation,-12.5);
        const auto saved=editor.document();const auto image=renderCover(saved);editor.setDocument(saved);
        QCOMPARE(rotation->value(),-12.5);QCOMPARE(renderCover(editor.document()),image);
    }
    void horizontalTrackingAndModeSwitch() {
        TextLayer text;text.text=QStringLiteral("HHH");text.fontSize=60;
        const auto normal=textLayerRect(text);text.horizontalSpacing=12;const auto wider=textLayerRect(text);
        QVERIFY(qAbs(wider.width()-normal.width()-24)<1);
        text.horizontalSpacing=-6;QVERIFY(textLayerRect(text).width()<normal.width());
        text.vertical=true;const auto vertical=textLayerRect(text);text.horizontalSpacing=99;QCOMPARE(textLayerRect(text),vertical);
        CoverEditor editor;CoverDocument d;d.outputSize={600,900};d.editableTextLayers=true;text.vertical=false;text.horizontalSpacing=0;d.textLayers.append(text);
        editor.setDocument(d);editor.resize(1100,950);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto *spacing=editor.findChild<QSpinBox *>(QStringLiteral("horizontalTextSpacing"));QVERIFY(spacing);QVERIFY(spacing->isVisible());
        auto *mode=editor.findChild<QComboBox *>(QStringLiteral("textWritingMode"));QVERIFY(mode);
        spacing->setValue(12);QCOMPARE(editor.document().textLayers[0].horizontalSpacing,12);
        const auto bounds=textLayerRect(editor.document().textLayers[0]);QVERIFY(qAbs(bounds.width()-normal.width()-24)<1);
        editor.undoStack()->undo();QCOMPARE(editor.document().textLayers[0].horizontalSpacing,0);editor.undoStack()->redo();QCOMPARE(spacing->value(),12);
        editor.findChild<QPushButton *>(QStringLiteral("align1"))->click();
        editor.findChild<QPushButton *>(QStringLiteral("align4"))->click();
        const auto centered=textLayerBounds(editor.document().textLayers[0],d.outputSize);QCOMPARE(centered.center(),QPointF(300,450));
        mode->setCurrentIndex(1);QVERIFY(!spacing->isVisible());QCOMPARE(editor.document().textLayers[0].horizontalSpacing,12);
        mode->setCurrentIndex(0);QVERIFY(spacing->isVisible());QCOMPARE(spacing->value(),12);
    }
    void glyphIntersectionsRemainFilled() {
        for(bool bold:{false,true}) {
            CoverDocument d;d.outputSize={800,400};d.backgroundColor=Qt::transparent;d.editableImageLayers=true;d.editableTextLayers=true;
            TextLayer layer;layer.text=QStringLiteral("重田中 B8O");layer.fontSize=80;layer.bold=bold;d.textLayers.append(layer);
            QPainterPath expectedPath;expectedPath.setFillRule(Qt::WindingFill);
            expectedPath.addText(0,0,textLayerFont(layer),layer.text);
            expectedPath.translate(-expectedPath.boundingRect().center());
            QImage expected(d.outputSize,QImage::Format_ARGB32_Premultiplied);expected.fill(Qt::transparent);
            {QPainter p(&expected);p.setRenderHint(QPainter::Antialiasing);p.translate(400,200);p.fillPath(expectedPath,Qt::white);}
            QCOMPARE(renderCover(d),expected);
            layer.vertical=true;layer.text=QStringLiteral("HH");layer.verticalSpacing=-80;
            QCOMPARE(textLayerShape(layer).fillRule(),Qt::WindingFill);
        }
    }
    void textBoundsMatchPaintedPixels() {
        const auto inkBounds=[](const QImage &image){
            int left=image.width(),right=-1,top=image.height(),bottom=-1;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
                if(qAlpha(image.pixel(x,y))>0){left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
            return QRect(left,top,right-left+1,bottom-top+1);
        };
        for(bool vertical:{false,true})for(qreal angle:{0.,31.,90.})for(bool effects:{false,true}) {
            CoverDocument d;d.outputSize={700,700};d.backgroundColor=Qt::transparent;d.editableImageLayers=true;d.editableTextLayers=true;
            TextLayer text;text.text=QStringLiteral("jÁgy 测试\n封套");text.fontSize=52;text.vertical=vertical;text.rotation=angle;
            text.horizontalSpacing=vertical?0:11;text.stroke=effects;text.strokeWidth=4;text.shadow=effects;text.shadowOffset={12,-9};text.background=effects;
            d.textLayers.append(text);const auto pixels=inkBounds(renderCover(d));QVERIFY(!pixels.isEmpty());
            const auto bounds=textLayerBounds(text,d.outputSize);
            QVERIFY2(qAbs(pixels.left()-bounds.left())<2,qPrintable(QStringLiteral("left %1 vs %2").arg(pixels.left()).arg(bounds.left())));
            QVERIFY(qAbs(pixels.top()-bounds.top())<2);QVERIFY(qAbs(pixels.right()+1-bounds.right())<2);QVERIFY(qAbs(pixels.bottom()+1-bounds.bottom())<2);
            if(angle==0){QVERIFY(qAbs(bounds.center().x()-350)<2);QVERIFY(qAbs(bounds.center().y()-350)<2);}
            // Test the actual alignment action, including rotated non-rectangular shapes.
            text.center={.3,.4};d.textLayers[0]=text;CoverEditor editor;editor.setDocument(d);
            editor.findChild<QPushButton *>(QStringLiteral("align1"))->click();
        editor.findChild<QPushButton *>(QStringLiteral("align4"))->click();
            const auto centered=inkBounds(renderCover(editor.document()));
            QVERIFY(qAbs(centered.center().x()-350)<2);QVERIFY(qAbs(centered.center().y()-350)<2);
        }
    }
    void simpleWorkflowDropAndInlineText() {
        CoverEditor editor;CoverDocument d;d.outputSize={600,900};editor.setDocument(d);
        editor.resize(1100,950);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto *advanced=editor.findChild<QWidget *>(QStringLiteral("advancedOptions"));
        auto *more=editor.findChild<QToolButton *>(QStringLiteral("moreOptions"));QVERIFY(advanced && more);
        QVERIFY(!advanced->isVisible());QVERIFY(!more->isChecked());
        auto *canvas=editor.findChild<QWidget *>(QStringLiteral("coverCanvas"));QVERIFY(canvas);QVERIFY(canvas->acceptDrops());
        QTemporaryDir dir;QImage source(200,100,QImage::Format_RGB32);source.fill(Qt::blue);
        const auto path=dir.filePath("photo.png");QVERIFY(source.save(path));QMimeData mime;mime.setUrls({QUrl::fromLocalFile(path)});
        const QPoint point(canvas->width()/2,canvas->height()/2);
        QDragEnterEvent enter(QPointF(point),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&enter);QVERIFY(enter.isAccepted());
        QDropEvent drop(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&drop);QVERIFY(drop.isAccepted());
        QCOMPARE(editor.document().imageLayers.size(),1);QCOMPARE(editor.undoStack()->count(),1);
        const auto rect=imageLayerRect(editor.document().imageLayers[0],d.outputSize);
        QVERIFY(qAbs(rect.width()/rect.height()-2)<.001);QVERIFY(qAbs(editor.document().imageLayers[0].frame.center().x()-.5)<.01);
        editor.undoStack()->undo();QVERIFY(editor.document().imageLayers.isEmpty());editor.undoStack()->redo();QCOMPARE(editor.document().imageLayers.size(),1);
        editor.findChild<QPushButton *>(QStringLiteral("addText"))->click();
        auto *inlineText=editor.findChild<QPlainTextEdit *>(QStringLiteral("inlineTextEditor"));QVERIFY(inlineText);QVERIFY(inlineText->isVisible());
        QTest::keyClicks(inlineText,"Quick cover");QCOMPARE(editor.document().textLayers.last().text,QStringLiteral("Quick cover"));
        QTest::keyClick(inlineText,Qt::Key_Return);QVERIFY(!inlineText->isVisible());
        QVERIFY(!advanced->isVisible());
        const auto snapshot=qEnvironmentVariable("DARKEYE_SIMPLE_SNAPSHOT");if(!snapshot.isEmpty())QVERIFY(editor.grab().save(snapshot));
        more->click();QVERIFY(advanced->isVisible());
        auto *tree=editor.findChild<QTreeWidget *>(QStringLiteral("allLayers"));QVERIFY(tree->isVisible());QCOMPARE(tree->topLevelItemCount(),2);
        QVERIFY(editor.findChild<QCheckBox *>(QStringLiteral("textStroke"))->isVisible());
        more->click();QVERIFY(!advanced->isVisible());
        editor.findChild<QPushButton *>(QStringLiteral("align1"))->click();
        editor.findChild<QPushButton *>(QStringLiteral("align4"))->click();
        QCOMPARE(editor.document().textLayers.last().center,QPointF(.5,.5));
        QTest::mouseDClick(canvas,Qt::LeftButton,Qt::NoModifier,point);QVERIFY(inlineText->isVisible());
        QTest::keyClicks(inlineText,"Changed");QTest::keyClick(inlineText,Qt::Key_Return,Qt::ShiftModifier);QTest::keyClicks(inlineText,"Title");
        QTest::keyClick(inlineText,Qt::Key_Return);QCOMPARE(editor.document().textLayers.last().text,QStringLiteral("Changed\nTitle"));
        SleeveEditor sleeve;sleeve.setApplyVisible(false);QVERIFY(sleeve.findChild<QPushButton *>(QStringLiteral("sleeveApply"))->isHidden());
    }
    void historyAndUnifiedLayerActions() {
        CoverEditor editor;CoverDocument d;d.outputSize={600,900};d.editableImageLayers=true;
        QImage source(100,100,QImage::Format_RGB32);source.fill(Qt::red);d.imageLayers.append(makeImageLayer(source,d.outputSize,QStringLiteral("原图")));
        editor.setDocument(d);editor.resize(1100,1000);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto *stack=editor.undoStack();QCOMPARE(stack->count(),0);
        auto *tree=editor.findChild<QTreeWidget *>(QStringLiteral("allLayers"));QVERIFY(tree);QCOMPARE(tree->topLevelItemCount(),1);
        editor.findChild<QPushButton *>(QStringLiteral("addText"))->click();QCOMPARE(stack->count(),1);QCOMPARE(tree->topLevelItemCount(),2);
        auto *content=editor.findChild<QPlainTextEdit *>(QStringLiteral("textContent"));content->setPlainText(QStringLiteral("TEST"));
        auto *stroke=editor.findChild<QCheckBox *>(QStringLiteral("textStroke"));QVERIFY(stroke);stroke->setChecked(true);
        QVERIFY(editor.document().textLayers[0].stroke);stack->undo();QVERIFY(!editor.document().textLayers[0].stroke);stack->redo();QVERIFY(editor.document().textLayers[0].stroke);
        QTest::keySequence(content,QKeySequence(QKeySequence::Undo));QVERIFY(!editor.document().textLayers[0].stroke);
        stack->redo();QVERIFY(editor.document().textLayers[0].stroke);
        const auto rendered=renderCover(editor.document());
        editor.findChild<QPushButton *>(QStringLiteral("duplicateLayer"))->click();QCOMPARE(editor.document().textLayers.size(),2);QCOMPARE(tree->topLevelItemCount(),3);
        stack->undo();QCOMPARE(editor.document().textLayers.size(),1);QCOMPARE(renderCover(editor.document()),rendered);
        stack->redo();QCOMPARE(editor.document().textLayers.size(),2);
        tree->topLevelItem(0)->setCheckState(2,Qt::Checked);QVERIFY(editor.document().textLayers[1].locked);QVERIFY(!content->isEnabled());
        stack->undo();QVERIFY(!editor.document().textLayers[1].locked);QVERIFY(content->isEnabled());
        tree->topLevelItem(0)->setCheckState(1,Qt::Unchecked);QVERIFY(!editor.document().textLayers[1].visible);
        stack->undo();QVERIFY(editor.document().textLayers[1].visible);
        editor.findChild<QPushButton *>(QStringLiteral("deleteLayer"))->click();QCOMPARE(editor.document().textLayers.size(),1);
        stack->undo();QCOMPARE(editor.document().textLayers.size(),2);
        // Cross-type ordering changes the actual export, not just the list display.
        d.editableTextLayers=true;TextLayer text;text.text=QStringLiteral("X");text.fontSize=90;text.background=true;text.textBackground=Qt::green;text.z=2;d.textLayers.append(text);
        d.imageLayers[0].frame={0,0,1,1};editor.setDocument(d);
        const auto above=renderCover(editor.document());
        editor.findChild<QPushButton *>(QStringLiteral("layerDown"))->click();
        const auto below=renderCover(editor.document());QVERIFY(above!=below);QCOMPARE(below.pixelColor(300,450),QColor(Qt::red));
        stack->undo();QCOMPARE(renderCover(editor.document()),above);
        const auto snapshot=qEnvironmentVariable("DARKEYE_FEATURE_SNAPSHOT");
        if(!snapshot.isEmpty()) {
            editor.findChild<QCheckBox *>(QStringLiteral("textStroke"))->setChecked(true);
            editor.findChild<QCheckBox *>(QStringLiteral("textShadow"))->setChecked(true);
            QVERIFY(editor.grab().save(snapshot));QVERIFY(renderCover(editor.document()).save(snapshot+QStringLiteral(".cover.png")));
        }
    }
    void snapAlignAndGestureHistory() {
        CoverEditor editor;CoverDocument d;d.outputSize={600,900};d.editableImageLayers=true;
        QImage source(100,100,QImage::Format_RGB32);source.fill(Qt::red);auto layer=makeImageLayer(source,d.outputSize);layer.frame={.25,.25,.2,.2};d.imageLayers.append(layer);
        editor.setDocument(d);editor.setGuideLines({.4,.6});editor.resize(1100,900);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto *canvas=editor.findChild<QWidget *>(QStringLiteral("coverCanvas"));QVERIFY(canvas);
        QSizeF a(d.outputSize);a.scale(QSizeF(canvas->width()-48,canvas->height()-48),Qt::KeepAspectRatio);
        const QPointF origin((canvas->width()-a.width())/2,(canvas->height()-a.height())/2);
        const auto start=(origin+QPointF(.35*a.width(),.35*a.height())).toPoint();
        const auto end=start+QPoint(qRound(.15*a.width())-2,0);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(canvas,end);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(qAbs(editor.document().imageLayers[0].frame.center().x()-.5)<.00001);
        QCOMPARE(editor.undoStack()->count(),1);editor.undoStack()->undo();QCOMPARE(editor.document().imageLayers[0].frame,layer.frame);
        editor.undoStack()->redo();
        auto *target=editor.findChild<QComboBox *>(QStringLiteral("alignTarget"));QVERIFY(target);target->setCurrentIndex(3);
        editor.findChild<QPushButton *>(QStringLiteral("align0"))->click();QVERIFY(qAbs(editor.document().imageLayers[0].frame.left()-.6)<.00001);
        const auto aligned=editor.document();editor.undoStack()->undo();QVERIFY(editor.document().imageLayers[0].frame!=aligned.imageLayers[0].frame);
        editor.undoStack()->redo();QCOMPARE(editor.document().imageLayers[0].frame,aligned.imageLayers[0].frame);
        editor.setDocument(d);editor.findChild<QCheckBox *>(QStringLiteral("snapLayers"))->setChecked(false);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(canvas,end);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(qAbs(editor.document().imageLayers[0].frame.center().x()-.5)>.001);
    }
    void effectsSharePreviewAndExport() {
        CoverDocument d;d.outputSize={400,600};d.editableImageLayers=true;d.editableTextLayers=true;
        TextLayer text;text.text=QStringLiteral("封套\nTEST");text.fontSize=60;d.textLayers.append(text);
        const auto plain=renderCover(d);
        d.textLayers[0].stroke=true;d.textLayers[0].strokeColor=Qt::yellow;d.textLayers[0].strokeWidth=4;
        const auto stroke=renderCover(d);QVERIFY(stroke!=plain);
        d.textLayers[0].shadow=true;d.textLayers[0].shadowOffset={20,20};QVERIFY(renderCover(d)!=stroke);
        d.textLayers[0].background=true;d.textLayers[0].textBackground=Qt::green;
        d.textLayers[0].vertical=true;d.textLayers[0].rotation=25;
        QImage preview(d.outputSize,QImage::Format_ARGB32_Premultiplied);preview.fill(Qt::transparent);
        {QPainter painter(&preview);paintCover(painter,QRectF(QPointF(),QSizeF(d.outputSize)),d);}
        QCOMPARE(preview,renderCover(d));
        QTemporaryDir dir;QString error;QVERIFY2(saveCover(d,dir.filePath("effects.png"),&error),qPrintable(error));
        QCOMPARE(QImage(dir.filePath("effects.png")),renderCover(d).convertToFormat(QImage(dir.filePath("effects.png")).format()));
    }
    void propertiesFollowSingleSelection() {
        CoverEditor editor;CoverDocument d;d.outputSize={600,900};editor.setDocument(d);
        auto *properties=editor.findChild<QStackedWidget *>(QStringLiteral("selectionProperties"));QVERIFY(properties);
        QCOMPARE(properties->currentWidget()->objectName(),QStringLiteral("emptyProperties"));
        auto *addImage=editor.findChild<QPushButton *>(QStringLiteral("addImage"));
        auto *addText=editor.findChild<QPushButton *>(QStringLiteral("addText"));QVERIFY(addImage && addText);
        QVERIFY(!addImage->icon().isNull());QVERIFY(!addText->icon().isNull());
        QVERIFY(addImage->text().isEmpty());QVERIFY(addText->text().isEmpty());
        addText->click();QCOMPARE(properties->currentWidget()->objectName(),QStringLiteral("textProperties"));
        d.editableImageLayers=true;QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::red);
        d.imageLayers.append(makeImageLayer(image,d.outputSize));editor.setDocument(d);
        QCOMPARE(properties->currentWidget()->objectName(),QStringLiteral("imageProperties"));
        QVERIFY(!properties->widget(2)->isVisible());
        addText->click();QCOMPARE(properties->currentWidget()->objectName(),QStringLiteral("textProperties"));
        QVERIFY(!properties->widget(1)->isVisible());
    }
    void unifiedCanvasDoesNotClipAtGuides() {
        SleeveDocument d;d.backWidth=120;d.spineWidth=20;d.frontWidth=120;d.height=180;
        d.unified=true;d.composition.outputSize=d.outputSize();d.composition.editableImageLayers=true;
        d.composition.editableTextLayers=true;
        QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::red);
        auto layer=makeImageLayer(image,d.outputSize());layer.frame={.25,.25,.5,.5};d.composition.imageLayers.append(layer);
        TextLayer text;text.text=QStringLiteral("跨区域文字");text.center={.5,.2};text.fontSize=20;d.composition.textLayers.append(text);
        const auto rendered=renderSleeve(d);QCOMPARE(rendered,renderCover(d.composition));
        for (int x : {119,120,139,140}) QCOMPARE(rendered.pixelColor(x,90),QColor(Qt::red));
        SleeveEditor editor;editor.setDocument(d);
        auto *composition=editor.findChild<CoverEditor *>(QStringLiteral("sleeveComposition"));QVERIFY(composition);
        QCOMPARE(composition->document().imageLayers[0].frame,layer.frame);
        // Changing a guide's position without changing total size leaves the composition intact.
        d.backWidth=110;d.frontWidth=130;editor.setDocument(d);
        QCOMPARE(renderSleeve(editor.document()),rendered);
        QSignalSpy spy(&editor,&SleeveEditor::applyRequested);
        for(auto *button:editor.findChildren<QPushButton *>()) if(button->objectName()==QStringLiteral("sleeveApply"))button->click();
        QCOMPARE(spy.count(),1);QCOMPARE(qvariant_cast<QImage>(spy.first().first()),rendered);
    }
    void horizontalAndVerticalText() {
        TextLayer layer; layer.text=QStringLiteral("天地玄黄");layer.fontSize=40;
        const auto horizontal=textLayerRect(layer);
        layer.vertical=true;const auto vertical=textLayerRect(layer);
        QVERIFY(vertical.height()>vertical.width());
        QVERIFY(horizontal.width()>horizontal.height());
        layer.text=QStringLiteral("甲乙\n丙");const auto twoColumns=textLayerRect(layer);
        layer.text=QStringLiteral("甲乙");const auto oneColumn=textLayerRect(layer);
        QVERIFY(twoColumns.width()>oneColumn.width());
        QVERIFY(qAbs(twoColumns.height()-oneColumn.height())<3);
        layer.text=QStringLiteral("甲");
        QVERIFY(qAbs(oneColumn.height()-textLayerRect(layer).height()-layer.fontSize)<3);
        layer.text=QString::fromUtf8("é");QVERIFY(textLayerRect(layer).height()>0);
        layer.text=QStringLiteral("e");QVERIFY(textLayerRect(layer).height()>0);
        layer.text=QString::fromUtf8("😀");QVERIFY(textLayerRect(layer).height()>0);
        CoverEditor editor;CoverDocument d;d.outputSize={600,900};d.showImagePlaceholders=false;
        editor.setDocument(d);editor.resize(1000,950);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        editor.findChild<QPushButton *>(QStringLiteral("addText"))->click();
        editor.findChild<QPlainTextEdit *>(QStringLiteral("textContent"))->setPlainText(QStringLiteral("天地玄黄"));
        auto *mode=editor.findChild<QComboBox *>(QStringLiteral("textWritingMode"));QVERIFY(mode);mode->setCurrentIndex(1);
        QVERIFY(editor.document().textLayers[0].vertical);
        QCOMPARE(editor.document().textLayers[0].rotation,0.);
        auto *spacing=editor.findChild<QSpinBox *>(QStringLiteral("verticalTextSpacing"));QVERIFY(spacing);
        QVERIFY(spacing->isVisible());
        const auto originalBounds=textLayerRect(editor.document().textLayers[0]);
        spacing->setValue(20);
        QCOMPARE(editor.document().textLayers[0].verticalSpacing,20);
        QCOMPARE(textLayerRect(editor.document().textLayers[0]).height(),originalBounds.height()+60);
        spacing->setValue(-5);
        QCOMPARE(textLayerRect(editor.document().textLayers[0]).height(),originalBounds.height()-15);

        const auto position=editor.document().textLayers[0].center;
        auto *canvas=editor.findChild<QWidget *>(QStringLiteral("coverCanvas"));QVERIFY(canvas);
        const QPoint center(canvas->width()/2,canvas->height()/2);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,center);
        QTest::mouseMove(canvas,center+QPoint(20,30));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,center+QPoint(20,30));
        QVERIFY(editor.document().textLayers[0].center.y()>position.y());
        const auto snapshot=qEnvironmentVariable("DARKEYE_COVER_TEST_SNAPSHOT");
        if(!snapshot.isEmpty()) {QVERIFY(editor.grab().save(snapshot));QVERIFY(renderCover(editor.document()).save(snapshot+QStringLiteral(".cover.png")));}
        QImage preview(600,900,QImage::Format_ARGB32_Premultiplied);preview.fill(Qt::transparent);
        {QPainter p(&preview);paintCover(p,QRectF(0,0,600,900),editor.document());}
        QCOMPARE(preview,renderCover(editor.document()));
        const auto saved=editor.document();editor.setDocument(saved);QCOMPARE(mode->currentIndex(),1);
        const auto moved=editor.document().textLayers[0].center;
        mode->setCurrentIndex(0);QVERIFY(!editor.document().textLayers[0].vertical);
        QVERIFY(!spacing->isVisible());
        QCOMPARE(editor.document().textLayers[0].verticalSpacing,-5);
        const auto horizontalBounds=textLayerRect(editor.document().textLayers[0]);
        auto horizontalLayer=editor.document().textLayers[0];horizontalLayer.verticalSpacing=100;
        QCOMPARE(textLayerRect(horizontalLayer),horizontalBounds);

        QCOMPARE(editor.document().textLayers[0].center,moved);
        QCOMPARE(editor.document().textLayers[0].text,QStringLiteral("天地玄黄"));
    }
    void freeImagesMoveResizeCropAndRestore() {
        CoverEditor editor; CoverDocument d;d.outputSize={600,900};d.editableImageLayers=true;
        QImage source(200,100,QImage::Format_RGB32);source.fill(Qt::red);
        {QPainter p(&source);p.fillRect(100,0,100,100,Qt::blue);}
        d.imageLayers.append(makeImageLayer(source,d.outputSize,QStringLiteral("素材")));
        editor.setDocument(d);editor.resize(1000,900);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto *canvas=editor.findChild<QWidget *>(QStringLiteral("coverCanvas"));QVERIFY(canvas);
        const QPoint center(canvas->width()/2,canvas->height()/2);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,center);
        QTest::mouseMove(canvas,center+QPoint(30,20));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,center+QPoint(30,20));
        QVERIFY(editor.document().imageLayers[0].frame.x()>d.imageLayers[0].frame.x());
        const auto before=editor.document().imageLayers[0];
        QSizeF display(d.outputSize);display.scale(QSizeF(canvas->width()-48,canvas->height()-48),Qt::KeepAspectRatio);
        const QPointF topLeft((canvas->width()-display.width())/2,(canvas->height()-display.height())/2);
        const QPointF corners[]={before.frame.topLeft(),before.frame.topRight(),before.frame.bottomLeft(),before.frame.bottomRight()};
        for(int i=0;i<4;++i) {
            const auto point=(topLeft+QPointF(corners[i].x()*display.width(),corners[i].y()*display.height())).toPoint();
            QTest::mouseMove(canvas,point);
            QCOMPARE(canvas->cursor().shape(),i==0 || i==3?Qt::SizeFDiagCursor:Qt::SizeBDiagCursor);
        }
        QTest::mouseMove(canvas,center);
        QCOMPARE(canvas->cursor().shape(),Qt::OpenHandCursor);
        const QPoint corner=(topLeft+QPointF(before.frame.right()*display.width(),before.frame.bottom()*display.height())).toPoint();
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,corner);
        QTest::mouseMove(canvas,corner+QPoint(25,25));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,corner+QPoint(25,25));
        const auto resized=editor.document().imageLayers[0].frame;
        QVERIFY(resized.width()>before.frame.width());
        QVERIFY(qAbs(resized.width()/resized.height()-before.frame.width()/before.frame.height())<.0001);
        auto *crop=editor.findChild<QPushButton *>(QStringLiteral("cropImage"));QVERIFY(crop);
        const auto cropBefore=editor.document();const int historyBeforeCrop=editor.undoStack()->count();
        crop->click();QVERIFY(!QApplication::activeModalWidget());
        const auto cropFrame=editor.document().imageLayers[0].frame;
        const auto cropPoint=[&](QPointF normalized){return (topLeft+QPointF(normalized.x()*display.width(),normalized.y()*display.height())).toPoint();};
        const auto edge=cropPoint(QPointF(cropFrame.left(),cropFrame.center().y()));
        const auto middle=cropPoint(cropFrame.center());
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,edge);
        QTest::mouseMove(canvas,middle);
        // The document and final renderer change before mouse release or confirmation.
        QVERIFY(qAbs(editor.document().imageLayers[0].crop.x()-.5)<.01);
        QVERIFY(editor.document().imageLayers[0].frame.width()<cropFrame.width());
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,middle);
        const auto liveCenter=cropPoint(editor.document().imageLayers[0].frame.center());
        QCOMPARE(canvas->grab().toImage().pixelColor(liveCenter),QColor(Qt::blue));
        const auto snapshot=qEnvironmentVariable("DARKEYE_CROP_SNAPSHOT");
        if(!snapshot.isEmpty())QVERIFY(editor.grab().save(snapshot));
        QCOMPARE(editor.document().imageLayers[0].image,source);
        const auto cropped=editor.document();const auto rendered=renderCover(cropped);
        const auto rect=imageLayerRect(cropped.imageLayers[0],cropped.outputSize);
        QCOMPARE(rendered.pixelColor(rect.center().toPoint()),QColor(Qt::blue));
        QCOMPARE(editor.undoStack()->count(),historyBeforeCrop);
        QTest::keyClick(canvas,Qt::Key_Return);
        QCOMPARE(editor.undoStack()->count(),historyBeforeCrop+1);
        editor.undoStack()->undo();QCOMPARE(editor.document().imageLayers[0].frame,cropBefore.imageLayers[0].frame);
        QCOMPARE(editor.document().imageLayers[0].crop,cropBefore.imageLayers[0].crop);
        editor.undoStack()->redo();QCOMPARE(editor.document().imageLayers[0].crop,cropped.imageLayers[0].crop);
        QVERIFY(editor.findChild<QPushButton *>(QStringLiteral("cropImage"))->isEnabled());
        crop->click();
        const auto current=editor.document().imageLayers[0].frame;
        const auto right=cropPoint(QPointF(current.right(),current.center().y()));
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,right);
        QTest::mouseMove(canvas,right-QPoint(20,0));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,right-QPoint(20,0));
        QVERIFY(editor.document().imageLayers[0].crop!=cropped.imageLayers[0].crop);
        QTest::keyClick(canvas,Qt::Key_Escape);
        QCOMPARE(editor.document().imageLayers[0].crop,cropped.imageLayers[0].crop);
        QCOMPARE(editor.document().imageLayers[0].frame,cropped.imageLayers[0].frame);
        auto *reset=editor.findChild<QPushButton *>(QStringLiteral("resetImage"));QVERIFY(reset);reset->click();
        QCOMPARE(editor.document().imageLayers[0].crop,QRectF(0,0,1,1));
        const auto fitted=imageLayerRect(editor.document().imageLayers[0],d.outputSize);
        QCOMPARE(fitted.width(),qreal(d.outputSize.width()));
        QCOMPARE(fitted.height(),qreal(300));
        QCOMPARE(fitted.center(),QPointF(300,450));
        auto *remove=editor.findChild<QPushButton *>(QStringLiteral("removeImage"));QVERIFY(remove);remove->click();
        const auto saved=editor.document();editor.setDocument(saved);QVERIFY(editor.document().imageLayers.isEmpty());
    }
    void cropCanvasAndImageComposition() {
        QImage source(200,100,QImage::Format_RGB32);source.fill(Qt::green);
        SleeveDocument d;d.backWidth=120;d.spineWidth=20;d.frontWidth=120;d.height=180;
        auto &panel=d.panels[1];panel.editableImageLayers=true;panel.showImagePlaceholders=false;
        auto layer=makeImageLayer(source,{20,180});layer.crop={.25,.25,.5,.5};layer.frame={-.5,.25,2,.5};panel.imageLayers.append(layer);
        const auto composite=renderSleeve(d);panel.outputSize={20,180};
        QCOMPARE(composite.copy(120,0,20,180),renderCover(panel));
        QCOMPARE(composite.pixelColor(119,90),d.panels[0].backgroundColor);
        QCOMPARE(composite.pixelColor(140,90),d.panels[2].backgroundColor);
    }
    void freeTextControlsAndDragging() {
        CoverEditor editor; CoverDocument d; d.outputSize = {600,900}; d.showImagePlaceholders = false;
        editor.setDocument(d); editor.resize(1000,900); editor.show(); QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto *add = editor.findChild<QPushButton *>(QStringLiteral("addText"));
        auto *remove = editor.findChild<QPushButton *>(QStringLiteral("removeText"));
        auto *content = editor.findChild<QPlainTextEdit *>(QStringLiteral("textContent"));
        auto *size = editor.findChild<QSpinBox *>(QStringLiteral("textSize"));
        auto *family = editor.findChild<QComboBox *>(QStringLiteral("textFont"));
        auto *canvas = editor.findChild<QWidget *>(QStringLiteral("coverCanvas"));
        QVERIFY(add && remove && content && size && family && canvas);
        add->click(); content->setPlainText(QStringLiteral("自由文字\n第二行")); size->setValue(60);
        QVERIFY(family->count() > 1); family->setCurrentIndex(1);
        QCOMPARE(editor.document().textLayers.size(),1);
        QCOMPARE(editor.document().textLayers[0].fontSize,60);
        QCOMPARE(editor.document().textLayers[0].fontFamily,family->currentData().toString());
        const auto before = editor.document();
        const QPoint center(canvas->width()/2,canvas->height()/2);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,center);
        QTest::mouseMove(canvas,center+QPoint(30,40));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,center+QPoint(30,40));
        QVERIFY(editor.document().textLayers[0].center.x() > before.textLayers[0].center.x());
        QVERIFY(editor.document().textLayers[0].center.y() > before.textLayers[0].center.y());
        QCOMPARE(editor.document().imageSlots[0].position,before.imageSlots[0].position);
        add->click(); QCOMPARE(editor.document().textLayers.size(),2);
        content->setPlainText(QStringLiteral("另一段文字"));
        QCOMPARE(editor.document().textLayers[0].text,QStringLiteral("自由文字\n第二行"));
        remove->click(); remove->click();
        QVERIFY(editor.document().textLayers.isEmpty());
        // Removed legacy layers must not reappear after switching entries.
        const auto saved = editor.document(); editor.setDocument(saved);
        QVERIFY(editor.document().textLayers.isEmpty());
    }
    void rotatedTextMatchesCompositeAndClipsToPanel() {
        SleeveDocument d; d.backWidth=120; d.spineWidth=30; d.frontWidth=120; d.height=180;
        for (auto &panel : d.panels) { panel.editableTextLayers=true; panel.showImagePlaceholders=false; }
        TextLayer layer; layer.text=QStringLiteral("Spine text"); layer.fontSize=20; layer.rotation=90;
        d.panels[1].textLayers.append(layer);
        const auto image=renderSleeve(d);
        auto panel=d.panels[1]; panel.outputSize={30,180};
        QCOMPARE(image.copy(120,0,30,180),renderCover(panel));
        layer.text=QStringLiteral("This text is much wider than the panel"); layer.rotation=0; layer.fontSize=100;
        d.panels[1].textLayers[0]=layer;
        const auto largeText=renderSleeve(d);
        QCOMPARE(largeText.copy(0,0,120,180),image.copy(0,0,120,180));
        QCOMPARE(largeText.copy(150,0,120,180),image.copy(150,0,120,180));
    }
    void sleeveSeamsAndPanelIsolation() {
        SleeveDocument d;
        QCOMPARE(d.outputSize(), QSize(3200,2160));
        const qreal uBackEnd = qreal(d.backWidth) / d.outputSize().width();
        const qreal uSpineEnd = qreal(d.backWidth + d.spineWidth) / d.outputSize().width();
        QVERIFY(qAbs(uBackEnd - .4766346) < 1. / 3200);
        QVERIFY(qAbs(uSpineEnd - .5252864) < 1. / 3200);
        d.backWidth = 120; d.spineWidth = 15; d.frontWidth = 125; d.height = 180;
        const QColor colors[] = {Qt::red, Qt::green, Qt::blue};
        for (int i = 0; i < 3; ++i) d.panels[i].backgroundColor = colors[i];
        const auto image = renderSleeve(d);
        QCOMPARE(image.size(), QSize(260,180));
        // Every boundary pixel is covered exactly once, with no guide lines.
        for (int y : {0, 90, 179}) {
            QCOMPARE(image.pixelColor(119,y), colors[0]);
            QCOMPARE(image.pixelColor(120,y), colors[1]);
            QCOMPARE(image.pixelColor(134,y), colors[1]);
            QCOMPARE(image.pixelColor(135,y), colors[2]);
            QCOMPARE(image.pixelColor(259,y), colors[2]);
        }
        d.panels[1].title = QStringLiteral("书脊"); d.panels[1].subtitle = QStringLiteral("DEMO");
        const auto withSpineText = renderSleeve(d);
        QCOMPARE(withSpineText.copy(0,0,120,180), image.copy(0,0,120,180));
        QCOMPARE(withSpineText.copy(135,0,125,180), image.copy(135,0,125,180));
        QVERIFY(withSpineText.copy(120,0,15,180) != image.copy(120,0,15,180));
        QTemporaryDir dir; QString error;
        QVERIFY2(saveSleeve(d,dir.filePath("sleeve.png"),&error), qPrintable(error));
        QCOMPARE(QImage(dir.filePath("sleeve.png")).size(), d.outputSize());
        d.spineWidth = 0; QVERIFY(renderSleeve(d).isNull());
        d.spineWidth = 8192; QVERIFY(renderSleeve(d).isNull());
    }
    void sleeveEditorPreservesIndependentPanelsAndAppliesComposite() {
        SleeveEditor editor; SleeveDocument d;
        d.panels[0].title = QStringLiteral("封底简介"); d.panels[1].title = QStringLiteral("书脊标题");
        d.panels[2].title = QStringLiteral("封面标题"); editor.setDocument(d);
        QVERIFY(editor.findChildren<QTabWidget *>().isEmpty());
        auto *composition = editor.findChild<CoverEditor *>(QStringLiteral("sleeveComposition")); QVERIFY(composition);
        QVERIFY(editor.document().unified);
        QCOMPARE(composition->document().textLayers.size(),3);
        QCOMPARE(composition->document().textLayers[0].text,d.panels[0].title);
        QCOMPARE(composition->document().textLayers[2].text,d.panels[2].title);
        QVERIFY(editor.findChildren<QSpinBox *>(QRegularExpression(QStringLiteral("sleeveDimension.*"))).isEmpty());
        auto *settings=editor.findChild<QPushButton *>(QStringLiteral("sleeveSettings"));QVERIFY(settings);
        QTimer::singleShot(0,[&]{
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);
            auto *width=dialog->findChild<QSpinBox *>(QStringLiteral("sleeveDimension1"));QVERIFY(width);
            width->setValue(200);QCOMPARE(editor.document().spineWidth,156);
            const auto snapshot=qEnvironmentVariable("DARKEYE_SETTINGS_SNAPSHOT");
            if(!snapshot.isEmpty())QVERIFY(dialog->grab().save(snapshot));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        settings->click();
        QCOMPARE(editor.document().spineWidth,200);
        QCOMPARE(editor.document().composition.outputSize,QSize(3244,2160));
        composition->undoStack()->undo();QCOMPARE(editor.document().spineWidth,156);
        composition->undoStack()->redo();QCOMPARE(editor.document().spineWidth,200);
        QCOMPARE(editor.document().panels[0].title,d.panels[0].title);
        QTimer::singleShot(0,[&]{
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);
            auto *width=dialog->findChild<QSpinBox *>(QStringLiteral("sleeveDimension1"));QVERIFY(width);
            width->setValue(8192);QVERIFY(!dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
            dialog->findChild<QPushButton *>(QStringLiteral("restoreSleeveDimensions"))->click();
            QCOMPARE(width->value(),156);
            dialog->reject();
        });
        settings->click();QCOMPARE(editor.document().spineWidth,200);
        QSignalSpy spy(&editor,&SleeveEditor::applyRequested);
        for (auto *button : editor.findChildren<QPushButton *>())
            if (button->objectName() == QStringLiteral("sleeveApply")) button->click();
        QCOMPARE(spy.count(),1);
        QCOMPARE(qvariant_cast<QImage>(spy.first().first()), renderSleeve(editor.document()));
    }
    void cropAndLayout() {
        CoverDocument d; d.outputSize = {120, 180}; d.layout = Layout::Split;
        d.imageSlots[0].image = QImage(200,100,QImage::Format_RGB32); d.imageSlots[0].image.fill(Qt::red);
        d.imageSlots[1].image = QImage(100,200,QImage::Format_RGB32); d.imageSlots[1].image.fill(Qt::blue);
        const auto image = renderCover(d);
        QCOMPARE(image.size(), d.outputSize);
        QCOMPARE(image.pixelColor(60,40), QColor(Qt::red));
        QCOMPARE(image.pixelColor(60,140), QColor(Qt::blue));
        d.layout = Layout::Inset;
        const auto inset = renderCover(d);
        QCOMPARE(inset.pixelColor(90,40), QColor(Qt::blue));
        QCOMPARE(inset.pixelColor(20,40), QColor(Qt::red));
        ImageSlot slot; slot.image = QImage(200,100,QImage::Format_RGB32);
        slot.position = {0,0}; const QRectF frame(0,0,100,100);
        QCOMPARE(imageRect(slot,frame), QRectF(0,0,200,100));
        slot.position = {1,1}; QCOMPARE(imageRect(slot,frame), QRectF(-100,0,200,100));
        slot.zoom = 2; QVERIFY(imageRect(slot,frame).contains(frame));
    }
    void previewMatchesExportAndAtomicSave() {
        CoverDocument d; d.outputSize = {300,450}; d.title = QStringLiteral("测试标题"); d.subtitle = QStringLiteral("DEMO-001");
        d.imageSlots[0].image = QImage(200,100,QImage::Format_RGB32); d.imageSlots[0].image.fill(Qt::green);
        QImage preview(300,450,QImage::Format_ARGB32_Premultiplied); preview.fill(Qt::black);
        { QPainter painter(&preview); paintCover(painter, QRectF(0,0,300,450), d); }
        QCOMPARE(preview, renderCover(d));
        QTemporaryDir dir; QString error; const auto path = dir.filePath("cover.png");
        QVERIFY2(saveCover(d,path,&error), qPrintable(error));
        QCOMPARE(QImage(path), renderCover(d).convertToFormat(QImage(path).format()));
        d.outputSize = {9000,9000}; QVERIFY(!saveCover(d,path,&error));
        QCOMPARE(QImage(path).size(), QSize(300,450));
        QVERIFY(!saveCover(CoverDocument{},dir.filePath("missing/cover.png"),&error));
    }
    void editorDeliversImageWithoutPersistence() {
        CoverEditor editor; CoverDocument d;
        d.imageSlots[0].image = QImage(100,100,QImage::Format_RGB32); d.imageSlots[0].image.fill(Qt::yellow);
        d.title = QStringLiteral("条目标题"); editor.setDocument(d);
        QSignalSpy spy(&editor, &CoverEditor::applyRequested);
        QPushButton *apply = nullptr;
        for (auto *button : editor.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("应用到当前条目")) apply = button;
        QVERIFY(apply); apply->click(); QCOMPARE(spy.count(),1);
        QCOMPARE(qvariant_cast<QImage>(spy.first().first()), renderCover(d));
    }
};
QTEST_MAIN(CoverMakerTest)
#include "CoverMakerTest.moc"
