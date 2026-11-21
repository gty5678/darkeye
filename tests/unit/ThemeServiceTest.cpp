#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/theme/StylesheetLoader.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/ColorWheel.h"
#include "darkeye_ui/components/MakerSelector.h"
#include "darkeye_ui/components/Pagination.h"
#include "darkeye_ui/components/RatingSelector.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCompleter>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QtTest>

class ThemeServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void stateOnlyMethodsDoNotApplyStyleSheet();
    void mapsAllPersistedThemeIds();
    void chamferEdgesKeepStyleAndAntialiasing();
    void scrollBarHandlesHavePointedEnds();
    void comboPopupAndSelectionHaveChamferedCorners();
    void derivesCustomLightAndDarkTokens();
    void appliesThemeWithoutUnresolvedTokens();
    void skipsRepolishingAnUnchangedTheme();
    void initializesCommonComponents();
    void renamedComponentsKeepStyle();
    void confirmsColorWhenPickerPopupCloses();
    void closesColorPickerOnFirstOutsideClick();
    void exposesCompletePythonTokenMapAndLoader();
};

void ThemeServiceTest::stateOnlyMethodsDoNotApplyStyleSheet()
{
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    application->setStyleSheet(QStringLiteral("QWidget { padding: 3px; }"));

    darkeye::ThemeService service(*application);
    QSignalSpy changed(&service, &darkeye::ThemeService::themeChanged);
    service.setCustomPrimary(QStringLiteral("#336699"));
    service.setCurrent(darkeye::ThemeId::Dark);

    QCOMPARE(application->styleSheet(), QStringLiteral("QWidget { padding: 3px; }"));
    QCOMPARE(service.current(), darkeye::ThemeId::Dark);
    QCOMPARE(service.customPrimary(), QStringLiteral("#336699"));
    QCOMPARE(service.currentTokens().primary, QStringLiteral("#336699"));
    QCOMPARE(changed.count(), 1);
}

void ThemeServiceTest::exposesCompletePythonTokenMapAndLoader()
{
    const auto blue = darkeye::ThemeService::tokens(darkeye::ThemeId::Blue);
    QCOMPARE(blue.radiusMd, QStringLiteral("8px"));
    QCOMPARE(blue.fontFamilyBase, QStringLiteral("Microsoft YaHei"));
    QCOMPARE(blue.fontSizeWorkspaceTab, QStringLiteral("16px"));
    QCOMPARE(blue.borderWidth, QStringLiteral("2px"));
    QCOMPARE(blue.toMap().size(), 23);
    QCOMPARE(darkeye::StylesheetLoader::render(
                 QStringLiteral("color:{{color_text}};border:{{border_width}} solid;"), blue),
             QStringLiteral("color:#1e3a5f;border:2px solid;"));
}

void ThemeServiceTest::mapsAllPersistedThemeIds()
{
    QCOMPARE(darkeye::ThemeService::availableThemes().size(), 7);
    QCOMPARE(darkeye::ThemeService::fromSettings(QStringLiteral("purple")),
             darkeye::ThemeId::Purple);
    QCOMPARE(darkeye::ThemeService::toSettings(darkeye::ThemeId::Green),
             QStringLiteral("GREEN"));
    QCOMPARE(darkeye::ThemeService::fromSettings(QStringLiteral("unknown")),
             darkeye::ThemeId::Light);
}

void ThemeServiceTest::derivesCustomLightAndDarkTokens()
{
    const auto light =
        darkeye::ThemeService::tokens(darkeye::ThemeId::Light, QStringLiteral("#123456"));
    QCOMPARE(light.primary, QStringLiteral("#123456"));
    QCOMPARE(light.borderFocus, light.primary);
    QVERIFY(light.primaryHover != light.primary);

    const auto purple =
        darkeye::ThemeService::tokens(darkeye::ThemeId::Purple,
                                      QStringLiteral("#123456"));
    QCOMPARE(purple.primary, QStringLiteral("#8e24aa"));
}

void ThemeServiceTest::appliesThemeWithoutUnresolvedTokens()
{
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService service(*application);

    QVERIFY(service.setTheme(darkeye::ThemeId::Dark, QStringLiteral("#336699")));
    QCOMPARE(service.current(), darkeye::ThemeId::Dark);
    QCOMPARE(service.customPrimary(), QStringLiteral("#336699"));
    QVERIFY(application->styleSheet().contains(QStringLiteral("#336699")));
    QVERIFY(!application->styleSheet().contains(QLatin1Char('@')));
}

