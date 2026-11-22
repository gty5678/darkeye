#include "ui/pages/PersonPage.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/WorkRepository.h"
#include "ui/pages/PersonDetailPage.h"
#include "darkeye_ui/components/CompleterLineEdit.h"
#include "darkeye_ui/components/Charts.h"
#include "darkeye_ui/components/CollapsibleSection.h"
#include "darkeye_ui/components/HeartLabel.h"
#include "darkeye_ui/components/LazyScrollArea.h"
#include "darkeye_ui/components/OctImage.h"
#include "ui/components/ImageDropWidget.h"
#include "ui/pages/ModifyActorPage.h"
#include "ui/pages/ModifyActressPage.h"
#include "ui/components/PersonInfoPanel.h"
#include "ui/components/ActressWorkTimeline.h"
#include "ui/components/AsyncImageLabel.h"
#include "ui/components/WorkCard.h"
#include "ui/components/WikiTextEdit.h"
#include "ui/layouts/myads/PaneWidget.h"
#include "ui/layouts/myads/WorkspaceWidget.h"

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QHelpEvent>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSqlQuery>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolTip>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>
#include <algorithm>

class PersonPageTest final : public QObject
{
    Q_OBJECT

private slots:
    void pageSwitchDoesNotFocusNameFilter_data();
    void pageSwitchDoesNotFocusNameFilter();
    void filtersActressesAndRespectsFavoriteScope();
    void filterBarsShareGeometry();
    void actressDetailDatesAlign();
    void detailAndEditorCompletePersonRoundTrip();
    void editorLayoutsPersistSeparately();
    void actressTimelineSupportsPreviewAndMiddleButtonPan();
    void actressTimelineScrollsWithHiddenBars();
    void actressTimelineFitsAfterLayout();
    void actressTimelineStacksSameDayWorksBelowAxis();
    void actressTimelineZoomKeepsPointerAnchor();
    void actressTimelineFollowsTheme();
};

void PersonPageTest::editorLayoutsPersistSeparately()
{
    using namespace darkeye::myads;
    QTemporaryDir data;
    QVERIFY(data.isValid());
    darkeye::SqliteConnection connection;
    QVERIFY(connection.open(data.filePath(QStringLiteral("public.db")), false));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public));
    darkeye::ThemeService themes(*qApp);
    const QString actorPath = data.filePath(QStringLiteral("actor.json"));
    const QString actressPath = data.filePath(QStringLiteral("actress.json"));
    const QString names = QStringLiteral("names");
    const QString actions = QStringLiteral("actions");
    const QString notes = QStringLiteral("notes");
    const QString links = QStringLiteral("external_links");
    const QString navPath = data.filePath(QStringLiteral("nav.json"));

    QList<int> actorSplitterSizes;
    {
        darkeye::ModifyActorPage editor(connection.database(), themes, data.path());
        editor.enableLayoutPersistence(actorPath);
        editor.resize(1000, 720);
        editor.show();
        QCoreApplication::processEvents();

        auto *workspace = editor.findChild<WorkspaceWidget *>();
        auto *namesPane = workspace->findPaneByContentId(names);
        auto *actionsPane = workspace->findPaneByContentId(actions);
        QVERIFY(workspace->moveContent(namesPane->paneId(), names, actionsPane->paneId(), DropZone::Center));
        QCoreApplication::processEvents();
        auto *splitter = qobject_cast<QSplitter *>(workspace->findPaneByContentId(notes)->parentWidget());
        QVERIFY(splitter);
        splitter->setSizes({350, 450, 200});
        const auto defaultLayout = workspace->layoutTree().toJson();
        QVERIFY(QMetaObject::invokeMethod(splitter, "splitterMoved", Qt::DirectConnection,
                                          Q_ARG(int, 350), Q_ARG(int, 1)));
        QVERIFY(workspace->layoutTree().toJson() != defaultLayout);
        actorSplitterSizes = splitter->sizes();
    }
    QVERIFY(QFileInfo::exists(actorPath));

    darkeye::ModifyActressPage actress(connection.database(), themes, data.path(), nullptr, {}, navPath);
    actress.enableLayoutPersistence(actressPath);
    QVERIFY(!actress.isInitialized());
    actress.initialize();
    auto *actressWorkspace = actress.findChild<WorkspaceWidget *>();
    QVERIFY(actressWorkspace->findPaneByContentId(names) != actressWorkspace->findPaneByContentId(actions));
    auto *linksPane = actressWorkspace->findPaneByContentId(links);
    auto *notesPane = actressWorkspace->findPaneByContentId(notes);
    QVERIFY(linksPane);
    QVERIFY(actressWorkspace->moveContent(linksPane->paneId(), links, notesPane->paneId(), DropZone::Center));
    QTRY_VERIFY(QFileInfo::exists(actressPath));

    darkeye::ModifyActorPage restoredActor(connection.database(), themes, data.path());
    restoredActor.enableLayoutPersistence(actorPath);
    restoredActor.resize(1000, 720);
    restoredActor.show();
    QCoreApplication::processEvents();
    auto *actorWorkspace = restoredActor.findChild<WorkspaceWidget *>();
    QCOMPARE(actorWorkspace->findPaneByContentId(names), actorWorkspace->findPaneByContentId(actions));
    auto *restoredNotes = actorWorkspace->findPaneByContentId(notes);
    QVERIFY(restoredNotes);
    auto *restoredSplitter = qobject_cast<QSplitter *>(restoredNotes->parentWidget());
    QVERIFY(restoredSplitter);
    const auto restoredSizes = restoredSplitter->sizes();
    QCOMPARE(restoredSizes.size(), actorSplitterSizes.size());
    for (qsizetype i = 0; i < restoredSizes.size(); ++i)
        QVERIFY(qAbs(restoredSizes.at(i) - actorSplitterSizes.at(i)) <= 2);

    darkeye::ModifyActressPage restoredActress(connection.database(), themes, data.path(), nullptr, {}, navPath);
    restoredActress.enableLayoutPersistence(actressPath);
    QVERIFY(!restoredActress.isInitialized());
    restoredActress.initialize();
    auto *restoredActressWorkspace = restoredActress.findChild<WorkspaceWidget *>();
    QVERIFY(restoredActressWorkspace->findPaneByContentId(links));
    QCOMPARE(restoredActressWorkspace->findPaneByContentId(links),
             restoredActressWorkspace->findPaneByContentId(notes));

    darkeye::PersonRepository people(connection.database());
    const auto actorId = people.create(darkeye::PersonKind::Actor, QStringLiteral("男演员"), {});
    const auto actressId = people.create(darkeye::PersonKind::Actress, QStringLiteral("女演员"), {});
    QVERIFY(actorId && actressId);
    darkeye::PersonEditorPage sharedEditor(connection.database(), themes, data.path());
    const auto hasField = [&sharedEditor](const QString &id) {
        const auto fields = sharedEditor.findChildren<QComboBox *>();
        return std::any_of(fields.cbegin(), fields.cend(), [&id](const QComboBox *field) {
            return field->property("testId").toString() == id;
        });
    };
    const auto handsome = QStringLiteral("PersonHandsomeInput");
    const auto cup = QStringLiteral("PersonCupInput");
    QVERIFY(sharedEditor.loadPerson(darkeye::PersonKind::Actor, *actorId));
    QVERIFY(hasField(handsome));
    QVERIFY(!hasField(cup));
    QVERIFY(sharedEditor.loadPerson(darkeye::PersonKind::Actress, *actressId));
    QVERIFY(!hasField(handsome));
    QVERIFY(hasField(cup));
    QVERIFY(sharedEditor.loadPerson(darkeye::PersonKind::Actor, *actorId));
    QVERIFY(hasField(handsome));
    QVERIFY(!hasField(cup));
}

