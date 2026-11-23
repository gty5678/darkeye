#include "window.h"
#include "platform.h"
#include "../tests/app_tests.h"
#include "latex.h"

int main(int argc,char **argv){
    #ifdef Q_OS_MACOS
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    QApplication app(argc,argv);app.setStyle("Fusion");
    QFont applicationFont=app.font();
    if(applicationFont.pointSizeF()>0)applicationFont.setPointSizeF(applicationFont.pointSizeF()+1);
    else applicationFont.setPixelSize(applicationFont.pixelSize()+1);
    app.setFont(applicationFont);
    QCoreApplication::setOrganizationName("MdEdit");QCoreApplication::setApplicationName("MdEdit");
    QTemporaryDir testSettings;
    if(app.arguments().contains("--smoke-test")||app.arguments().contains("--edit-menu-test")){
        if(!testSettings.isValid())return 4;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,testSettings.path());
    }
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
    try{tex::LaTeX::init(formulaResourcePath().toStdString());}catch(const std::exception &e){QMessageBox::critical(nullptr,"公式引擎初始化失败",QString::fromUtf8(e.what()));return 3;}
    int result=0;
    if(const auto testResult=runAppTests(app))result=*testResult;
    else{
        Window window;
        if(app.arguments().size()>1)window.load(app.arguments().at(1));
        window.show();
        result=app.exec();
    }
    tex::LaTeX::release();
    return result;
}
