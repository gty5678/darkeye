#include "cover_maker/SleeveEditor.h"
#include <QApplication>
#include <QMainWindow>
#include <QTimer>

using namespace darkeye::cover_maker;
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Darkeye"));
    QCoreApplication::setApplicationName(QStringLiteral("CoverMakerDemo"));
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("DarkEye · 封面制作器 Demo"));
    auto *editor = new SleeveEditor;
    editor->setApplyVisible(false);
    window.setCentralWidget(editor);
    window.resize(1200, 900);
    window.show();
    const auto args = app.arguments();
    const int snapshot = args.indexOf(QStringLiteral("--snapshot"));
    if (snapshot >= 0 && snapshot + 1 < args.size()) QTimer::singleShot(400, &window, [&] {
        const bool ok = window.grab().save(args[snapshot + 1]); app.exit(ok ? 0 : 1);
    });
    return app.exec();
}
