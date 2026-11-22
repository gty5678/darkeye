#include "dvd_scene/DvdSceneWidget.h"
#include "dvd_scene/DvdSceneAdapter.h"
#include "render3d/GltfLoader.h"
#include "dvd_scene/DvdModelAsset.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSlider>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <ctime>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace darkeye::dvd_scene;
using namespace darkeye::render3d;

static double processCpuSeconds()
{
#ifdef Q_OS_WIN
    FILETIME created{},exited{},kernel{},user{};
    if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) {
        const auto ticks=[](const FILETIME &t) { return (quint64(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
        return double(ticks(kernel)+ticks(user))/1e7;
    }
    return 0;
#else
    return double(std::clock())/CLOCKS_PER_SEC;
#endif
}

class DvdDemoWindow final:public QMainWindow {
public:
    DvdDemoWindow():m_temp(QDir(QCoreApplication::applicationDirPath()).filePath("dvd-demo-XXXXXX"))
    {
        setWindowTitle(QStringLiteral("DarkEye · 独立 QRhi DVD 书架"));
        resize(1280,760);
        auto *central=new QWidget(this);
        auto *layout=new QHBoxLayout(central);
        m_host=new QWidget(central);
        m_viewLayout=new QVBoxLayout(m_host);
        m_viewLayout->setContentsMargins(0,0,0,0);
        m_view=new DvdSceneWidget(m_host);
        QString environmentError;
        auto environment=EnvironmentMap::loadHdr(QStringLiteral(":/dvd_scene_demo/lebombo_1k.hdr"),&environmentError);
        if(!environment) { qCritical().noquote()<<environmentError; m_modelReady=false; }
        m_view->renderWidget()->setEnvironment(std::move(environment));
        LightingSettings lighting;
        lighting.environmentBackground=lighting.imageBasedLighting=lighting.ambientOcclusion=lighting.shadows=lighting.toneMapping=true;
        m_view->renderWidget()->setLighting(lighting);
        m_view->setMinimumSize(400,300);
        m_viewLayout->addWidget(m_view);
        layout->addWidget(m_host,1);
        auto *panel=new QWidget(central);
        panel->setFixedWidth(270);
        auto *controls=new QVBoxLayout(panel);
        auto *heading=new QLabel(QStringLiteral("DVD · QRhi 3D"),panel);
        heading->setStyleSheet(QStringLiteral("font-size:22px;font-weight:600;"));
        controls->addWidget(heading);
        auto *description=new QLabel(QStringLiteral("原 DVD 模型与封面\n独立场景 · 合成数据"),panel);
        controls->addWidget(description);
        m_count=new QComboBox(panel);
        for(int count:{0,1,10,100,500,1000}) m_count->addItem(QStringLiteral("%1 件 DVD").arg(count),count);
        m_count->setCurrentIndex(3);
        controls->addWidget(m_count);
        auto *regenerate=new QPushButton(QStringLiteral("重置样本"),panel);
        auto *load=new QPushButton(QStringLiteral("载入 JSON…"),panel);
        const auto pair=[controls](QWidget *first,QWidget *second) {
            auto *row=new QHBoxLayout;
            row->addWidget(first); row->addWidget(second); controls->addLayout(row);
        };
        pair(regenerate,load);
        auto *loadModelButton=new QPushButton(QStringLiteral("载入 DVD GLB…"),panel);
        m_shellPreset=new QComboBox(panel);
        m_shellPreset->setObjectName(QStringLiteral("shell-material"));
        m_shellPreset->addItems({QStringLiteral("模型原材质"),QStringLiteral("透明塑料盒壳"),QStringLiteral("薄片透射预览"),QStringLiteral("透明盒壳＋外膜预览")});
        m_shellPreset->setToolTip(QStringLiteral("透明预设覆盖 shell；外膜预览另建偏移薄面。薄片透射预览不新增外膜。切换会重置开盒姿态。"));
        controls->addWidget(m_shellPreset);
        controls->addWidget(loadModelButton);
        auto *transmissionPasses=new QSpinBox(panel);
        transmissionPasses->setObjectName(QStringLiteral("transmission-passes"));
        transmissionPasses->setRange(1,16); transmissionPasses->setValue(8);
        transmissionPasses->setPrefix(QStringLiteral("透射合成上限 ")); transmissionPasses->setSuffix(QStringLiteral(" 次"));
        transmissionPasses->setToolTip(QStringLiteral("重叠透明表面逐次合成；超限时优先保留近景层，远景合并并报告回退数。"));
        controls->addWidget(transmissionPasses);
        connect(transmissionPasses,&QSpinBox::valueChanged,this,[this](int limit) {
            auto settings=m_view->renderWidget()->lighting(); settings.transmissionPassLimit=limit;
            m_view->renderWidget()->setLighting(settings); m_statsFingerprint.clear();
        });
        connect(m_shellPreset,&QComboBox::currentIndexChanged,this,[this](int preset) { applyShellPreset(preset); });
        connect(loadModelButton,&QPushButton::clicked,this,[this] {
            const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("载入 DVD 模型"),{},"glTF (*.glb *.gltf)");
            if(!path.isEmpty()) loadModel(path);
        });
        controls->addWidget(new QLabel(QStringLiteral("作品序号"),panel));
        m_index=new QSpinBox(panel);
        m_index->setRange(1,1000);
        controls->addWidget(m_index);
        auto *focus=new QPushButton(QStringLiteral("聚焦作品"),panel);
        auto *open=new QPushButton(QStringLiteral("开盒 / 关盒"),panel);
        auto *back=new QPushButton(QStringLiteral("返回（Esc）"),panel);
        auto *reset=new QPushButton(QStringLiteral("重置镜头"),panel);
        pair(focus,open);
        pair(back,reset);
        auto *light=new QCheckBox(QStringLiteral("明亮场景"),panel);
        controls->addWidget(light);
        auto *effects=new QGridLayout;
        controls->addLayout(effects);
        int effectIndex=0;
        const auto effect=[&](const QString &text,const QString &name,bool LightingSettings::*field) {
            auto *box=new QCheckBox(text,panel); box->setChecked(true);
            box->setObjectName(name);
            effects->addWidget(box,effectIndex/2,effectIndex%2); ++effectIndex;
            connect(box,&QCheckBox::toggled,this,[this,field](bool enabled) {
                auto settings=m_view->renderWidget()->lighting(); settings.*field=enabled;
                m_view->renderWidget()->setLighting(settings); m_statsFingerprint.clear();
            });
        };
        effect(QStringLiteral("HDR 环境背景"),"hdr-background",&LightingSettings::environmentBackground);
        effect(QStringLiteral("IBL 环境光照"),"ibl",&LightingSettings::imageBasedLighting);
        effect(QStringLiteral("AO 环境遮蔽"),"ao",&LightingSettings::ambientOcclusion);
        effect(QStringLiteral("方向光阴影"),"shadows",&LightingSettings::shadows);
        effect(QStringLiteral("HDR 色调映射"),"tone-mapping",&LightingSettings::toneMapping);
        auto *exposure=new QSpinBox(panel);
        exposure->setRange(10,400); exposure->setValue(100);
        exposure->setPrefix(QStringLiteral("曝光 ")); exposure->setSuffix(QStringLiteral("%"));
        controls->addWidget(exposure);
        connect(exposure,&QSpinBox::valueChanged,this,[this](int percent) {
            auto settings=m_view->renderWidget()->lighting(); settings.exposure=percent/100.0f;
            m_view->renderWidget()->setLighting(settings); m_statsFingerprint.clear();
        });
        controls->addWidget(new QLabel(QStringLiteral("环境旋转"),panel));
        auto *environmentRotation=new QSlider(Qt::Horizontal,panel);
        environmentRotation->setRange(0,360);
        environmentRotation->setSingleStep(1); environmentRotation->setPageStep(15);
        auto *environmentAngle=new QSpinBox(panel);
        environmentAngle->setObjectName(QStringLiteral("environment-rotation"));
        environmentAngle->setRange(0,360); environmentAngle->setSuffix(QStringLiteral("°"));
        environmentAngle->setWrapping(true);
        environmentAngle->setToolTip(QStringLiteral("HDR 背景与 IBL 光照同步绕竖轴旋转"));
        pair(environmentRotation,environmentAngle);
        connect(environmentRotation,&QSlider::valueChanged,environmentAngle,&QSpinBox::setValue);
        connect(environmentAngle,&QSpinBox::valueChanged,environmentRotation,&QSlider::setValue);
        connect(environmentAngle,&QSpinBox::valueChanged,this,[this](int degrees) {
            auto settings=m_view->renderWidget()->lighting(); settings.environmentRotation=float(degrees);
            m_view->renderWidget()->setLighting(settings); m_statsFingerprint.clear();
        });
        auto *budget=new QSpinBox(panel);
        budget->setRange(1,128);
        budget->setValue(32);
        budget->setPrefix(QStringLiteral("封面预算 "));
        budget->setSuffix(QStringLiteral(" MiB"));
        controls->addWidget(budget);
        connect(budget,&QSpinBox::valueChanged,this,[this](int mb) {
            m_view->setCoverBudget(quint64(mb)*1024*1024);
            m_statsFingerprint.clear();
        });
        auto *hide=new QPushButton(QStringLiteral("隐藏 / 恢复"),panel);
        auto *reparent=new QPushButton(QStringLiteral("移动窗口"),panel);
        auto *screenshot=new QPushButton(QStringLiteral("保存场景截图…"),panel);
        pair(hide,reparent);
        controls->addWidget(screenshot);
        m_selection=new QLabel(QStringLiteral("未选择作品"),panel);
        m_selection->setWordWrap(true);
        controls->addWidget(m_selection);
        m_stats=new QLabel(panel);
        m_stats->setWordWrap(true);
        m_stats->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controls->addWidget(m_stats);
        m_error=new QLabel(panel);
        m_error->setWordWrap(true);
        m_error->setStyleSheet(QStringLiteral("color:#d85867"));
        controls->addWidget(m_error);
        controls->addStretch();
        auto *help=new QLabel(QStringLiteral(
            "单击选择，再次单击开盒\n双击聚焦 · 右键拖动旋转\n中键平移 · 滚轮横移书架\nCtrl + 滚轮缩放"),panel);
        help->setWordWrap(true);
        controls->addWidget(help);
        auto *panelScroll=new QScrollArea(central);
        panelScroll->setWidgetResizable(true);
        panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        panelScroll->setFixedWidth(292);
        panelScroll->setFrameShape(QFrame::NoFrame);
        panelScroll->setWidget(panel);
        layout->addWidget(panelScroll);
        setCentralWidget(central);
        m_other.setWindowTitle(QStringLiteral("QRhi DVD · 第二个窗口"));
        m_other.resize(1000,720);
        m_otherLayout=new QVBoxLayout(&m_other);
        m_other.installEventFilter(this);

        connect(regenerate,&QPushButton::clicked,this,[this] { setSampleCount(m_count->currentData().toInt()); });
        connect(load,&QPushButton::clicked,this,[this] {
            const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("载入 DVD 样本"),{},"JSON (*.json)");
            if(!path.isEmpty()) loadItems(path);
        });
        connect(focus,&QPushButton::clicked,this,[this] {
            const int index=m_index->value()-1;
            if(index>=0 && index<m_view->items().size()) m_view->focusItem(m_view->items()[index].stableId);
        });
        connect(open,&QPushButton::clicked,m_view,&DvdSceneWidget::toggleOpen);
        connect(back,&QPushButton::clicked,m_view,&DvdSceneWidget::returnToShelf);
        connect(reset,&QPushButton::clicked,m_view,&DvdSceneWidget::resetCamera);
        connect(light,&QCheckBox::toggled,this,[this](bool enabled) { m_view->setDarkTheme(!enabled); });
        connect(hide,&QPushButton::clicked,this,[this] {
            m_view->hide();
            QTimer::singleShot(800,this,[this] { m_view->show(); });
        });
        connect(reparent,&QPushButton::clicked,this,[this] { moveCanvas(); });
        connect(screenshot,&QPushButton::clicked,this,[this] {
            const auto path=QFileDialog::getSaveFileName(this,QStringLiteral("保存截图"),"dvd-scene.png","PNG (*.png)");
            if(!path.isEmpty() && !m_view->renderWidget()->grabFramebuffer().save(path))
                m_error->setText(QStringLiteral("无法保存截图"));
        });
        connect(m_view,&DvdSceneWidget::selectionChanged,this,[this](const QString &id) {
            QString title;
            for(const auto &item:m_view->items()) if(item.stableId==id) { title=item.title; break; }
            m_selection->setText(id.isEmpty()?QStringLiteral("未选择作品"):QStringLiteral("%1\n%2").arg(id,title));
        });
        connect(m_view,&DvdSceneWidget::sceneError,m_error,&QLabel::setText);
        connect(m_view,&DvdSceneWidget::coverWarning,this,[this](const QString &) {
            m_error->setText(QStringLiteral("部分封面无法读取，已使用占位图。"));
        });
        m_statsTimer.setInterval(400);
        connect(&m_statsTimer,&QTimer::timeout,this,[this] { updateStatistics(); });
        m_statsTimer.start();

        GltfLoader loader;
        const auto model=darkeye::dvd_scene::loadDvdModel(loader,QStringLiteral(":/dvd_scene_demo/dvd.glb"));
        if(!model || !m_view->setModel(model.asset)) {
            m_error->setText(model.error);
            m_modelReady=false;
            return;
        }
        m_sourceModel=model.asset;
        m_shellPreset->setCurrentIndex(0);
        m_error->setText(model.asset->warnings.join('\n'));
        generateCovers();
        setSampleCount(100);
    }

    bool loadModel(const QString &path)
    {
        GltfLoader loader;
        const auto result=loader.load(path);
        if(!result) { m_error->setText(result.error); return false; }
        if(!m_view->setModel(result.asset)) return false;
        m_sourceModel=result.asset;
        m_shellPreset->setCurrentIndex(0); // respect Blender's authored materials
        m_error->setText(result.asset->warnings.join('\n'));
        return true;
    }

    void applyShellPreset(int preset)
    {
        if(!m_sourceModel) return;
        if(preset==0) { m_view->setModel(m_sourceModel); return; }
        auto model=std::make_shared<ModelAsset>(*m_sourceModel);
        std::map<const Material *,MaterialHandle> replacements;
        for(auto &node:model->nodes) for(auto &primitive:node.primitives) {
            const auto &source=primitive.material;
            if(!source || source->name!="shell") continue;
            auto &replacement=replacements[source.get()];
            if(!replacement) {
                auto material=std::make_shared<Material>(*source);
                material->shading=Material::Shading::MetallicRoughness;
                material->baseColor={0.98f,0.99f,1,1}; material->baseColorTexture.reset();
                material->metallic=0; material->roughness=preset==2?0.07f:0.12f;
                material->metallicRoughnessTexture.reset(); material->transmissionTexture.reset();
                material->transmission=1; material->ior=1.49f;
                material->thickness=preset==2?0:0.0008f;
                material->thicknessTexture.reset();
                material->attenuationColor={0.9f,0.96f,1}; material->attenuationDistance=0.1f;
                material->alphaMode=Material::AlphaMode::Opaque;
                // A volume is rendered at its entry surface; avoid a second full transmission on exit.
                material->doubleSided=false;
                replacement=std::move(material);
            }
            primitive.material=replacement;
        }
        if(preset==3) {
            // Demo-only film: offset the existing paper surfaces, keeping the
            // original GLB and its hinges untouched. Authored films can be loaded.
            auto wrinkles=std::make_shared<TextureData>(); wrinkles->srgb=false;
            wrinkles->mipmap=TextureData::Mipmap::Linear;
            wrinkles->image=QImage(128,128,QImage::Format_RGBA8888);
            for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
                const float edge=0.2f+0.8f*std::pow(std::abs(x/63.5f-1),6)+0.8f*std::pow(std::abs(y/63.5f-1),6);
                constexpr float step=6.28318530718f/128;
                const float nx=edge*(0.022f*std::sin((6*x+y)*step)+0.006f*std::sin(14*y*step));
                const float ny=edge*0.012f*std::sin((4*y+x)*step);
                const auto normal=QVector3D(nx,ny,1).normalized();
                wrinkles->image.setPixelColor(x,y,QColor::fromRgbF(normal.x()*0.5f+0.5f,normal.y()*0.5f+0.5f,normal.z()*0.5f+0.5f));
            }
            auto film=std::make_shared<Material>(); film->name=QStringLiteral("demo-film");
            film->shading=Material::Shading::MetallicRoughness; film->transmission=1;
            film->roughness=0.1f; film->ior=1.49f; film->normalTexture=wrinkles;
            for(auto &node:model->nodes) {
                if(node.name!="back" && node.name!="front") continue;
                const auto originals=node.primitives;
                for(const auto &primitive:originals) {
                    if(!primitive.material || primitive.material->name!="cover") continue;
                    auto surface=std::make_shared<MeshData>(*primitive.mesh);
                    for(auto &vertex:surface->vertices)
                        for(int axis=0;axis<3;++axis) vertex.position[axis]+=vertex.normal[axis]*0.00025f;
                    node.primitives.push_back({surface,film});
                }
            }
        }
        m_view->setModel(std::move(model)); m_statsFingerprint.clear();
    }

    ~DvdDemoWindow() override
    {
        m_statsTimer.stop();
        m_benchmarkTimer.stop();
        delete m_view; // finish decoder jobs before deleting temporary covers
        m_view=nullptr;
    }
    DvdSceneWidget *view() const { return m_view; }
    bool modelReady() const { return m_modelReady; }

    void setSampleCount(int count)
    {
        QList<DvdItemViewModel> items;
        for(int i=0;i<count;++i)
            items.push_back({QStringLiteral("DVD-%1").arg(i+1,4,10,QChar('0')),
                m_coverPaths.isEmpty()?QString():m_coverPaths[i%m_coverPaths.size()],
                QStringLiteral("合成作品 %1").arg(i+1),i,false});
        m_error->clear();
        const int combo=m_count->findData(count);
        if(combo>=0) m_count->setCurrentIndex(combo);
        m_view->setItems(std::move(items));
        m_index->setRange(1,std::max(count,1));
        m_index->setValue(count?count/2+1:1);
    }

    bool loadItems(const QString &path)
    {
        const auto result=DvdSceneAdapter::loadJson(path);
        if(!result) { m_error->setText(result.error); return false; }
        if(!m_view->setItems(result.items)) return false;
        m_index->setRange(1,std::max(1,m_view->itemCount()));
        return true;
    }

    void beginSmoke(const QString &screenshot, const QString &closedScreenshot = {})
    {
        m_view->setEnabled(false); // deterministic replay; mouse input has separate tests
        if(!screenshot.isEmpty() || !closedScreenshot.isEmpty()) {
            m_view->setFixedSize(1280,720);
            m_view->renderWidget()->setSampleCount(4);
            resize(1590,760);
        }
        connect(m_view,&DvdSceneWidget::sceneError,qApp,[](const QString &) { qApp->exit(2); },Qt::QueuedConnection);
        QTimer::singleShot(400,this,[this] {
            if(!m_view->items().isEmpty()) m_view->focusItem(m_view->items()[m_view->itemCount()/2].stableId);
        });
        QTimer::singleShot(1250,this,[this,closedScreenshot] {
            if(!closedScreenshot.isEmpty()
                && !m_view->renderWidget()->grabFramebuffer().save(closedScreenshot,"PNG")) qApp->exit(3);
        });
        QTimer::singleShot(1400,this,[this] { m_view->toggleOpen(); });
        QTimer::singleShot(2200,Qt::PreciseTimer,this,[this,screenshot] {
            const auto image=m_view->renderWidget()->grabFramebuffer();
            const auto &stats=m_view->renderWidget()->statistics();
            if(image.isNull() || !m_view->renderWidget()->lastError().isEmpty()
                || (m_view->itemCount() && (m_view->state()!=DvdInteractionController::State::Focused
                    || !stats.geometryCount || m_view->pendingCovers()))) {
                qCritical("DVD demo smoke test failed before framebuffer capture");
                qApp->exit(2); return;
            }
            if(!screenshot.isEmpty() && !image.save(screenshot,"PNG")) { qApp->exit(3); return; }
            m_view->returnToShelf();
        });
        QTimer::singleShot(3500,Qt::PreciseTimer,this,[this] {
            if(m_view->state()!=DvdInteractionController::State::Idle
                || m_view->visibleItemCount()>60 || !m_view->renderWidget()->lastError().isEmpty()) {
                qCritical("DVD demo smoke test failed while returning");
                qApp->exit(2); return;
            }
            qInfo("DVD demo smoke test passed: %d items, %d visible",m_view->itemCount(),m_view->visibleItemCount());
            qApp->exit(0);
        });
        QTimer::singleShot(15000,qApp,[] { qApp->exit(4); });
    }

    void beginBenchmark(const QString &path)
    {
        m_benchmarkPath=path;
        m_benchmarkItems=m_view->items();
        m_statsTimer.stop();
        m_view->setEnabled(false);
        m_view->setFixedSize(1280,720);
        resize(1590,760);
        m_view->renderWidget()->setSampleCount(4);
        m_benchmarkTimer.setInterval(16);
        m_benchmarkTimer.setTimerType(Qt::PreciseTimer);
        connect(&m_benchmarkTimer,&QTimer::timeout,this,[this] { benchmarkTick(); });
        startBenchmarkScene();
        QTimer::singleShot(30000,qApp,[] { qApp->exit(4); });
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        m_other.hide();
        QMainWindow::closeEvent(event);
    }
    bool eventFilter(QObject *object,QEvent *event) override
    {
        if(object==&m_other && event->type()==QEvent::Close) {
            if(m_view && m_view->parentWidget()==&m_other) moveCanvas();
            event->ignore(); return true;
        }
        return QMainWindow::eventFilter(object,event);
    }
