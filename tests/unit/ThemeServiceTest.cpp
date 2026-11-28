#include "QtTestEntry.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/ColorWheel.h"
#include "darkeye_ui/components/MakerSelector.h"
#include "darkeye_ui/components/Pagination.h"
#include "darkeye_ui/components/RatingSelector.h"
#include "darkeye_ui/components/TokenControls.h"
#include "darkeye_ui/components/TokenViews.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCompleter>
#include <QHeaderView>
#include <QListWidget>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QSurfaceFormat>
#include <QVBoxLayout>
#include <QWindow>
#include <QtTest>

class ThemeServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void mapsAllPersistedThemeIds();
    void chamferEdgesKeepStyleAndAntialiasing();
    void tableChamfersShowParentBackground();
    void singleLineFieldsShareHeightAndTextOrigin();
    void scrollBarHandlesHavePointedEnds();
    void selectionIndicatorsUseThemedDiamonds();
    void comboPopupAndSelectionHaveChamferedCorners();
    void derivesCustomLightAndDarkTokens();
    void convertsThemeColorsToWindowColorSpace();
    void appliesThemeWithoutUnresolvedTokens();
    void skipsRepolishingAnUnchangedTheme();
    void initializesCommonComponents();
    void renamedComponentsKeepStyle();
    void confirmsColorWhenPickerPopupCloses();
    void closesColorPickerOnFirstOutsideClick();
    void exposesThemeTokenMap();
};

void ThemeServiceTest::initTestCase()
{
    auto format = QSurfaceFormat::defaultFormat();
    format.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    QSurfaceFormat::setDefaultFormat(format);
}

void ThemeServiceTest::exposesThemeTokenMap()
{
    const auto blue = darkeye::ThemeService::tokens(darkeye::ThemeId::Blue);
    QCOMPARE(blue.radiusMd, QStringLiteral("8px"));
    QCOMPARE(blue.fontFamilyBase, QStringLiteral("Microsoft YaHei"));
    QCOMPARE(blue.fontSizeWorkspaceTab, QStringLiteral("16px"));
    QCOMPARE(blue.borderWidth, QStringLiteral("2px"));
    const auto values = blue.toMap();
    QCOMPARE(values.value(QStringLiteral("color_modified")), blue.modified);
    QCOMPARE(values.value(QStringLiteral("field_content_height")), QStringLiteral("20px"));
    QCOMPARE(values.value(QStringLiteral("scrollbar_width")), QStringLiteral("10px"));
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

void ThemeServiceTest::convertsThemeColorsToWindowColorSpace()
{
    using darkeye::ThemeId;
    using darkeye::ThemeService;
    const QColorSpace srgb(QColorSpace::SRgb);
    const QColorSpace p3(QColorSpace::DisplayP3);
    const auto savedFormat = QSurfaceFormat::defaultFormat();
    const auto restoreFormat = qScopeGuard([&] { QSurfaceFormat::setDefaultFormat(savedFormat); });
    auto format = savedFormat;

    format.setColorSpace(srgb);
    QSurfaceFormat::setDefaultFormat(format);
    const auto lightSrgb = ThemeService::tokens(ThemeId::Light);
    QCOMPARE(lightSrgb.colorSpace, srgb);

    format.setColorSpace(p3);
    QSurfaceFormat::setDefaultFormat(format);
    const auto lightP3 = ThemeService::tokens(ThemeId::Light);
    for (const auto theme : ThemeService::availableThemes()) {
        QCOMPARE(ThemeService::sourceTokens(theme).colorSpace, p3);
    }
    QCOMPARE(lightP3.colorSpace, p3);
    QCOMPARE(lightP3.primary, QStringLiteral("#00aaff"));
    QCOMPARE(lightSrgb.primary, lightP3.convertedTo(srgb).primary);
    QVERIFY(lightP3.primary != lightSrgb.primary);
    QCOMPARE(lightP3.pageBackground, lightSrgb.pageBackground);

    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        QWidget window;
        window.winId();
        const auto actual = window.windowHandle()->format().colorSpace();
        QVERIFY(actual.isValid());
        QCOMPARE(actual.primaries(), QColorSpace::Primaries::DciP3D65);
    }
}

