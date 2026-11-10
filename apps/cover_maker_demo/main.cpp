#include "cover_maker/SleeveEditor.h"
#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <array>

using namespace darkeye::cover_maker;
namespace {
QImage sampleImage(const QColor &a, const QColor &b)
{
    QImage image(1200, 1800, QImage::Format_RGB32);
    QPainter p(&image);
    QLinearGradient gradient(0, 0, 1200, 1800); gradient.setColorAt(0, a); gradient.setColorAt(1, b);
    p.fillRect(image.rect(), gradient);
    p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, 45));
    p.drawEllipse(QRectF(120, 180, 850, 850)); p.drawEllipse(QRectF(500, 900, 900, 900));
    return image;
}
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Darkeye"));
    QCoreApplication::setApplicationName(QStringLiteral("CoverMakerDemo"));
    QMainWindow window; window.setWindowTitle(QStringLiteral("DarkEye · 封面制作器 Demo"));
    auto *central = new QWidget; auto *root = new QHBoxLayout(central);
    auto *sidebar = new QWidget; sidebar->setFixedWidth(210); auto *side = new QVBoxLayout(sidebar);
    side->addWidget(new QLabel(QStringLiteral("示例条目 · 独立于主资料库")));
    auto *entries = new QListWidget; entries->addItems({QStringLiteral("DEMO-001 · 山间来信"), QStringLiteral("DEMO-002 · 蓝色时刻"), QStringLiteral("DEMO-003 · 光与影")});
    entries->setMaximumHeight(150); side->addWidget(entries);
    auto *cover = new QLabel(QStringLiteral("尚未应用封面")); cover->setAlignment(Qt::AlignCenter); cover->setMinimumHeight(260);
    side->addWidget(cover, 1);
    auto *hint = new QLabel(QStringLiteral("选择条目 → 分别制作封底、书脊、封面 → 应用完整封套\n\n应用后的 PNG 保存在 Demo 专属目录，重新打开后仍可查看。编辑草稿只在本次运行期间保留。"));
    hint->setWordWrap(true); side->addWidget(hint);
    auto *editor = new SleeveEditor;
    root->addWidget(sidebar); root->addWidget(editor, 1); window.setCentralWidget(central);
    std::array<SleeveDocument, 3> drafts;
    const QStringList titles{QStringLiteral("山间来信"), QStringLiteral("蓝色时刻"), QStringLiteral("光与影")};
    for (int i = 0; i < 3; ++i) {
        for (int panel = 0; panel < 3; ++panel) {
            drafts[i].panels[panel].title = panel == 0 ? QStringLiteral("内容简介") : titles[i];
            drafts[i].panels[panel].subtitle = QStringLiteral("DEMO-00%1").arg(i + 1);
        }
        drafts[i].panels[2].imageSlots[0].image = sampleImage(QColor::fromHsv(140 + i * 55, 150, 150), QColor::fromHsv(200 + i * 45, 190, 55));
        drafts[i].panels[0].imageSlots[0].image = sampleImage(QColor("#d9ae73"), QColor("#693d63"));
    }
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/sleeves");
    const auto pathFor = [directory](int row) { return QDir(directory).filePath(QStringLiteral("DEMO-00%1.png").arg(row + 1)); };
    const auto refreshCover = [cover, entries, pathFor] {
        const QImage image(pathFor(entries->currentRow()));
        if (image.isNull()) { cover->setPixmap({}); cover->setText(QStringLiteral("尚未应用封面")); }
        else cover->setPixmap(QPixmap::fromImage(image).scaled(190, 285, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    int current = -1;
    QObject::connect(entries, &QListWidget::currentRowChanged, &window, [&](int row) {
        if (current >= 0) drafts[current] = editor->document();
        current = row; if (row < 0) return;
        editor->setDocument(drafts[row]); refreshCover();
        window.statusBar()->showMessage(QStringLiteral("当前条目：%1").arg(entries->currentItem()->text()));
    });
    QObject::connect(editor, &SleeveEditor::applyRequested, &window, [&](const QImage &image) {
        if (current < 0) return;
        QDir().mkpath(directory);
        QSaveFile file(pathFor(current));
        if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
            QMessageBox::warning(&window, QStringLiteral("应用失败"), file.errorString()); return;
        }
        drafts[current] = editor->document(); refreshCover();
        window.statusBar()->showMessage(QStringLiteral("封面已应用到 %1 · %2").arg(drafts[current].panels[2].subtitle, pathFor(current)));
    });
    entries->setCurrentRow(0); window.resize(1400, 1000); window.show();
    // Deterministic visual review without touching the main application's data.
    const auto args = app.arguments();
    const int snapshot = args.indexOf(QStringLiteral("--snapshot"));
    if (snapshot >= 0 && snapshot + 1 < args.size()) QTimer::singleShot(400, &window, [&] {
        const bool ok = window.grab().save(args[snapshot + 1]); app.exit(ok ? 0 : 1);
    });
    return app.exec();
}
