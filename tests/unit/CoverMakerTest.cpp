#include "cover_maker/CoverEditor.h"
#include "../../libs/cover_maker/src/ImageCropDialog.h"
#include <QTimer>
#include "cover_maker/SleeveEditor.h"
#include <QSpinBox>
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
    void horizontalAndVerticalText() {
        TextLayer layer; layer.text=QStringLiteral("天地玄黄");layer.fontSize=40;
        const auto horizontal=textLayerRect(layer);
        layer.vertical=true;const auto vertical=textLayerRect(layer);
        QVERIFY(vertical.height()>vertical.width());
        QVERIFY(horizontal.width()>horizontal.height());
        layer.text=QStringLiteral("甲乙\n丙");const auto twoColumns=textLayerRect(layer);
        layer.text=QStringLiteral("甲乙");const auto oneColumn=textLayerRect(layer);
        QCOMPARE(twoColumns.width(),oneColumn.width()*2);
        QCOMPARE(twoColumns.height(),oneColumn.height());
        layer.text=QString::fromUtf8("é");const auto combining=textLayerRect(layer);
        layer.text=QStringLiteral("e");QCOMPARE(textLayerRect(layer).height(),combining.height());
        layer.text=QString::fromUtf8("😀");QCOMPARE(textLayerRect(layer).height(),combining.height());
        CoverEditor editor;CoverDocument d;d.outputSize={600,900};d.showImagePlaceholders=false;
        editor.setDocument(d);editor.resize(1000,950);editor.show();QVERIFY(QTest::qWaitForWindowExposed(&editor));
        editor.findChild<QPushButton *>(QStringLiteral("addText"))->click();
        editor.findChild<QPlainTextEdit *>(QStringLiteral("textContent"))->setPlainText(QStringLiteral("天地玄黄"));
        auto *mode=editor.findChild<QComboBox *>(QStringLiteral("textWritingMode"));QVERIFY(mode);mode->setCurrentIndex(1);
        QVERIFY(editor.document().textLayers[0].vertical);
        QCOMPARE(editor.document().textLayers[0].rotation,0.);
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
        const QPoint corner=(topLeft+QPointF(before.frame.right()*display.width(),before.frame.bottom()*display.height())).toPoint();
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,corner);
        QTest::mouseMove(canvas,corner+QPoint(25,25));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,corner+QPoint(25,25));
        const auto resized=editor.document().imageLayers[0].frame;
        QVERIFY(resized.width()>before.frame.width());
        QVERIFY(qAbs(resized.width()/resized.height()-before.frame.width()/before.frame.height())<.0001);
        auto *crop=editor.findChild<QPushButton *>(QStringLiteral("cropImage"));QVERIFY(crop);
        QTimer::singleShot(0,[&]{auto *dialog=dynamic_cast<ImageCropDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);dialog->canvas->crop={.5,0,.5,1};dialog->accept();});
        crop->click();
        QCOMPARE(editor.document().imageLayers[0].crop,QRectF(.5,0,.5,1));
        QCOMPARE(editor.document().imageLayers[0].image,source);
        const auto cropped=editor.document();const auto rendered=renderCover(cropped);
        const auto rect=imageLayerRect(cropped.imageLayers[0],cropped.outputSize);
        QCOMPARE(rendered.pixelColor(rect.center().toPoint()),QColor(Qt::blue));
        QTimer::singleShot(0,[&]{auto *dialog=dynamic_cast<ImageCropDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);dialog->canvas->crop={0,0,1,1};dialog->reject();});
        crop->click();QCOMPARE(editor.document().imageLayers[0].crop,cropped.imageLayers[0].crop);
        auto *reset=editor.findChild<QPushButton *>(QStringLiteral("resetImage"));QVERIFY(reset);reset->click();
        QCOMPARE(editor.document().imageLayers[0].crop,QRectF(0,0,1,1));
        auto *remove=editor.findChild<QPushButton *>(QStringLiteral("removeImage"));QVERIFY(remove);remove->click();
        const auto saved=editor.document();editor.setDocument(saved);QVERIFY(editor.document().imageLayers.isEmpty());
    }
    void cropCanvasAndImageComposition() {
        QImage source(200,100,QImage::Format_RGB32);source.fill(Qt::green);
        CropCanvas crop(source,{0,0,1,1},nullptr);crop.resize(600,400);crop.show();QVERIFY(QTest::qWaitForWindowExposed(&crop));
        const auto area=crop.area();
        const auto from=(area.topLeft()+QPointF(area.width()*.25,area.height()*.25)).toPoint();
        const auto to=(area.topLeft()+QPointF(area.width()*.75,area.height()*.75)).toPoint();
        QTest::mousePress(&crop,Qt::LeftButton,Qt::NoModifier,from);
        QTest::mouseMove(&crop,to);QTest::mouseRelease(&crop,Qt::LeftButton,Qt::NoModifier,to);
        QVERIFY(qAbs(crop.crop.width()-.5)<.01);QVERIFY(qAbs(crop.crop.height()-.5)<.01);
        SleeveDocument d;d.backWidth=120;d.spineWidth=20;d.frontWidth=120;d.height=180;
        auto &panel=d.panels[1];panel.editableImageLayers=true;panel.showImagePlaceholders=false;
        auto layer=makeImageLayer(source,{20,180});layer.crop=crop.crop;layer.frame={-.5,.25,2,.5};panel.imageLayers.append(layer);
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
        auto *tabs = editor.findChild<QTabWidget *>(QStringLiteral("sleevePanels")); QVERIFY(tabs);
        for (int i = 0; i < 3; ++i) {
            tabs->setCurrentIndex(i);
            auto *panel = qobject_cast<CoverEditor *>(tabs->widget(i)); QVERIFY(panel);
            QCOMPARE(panel->document().title, d.panels[i].title);
        }
        auto *width = editor.findChild<QSpinBox *>(QStringLiteral("sleeveDimension1")); QVERIFY(width);
        width->setValue(200);
        QCOMPARE(editor.document().spineWidth,200);
        QCOMPARE(editor.document().panels[1].outputSize, QSize(200,2160));
        QCOMPARE(editor.document().panels[0].title, d.panels[0].title);
        width->setValue(8192); QCOMPARE(editor.document().spineWidth,200);
        QSignalSpy spy(&editor,&SleeveEditor::applyRequested);
        for (auto *button : editor.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("应用完整封套到当前条目")) button->click();
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