void ThemeServiceTest::derivesCustomLightAndDarkTokens()
{
    const auto defaultLight = darkeye::ThemeService::tokens(darkeye::ThemeId::Light);
    QCOMPARE(defaultLight.pageBackground, QStringLiteral("#f4f4f4"));
    QCOMPARE(defaultLight.background, QStringLiteral("#ffffff"));
    QVERIFY(defaultLight.borderFocus != defaultLight.primary);

    const auto light =
        darkeye::ThemeService::tokens(darkeye::ThemeId::Light, QStringLiteral("#123456"));
    QCOMPARE(light.primary, QStringLiteral("#123456"));
    QCOMPARE(light.borderFocus, light.primary);
    QCOMPARE(light.inputBackground, defaultLight.inputBackground);
    QVERIFY(light.primaryHover != light.primary);

    const auto dark = darkeye::ThemeService::tokens(darkeye::ThemeId::Dark,
                                                    QStringLiteral("#000000"));
    QVERIFY(dark.borderFocus != dark.primary);
    QCOMPARE(dark.textInverse, QStringLiteral("#ffffff"));
    const auto brightDark = darkeye::ThemeService::tokens(
        darkeye::ThemeId::Dark, QStringLiteral("#ffffff"));
    QCOMPARE(brightDark.textInverse, QStringLiteral("#000000"));
    QCOMPARE(brightDark.textOnImage, QStringLiteral("#ffffff"));
    QCOMPARE(brightDark.textOnStatus, dark.textOnStatus);

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
    const auto dark = service.currentTokens();
    QCOMPARE(application->palette().color(QPalette::Base), QColor(dark.inputBackground));
    QCOMPARE(application->palette().color(QPalette::Text), QColor(dark.text));
    QCOMPARE(application->palette().color(QPalette::HighlightedText), QColor(dark.textInverse));
    QVERIFY(!application->styleSheet().contains(QStringLiteral("{{")));

    QWidget filter;
    filter.setObjectName(QStringLiteral("ShelfFilterBar"));
    filter.setAttribute(Qt::WA_StyledBackground);
    filter.resize(80, 40);
    darkeye::DesignButton save(QStringLiteral("Save"));
    save.setVariant(QStringLiteral("personSave"));
    for (const auto theme : darkeye::ThemeService::availableThemes()) {
        QVERIFY(service.setTheme(theme));
        QVERIFY(!application->styleSheet().contains(QStringLiteral("{{")));
        const auto tokens = service.currentTokens();
        const auto image = filter.grab().toImage();
        QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2),
                 QColor(tokens.background));
        save.ensurePolished();
        QCOMPARE(save.palette().color(QPalette::Button), QColor(tokens.modified));
    }
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

void ThemeServiceTest::tableChamfersShowParentBackground()
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen"))
        QSKIP("Requires the offscreen platform to capture the window backing store.");

    darkeye::ThemeService service(*qApp);
    QVERIFY(service.setTheme(darkeye::ThemeId::Light));
    QWidget surface;
    surface.setObjectName(QStringLiteral("tableSurface"));
    surface.setStyleSheet(QStringLiteral("#tableSurface { background: #102030; }"));
    // The offscreen plugin can crop a scaled window capture to its logical size.
    surface.resize(QSize(280, 200) * surface.devicePixelRatio());
    darkeye::TokenTableWidget table(30, 8, &surface);
    table.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setGeometry(10, 10, 260, 180);
    surface.show();

    const auto *cornerButton = table.findChild<QAbstractButton *>(Qt::FindDirectChildrenOnly);
    QVERIFY(cornerButton);
    QVERIFY(cornerButton->isVisible());
    const QRect corner = cornerButton->geometry();
    const QColor headerBackground(service.currentTokens().pageBackground);
    const QColor border(service.currentTokens().border);

    for (const int scrollPosition : {0, 10}) {
        table.verticalScrollBar()->setValue(scrollPosition);
        table.selectRow(table.rowAt(0));
        QApplication::processEvents();
        // Capture the actual window repaint, including Qt's opaque-region optimization.
        const QImage image = surface.screen()->grabWindow(surface.winId()).toImage();
        QVERIFY(!image.isNull());
        const qreal scale = image.devicePixelRatio();
        const auto pixel = [&](QPoint local) {
            const QPoint pos = table.mapTo(&surface, local);
            return image.pixelColor(qFloor(pos.x() * scale), qFloor(pos.y() * scale));
        };
        const int bottom = table.height() - 1;
        QCOMPARE(pixel(QPoint(1, bottom - 1)), QColor(QStringLiteral("#102030")));
        QCOMPARE(pixel(QPoint(table.width() / 2, bottom)),
                 QColor(service.currentTokens().border));
        const int firstRowCenter = table.rowHeight(table.rowAt(0)) / 2;
        const QPoint firstCell(4, firstRowCenter);
        auto *columnHeader = table.horizontalHeader()->viewport();
        auto *rowHeader = table.verticalHeader()->viewport();
        QCOMPARE(pixel(columnHeader->mapTo(&table, QPoint(4, 4))),
                 QColor(service.currentTokens().pageBackground));
        QCOMPARE(pixel(rowHeader->mapTo(&table, QPoint(4, firstRowCenter))),
                 QColor(service.currentTokens().pageBackground));
        QCOMPARE(pixel(table.viewport()->mapTo(&table, firstCell)),
                 QColor(service.currentTokens().primary));
        QCOMPARE(pixel(QPoint(corner.right(), corner.center().y())), border);
        QCOMPARE(pixel(QPoint(corner.right() - 1, corner.center().y())), headerBackground);
        QCOMPARE(pixel(QPoint(corner.center().x(), corner.bottom())), border);
        QCOMPARE(pixel(QPoint(corner.center().x(), corner.bottom() - 1)), headerBackground);
    }
}