void ThemeServiceTest::skipsRepolishingAnUnchangedTheme()
{
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    darkeye::ThemeService service(*application);
    darkeye::DesignButton button(QStringLiteral("Save"));
    button.setVariant(QStringLiteral("primary"));
    button.ensurePolished();

    class StyleChanges final : public QObject
    {
    public:
        int count = 0;

        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::StyleChange) ++count;
            return false;
        }
    } styleChanges;
    button.installEventFilter(&styleChanges);
    QSignalSpy changed(&service, &darkeye::ThemeService::themeChanged);

    QVERIFY(service.setTheme(darkeye::ThemeId::Light, QStringLiteral("#ab1234")));
    QCOMPARE(button.palette().color(QPalette::Button), QColor("#ab1234"));
    QVERIFY(styleChanges.count > 0);
    QCOMPARE(changed.count(), 1);

    styleChanges.count = 0;
    QVERIFY(service.setTheme(darkeye::ThemeId::Light, QStringLiteral(" #AB1234 ")));
    QCOMPARE(styleChanges.count, 0);
    QCOMPARE(changed.count(), 1);

    QVERIFY(service.setTheme(darkeye::ThemeId::Light, QStringLiteral("#123456")));
    QCOMPARE(button.palette().color(QPalette::Button), QColor("#123456"));
    QVERIFY(styleChanges.count > 0);
    QCOMPARE(changed.count(), 2);
}

void ThemeServiceTest::renamedComponentsKeepStyle()
{
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    darkeye::ThemeService service(*application);
    for (const auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark}) {
        QVERIFY(service.setTheme(theme));
        darkeye::DesignButton button(QStringLiteral("Save"));
        button.setVariant(QStringLiteral("primary"));
        button.ensurePolished();
        QCOMPARE(button.palette().color(QPalette::Button),
                 QColor(service.currentTokens().primary));
        darkeye::DesignLineEdit input;
        darkeye::DesignTextEdit text;
        darkeye::DesignPlainTextEdit plain;
        darkeye::DesignComboBox combo;
        combo.addItem(QStringLiteral("Item"));
        darkeye::DesignLabel label(QStringLiteral("Description"), QStringLiteral("muted"));
        label.ensurePolished();
        QCOMPARE(label.palette().color(QPalette::WindowText),
                 QColor(service.currentTokens().textPlaceholder));
        const QList<QWidget *> widgets{&button, &input, &text, &plain, &combo, &label};
        for (QWidget *widget : widgets) {
            widget->resize(180, 50);
            widget->setAttribute(Qt::WA_DontShowOnScreen);
            widget->show();
            widget->clearFocus();
            application->processEvents();
            const QImage before = widget->grab().toImage();
            widget->setObjectName(QStringLiteral("BusinessControl"));
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            application->processEvents();
            QCOMPARE(widget->grab().toImage(), before);
        }
    }
}

void ThemeServiceTest::initializesCommonComponents()
{
    darkeye::DesignButton button(QStringLiteral("保存"));
    QCOMPARE(button.objectName(), QStringLiteral("DesignButton"));
    QCOMPARE(button.variant(), QStringLiteral("default"));
    button.setVariant(QStringLiteral("primary"));
    QCOMPARE(button.variant(), QStringLiteral("primary"));

    darkeye::DesignLineEdit lineEdit;
    darkeye::DesignTextEdit textEdit;
    darkeye::DesignPlainTextEdit plainTextEdit;
    darkeye::DesignComboBox comboBox;
    darkeye::RatingSelector rating;
    darkeye::Pagination pagination(101, 20);
    QCOMPARE(lineEdit.objectName(), QStringLiteral("DesignInput"));
    QCOMPARE(textEdit.objectName(), QStringLiteral("DesignTextEdit"));
    QCOMPARE(plainTextEdit.objectName(), QStringLiteral("DesignPlainTextEdit"));
    QCOMPARE(comboBox.objectName(), QStringLiteral("DesignComboBox"));
    rating.setRating(4);
    QCOMPARE(rating.rating(), 4);
    QCOMPARE(pagination.totalPages(), 6);
    pagination.setCurrentPage(6, false);
    QVERIFY(pagination.findChild<QPushButton *>(QStringLiteral("PaginationNext"))
                ->isEnabled() == false);
}

