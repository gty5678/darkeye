#include "services/LlamaRuntime.h"
#include "ui/pages/SettingsPage.h"

#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <cstdio>

using namespace darkeye;

class LlamaRuntimeTest final : public QObject
{
    Q_OBJECT
private slots:
    void sharedProcessSurvivesPageAndCanBeStopped();
    void failedStartUpdatesObservers();
    void applicationQuitStopsSharedProcess();
};

void LlamaRuntimeTest::sharedProcessSurvivesPageAndCanBeStopped()
{
    auto &runtime = get_llama_runtime();
    QCOMPARE(&runtime, &get_llama_runtime());
    LlamaCppSettings config;
    config.serverExecutable = QCoreApplication::applicationFilePath();
    config.modelPath = QStringLiteral("runtime-test-helper");
    QVERIFY(runtime.start(config).isEmpty());
    QTRY_VERIFY(runtime.processId() > 0);
    QTRY_VERIFY(runtime.status().contains(QStringLiteral("运行中")));
    QTRY_VERIFY(runtime.logs().join(u'\n').contains(QStringLiteral("helper ready")));
    const qint64 pid = runtime.processId();
    QVERIFY(runtime.start(config).isEmpty());
    QCOMPARE(runtime.processId(), pid);

    {
        TranslationSettingsPage page;
        page.initialize();
        bool foundStatus = false;
        for (auto *label : page.findChildren<QLabel *>())
            foundStatus |= label->text().contains(QString::number(pid));
        QVERIFY(foundStatus);
        bool foundLog = false;
        for (auto *log : page.findChildren<QPlainTextEdit *>())
            foundLog |= log->toPlainText().contains(QStringLiteral("helper ready"));
        QVERIFY(foundLog);
    }
    QVERIFY(runtime.isRunning());
    QCOMPARE(runtime.processId(), pid);

    TranslationSettingsPage page;
    page.initialize();
    QPushButton *start = nullptr;
    QPushButton *stop = nullptr;
    for (auto *button : page.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("启动 llama-server")) start = button;
        if (button->text() == QStringLiteral("停止")) stop = button;
    }
    QVERIFY(start && stop);
    QVERIFY(!start->isEnabled());
    QVERIFY(stop->isEnabled());
    stop->click();
    QVERIFY(!runtime.isRunning());
    QVERIFY(start->isEnabled());
    QVERIFY(!stop->isEnabled());
}

void LlamaRuntimeTest::failedStartUpdatesObservers()
{
    auto &runtime = get_llama_runtime();
    QSignalSpy running(&runtime, &LlamaRuntime::runningChanged);
    LlamaCppSettings config;
    config.serverExecutable = QStringLiteral("missing-llama-executable.exe");
    config.modelPath = QStringLiteral("model.gguf");
    QVERIFY(runtime.start(config).isEmpty());
    QTRY_VERIFY(!runtime.isRunning());
    QVERIFY(runtime.status().startsWith(QStringLiteral("失败")));
    QVERIFY(!running.isEmpty());
    QCOMPARE(running.last().first().toBool(), false);
}

void LlamaRuntimeTest::applicationQuitStopsSharedProcess()
{
    auto &runtime = get_llama_runtime();
    LlamaCppSettings config;
    config.serverExecutable = QCoreApplication::applicationFilePath();
    config.modelPath = QStringLiteral("runtime-test-helper");
    QVERIFY(runtime.start(config).isEmpty());
    QTRY_VERIFY(runtime.processId() > 0);
    QVERIFY(QMetaObject::invokeMethod(qApp, "aboutToQuit", Qt::DirectConnection));
    QVERIFY(!runtime.isRunning());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("runtime-test-helper"))) {
        std::puts("helper ready");
        std::fflush(stdout);
        QTimer::singleShot(30000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    LlamaRuntimeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "LlamaRuntimeTest.moc"
