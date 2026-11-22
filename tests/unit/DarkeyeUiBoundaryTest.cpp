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
    void publicFacadeIsUsableWithoutBusinessUi();
    void lazyWidgetInitializesExactlyOnce();
};

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

QTEST_MAIN(DarkeyeUiBoundaryTest)
#include "DarkeyeUiBoundaryTest.moc"