void PersonPageTest::actressTimelineFollowsTheme()
{
    darkeye::ThemeService themes(*qApp);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    darkeye::ActressWorkTimeline timeline(nullptr, {}, {}, &themes);
    timeline.resize(640, 180);
    timeline.setWorks({{1, QStringLiteral("TEST"), {}, QStringLiteral("2020-05-01")}});
    timeline.show();
    QCoreApplication::processEvents();

    auto *canvas = timeline.findChild<QWidget *>(QStringLiteral("ActressWorkTimelineCanvas"));
    QVERIFY(canvas);
    const auto sample = [canvas](const QPoint &point) {
        return canvas->grab(QRect(point, QSize(1, 1))).toImage().pixelColor(0, 0);
    };
    for (const auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Red, darkeye::ThemeId::Dark}) {
        QVERIFY(themes.setTheme(theme));
        const auto tokens = themes.currentTokens();
        QCOMPARE(sample(QPoint(2, 2)), QColor(tokens.inputBackground));
        QCOMPARE(sample(QPoint(2, 70)), QColor(tokens.pageBackground));
        QCOMPARE(sample(timeline.markerCenter(1)), QColor(tokens.primary));
    }
    QVERIFY(themes.setTheme(darkeye::ThemeId::Dark, QStringLiteral("#ab1234")));
    QCOMPARE(sample(timeline.markerCenter(1)), QColor(QStringLiteral("#ab1234")));
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
}