void ThemeServiceTest::singleLineFieldsShareHeightAndTextOrigin()
{
    darkeye::ThemeService service(*qApp);
    QVERIFY(service.setTheme(darkeye::ThemeId::Light));
    QWidget surface;
    surface.setFocusPolicy(Qt::StrongFocus);
    QVBoxLayout layout(&surface);
    const QString sample = QStringLiteral("HHH");
    darkeye::DesignLineEdit line;
    line.setText(sample);
    darkeye::TokenSpinBox spin;
    spin.setPrefix(sample);
    darkeye::TokenDateTimeEdit date;
    date.setDisplayFormat(QStringLiteral("'HHH' yyyy-MM-dd"));
    darkeye::DesignComboBox combo;
    combo.addItem(sample);
    darkeye::DesignComboBox compactCombo;
    compactCombo.setProperty("compactFilter", true);
    compactCombo.addItem(sample);
    darkeye::MakerSelector editableCombo;
    editableCombo.setEditText(sample);
    const QList<QWidget *> fields{&line, &spin, &date, &combo, &compactCombo, &editableCombo};
    for (auto *field : fields) layout.addWidget(field);
    surface.resize(280, 360);
    surface.show();
    surface.setFocus();
    QApplication::processEvents();

    int referenceInk = -1;
    for (auto *field : fields) {
        QCOMPARE(field->height(), 32);
        const QImage image = field->grab().toImage();
        const qreal dpr = image.devicePixelRatio();
        QCOMPARE(image.pixelColor(qFloor(field->width() / 2.0 * dpr), 0),
                 QColor(service.currentTokens().border));
        QCOMPARE(image.pixelColor(qFloor(field->width() / 2.0 * dpr),
                                  qFloor(field->height() / 2.0 * dpr)),
                 QColor(service.currentTokens().inputBackground));
        int firstInk = image.width();
        for (int y = image.height() / 4; y < image.height() * 3 / 4; ++y) {
            for (int x = 0; x < image.width() / 2; ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() > 200 && pixel.lightness() < 100)
                    firstInk = qMin(firstInk, x);
            }
        }
        QVERIFY(firstInk < image.width() / 2);
        if (referenceInk < 0) {
            referenceInk = firstInk;
            // The first visible pixel includes the glyph's own side bearing.
            QVERIFY(qAbs(firstInk / image.devicePixelRatio() - 6) <= 2);
        }
        QCOMPARE(firstInk, referenceInk);
    }

    line.setFocus(Qt::TabFocusReason);
    QApplication::processEvents();
    QVERIFY(line.hasFocus());
    const QImage focused = line.grab().toImage();
    const qreal dpr = focused.devicePixelRatio();
    QCOMPARE(focused.pixelColor(qFloor(line.width() / 2.0 * dpr), 0),
             QColor(service.currentTokens().borderFocus));
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

