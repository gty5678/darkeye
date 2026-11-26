#include "DvdSceneAdapter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class DvdSceneAdapterTest : public QObject
{
    Q_OBJECT

private slots:
    void resolvesRelativeCoverPath()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile file(temporary.filePath(QStringLiteral("items.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto json = QStringLiteral(R"({"items":[{"stableId":"test","title":"测试","coverPath":"封面.png"}]})").toUtf8();
        QCOMPARE(file.write(json), json.size());
        file.close();

        const auto result = darkeye::dvd_scene::DvdSceneAdapter::loadJson(file.fileName());
        QVERIFY2(bool(result), qPrintable(result.error));
        QCOMPARE(result.items.size(), 1);
        QCOMPARE(result.items.first().coverPath, temporary.filePath(QStringLiteral("封面.png")));
    }
};

QTEST_APPLESS_MAIN(DvdSceneAdapterTest)
#include "DvdSceneAdapterTest.moc"