void PersonPageTest::filterBarsShareGeometry()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    QVERIFY2(publicConnection.open(QDir(temporaryDirectory.path()).filePath("public.db"),
                                   false, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(privateConnection.open(QDir(temporaryDirectory.path()).filePath("private.db"),
                                    false, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 publicConnection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 privateConnection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    darkeye::ThemeService themes(*application);
    darkeye::PersonRepository people(publicConnection.database());
    QVERIFY(people.create(darkeye::PersonKind::Actress, QStringLiteral("女演员"), {}));
    QVERIFY(people.create(darkeye::PersonKind::Actor, QStringLiteral("男演员"), {}));
    for (const auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark}) {
        QVERIFY(themes.setTheme(theme));
        darkeye::PersonPage actress(darkeye::PersonKind::Actress,
            publicConnection.database(), privateConnection.database(), themes);
        darkeye::PersonPage actor(darkeye::PersonKind::Actor,
            publicConnection.database(), privateConnection.database(), themes);
        for (auto *page : {&actress, &actor}) {
            page->initialize();
            page->resize(1200, 600);
            page->show();
        }
        QApplication::processEvents();
        const auto rect = [](QWidget *control, QWidget *page) {
            return QRect(control->mapTo(page, QPoint()), control->size());
        };
        auto *femaleInput = actress.findChild<darkeye::CompleterLineEdit *>();
        auto *maleInput = actor.findChild<darkeye::CompleterLineEdit *>();
        QVERIFY(femaleInput && maleInput);
        QCOMPARE(rect(femaleInput, &actress), rect(maleInput, &actor));
        for (auto *page : {&actress, &actor}) {
            auto *bar = page->findChild<QWidget *>(QStringLiteral("PersonFilterBar"));
            QVERIFY(bar);
            QCOMPARE(bar->y(), 0);
            QCOMPARE(bar->height(), 34);
            auto *input = page->findChild<darkeye::CompleterLineEdit *>();
            const auto inputRect = rect(input, bar);
            QCOMPARE(inputRect.top(), 0);
            QCOMPARE(bar->height() - inputRect.y() - inputRect.height(), 2);
            QCOMPARE(input->height(), input->sizeHint().height());
            for (auto *combo : page->findChildren<QComboBox *>()) {
                if (combo->isHidden()) continue;
                const auto comboRect = rect(combo, bar);
                QCOMPARE(comboRect.top(), 0);
                QCOMPARE(bar->height() - comboRect.y() - comboRect.height(), 2);
                QCOMPARE(combo->height(), combo->sizeHint().height());
            }
            auto *area = page->findChild<QScrollArea *>(QStringLiteral("DesignLazyScrollArea"));
            QVERIFY(area);
            QCOMPARE(rect(area->viewport(), page).top(), bar->geometry().bottom() + 1);
            auto *avatar = page->findChild<darkeye::OctImage *>(QStringLiteral("PersonCardAvatar"));
            QVERIFY(avatar);
            QCOMPARE(rect(avatar, area->viewport()).top(), 0);
            QVERIFY(avatar->containsAvatarPoint(QPointF(avatar->width() / 2.0, 0.5)));

            const auto preview = qEnvironmentVariable("DARKEYE_PERSON_BAR_PREVIEW");
            if (!preview.isEmpty()) {
                const auto name = page->kind() == darkeye::PersonKind::Actress ? "actress" : "actor";
                const auto mode = theme == darkeye::ThemeId::Light ? "light" : "dark";
                QVERIFY(bar->grab().save(preview + '/' + name + '-' + mode + ".png"));
            }
        }
        const auto femaleCombos = actress.findChildren<QComboBox *>();
        const auto maleCombos = actor.findChildren<QComboBox *>();
        QCOMPARE(femaleCombos.size(), 3);
        QCOMPARE(maleCombos.size(), 1);
        QCOMPARE(rect(femaleCombos.last(), &actress), rect(maleCombos.first(), &actor));
        for (const auto *combo : femaleCombos)
            QVERIFY(combo->height() >= combo->minimumSizeHint().height());
        auto *femaleArea = actress.findChild<QScrollArea *>(QStringLiteral("DesignLazyScrollArea"));
        auto *maleArea = actor.findChild<QScrollArea *>(QStringLiteral("DesignLazyScrollArea"));
        QVERIFY(femaleArea && maleArea);
        QCOMPARE(rect(femaleArea, &actress), rect(maleArea, &actor));
    }
}

void PersonPageTest::pageSwitchDoesNotFocusNameFilter_data()
{
    QTest::addColumn<bool>("actress");
    QTest::newRow("actress") << true;
    QTest::newRow("actor") << false;
}

void PersonPageTest::pageSwitchDoesNotFocusNameFilter()
{
    QFETCH(bool, actress);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    QVERIFY2(publicConnection.open(QDir(temporaryDirectory.path()).filePath("public.db"),
                                   false, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(privateConnection.open(QDir(temporaryDirectory.path()).filePath("private.db"),
                                    false, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 publicConnection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 privateConnection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    darkeye::ThemeService themes(*application);
    darkeye::myads::PaneWidget pane(QStringLiteral("people"));
    auto *otherPage = new QLineEdit;
    QVERIFY(pane.addContent(QStringLiteral("other"), QStringLiteral("Other"), otherPage));
    auto *page = new darkeye::PersonPage(
        actress ? darkeye::PersonKind::Actress : darkeye::PersonKind::Actor,
        publicConnection.database(), privateConnection.database(), themes);
    QVERIFY(pane.addContent(QStringLiteral("people"), QStringLiteral("People"), page));
    QVERIFY(pane.setCurrentContentId(QStringLiteral("other")));
    pane.resize(800, 500);
    pane.show();
    pane.activateWindow();
    otherPage->setFocus();
    QVERIFY(pane.setCurrentContentId(QStringLiteral("people")));
    QApplication::processEvents();
    auto *name = page->findChild<darkeye::CompleterLineEdit *>();
    QVERIFY(name != nullptr);
    QVERIFY(!name->hasFocus());
    QVERIFY(!name->completer()->popup()->isVisible());

    for (int i = 0; i < 3; ++i)
    {
        QVERIFY(pane.setCurrentContentId(QStringLiteral("other")));
        otherPage->setFocus();
        QVERIFY(pane.setCurrentContentId(QStringLiteral("people")));
        QApplication::processEvents();
        QVERIFY(!name->hasFocus());
        QVERIFY(!name->completer()->popup()->isVisible());
    }

    QTest::mouseClick(name, Qt::LeftButton);
    QTRY_VERIFY(name->hasFocus());
    QTest::keyClicks(name, "test");
    QCOMPARE(name->text(), QStringLiteral("test"));
    name->completer()->popup()->hide();
    QVERIFY(pane.setCurrentContentId(QStringLiteral("other")));
    otherPage->setFocus();
    QVERIFY(pane.setCurrentContentId(QStringLiteral("people")));
    QApplication::processEvents();
    QVERIFY(!name->hasFocus());
    QVERIFY(!name->completer()->popup()->isVisible());
    QCOMPARE(name->text(), QStringLiteral("test"));
}

void PersonPageTest::actressTimelineSupportsPreviewAndMiddleButtonPan()
{
    darkeye::ActressWorkTimeline timeline;
    timeline.resize(760, 180);
    darkeye::PersonWorkSummary work;
    work.id = 42;
    work.serialNumber = QStringLiteral("TIMELINE-042");
    work.title = QStringLiteral("时间线悬浮作品");
    work.releaseDate = QStringLiteral("2020-06-18");
    work.highlightTagId = 2;
    work.standard = true;
    timeline.setWorks({work});
    timeline.show();
    QApplication::processEvents();

    auto *canvas = timeline.findChild<QWidget *>(QStringLiteral("ActressWorkTimelineCanvas"));
    auto *scroll = timeline.findChild<QScrollArea *>();
    QVERIFY(canvas != nullptr);
    QVERIFY(scroll != nullptr);
    QTRY_VERIFY(scroll->horizontalScrollBar()->value() > 0);
    const QPoint marker = timeline.markerCenter(work.id);
    QVERIFY(!marker.isNull());
    QMouseEvent hover(QEvent::MouseMove, marker, canvas->mapToGlobal(marker),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &hover);
    QTRY_VERIFY(timeline.hoverPreviewVisible());

    // A delayed Qt tooltip request must keep the standard card, without adding
    // a second plain-text popup over its serial number, cover and title.
    QHelpEvent help(QEvent::ToolTip, marker, canvas->mapToGlobal(marker));
    QApplication::sendEvent(canvas, &help);
    QVERIFY(timeline.hoverPreviewVisible());
    QVERIFY(!QToolTip::isVisible());
    darkeye::WorkCard *preview = nullptr;
    int visiblePreviews = 0;
    for (QWidget *window : QApplication::topLevelWidgets()) {
        auto *card = window->findChild<darkeye::WorkCard *>();
        if (card && card->isVisible() && card->workId() == work.id) {
            preview = card;
            ++visiblePreviews;
        }
    }
    QCOMPARE(visiblePreviews, 1);
    QVERIFY(preview);
    auto *popup = preview->window();
    QImage previewImage(popup->size(), QImage::Format_ARGB32_Premultiplied);
    previewImage.fill(Qt::transparent);
    popup->render(&previewImage);
    const QRect previewRect = previewImage.rect();
    QCOMPARE(previewImage.pixelColor(previewRect.topLeft()).alpha(), 0);
    QCOMPARE(previewImage.pixelColor(previewRect.topRight()).alpha(), 0);
    QCOMPARE(previewImage.pixelColor(previewRect.bottomLeft()).alpha(), 0);
    QCOMPARE(previewImage.pixelColor(previewRect.bottomRight()).alpha(), 0);
    QCOMPARE(previewImage.pixelColor(QPoint(previewRect.center().x(), 3)),
             QColor(QStringLiteral("#FFA475")));
    auto *serial = preview->findChild<QLabel *>(QStringLiteral("WorkCardSerialNumber"));
    QVERIFY(serial);
    QCOMPARE(serial->text(), work.serialNumber);
    QVERIFY(preview->findChild<darkeye::AsyncImageLabel *>(QStringLiteral("WorkCardCover")));
    bool titleVisible = false;
    for (auto *label : preview->findChildren<QLabel *>()) {
        if (label->text() == work.title && label->isVisible()) titleVisible = true;
    }
    QVERIFY(titleVisible);

    const int before = scroll->horizontalScrollBar()->value();
    const QPoint dragStart(qBound(20, marker.x(), canvas->width() - 20), marker.y());
    QTest::mousePress(canvas, Qt::MiddleButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(canvas, dragStart - QPoint(50, 0), 20);
    QTest::mouseRelease(canvas, Qt::MiddleButton, Qt::NoModifier,
                        dragStart - QPoint(50, 0));
    QVERIFY(scroll->horizontalScrollBar()->value() > before);
    QVERIFY(!timeline.hoverPreviewVisible());
}

void PersonPageTest::actressTimelineScrollsWithHiddenBars()
{
    darkeye::ActressWorkTimeline timeline;
    timeline.resize(760, 130);
    QList<darkeye::PersonWorkSummary> works;
    for (int id = 1; id <= 12; ++id) {
        darkeye::PersonWorkSummary work;
        work.id = id;
        work.releaseDate = QStringLiteral("2020-06-18");
        works.append(work);
    }
    timeline.setWorks(works);
    timeline.show();

    auto *scroll = timeline.findChild<QScrollArea *>();
    QVERIFY(scroll);
    auto *horizontal = scroll->horizontalScrollBar();
    auto *vertical = scroll->verticalScrollBar();
    QTRY_VERIFY(horizontal->value() > 0);
    QVERIFY(vertical->maximum() > 0);
    QVERIFY(!horizontal->isVisible());
    QVERIFY(!vertical->isVisible());

    auto *canvas = scroll->widget();
    const QPoint position = canvas->mapFromGlobal(scroll->viewport()->mapToGlobal(QPoint(20, 20)));
    const auto scrollWheel = [canvas, position](Qt::KeyboardModifier modifier) {
        QWheelEvent event(position, canvas->mapToGlobal(position), QPoint(), QPoint(0, -120),
                          Qt::NoButton, modifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas, &event);
    };
    const int initialX = horizontal->value();
    const auto rulerImage = [scroll] {
        auto *viewport = scroll->viewport();
        return viewport->grab(QRect(0, 0, viewport->width(), 36)).toImage();
    };
    const auto initialRuler = rulerImage();
    scrollWheel(Qt::ControlModifier);
    QVERIFY(horizontal->value() > initialX);
    const auto shiftedRuler = rulerImage();
    QVERIFY(shiftedRuler != initialRuler);
    const int initialCanvasY = canvas->y();
    scrollWheel(Qt::ShiftModifier);
    QVERIFY(vertical->value() > 0);
    QVERIFY(canvas->y() < initialCanvasY);
    QCOMPARE(rulerImage(), shiftedRuler);
    vertical->setValue(0);
    QCOMPARE(rulerImage(), shiftedRuler);
}

void PersonPageTest::actressTimelineFitsAfterLayout()
{
    darkeye::ActressWorkTimeline timeline;
    darkeye::PersonWorkSummary start;
    start.id = 1;
    start.releaseDate = QStringLiteral("2016-01-01");
    auto end = start;
    end.id = 2;
    end.releaseDate = QStringLiteral("2026-12-31");
    timeline.setWorks({start, end});
    timeline.show();
    // Workspace layouts can assign the final width after the first show event.
    timeline.resize(1080, 180);
    auto *scroll = timeline.findChild<QScrollArea *>();
    QVERIFY(scroll);
    const auto visibleX = [&](qint64 id) {
        return timeline.markerCenter(id).x() - scroll->horizontalScrollBar()->value();
    };
    QTRY_VERIFY(qAbs(visibleX(start.id) - 32) <= 2);
    QTRY_VERIFY(qAbs(visibleX(end.id) - (scroll->viewport()->width() - 32)) <= 3);
    timeline.hide();
    timeline.resize(760, 180);
    timeline.setWorks({start, end});
    timeline.show();
    QTRY_VERIFY(qAbs(visibleX(start.id) - 32) <= 2);
    QTRY_VERIFY(qAbs(visibleX(end.id) - (scroll->viewport()->width() - 32)) <= 3);
}

void PersonPageTest::actressTimelineStacksSameDayWorksBelowAxis()
{
    darkeye::ActressWorkTimeline timeline;
    timeline.resize(760, 220);
    darkeye::PersonWorkSummary single, first, second, nearby;
    single.id = 1;
    single.releaseDate = QStringLiteral("2019-01-01");
    first.id = 2;
    first.releaseDate = QStringLiteral("2020-06-18");
    second.id = 3;
    second.releaseDate = QStringLiteral("2020-06-18T21:00:00");
    nearby.id = 4;
    nearby.releaseDate = QStringLiteral("2020-06-19");
    timeline.setWorks({single, first, second, nearby});
    timeline.show();

    auto *canvas = timeline.findChild<QWidget *>(QStringLiteral("ActressWorkTimelineCanvas"));
    auto *scroll = timeline.findChild<QScrollArea *>();
    QVERIFY(canvas && scroll);
    QTRY_VERIFY(scroll->horizontalScrollBar()->value() > 0);
    const QPoint firstMark = timeline.markerCenter(first.id);
    const QPoint secondMark = timeline.markerCenter(second.id);
    QCOMPARE(firstMark.x(), secondMark.x());
    QVERIFY(firstMark.y() > timeline.markerCenter(single.id).y());
    QVERIFY(secondMark.y() > firstMark.y());
    QVERIFY(timeline.markerCenter(nearby.id) != firstMark);
    QVERIFY(timeline.markerCenter(nearby.id) != secondMark);

    QSignalSpy clicked(&timeline, &darkeye::ActressWorkTimeline::workRequested);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, firstMark);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, secondMark);
    QCOMPARE(clicked.size(), 2);
    QCOMPARE(clicked.at(0).at(0).toLongLong(), first.id);
    QCOMPARE(clicked.at(1).at(0).toLongLong(), second.id);
}

void PersonPageTest::actressTimelineZoomKeepsPointerAnchor()
{
    darkeye::ActressWorkTimeline timeline;
    timeline.resize(760, 180);
    darkeye::PersonWorkSummary work;
    work.id = 42;
    work.releaseDate = QStringLiteral("2020-06-18");
    auto first = work;
    first.id = 1;
    first.releaseDate = QStringLiteral("2000-01-01");
    auto last = work;
    last.id = 2;
    last.releaseDate = QStringLiteral("2030-12-31");
    auto earlier = first;
    earlier.id = 3;
    earlier.releaseDate = QStringLiteral("1999-12-31");
    auto later = last;
    later.id = 4;
    later.releaseDate = QStringLiteral("2031-01-01");
    timeline.setWorks({work, first, last, earlier, later});
    timeline.show();
    auto *scroll = timeline.findChild<QScrollArea *>();
    auto *canvas = timeline.findChild<QWidget *>(QStringLiteral("ActressWorkTimelineCanvas"));
    QVERIFY(scroll);
    QVERIFY(canvas);
    QTRY_VERIFY(scroll->horizontalScrollBar()->value() > 0);
    QCOMPARE(timeline.markerCount(), 3);
    QVERIFY(timeline.markerCenter(earlier.id).isNull());
    QVERIFY(timeline.markerCenter(later.id).isNull());
    QApplication::processEvents();
    const auto visibleX = [&] {
        return timeline.markerCenter(work.id).x() - scroll->horizontalScrollBar()->value();
    };
    const auto zoom = [&](int delta) {
        const QPoint position(timeline.markerCenter(work.id));
        QWheelEvent event(position, canvas->mapToGlobal(position), QPoint(), QPoint(0, delta),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas, &event);
        QApplication::processEvents();
    };
    const double initialScale = timeline.pixelsPerDay();
    zoom(0);
    QCOMPARE(timeline.pixelsPerDay(), initialScale);
    zoom(60);
    QVERIFY(qAbs(timeline.pixelsPerDay() / initialScale - qSqrt(1.2)) < 1e-6);
    zoom(-60);
    for (int i = 0; i < 12; ++i) {
        const int anchorX = visibleX();
        zoom(120);
        QVERIFY(qAbs(visibleX() - anchorX) <= 2);
    }
    for (int i = 0; i < 50; ++i) {
        const int anchorX = visibleX();
        zoom(-120);
        auto *bar = scroll->horizontalScrollBar();
        const int markerX = timeline.markerCenter(work.id).x();
        const int boundedAnchor = qBound(markerX - bar->maximum(), anchorX, markerX);
        QVERIFY(qAbs(visibleX() - boundedAnchor) <= 2);
        QVERIFY(canvas->width() >= scroll->viewport()->width());
    }
    const double minimumScale = timeline.pixelsPerDay();
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    const int firstX = timeline.markerCenter(first.id).x();
    const int lastX = timeline.markerCenter(last.id).x();
    QVERIFY(firstX >= 0);
    QVERIFY(lastX > firstX);
    QVERIFY(lastX < scroll->viewport()->width());
    zoom(-120);
    QCOMPARE(timeline.pixelsPerDay(), minimumScale);
    for (int i = 0; i < 90; ++i) zoom(120);
    QVERIFY(canvas->width() < QWIDGETSIZE_MAX);
    const int maximumAnchor = visibleX();
    zoom(120);
    QVERIFY(qAbs(visibleX() - maximumAnchor) <= 2);
}

void PersonPageTest::filtersActressesAndRespectsFavoriteScope()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    QVERIFY2(
        publicConnection.open(QDir(temporaryDirectory.path()).filePath(QStringLiteral("public.db")),
                              false, &errorMessage),
        qPrintable(errorMessage));
    QVERIFY2(privateConnection.open(
                 QDir(temporaryDirectory.path()).filePath(QStringLiteral("private.db")), false,
                 &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 publicConnection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 privateConnection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    darkeye::PersonRepository people(publicConnection.database());
    const auto favoriteId = people.create(darkeye::PersonKind::Actress, QStringLiteral("收藏演员"),
                                          QStringLiteral("お気に入り"), &errorMessage);
    const auto otherId = people.create(darkeye::PersonKind::Actress, QStringLiteral("其他演员"),
                                       QStringLiteral("その他"), &errorMessage);
    QVERIFY2(favoriteId.has_value() && otherId.has_value(), qPrintable(errorMessage));
    darkeye::PrivateRepository privateData(privateConnection.database());
    QVERIFY2(
        privateData.addFavoriteActress(*favoriteId, QStringLiteral("お気に入り"), &errorMessage),
        qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::PersonPage page(darkeye::PersonKind::Actress, publicConnection.database(),
                             privateConnection.database(), themes);
    page.initialize();
    auto *area = dynamic_cast<darkeye::LazyScrollArea *>(
        page.findChild<QScrollArea *>(QStringLiteral("DesignLazyScrollArea")));
    auto *name = page.findChild<darkeye::CompleterLineEdit *>();
    QComboBox *scope = nullptr;
    for (auto *combo : page.findChildren<QComboBox *>()) {
        if (combo->findText(QStringLiteral("收藏库范围")) >= 0) {
            scope = combo;
            break;
        }
    }
    QVERIFY(area != nullptr);
    QVERIFY(name != nullptr);
    QVERIFY(scope != nullptr);
    QCOMPARE(area->itemCount(), 2);

    scope->setCurrentIndex(1);
    QTRY_COMPARE_WITH_TIMEOUT(area->itemCount(), 1, 500);
    name->setText(QStringLiteral("其他"));
    QTRY_COMPARE_WITH_TIMEOUT(area->itemCount(), 0, 500);
    scope->setCurrentIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(area->itemCount(), 1, 500);
}

void PersonPageTest::actressDetailDatesAlign()
{
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    darkeye::ThemeService themes(*application);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    darkeye::PersonInfoPanel panel(themes, {});
    darkeye::PersonDetails details;
    details.kind = darkeye::PersonKind::Actress;
    details.birthday = QStringLiteral("1991-11-11");
    details.debutDate = QStringLiteral("2008-08-28");
    panel.setDetails(details, false);
    panel.resize(380, 600);
    panel.show();

    const QLabel *birthday = nullptr;
    const QLabel *debutDate = nullptr;
    for (const auto *label : panel.findChildren<QLabel *>()) {
        if (label->text() == QStringLiteral("1991年11月11日"))
            birthday = label;
        if (label->text() == QStringLiteral("2008年08月28日"))
            debutDate = label;
    }
    QVERIFY(birthday && debutDate);
    QCOMPARE(birthday->mapTo(&panel, QPoint()).x(), debutDate->mapTo(&panel, QPoint()).x());
    const QFontMetricsF metrics(birthday->font());
    for (const QChar digit : QStringLiteral("0123456789"))
        QCOMPARE(metrics.horizontalAdvance(digit), metrics.horizontalAdvance(QChar(u'0')));
    QCOMPARE(birthday->font(), debutDate->font());
}

void PersonPageTest::detailAndEditorCompletePersonRoundTrip()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    QVERIFY2(
        publicConnection.open(QDir(temporaryDirectory.path()).filePath(QStringLiteral("public.db")),
                              false, &errorMessage),
        qPrintable(errorMessage));
    QVERIFY2(privateConnection.open(
                 QDir(temporaryDirectory.path()).filePath(QStringLiteral("private.db")), false,
                 &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 publicConnection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 privateConnection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    darkeye::PersonRepository people(publicConnection.database());
    const auto actressId = people.create(darkeye::PersonKind::Actress, QStringLiteral("详情演员"),
                                         QStringLiteral("詳細女優"), &errorMessage);
    QVERIFY2(actressId.has_value(), qPrintable(errorMessage));
    const auto referenceActressId = people.create(
        darkeye::PersonKind::Actress, QStringLiteral("参考演员"), QStringLiteral("参考女優"),
        &errorMessage);
    const auto zeroActressId = people.create(
        darkeye::PersonKind::Actress, QStringLiteral("零值演员"), QStringLiteral("零値女優"),
        &errorMessage);
    QVERIFY2(referenceActressId.has_value(), qPrintable(errorMessage));
    QVERIFY2(zeroActressId.has_value(), qPrintable(errorMessage));
    QSqlQuery fixture(publicConnection.database());
    fixture.prepare(QStringLiteral(
        "UPDATE actress SET height=?,bust=?,waist=?,hip=?,cup=? WHERE actress_id=?"));
    const auto updateBody = [&fixture](qint64 id, int height, int bust, int waist, int hip,
                                       const QString &cup) {
        fixture.bindValue(0, height);
        fixture.bindValue(1, bust);
        fixture.bindValue(2, waist);
        fixture.bindValue(3, hip);
        fixture.bindValue(4, cup);
        fixture.bindValue(5, id);
        return fixture.exec();
    };
    QVERIFY(updateBody(*actressId, 162, 88, 58, 87, QStringLiteral("F")));
    QVERIFY(updateBody(*referenceActressId, 168, 90, 60, 89, QStringLiteral("G")));
    QVERIFY(updateBody(*zeroActressId, 0, 0, 0, 0, QStringLiteral("A")));
    darkeye::WorkRepository works(publicConnection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("PERSON-001");
    work.japaneseTitle = QStringLiteral("不能回退到日文标题");
    const auto workId = works.insertComplete(work, {*actressId}, {}, {1, 2}, &errorMessage);
    QVERIFY2(workId.has_value(), qPrintable(errorMessage));
    darkeye::Work deletedWork;
    deletedWork.serialNumber = QStringLiteral("PERSON-DELETED");
    deletedWork.chineseTitle = QStringLiteral("软删除仍读取");
    const auto deletedWorkId = works.insertComplete(
        deletedWork, {*actressId}, {}, {}, &errorMessage);
    QVERIFY2(deletedWorkId.has_value(), qPrintable(errorMessage));
    QVERIFY2(works.setDeleted(*deletedWorkId, true, &errorMessage), qPrintable(errorMessage));

    const auto sqlAligned = people.findDetails(darkeye::PersonKind::Actress, *actressId,
                                               &errorMessage);
    QVERIFY2(sqlAligned.has_value(), qPrintable(errorMessage));
    QCOMPARE(sqlAligned->bodyReference.size(), 2);
    QCOMPARE(sqlAligned->works.size(), 3);
    QSet<int> timelineTags;
    bool foundEmptyChineseTitle = false;
    bool foundDeletedWork = false;
    for (const auto &item : sqlAligned->works)
    {
        if (item.serialNumber == work.serialNumber)
            foundEmptyChineseTitle = item.title.isEmpty();
        if (item.serialNumber == deletedWork.serialNumber)
            foundDeletedWork = true;
        if (item.highlightTagId > 0) timelineTags.insert(item.highlightTagId);
    }
    QVERIFY(foundEmptyChineseTitle);
    QVERIFY(foundDeletedWork);
    QCOMPARE(timelineTags, QSet<int>({1, 2}));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::PersonDetailPage detail(publicConnection.database(),
                                     privateConnection.database(), themes);
    QVERIFY(detail.showPerson(*actressId));
    QCOMPARE(detail.currentPersonId(), *actressId);
    auto *panel = detail.findChild<darkeye::PersonInfoPanel *>(QStringLiteral("PersonInfoPanel"));
    auto *workTable = detail.findChild<QTableWidget *>(QStringLiteral("PersonWorkTable"));
    auto *timeline = detail.findChild<darkeye::ActressWorkTimeline *>(
        QStringLiteral("ActressWorkTimeline"));
    auto *heart = detail.findChild<darkeye::HeartLabel *>(QStringLiteral("PersonFavoriteButton"));
    auto *radar = detail.findChild<QWidget *>(QStringLiteral("DesignRadarChart"));
    auto *portrait = detail.findChild<QWidget *>(QStringLiteral("PersonDetailAvatar"));
    auto *scroll = detail.findChild<QScrollArea *>();
    auto *aliases = detail.findChild<darkeye::TokenCollapsibleSection *>();
    QVERIFY(panel != nullptr);
    QVERIFY(workTable != nullptr);
    QVERIFY(timeline == nullptr);
    QVERIFY(heart != nullptr);
    QVERIFY(radar != nullptr);
    QVERIFY(portrait != nullptr);
    QVERIFY(scroll != nullptr);
    QVERIFY(aliases != nullptr);
    auto *aliasToggle = aliases->findChild<QToolButton *>();
    QVERIFY(aliasToggle != nullptr);
    detail.resize(800, 700);
    detail.show();
    QCoreApplication::processEvents();
    QVERIFY(aliases->graphicsEffect() == nullptr);
    QVERIFY(aliasToggle->graphicsEffect() == nullptr);
    QVERIFY(panel->height() > panel->width());
    QVERIFY(qAbs(panel->geometry().center().x() - scroll->widget()->rect().center().x()) <= 1);
    QVERIFY(portrait->geometry().bottom() < radar->geometry().top());
    QCOMPARE(portrait->geometry().center().x(), radar->geometry().center().x());
    QVERIFY(radar->geometry().bottom() < aliases->geometry().top());
    QCOMPARE(aliasToggle->text(), QStringLiteral("别名（0）"));
    QVERIFY(!aliases->isExpanded());
    detail.resize(800, scroll->widget()->sizeHint().height() + 4);
    QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
    const QRect collapsedToggle(aliasToggle->mapTo(&detail, QPoint()), aliasToggle->size());
    aliasToggle->click();
    QVERIFY(aliases->contentWidget()->isVisible());
    QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    QCOMPARE(QRect(aliasToggle->mapTo(&detail, QPoint()), aliasToggle->size()), collapsedToggle);
    auto *emptyAliases = aliases->contentWidget()->findChild<QLabel *>();
    QVERIFY(emptyAliases && emptyAliases->isVisible());
    QCOMPARE(emptyAliases->text(), QStringLiteral("暂无别名"));
    auto *verticalBar = scroll->verticalScrollBar();
    verticalBar->setValue(verticalBar->maximum());
    QVERIFY(verticalBar->value() > 0);
    QTest::mouseClick(aliasToggle, Qt::LeftButton);
    QVERIFY(!aliases->contentWidget()->isVisible());
    QCOMPARE(verticalBar->maximum(), 0);
    QCOMPARE(verticalBar->value(), 0);
    QCOMPARE(QRect(aliasToggle->mapTo(&detail, QPoint()), aliasToggle->size()), collapsedToggle);
    const int wideCardWidth = panel->width();
    detail.resize(340, 330);
    QCoreApplication::processEvents();
    QVERIFY(panel->width() < wideCardWidth);
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);

    QCOMPARE(workTable->rowCount(), 3);
    auto *radarChart = static_cast<darkeye::RadarChartWidget *>(radar);
    QCOMPARE(radarChart->categories(),
             QStringList({QStringLiteral("身高"), QStringLiteral("罩杯"),
                          QStringLiteral("胸围"), QStringLiteral("腰围"),
                          QStringLiteral("臀围")}));
    QCOMPARE(radarChart->values().size(), 5);
    QCOMPARE(workTable->item(0, 0)->text(), QStringLiteral("PERSON-001"));
    QTest::mouseClick(heart, Qt::LeftButton);
    darkeye::PrivateRepository privateData(privateConnection.database());
    QVERIFY(privateData.isFavoriteActress(*actressId));
    QVERIFY(detail.showPerson(*actressId));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(detail.findChild<darkeye::HeartLabel *>(QStringLiteral("PersonFavoriteButton")),
             heart);

    const QString imageDirectory =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("actressimages"));
    darkeye::ModifyActressPage editor(publicConnection.database(), themes, imageDirectory);
    QVERIFY(editor.loadActress(*actressId));
    QComboBox *cup = nullptr;
    for (QComboBox *combo : editor.findChildren<QComboBox *>())
    {
        if (combo->property("testId") == QStringLiteral("PersonCupInput"))
            cup = combo;
    }
    QVERIFY(cup != nullptr);
    QVERIFY(!cup->isEditable());
    QCOMPARE(cup->count(), 13);
    for (int index = 0; index < cup->count(); ++index)
        QCOMPARE(cup->itemText(index), QString(QChar('A' + index)));
    QJsonObject alias;
    alias.insert(QStringLiteral("jp"), QStringLiteral("采集艺名"));
    alias.insert(QStringLiteral("kana"), QStringLiteral("げいめい"));
    alias.insert(QStringLiteral("en"), QStringLiteral("Captured Alias"));
    QJsonObject captureData;
    captureData.insert(QStringLiteral("日文名"), QStringLiteral("采集日文名"));
    captureData.insert(QStringLiteral("假名"), QStringLiteral("さいしゅう"));
    captureData.insert(QStringLiteral("英文名"), QStringLiteral("Captured Name"));
    captureData.insert(QStringLiteral("身高"), 166);
    captureData.insert(QStringLiteral("胸围"), 88);
    captureData.insert(QStringLiteral("腰围"), 57);
    captureData.insert(QStringLiteral("臀围"), 86);
    captureData.insert(QStringLiteral("罩杯"), QStringLiteral("F"));
    captureData.insert(QStringLiteral("出生日期"), QStringLiteral("2000-01-02"));
    captureData.insert(QStringLiteral("出道日期"), QStringLiteral("2020-03-04"));
    captureData.insert(QStringLiteral("minnano_actress_id"), QStringLiteral("12345"));
    captureData.insert(QStringLiteral("alias_chain"), QJsonArray{alias});
    QJsonObject context;
    context.insert(QStringLiteral("actress_id"), *actressId);
    QJsonObject capture;
    capture.insert(QStringLiteral("context"), context);
    capture.insert(QStringLiteral("data"), captureData);
    QString captureError;
    QVERIFY2(editor.applyCapture(capture, &captureError), qPrintable(captureError));
    QCOMPARE(cup->currentText(), QStringLiteral("F"));
    QSpinBox *capturedHeight = nullptr;
    QLineEdit *capturedMinnano = nullptr;
    for (QSpinBox *spinBox : editor.findChildren<QSpinBox *>())
    {
        if (spinBox->property("testId") == QStringLiteral("PersonHeightInput"))
            capturedHeight = spinBox;
    }
    for (QLineEdit *lineEdit : editor.findChildren<QLineEdit *>())
    {
        if (lineEdit->property("testId") == QStringLiteral("PersonMinnanoInput"))
            capturedMinnano = lineEdit;
    }
    QVERIFY(capturedHeight != nullptr);
    QCOMPARE(capturedHeight->value(), 166);
    QVERIFY(capturedMinnano != nullptr);
    QCOMPARE(capturedMinnano->text(), QStringLiteral("12345"));
    auto *capturedNames = editor.findChild<QTableWidget *>(QStringLiteral("DesignTableWidget"));
    QVERIFY(capturedNames != nullptr);
    QCOMPARE(capturedNames->item(0, 1)->text(), QStringLiteral("采集日文名"));
    QCOMPARE(capturedNames->item(1, 1)->text(), QStringLiteral("采集艺名"));
    QVERIFY(editor.loadActress(*actressId));
    const auto uncommitted =
        people.findDetails(darkeye::PersonKind::Actress, *actressId, &errorMessage);
    QVERIFY2(uncommitted.has_value(), qPrintable(errorMessage));
    QVERIFY(uncommitted->height != 166);
    auto *nameTable = editor.findChild<QTableWidget *>(QStringLiteral("DesignTableWidget"));
    QPushButton *addName = nullptr;
    QPushButton *moveNameUp = nullptr;
    for (QPushButton *button : editor.findChildren<QPushButton *>())
    {
        if (button->property("testId") == QStringLiteral("PersonAddNameButton"))
            addName = button;
        if (button->property("testId") == QStringLiteral("PersonMoveNameUpButton"))
            moveNameUp = button;
    }
    auto *notes = editor.findChild<darkeye::WikiTextEdit *>();
    auto *imageDrop =
        editor.findChild<darkeye::ImageDropWidget *>(QStringLiteral("PersonImageDropWidget"));
    QVERIFY(nameTable != nullptr);
    QVERIFY(addName != nullptr);
    QVERIFY(moveNameUp != nullptr);
    QVERIFY(notes != nullptr);
    QVERIFY(notes->completerList().contains(QStringLiteral("PERSON-001")));
    QVERIFY(imageDrop != nullptr);
    QPushButton *save = nullptr;
    for (QPushButton *button : editor.findChildren<QPushButton *>())
    {
        if (button->property("testId") == QStringLiteral("PersonSaveButton"))
            save = button;
    }
    QVERIFY(save != nullptr);
    QVERIFY(!save->isEnabled());
    const QString originalCup = cup->currentText();
    cup->setCurrentText(QStringLiteral("M"));
    QVERIFY(save->isEnabled());
    QVERIFY(cup->property("personModified").toBool());
    cup->setCurrentIndex(cup->findText(originalCup));
    QVERIFY(!save->isEnabled());
    QVERIFY(!cup->property("personModified").toBool());
    cup->setCurrentText(QStringLiteral("M"));
    nameTable->item(0, 0)->setText(QStringLiteral("修改后姓名"));
    QVERIFY(save->isEnabled());
    addName->click();
    nameTable->item(1, 1)->setText(QStringLiteral("旧艺名"));
    nameTable->selectRow(1);
    moveNameUp->click();
    notes->setPlainText(QStringLiteral("修改后的备注"));
    const QString sourceImage =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("source-avatar.png"));
    QImage avatar(24, 32, QImage::Format_ARGB32);
    avatar.fill(QColor(80, 120, 200, 180));
    QVERIFY(avatar.save(sourceImage));
    imageDrop->setImagePath(sourceImage);
    QSignalSpy savedSignal(&editor, &darkeye::ModifyActressPage::personSaved);
    QVERIFY(editor.savePerson());
    QCOMPARE(savedSignal.count(), 1);
    QVERIFY(!save->isEnabled());

    const auto saved = people.findDetails(darkeye::PersonKind::Actress, *actressId, &errorMessage);
    QVERIFY2(saved.has_value(), qPrintable(errorMessage));
    QCOMPARE(saved->names.size(), 2);
    QCOMPARE(saved->names.first().japanese, QStringLiteral("旧艺名"));
    QCOMPARE(saved->names.last().chinese, QStringLiteral("修改后姓名"));
    QCOMPARE(saved->notes, QStringLiteral("修改后的备注"));
    QCOMPARE(saved->cup, QStringLiteral("M"));
    QCOMPARE(cup->currentText(), QStringLiteral("M"));
    QVERIFY(cup->styleSheet().isEmpty());
    QCOMPARE(saved->imagePath, QStringLiteral("%1-旧艺名.jpg").arg(*actressId));
    const QString storedImage = QDir(imageDirectory).filePath(saved->imagePath);
    QVERIFY(QFileInfo::exists(storedImage));
    QVERIFY(!QImage(storedImage).isNull());
    QVERIFY(detail.showPerson(*actressId));
    QCOMPARE(workTable->rowCount(), 3);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(aliasToggle->text(), QStringLiteral("别名（1）"));
    QVERIFY(!aliases->isExpanded());
    for (const auto *label : panel->findChildren<QLabel *>())
    {
        if (label->text() == QStringLiteral("修改后姓名"))
            QVERIFY(aliases->contentWidget()->isAncestorOf(label));
    }
    QCoreApplication::processEvents();
    const int collapsedScrollLimit = verticalBar->maximum();
    aliasToggle->click();
    QVERIFY(aliases->contentWidget()->isVisible());
    const auto aliasLabels = aliases->contentWidget()->findChildren<QLabel *>();
    QVERIFY(std::any_of(aliasLabels.cbegin(), aliasLabels.cend(), [](const QLabel *label) {
        return label->isVisible() && label->text() == QStringLiteral("修改后姓名");
    }));
    QTRY_VERIFY(verticalBar->maximum() > collapsedScrollLimit);
    const int expandedScrollLimit = verticalBar->maximum();
    verticalBar->setValue(expandedScrollLimit);
    QVERIFY(scroll->viewport()->rect().contains(aliasToggle->mapTo(
        scroll->viewport(), aliasToggle->rect().center())));
    QTest::mouseClick(aliasToggle, Qt::LeftButton);
    QVERIFY(!aliases->isExpanded());
    const int newScrollLimit = verticalBar->maximum();
    QVERIFY(newScrollLimit < expandedScrollLimit);
    QCOMPARE(verticalBar->value(), newScrollLimit);
    QCoreApplication::processEvents();
    QCOMPARE(verticalBar->value(), newScrollLimit);
    QCOMPARE(verticalBar->maximum(), newScrollLimit);
    aliasToggle->click();
    QVERIFY(detail.showPerson(*actressId));
    QVERIFY(aliases->isExpanded());
    QVERIFY(detail.showPerson(*referenceActressId));
    QVERIFY(!aliases->isExpanded());
    QCOMPARE(aliasToggle->text(), QStringLiteral("别名（0）"));
}

QTEST_MAIN(PersonPageTest)
#include "PersonPageTest.moc"
