#pragma once

#include <QtTest>

// The suite runner starts exactly one test class for each CTest invocation.
#define DARKEYE_TEST_MAIN(TestClass, Application)                 \
    int run_##TestClass(int argc, char **argv)                     \
    {                                                            \
        Application app(argc, argv);                             \
        app.setAttribute(Qt::AA_Use96Dpi);                        \
        TestClass test;                                          \
        QTest::setMainSourcePath(__FILE__, QT_TESTCASE_BUILDDIR);  \
        return QTest::qExec(&test, argc, argv);                    \
    }
