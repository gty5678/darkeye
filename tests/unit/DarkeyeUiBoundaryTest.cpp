#include "QtTestEntry.h"

#include "darkeye_ui/DarkeyeUi.h"

#include <QApplication>
#include <QtTest>

namespace
{

class TestLazyWidget final : public darkeye::LazyWidget
{
public:
    int loadCount = 0;

protected:
    void lazyLoad() override
    {
        ++loadCount;
    }
};

} // namespace

class DarkeyeUiBoundaryTest final : public QObject
{
    Q_OBJECT

private slots:
    void builtInIconsLoadBeforeThemeSetup();
    void publicFacadeIsUsableWithoutBusinessUi();
    void lazyWidgetInitializesExactlyOnce();
};

void DarkeyeUiBoundaryTest::builtInIconsLoadBeforeThemeSetup()
{
    using darkeye::IconProvider;
    QVERIFY(IconProvider::contains(QStringLiteral(" CHEVRON_LEFT ")));
    QVERIFY(IconProvider::contains(QStringLiteral("refresh")));
    QVERIFY(IconProvider::contains(QStringLiteral("circle_question")));
    QVERIFY(!IconProvider::contains(QStringLiteral("unknown")));
    QVERIFY(IconProvider::builtIn(QStringLiteral("unknown")).isNull());

    const QSize size(24, 24);
    const auto red = IconProvider::builtIn(QStringLiteral("chevron_left"), size, Qt::red, 2.0)
        .pixmap(size, 2.0);
    const auto blue = IconProvider::builtIn(QStringLiteral("chevron_left"), size, Qt::blue, 2.0)
        .pixmap(size, 2.0);
    QVERIFY(!red.isNull());
    QVERIFY(!blue.isNull());
    QCOMPARE(red.size(), size * 2);
    QCOMPARE(red.devicePixelRatio(), 2.0);
    QVERIFY(red.toImage() != blue.toImage());
}

void DarkeyeUiBoundaryTest::publicFacadeIsUsableWithoutBusinessUi()
{
    darkeye::DesignButton button(QStringLiteral("基础按钮"));
    darkeye::DesignLineEdit input;
    darkeye::FlowLayout layout;
    QCOMPARE(button.objectName(), QStringLiteral("DesignButton"));
    QCOMPARE(input.objectName(), QStringLiteral("DesignInput"));
    QCOMPARE(layout.count(), 0);
}

void DarkeyeUiBoundaryTest::lazyWidgetInitializesExactlyOnce()
{
    TestLazyWidget widget;
    QVERIFY(!widget.isInitialized());
    widget.show();
    QApplication::processEvents();
    QVERIFY(widget.isInitialized());
    QCOMPARE(widget.loadCount, 1);
    widget.hide();
    widget.show();
    QApplication::processEvents();
    QCOMPARE(widget.loadCount, 1);
}

DARKEYE_TEST_MAIN(DarkeyeUiBoundaryTest, QApplication)
#include "DarkeyeUiBoundaryTest.moc"
