#include <QApplication>

#include "darkeye_ui/components/ColorWheel.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    darkeye::ColorWheelSimple mycustomwidget;
    mycustomwidget.show();

    return app.exec();
}