void ThemeServiceTest::confirmsColorWhenPickerPopupCloses()
{
    const QColor initialColor("#00aaff");
    darkeye::ColorPicker picker(initialColor);
    QSignalSpy confirmed(&picker, &darkeye::ColorPicker::colorConfirmed);
    QSignalSpy changed(&picker, &darkeye::ColorPicker::colorChanged);

    QTest::mouseClick(&picker, Qt::LeftButton);
    auto *wheel = picker.findChild<darkeye::ColorWheelSimple *>();
    QVERIFY(wheel != nullptr);
    QCOMPARE(changed.count(), 0);
    wheel->hide();

    QCOMPARE(confirmed.count(), 1);
    QCOMPARE(confirmed.at(0).at(0).toString(), initialColor.name());

    QTest::mouseClick(&picker, Qt::LeftButton);
    wheel->setHexColor(QStringLiteral("#dd4488"));
    QVERIFY(changed.count() > 0);
    QVERIFY(picker.color() != initialColor.name());
    wheel->hide();
    QCOMPARE(confirmed.count(), 2);
    QCOMPARE(confirmed.at(1).at(0).toString(), picker.color());
}

void ThemeServiceTest::closesColorPickerOnFirstOutsideClick()
{
    QWidget surface;
    surface.resize(800, 600);
    darkeye::ColorPicker picker(QColor("#336699"), false,
                               darkeye::ColorPicker::Shape::Octagon, &surface);
    surface.show();
    QSignalSpy confirmed(&picker, &darkeye::ColorPicker::colorConfirmed);
    QTest::mouseClick(&picker, Qt::LeftButton);
    auto *wheel = picker.findChild<darkeye::ColorWheelSimple *>();
    QVERIFY(wheel != nullptr);
    QVERIFY(wheel->isVisible());

    // An inside click must not dismiss the panel.
    QTest::mouseClick(wheel, Qt::LeftButton, Qt::NoModifier, wheel->rect().center());
    QVERIFY(wheel->isVisible());
    QCOMPARE(confirmed.count(), 0);

    QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(790, 590));
    QVERIFY(!wheel->isVisible());
    QCOMPARE(confirmed.count(), 1);

    QTest::mouseClick(&picker, Qt::LeftButton);
    QVERIFY(wheel->isVisible());
    QTest::mouseClick(&picker, Qt::LeftButton);
    QVERIFY(!wheel->isVisible());
    QCOMPARE(confirmed.count(), 2);
}

void ThemeServiceTest::chamferEdgesKeepStyleAndAntialiasing()
{
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    darkeye::ThemeService service(*application);
    QVERIFY(service.setTheme(darkeye::ThemeId::Light));
    QWidget surface;
    surface.setStyleSheet(QStringLiteral("QWidget { background: #101820; }"));
    surface.resize(180, 70);
    darkeye::DesignButton button(QString(), &surface);
    button.setGeometry(10, 10, 140, 40);
    surface.show();
    for (int width : {1, 2, 3}) {
        for (const QString &colour : {QStringLiteral("#cc3344"), QStringLiteral("#2299dd")}) {
            button.setStyleSheet(QStringLiteral(
                "QPushButton { border: %1px solid %2; border-radius: 8px; background: #ffffff; }"
                "QPushButton:disabled { border-color: #778899; }").arg(width).arg(colour));
            for (bool enabled : {true, false}) {
                button.setEnabled(enabled);
                QApplication::processEvents();
                const QImage image = surface.grab().toImage();
                const qreal dpr = image.devicePixelRatio();
                const auto pixel = [&](qreal x, qreal y) {
                    return image.pixelColor(qFloor((10 + x) * dpr), qFloor((10 + y) * dpr));
                };
                const QColor expected(enabled ? colour : QStringLiteral("#778899"));
                QCOMPARE(pixel(60, 0), expected);
                QCOMPARE(pixel(0, 0), QColor(QStringLiteral("#101820")));
                QCOMPARE(pixel(7, 7), QColor(Qt::white));
                // The diagonal must contain the same colour as the straight
                // edge, and its subpixel coverage must blend with the parent.
                const QColor diagonal = pixel(3, 3);
                QVERIFY(diagonal != QColor(Qt::white));
                QVERIFY(diagonal != QColor(QStringLiteral("#101820")));
                QCOMPARE(pixel(60, width + 1), QColor(Qt::white));
            }
        }
    }
    QVERIFY(button.mask().isEmpty());
    QVERIFY(button.findChild<QWidget *>(QStringLiteral("darkeyeChamferOutline")) == nullptr);
}