void ThemeServiceTest::selectionIndicatorsUseThemedDiamonds()
{
    darkeye::ThemeService service(*qApp);
    QWidget surface;
    QVBoxLayout layout(&surface);
    QCheckBox checkBox;
    darkeye::TokenRadioButton radioButton;
    QListWidget list;
    list.setObjectName(QStringLiteral("DesignListWidget"));
    auto *item = new QListWidgetItem(&list);
    item->setCheckState(Qt::Unchecked);
    layout.addWidget(&checkBox);
    layout.addWidget(&radioButton);
    layout.addWidget(&list);
    surface.setAttribute(Qt::WA_DontShowOnScreen);
    surface.show();

    for (const auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark,
                             darkeye::ThemeId::Red}) {
        QVERIFY(service.setTheme(theme, QStringLiteral("#aa3366")));
        const QColor primary(service.currentTokens().primary);
        QApplication::processEvents();

        const auto verify = [&](QWidget &widget, QStyle::SubElement element,
                                QStyleOption &option, const auto &setChecked) {
            const QRect indicator = widget.style()->subElementRect(element, &option, &widget);
            QVERIFY(!indicator.isEmpty());
            const QPoint center = indicator.center();
            const auto pixel = [](const QImage &image, QPoint point) {
                return image.pixelColor(point * image.devicePixelRatio());
            };
            setChecked(false);
            const QImage unchecked = widget.grab().toImage();
            setChecked(true);
            const QImage checked = widget.grab().toImage();
            QVERIFY(pixel(unchecked, center) != primary);
            QCOMPARE(pixel(checked, center), primary);
            QCOMPARE(pixel(unchecked, indicator.topLeft()), pixel(checked, indicator.topLeft()));
            const int radius = qMin(indicator.width(), indicator.height()) / 2;
            const QPoint nearCorner = center + QPoint(radius - 1, radius - 1);
            QCOMPARE(pixel(checked, nearCorner), pixel(unchecked, nearCorner));
            QCOMPARE(pixel(checked, center + QPoint(4, 0)), primary);
            const QColor outline = pixel(unchecked, center + QPoint(radius - 2, 0));
            const QColor background = pixel(unchecked, center);
            const int gray = qGray(QColor(service.currentTokens().border).rgb());
            // The outline sample can be blended with the background by antialiasing.
            const auto isBlendedChannel = [gray](int edge, int base) {
                return edge >= qMin(gray, base) - 1 && edge <= qMax(gray, base) + 1;
            };
            QVERIFY(isBlendedChannel(outline.red(), background.red()));
            QVERIFY(isBlendedChannel(outline.green(), background.green()));
            QVERIFY(isBlendedChannel(outline.blue(), background.blue()));
            QVERIFY(outline != background);
        };

        QStyleOptionButton buttonOption;
        buttonOption.initFrom(&checkBox);
        verify(checkBox, QStyle::SE_CheckBoxIndicator, buttonOption,
               [&](bool checked) { checkBox.setChecked(checked); });
        buttonOption.initFrom(&radioButton);
        verify(radioButton, QStyle::SE_RadioButtonIndicator, buttonOption,
               [&](bool checked) { radioButton.setChecked(checked); });

        QStyleOptionViewItem itemOption;
        itemOption.initFrom(&list);
        itemOption.rect = list.visualItemRect(item);
        itemOption.features = QStyleOptionViewItem::HasCheckIndicator;
        verify(*list.viewport(), QStyle::SE_ItemViewItemCheckIndicator, itemOption,
               [&](bool checked) { item->setCheckState(checked ? Qt::Checked : Qt::Unchecked); });
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

#ifdef Q_OS_WIN
        // A widget grab has alpha even when the native popup's backing store lacks it.
        const QRegion mask = popup->mask();
        const QRect bounds = popup->rect();
        QVERIFY(mask.contains(bounds.center()));
        QVERIFY(!mask.contains(bounds.topLeft()));
        QVERIFY(!mask.contains(bounds.topRight()));
        QVERIFY(!mask.contains(bounds.bottomLeft()));
        QVERIFY(!mask.contains(bounds.bottomRight()));
#endif

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

DARKEYE_TEST_MAIN(ThemeServiceTest, QApplication)
#include "ThemeServiceTest.moc"
