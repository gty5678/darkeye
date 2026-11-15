#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/theme/StylesheetLoader.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/ColorWheel.h"
#include "darkeye_ui/components/Pagination.h"
#include "darkeye_ui/components/RatingSelector.h"

#include <QApplication>
#include <QSignalSpy>
#include <QStyle>
#include <QtTest>

class ThemeServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void stateOnlyMethodsDoNotApplyStyleSheet();
    void mapsAllPersistedThemeIds();
    void derivesCustomLightAndDarkTokens();
    void appliesThemeWithoutUnresolvedTokens();
    void initializesCommonComponents();
    void renamedComponentsKeepStyle();
    void confirmsColorWhenPickerPopupCloses();
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
    darkeye::ColorPicker picker;
    QSignalSpy confirmed(&picker, &darkeye::ColorPicker::colorConfirmed);

    QTest::mouseClick(&picker, Qt::LeftButton);
    auto *wheel = picker.findChild<darkeye::ColorWheelSimple *>();
    QVERIFY(wheel != nullptr);
    wheel->hide();

    QCOMPARE(confirmed.count(), 1);
    QCOMPARE(confirmed.at(0).at(0).toString(), picker.color());
}

QTEST_MAIN(ThemeServiceTest)
#include "ThemeServiceTest.moc"