void ThemeServiceTest::scrollBarHandlesHavePointedEnds()
{
    darkeye::ThemeService service(*qApp);
    for (const auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark}) {
        QVERIFY(service.setTheme(theme, QStringLiteral("#336699")));
        const auto tokens = service.currentTokens();
        for (const auto orientation : {Qt::Vertical, Qt::Horizontal}) {
            const bool horizontal = orientation == Qt::Horizontal;
            QScrollBar bar(orientation);
            bar.setRange(0, 100);
            bar.setPageStep(50);
            bar.setValue(50);
            bar.resize(horizontal ? QSize(180, 10) : QSize(10, 180));
            bar.setAttribute(Qt::WA_DontShowOnScreen);
            bar.show();

            QStyleOptionSlider option;
            option.initFrom(&bar);
            option.orientation = orientation;
            option.maximum = bar.maximum();
            option.pageStep = bar.pageStep();
            option.sliderPosition = bar.sliderPosition();
            if (horizontal) option.state |= QStyle::State_Horizontal;
            const QRect handle = bar.style()->subControlRect(
                QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, &bar);
            const QRectF bounds = horizontal ? QRectF(handle).adjusted(2, 1, -2, -1)
                                             : QRectF(handle).adjusted(1, 2, -1, -2);
            const auto colour = [](const QImage &image, QPointF point) {
                const QPointF pixel = point * image.devicePixelRatio();
                return image.pixelColor(int(pixel.x()), int(pixel.y()));
            };
            const auto point = [&](qreal across, qreal along) {
                return horizontal ? bounds.topLeft() + QPointF(along, across)
                                  : bounds.topLeft() + QPointF(across, along);
            };
            const qreal length = horizontal ? bounds.width() : bounds.height();
            const QColor track(tokens.pageBackground);
            const QColor fill(tokens.border);
            const QImage image = bar.grab().toImage();
            QCOMPARE(colour(image, point(1.5, 1.5)), track);
            QCOMPARE(colour(image, point(6.5, length - 1.5)), track);
            QCOMPARE(colour(image, point(1.5, 3.5)), fill);
            QCOMPARE(colour(image, point(6.5, length - 3.5)), fill);
            QCOMPARE(colour(image, bounds.center()), fill);

            QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, handle.center());
            QVERIFY(bar.isSliderDown());
            QCOMPARE(colour(bar.grab().toImage(), bounds.center()), QColor(tokens.primaryHover));
            QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, handle.center());
        }
    }
}

void ThemeServiceTest::comboPopupAndSelectionHaveChamferedCorners()
{
    darkeye::ThemeService service(*qApp);
    QWidget surface;
    surface.resize(250, 220);
    QComboBox combo(&surface);
    combo.setGeometry(20, 20, 180, 32);
    combo.addItems({QStringLiteral("First"), QStringLiteral("Second"),
                    QStringLiteral("Third")});
    darkeye::MakerSelector maker({{1, QStringLiteral("Studio"), {}, {}}}, &surface);
    maker.setGeometry(20, 100, 180, 32);
    surface.show();

    const auto verifyPopup = [&](QAbstractItemView *view) {
        auto *popup = view->window();
        QVERIFY(popup->isVisible());
        QCoreApplication::processEvents();
        QVERIFY(view->selectionModel()->isSelected(view->currentIndex()));

        const auto tokens = service.currentTokens();
        const QImage image = popup->grab().toImage();
        const qreal dpr = image.devicePixelRatio();
        const auto pixel = [&](const QPoint &point) {
            return image.pixelColor(qFloor(point.x() * dpr), qFloor(point.y() * dpr));
        };
        QCOMPARE(pixel(QPoint(0, 0)).alpha(), 0);
        QCOMPARE(pixel(QPoint(popup->width() / 2, 0)), QColor(tokens.border));

        const QRect row = view->visualRect(view->currentIndex());
        QVERIFY(row.height() >= view->fontMetrics().height() + 6);
        QVERIFY(view->viewport()->rect().contains(
            view->visualRect(view->model()->index(view->model()->rowCount() - 1, 0))));
        const QPoint rowOrigin = view->viewport()->mapTo(popup, row.topLeft());
        QCOMPARE(pixel(rowOrigin + QPoint(1, 1)), QColor(tokens.background));
        QCOMPARE(pixel(rowOrigin + QPoint(row.width() / 2, row.height() / 2)),
                 QColor(tokens.primary));
    };

    for (const auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark}) {
        QVERIFY(service.setTheme(theme, QStringLiteral("#336699")));
        combo.showPopup();
        auto *view = combo.view();
        QTest::keyClick(view, Qt::Key_Down);
        QCOMPARE(view->currentIndex().row(), 1);
        verifyPopup(view);
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(combo.currentIndex(), 1);
        combo.setCurrentIndex(0);

        auto *completer = maker.completer();
        completer->complete();
        auto *suggestions = completer->popup();
        suggestions->setCurrentIndex(suggestions->model()->index(1, 0));
        verifyPopup(suggestions);
        suggestions->hide();
    }
}

QTEST_MAIN(ThemeServiceTest)
#include "ThemeServiceTest.moc"