private:
    void startBenchmarkScene()
    {
        static constexpr int counts[]={1,10,100,500,1000};
        if(m_benchmarkIndex>=5) {
            QFile file(m_benchmarkPath);
            if(!QDir().mkpath(QFileInfo(file).absolutePath()) || !file.open(QIODevice::WriteOnly)) {
                qApp->exit(3); return;
            }
            const auto lighting=m_view->renderWidget()->lighting();
            const QJsonObject report{{"qtVersion",qVersion()},{"backend",m_view->renderWidget()->statistics().backend},
                {"lighting",QJsonObject{{"hdrBackground",lighting.environmentBackground},{"ibl",lighting.imageBasedLighting},
                    {"ao",lighting.ambientOcclusion},{"shadows",lighting.shadows},{"toneMapping",lighting.toneMapping},
                    {"exposure",lighting.exposure},{"environmentRotation",lighting.environmentRotation},
                    {"transmissionPassLimit",lighting.transmissionPassLimit},
                    {"environment","lebombo_1k.hdr"}}},
                {"shellPreset",m_shellPreset->currentText()},
                {"measurements",m_benchmarkMeasurements},
                {"note","D3D11 4x MSAA, 1280x720 logical pixels, eight shared covers. Frame submissions and CPU timings, not GPU execution timings. See lighting flags; new IBL/AO/shadow algorithms differ from Qt Quick 3D."}};
            file.write(QJsonDocument(report).toJson());
            file.close();
            qApp->exit(0); return;
        }
        const int count=counts[m_benchmarkIndex];
        m_view->setItems(m_benchmarkItems.mid(0,count));
        m_view->resetCamera();
        m_benchmarkCamera=m_view->renderWidget()->camera();
        m_benchmarkCamera.target.setX((std::min(count,60)-1)*0.0145f*0.5f);
        m_view->renderWidget()->setCamera(m_benchmarkCamera);
        m_benchmarkStep=0;
        m_benchmarkIntervals.clear();
        m_benchmarkCpuFrames.clear();
        QTimer::singleShot(600,this,[this] {
            m_benchmarkFrames=m_view->renderWidget()->statistics().submittedFrames;
            m_benchmarkCpuStart=processCpuSeconds();
            m_benchmarkClock.start();
            m_benchmarkLastNs=0;
            m_benchmarkTimer.start();
        });
    }

    void benchmarkTick()
    {
        const auto time=m_benchmarkClock.nsecsElapsed();
        if(m_benchmarkStep>0) {
            m_benchmarkIntervals.push_back(double(time-m_benchmarkLastNs)/1e6);
            m_benchmarkCpuFrames.push_back(m_view->renderWidget()->statistics().cpuFrameMs);
        }
        m_benchmarkLastNs=time;
        if(m_benchmarkStep>=30) {
            m_benchmarkTimer.stop();
            auto &canvas=*m_view->renderWidget();
            const auto &stats=canvas.statistics();
            std::sort(m_benchmarkIntervals.begin(),m_benchmarkIntervals.end());
            std::sort(m_benchmarkCpuFrames.begin(),m_benchmarkCpuFrames.end());
            const double seconds=double(time)/1e9;
            m_benchmarkMeasurements.append(QJsonObject{
                {"totalDvdCount",m_view->itemCount()},{"visibleDvdCount",m_view->visibleItemCount()},
                {"pixelWidth",stats.pixelSize.width()},{"pixelHeight",stats.pixelSize.height()},
                {"dpr",canvas.devicePixelRatioF()},{"samples",stats.samples},
                {"frameSubmissions",double(stats.submittedFrames-m_benchmarkFrames)},
                {"frameSubmissionHz",double(stats.submittedFrames-m_benchmarkFrames)/seconds},
                {"interactionStepHz",30.0/seconds},
                {"p95InteractionStepMs",m_benchmarkIntervals[28]},
                {"p95CpuSubmissionMs",m_benchmarkCpuFrames[28]},
                {"processCpuSeconds",processCpuSeconds()-m_benchmarkCpuStart},
                {"drawCalls",stats.drawCalls},{"drawnPrimitives",stats.visibleItems},
                {"transmissionPasses",stats.transmissionPasses},{"transmissionFallbackItems",stats.transmissionFallbackItems},
                {"geometryCount",stats.geometryCount},{"bufferBytes",double(stats.bufferBytes)},
                {"textureEstimateBytes",double(stats.textureBytes)},
                {"sourceTextureBytes",double(stats.sourceTextureBytes)},
                {"coverCacheBytes",double(m_view->cachedCoverBytes())},
                {"gpuTimeMs",QJsonValue::Null},{"gpuMemoryBytes",QJsonValue::Null}});
            ++m_benchmarkIndex;
            startBenchmarkScene();
            return;
        }
        auto camera=m_benchmarkCamera;
        camera.yaw=float(m_benchmarkStep%15-7)*0.4f;
        m_view->renderWidget()->setCamera(camera);
        ++m_benchmarkStep;
    }

    void generateCovers()
    {
        if(!m_temp.isValid()) return;
        for(int i=0;i<8;++i) {
            QImage image(512,320,QImage::Format_RGBA8888);
            image.fill(QColor::fromHsv(i*43,155,185));
            QPainter painter(&image);
            painter.fillRect(246,0,20,320,QColor("#17202f"));
            painter.setPen(Qt::white);
            painter.setFont(QFont("Arial",22,QFont::Bold));
            painter.drawText(QRect(0,0,245,320),Qt::AlignCenter,QString("DARKEYE\nBACK %1\nTOP ↑").arg(i+1));
            painter.drawText(QRect(267,0,245,320),Qt::AlignCenter,QString("QRhi DVD\nFRONT %1\nTOP ↑").arg(i+1));
            painter.end();
            const auto path=m_temp.filePath(QString("cover-%1.png").arg(i));
            if(image.save(path)) m_coverPaths.push_back(path);
        }
    }

    void moveCanvas()
    {
        const bool toOther=m_view->parentWidget()!=&m_other;
        m_view->setParent(toOther?&m_other:m_host);
        (toOther?m_otherLayout:m_viewLayout)->addWidget(m_view);
        m_other.setVisible(toOther);
        m_view->show();
    }

    void updateStatistics()
    {
        const auto *canvas=m_view->renderWidget();
        const auto &s=canvas->statistics();
        const auto camera=canvas->camera();
        // A changing frame counter/CPU time would repaint the stats label and
        // can itself wake the canvas. Only refresh after meaningful scene/input
        // changes, so the diagnostics do not create a static render loop.
        const QString fingerprint=QString("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11")
            .arg(canvas->scene()->revision()).arg(s.pixelSize.width()).arg(s.pixelSize.height())
            .arg(s.samples).arg(camera.target.x()).arg(camera.target.y()).arg(camera.yaw)
            .arg(camera.pitch).arg(camera.distance).arg(m_view->cachedCoverBytes()).arg(m_view->pendingCovers());
        if(fingerprint==m_statsFingerprint) return;
        m_statsFingerprint=fingerprint;
        m_stats->setText(QStringLiteral(
            "%1 · %2 × %3 px\nDPR %4 · %5\n"
            "总数 %6 · 驻留 %7\nDraw %8 · 几何 %9\n"
            "CPU %10 ms · GPU 未采集\n"
            "Buffer %11 KiB\n纹理/目标估算 %12 MiB\n封面缓存 %13 MiB · 排队 %14\n透射 %15 次 · 超限 %16 件")
            .arg(s.backend).arg(s.pixelSize.width()).arg(s.pixelSize.height())
            .arg(canvas->devicePixelRatioF(),0,'f',2)
            .arg(DvdInteractionController::stateName(m_view->state()))
            .arg(m_view->itemCount()).arg(m_view->visibleItemCount()).arg(s.drawCalls).arg(s.geometryCount)
            .arg(s.cpuFrameMs,0,'f',2).arg(s.bufferBytes/1024)
            .arg(double(s.textureBytes)/1048576,0,'f',1)
            .arg(double(m_view->cachedCoverBytes())/1048576,0,'f',1).arg(m_view->pendingCovers())
            .arg(s.transmissionPasses).arg(s.transmissionFallbackItems));
    }

    QTemporaryDir m_temp;
    QStringList m_coverPaths;
    QWidget m_other;
    QWidget *m_host=nullptr;
    QVBoxLayout *m_viewLayout=nullptr,*m_otherLayout=nullptr;
    DvdSceneWidget *m_view=nullptr;
    QComboBox *m_count=nullptr;
    QComboBox *m_shellPreset=nullptr;
    std::shared_ptr<const ModelAsset> m_sourceModel;
    QSpinBox *m_index=nullptr;
    QLabel *m_selection=nullptr,*m_stats=nullptr,*m_error=nullptr;
    QTimer m_statsTimer;
    QString m_statsFingerprint;
    bool m_modelReady=true;
    QTimer m_benchmarkTimer;
    QElapsedTimer m_benchmarkClock;
    QString m_benchmarkPath;
    QJsonArray m_benchmarkMeasurements;
    QList<DvdItemViewModel> m_benchmarkItems;
    std::vector<double> m_benchmarkIntervals,m_benchmarkCpuFrames;
    Camera m_benchmarkCamera;
    int m_benchmarkIndex=0,m_benchmarkStep=0;
    qint64 m_benchmarkLastNs=0;
    quint64 m_benchmarkFrames=0;
    double m_benchmarkCpuStart=0;
};

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QCoreApplication::setApplicationName(QStringLiteral("darkeye_dvd_scene_demo"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.setApplicationDescription(QStringLiteral("Independent QRhi DVD shelf; synthetic/JSON data only."));
    parser.addOption({"count","Synthetic DVD count (0..1000).","number","100"});
    parser.addOption({"items","Load standalone DVD items JSON.","path"});
    parser.addOption({"model","Load a DVD GLB/glTF (back/spine/front/disc nodes).","path"});
    parser.addOption({"shell-material","Shell preview: original, plastic, thin, film.","preset"});
    parser.addOption({"transmission-passes","Maximum transmission composition passes (1..16).","number","8"});
    parser.addOption({"smoke-test","Exercise focus/open/return and exit."});
    parser.addOption({"screenshot","Capture an opened DVD and exit.","path"});
    parser.addOption({"screenshot-closed","Also capture the focused closed DVD during smoke replay.","path"});
    parser.addOption({"benchmark","Record 1/10/100/500/1000 DVD measurements and exit.","path"});
    for(const auto &effect:QStringList{"hdr-background","ibl","ao","shadows","tone-mapping"})
        parser.addOption({"no-"+effect,"Disable "+effect+" for visual comparison."});
    parser.process(app);
    bool countOk=false;
    const int count=parser.value("count").toInt(&countOk);
    if(!countOk || count<0 || count>1000) return 2;
    bool passesOk=false;
    const int passLimit=parser.value("transmission-passes").toInt(&passesOk);
    if(!passesOk || passLimit<1 || passLimit>16) return 2;
    DvdDemoWindow window;
    if(!window.modelReady()) return 2;
    window.findChild<QSpinBox *>(QStringLiteral("transmission-passes"))->setValue(passLimit);
    if(parser.isSet("model") && !window.loadModel(parser.value("model"))) return 2;
    if(parser.isSet("shell-material")) {
        const int preset=QStringList{"original","plastic","thin","film"}.indexOf(parser.value("shell-material"));
        if(preset<0) return 2;
        window.findChild<QComboBox *>(QStringLiteral("shell-material"))->setCurrentIndex(preset);
    }
    for(const auto &effect:QStringList{"hdr-background","ibl","ao","shadows","tone-mapping"})
        if(parser.isSet("no-"+effect)) window.findChild<QCheckBox *>(effect)->setChecked(false);
    if(parser.isSet("items")) {
        if(!window.loadItems(parser.value("items"))) return 2;
    } else window.setSampleCount(parser.isSet("benchmark")?1000:count);
    window.show();
    if(parser.isSet("benchmark")) {
        if(window.view()->itemCount()<1000) {
            qCritical("Benchmark input requires at least 1000 DVD items");
            return 2;
        }
        window.beginBenchmark(parser.value("benchmark"));
    }
    else if(parser.isSet("smoke-test") || parser.isSet("screenshot") || parser.isSet("screenshot-closed"))
        window.beginSmoke(parser.value("screenshot"),parser.value("screenshot-closed"));
    return app.exec();
}
