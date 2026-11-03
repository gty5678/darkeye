#include "ui/components/DvdShelfView.h"

#include "app/Resources.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/WorkRepository.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "graph/GraphManager.h"
#include "graph_view/GraphViewWidget.h"
#include "graph_view/ForceViewRhiWidget.h"
#include "settings/Settings.h"
#include "ui/components/FanartStripWidget.h"
#include "utils/MediaUtils.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QJsonArray>
#include <QJsonDocument>
#include "darkeye_ui/layouts/FlowLayout.h"
#include <QTextLayout>
#include <QFontMetricsF>
#include <QScrollBar>
#include <QStyle>
#include <QLabel>
#include "dvd_scene/DvdSceneWidget.h"
#include "dvd_scene/DvdModelAsset.h"
#include <QtMath>

#include <cmath>

namespace darkeye
{

namespace
{

constexpr qreal dvdSpacing = 0.0145;
constexpr qreal shelfCameraDistance = 0.25;
constexpr qreal shelfVerticalFieldOfView = 60.0;
constexpr qreal dvdHalfWidth = 0.0067071;

QColor readableTextColor(const QString &background)
{
    const QColor color(background);
    return color.isValid() && color.lightness() < 135 ? QColor(Qt::white) : QColor(Qt::black);
}

QString coverExcerpt(const QString &text,QFont font,int width,int lines)
{
    QString normalized=text;
    normalized.replace('\n',QChar::LineSeparator);
    QTextLayout layout(normalized,font);
    QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    int lastStart=0,end=0;
    QString result;
    for(int i=0;i<lines;++i) {
        auto line=layout.createLine();
        if(!line.isValid()) break;
        line.setLineWidth(std::max(1,width));
        lastStart=line.textStart(); end=lastStart+line.textLength();
        if(i<lines-1) {
            auto part=normalized.mid(lastStart,line.textLength());
            while(part.endsWith(QChar::LineSeparator) || part.endsWith(' ')) part.chop(1);
            result+=part+QLatin1Char('\n');
        }
    }
    layout.endLayout();
    if(end>=normalized.size()) return text;
    return result+QFontMetricsF(font).elidedText(normalized.mid(lastStart).simplified(),Qt::ElideRight,width);
}

} // namespace

DvdShelfView::DvdShelfView(QSqlDatabase database, QSqlDatabase privateDatabase,
                           graph::GraphManager *graphManager, const QString &coverDirectory,
                           const QString &fanartDirectory, QWidget *parent)
    : QWidget(parent), m_database(std::move(database)), m_privateDatabase(std::move(privateDatabase)),
      m_graphManager(graphManager), m_coverDirectory(coverDirectory),
      m_fanartDirectory(fanartDirectory), m_scene(new dvd_scene::DvdSceneWidget(this))
{
    Resources::ensureInitialized();
    setObjectName(QStringLiteral("DvdShelfView"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_scene, 1);
    m_scene->setObjectName(QStringLiteral("ShelfDvdScene"));
    m_scene->setMinimumHeight(400);
    m_scene->renderWidget()->setSampleCount(4);
    m_scene->renderWidget()->setStatisticsVisible(false);
    m_scene->renderWidget()->installEventFilter(this);
    m_scene->setCoverBudget(128ULL*1024*1024);
    m_contextMenu = new QMenu(this);
    m_contextMenu->setObjectName(QStringLiteral("ShelfContextMenu"));
    const auto action = [this](const QString &text, const QString &name, auto handler) {
        auto *item = m_contextMenu->addAction(text);
        item->setObjectName(name);
        connect(item, &QAction::triggered, this, handler);
        return item;
    };
    m_openAction = action(QStringLiteral("开盒"), QStringLiteral("ShelfToggleOpen"), [this] { m_scene->toggleOpen(); });
    m_playAction = action(QStringLiteral("播放本地视频"), QStringLiteral("ShelfPlay"), [this] { on_cd_clicked(selectedIndex()); });
    m_favoriteAction = action(QStringLiteral("收藏"), QStringLiteral("ShelfFavorite"), [this] { on_heart_clicked(selectedIndex()); });
    action(QStringLiteral("复制番号"), QStringLiteral("ShelfCopyCode"), [this] { copy_to_clipboard(m_code); });
    action(QStringLiteral("编辑"), QStringLiteral("ShelfEdit"), [this] { on_edit_clicked(selectedIndex()); });
    action(QStringLiteral("删除"), QStringLiteral("ShelfDelete"), [this] { on_delete_clicked(selectedIndex()); });
    action(QStringLiteral("放回书架（Esc）"), QStringLiteral("ShelfReturn"), [this] { m_scene->returnToShelf(); });
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_scene->renderWidget()->statisticsAction());
    m_coverInfo=new QScrollArea(this);
    m_coverInfo->setObjectName(QStringLiteral("ShelfCoverInfo"));
    m_coverInfo->setWidgetResizable(false);
    m_coverInfo->setFrameShape(QFrame::NoFrame);
    m_coverInfo->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_coverInfo->setStyleSheet(QStringLiteral(
        "QScrollArea#ShelfCoverInfo { background:transparent;border:0; }"
        "QScrollArea#ShelfCoverInfo > QWidget { background:transparent; }"
        "QScrollArea#ShelfCoverInfo > QWidget > QWidget { background:transparent; }"
        "QScrollBar:vertical { background:transparent;width:8px;margin:8px 0; }"
        "QScrollBar::handle:vertical { background:#78808c;border-radius:4px;min-height:24px; }"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }"));
    m_coverInfo->viewport()->setAutoFillBackground(false);
    m_coverInfo->hide();
    m_coverActions=new QWidget(this);
    m_coverActions->setObjectName("ShelfCoverActions");
    m_coverActions->setAutoFillBackground(false);
    m_coverActions->setStyleSheet("QWidget#ShelfCoverActions { background:transparent;border:0; }");
    auto *tools=new QVBoxLayout(m_coverActions);
    tools->setContentsMargins(0,0,0,0); tools->setSpacing(8);
    const auto tool=[&](const QString &name,const QString &icon,const QString &tip,auto handler) {
        auto *button=new QToolButton(m_coverActions); button->setObjectName(name);
        button->setFixedSize(40,40); button->setIcon(QIcon(icon)); button->setIconSize(QSize(26,26));
        button->setToolTip(tip); button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet("QToolButton { background:transparent;border:0;border-radius:6px; } QToolButton:hover { background:rgba(255,255,255,100); }");
        connect(button,&QToolButton::clicked,this,handler); tools->addWidget(button); return button;
    };
    m_coverFavorite=tool("ShelfCoverFavorite",":/icons/dvd/love-off.svg",QStringLiteral("收藏"),[this]{on_heart_clicked(selectedIndex());});
    tool("ShelfCoverEdit",":/icons/dvd/square-pen.svg",QStringLiteral("编辑"),[this]{on_edit_clicked(selectedIndex());});
    tool("ShelfCoverDelete",":/icons/dvd/trash-2.svg",QStringLiteral("删除"),[this]{on_delete_clicked(selectedIndex());});
    m_coverActions->setFixedSize(40,136); m_coverActions->hide();
    connect(this,&DvdShelfView::expandedWorkMetaChanged,this,&DvdShelfView::updateCoverInfo);
    connect(this,&DvdShelfView::expandedWorkMetaChanged,this,&DvdShelfView::layoutOverlays);
    connect(m_scene,&dvd_scene::DvdSceneWidget::presentationChanged,this,&DvdShelfView::layoutOverlays);
    m_sceneError = new QLabel(this);
    m_sceneError->setObjectName(QStringLiteral("ShelfSceneError"));
    m_sceneError->setWordWrap(true);
    m_sceneError->hide();
    connect(m_scene, &dvd_scene::DvdSceneWidget::sceneError, this, [this](const QString &error) {
        m_sceneError->setText(error); m_sceneError->adjustSize(); m_sceneError->show(); m_sceneError->raise();
    });
    connect(m_scene,&dvd_scene::DvdSceneWidget::coverWarning,this,[](const QString &message) {
        qWarning().noquote()<<"Shelf cover:"<<message;
    });
    connect(this, &DvdShelfView::expandedWorkMetaChanged, this, &DvdShelfView::updateActions);
    connect(this, &DvdShelfView::expandedWorkFavoritedChanged, this, &DvdShelfView::updateActions);
    connect(m_scene, &dvd_scene::DvdSceneWidget::selectionChanged, this, [this] { updateSelection(); });
    connect(m_scene, &dvd_scene::DvdSceneWidget::stateChanged, this, [this] {
        if(m_scene->state()==dvd_scene::DvdInteractionController::State::Focused
            && m_pendingOpen==m_scene->selectedId()) {
            m_pendingOpen.clear();
            if(!m_scene->isExpanded()) m_scene->toggleOpen();
        }
        updateSelection();
    });
    connect(m_scene, &dvd_scene::DvdSceneWidget::expandedChanged, this, [this] { updateSelection(); });
    connect(m_scene, &dvd_scene::DvdSceneWidget::cameraPositionChanged, this, [this] {
        m_cameraX=m_scene->cameraX(); m_cameraTargetX=m_scene->cameraTargetX();
        emit cameraXChanged(); emit cameraTargetXChanged();
    });
    connect(m_scene, &dvd_scene::DvdSceneWidget::discClicked, this, [this](const QString &id) {
        if(id==m_scene->selectedId()) on_cd_clicked(selectedIndex());
    });
    if (m_graphManager != nullptr)
    {
        m_relationGraphContainer = new QWidget(this);
        m_relationGraphContainer->setObjectName(QStringLiteral("ShelfRelationGraphOverlay"));
        auto *graphLayout = new QVBoxLayout(m_relationGraphContainer);
        graphLayout->setContentsMargins(0, 0, 0, 0);
        m_relationGraph = new graph_view::GraphViewWidget(*m_graphManager, m_relationGraphContainer);
        m_relationGraph->view()->setBackgroundColor(Qt::white);
        graphLayout->addWidget(m_relationGraph);
        connect(m_relationGraph, &graph_view::GraphViewWidget::nodeLeftClicked, this,
                [this](const QString &nodeId) {
                    if (nodeId.size() < 2) return;
                    bool ok = false;
                    const qint64 id = nodeId.mid(1).toLongLong(&ok);
                    if (!ok || id <= 0) return;
                    if (nodeId.startsWith(QLatin1Char('w')))
                        emit workSelected(id);
                    else if (nodeId.startsWith(QLatin1Char('a')))
                        emit actressRequested(id);
                });
        m_relationGraphContainer->hide();
    }
    m_fanartContainer = new QWidget(this);
    m_fanartContainer->setObjectName(QStringLiteral("ShelfFanartOverlay"));
    auto *fanartLayout = new QVBoxLayout(m_fanartContainer);
    fanartLayout->setContentsMargins(6, 4, 6, 4);
    m_fanartStrip = new FanartStripWidget(
        m_fanartDirectory, m_coverDirectory,
        QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/image")), m_fanartContainer);
    m_fanartStrip->setCanAdd(false);
    m_fanartStrip->setMode(FanartStripWidget::Mode::Display);
    m_fanartContainer->setStyleSheet(QStringLiteral(
        "QWidget { background:transparent; }"
        "QLabel { background:transparent;color:#354052; }"
        "QScrollBar:horizontal { background:transparent;height:6px;margin:0; }"
        "QScrollBar::handle:horizontal { background:rgba(53,64,82,110);min-width:24px;border-radius:3px; }"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal { width:0; }"
        "QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal { background:transparent; }"));
    for(auto *scroll:m_fanartStrip->findChildren<QScrollArea *>()) {
        scroll->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        if(scroll->widget()) scroll->widget()->setAutoFillBackground(false);
    }
    fanartLayout->addWidget(m_fanartStrip);
    m_fanartContainer->hide();
    connect(m_fanartStrip, &FanartStripWidget::fanartChanged,
            this, &DvdShelfView::persistFanartPreview);
    render3d::GltfLoader loader;
    const auto model=dvd_scene::loadDvdModel(loader,QStringLiteral(":/assets/dvd/dvd.glb"));
    auto shelfModel=model?std::make_shared<render3d::ModelAsset>(*model.asset):nullptr;
    if(shelfModel) {
        // Paper remains matte; the separate plastic shell supplies reflections.
        // The exported pic roughness (0.08) otherwise washes bright artwork out.
        for(auto &node:shelfModel->nodes) for(auto &primitive:node.primitives) {
            if(!primitive.material) continue;
            const auto name=primitive.material->name;
            if(name!="trans" && name!="cover" && name!="disc-cover" && !(node.name=="disc" && (name=="transparent" || name=="rainbow"))) continue;
            auto paper=std::make_shared<render3d::Material>(*primitive.material);
            paper->roughness=0.9f;
            if(name=="trans") {
                paper->baseColor={1,1,1,1};
                paper->metallic=0; paper->transmission=0; paper->thickness=0;
                paper->alphaMode=render3d::Material::AlphaMode::Opaque;
                paper->transmissionTexture.reset();
            }
            if(node.name=="disc") {
                paper->metallic=0;
                paper->transmission=0;
                if(name=="transparent") {
                    paper->baseColor={1,1,1,0.8f};
                    paper->alphaMode=render3d::Material::AlphaMode::Blend;
                } else if(name=="rainbow") {
                    paper->baseColor={0.604f,0.604f,0.604f,1};
                    paper->metallic=0.15f; paper->roughness=0.85f;
                }
            }
            primitive.material=std::move(paper);
        }
    }
    if(!shelfModel || !m_scene->setModel(shelfModel)) {
        m_sceneError->setText(QStringLiteral("DVD 模型载入失败：%1").arg(model.error));
        m_sceneError->show();
    }
    QString environmentError;
    static const auto environment=render3d::EnvironmentMap::loadHdr(QStringLiteral(":/assets/hdr/lebombo_1k.hdr"),&environmentError);
    m_scene->renderWidget()->setEnvironment(environment);
    render3d::LightingSettings lighting;
    lighting.environmentBackground=lighting.imageBasedLighting=bool(environment);
    lighting.ambientOcclusion=lighting.shadows=lighting.toneMapping=true;
    // The legacy QML probeOrientation was (0,155,0). Rotate both the sky and
    // IBL together. Lift exposure and fill light so covers and shadowed spines
    // remain readable, while tone mapping preserves paper/plastic highlights.
    lighting.environmentRotation=155;
    lighting.exposure=0.95f;
    lighting.environmentIntensity=1.0f;
    lighting.keyLightDirection={-0.5f,0.8f,1.0f};
    lighting.keyLightIntensity=2.2f;
    // Keep the key light fixed above-left of the initial camera view.
    lighting.keyLightCameraRelative=false;
    m_scene->renderWidget()->setLighting(lighting);
    if(!environment) qWarning().noquote()<<environmentError;
}

void DvdShelfView::setWorks(const QList<WorkSummary> &works)
{
    m_settingWorks=true;
    m_pendingOpen.clear();
    m_scene->setItems({});
    m_works=works;
    m_useInitialCamera=true;
    clearExpandedMeta();
    hideOverlays();
    QList<dvd_scene::DvdItemViewModel> items;
    items.reserve(works.size());
    for(int i=0;i<works.size();++i) {
        const auto &work=works[i];
        QString cover=work.imageUrl.trimmed();
        const QUrl url(cover);
        if(url.isLocalFile()) cover=url.toLocalFile();
        else if(cover.startsWith("qrc:/")) cover=QStringLiteral(":")+cover.mid(4);
        else if(!cover.isEmpty() && !QFileInfo(cover).isAbsolute()) cover=QDir(m_coverDirectory).filePath(cover);
        if(!cover.isEmpty() && !QFileInfo::exists(cover)) {
            qWarning().noquote()<<"Shelf cover file missing for work"<<work.id<<":"<<cover;
            cover.clear();
        }
        if(cover.isEmpty()) cover=QStringLiteral(":/assets/maps/0.png");
        const auto title=!work.chineseTitle.isEmpty()?work.chineseTitle:work.serialNumber;
        items.append({QString::number(work.id),cover,title,i,false});
    }
    m_scene->setItems(std::move(items));
    updateInitialCamera();
    m_settingWorks=false;
    updateSelection();
}

qreal DvdShelfView::initialCameraX() const
{
    if(m_works.isEmpty()) return 0;
    const qreal visibleWidth=2.0*shelfCameraDistance*qTan(qDegreesToRadians(shelfVerticalFieldOfView*0.5))
        *qreal(m_scene->width())/qMax(1,m_scene->height());
    return qMin((m_works.size()-1)*dvdSpacing*0.5,qMax(qreal(0),visibleWidth*0.5-dvdHalfWidth));
}

void DvdShelfView::updateInitialCamera()
{
    if(m_useInitialCamera) m_scene->setCameraX(float(initialCameraX()),true);
    m_cameraX=m_scene->cameraX(); m_cameraTargetX=m_scene->cameraTargetX();
    emit cameraXChanged(); emit cameraTargetXChanged();
}

void DvdShelfView::set_camera_x(qreal x)
{
    m_useInitialCamera=false;
    m_scene->setCameraX(float(x));
}
void DvdShelfView::scroll_camera_by(qreal delta,qreal) { set_camera_x(m_scene->cameraTargetX()+delta); }
qint64 DvdShelfView::workIdAt(int index) const { return index>=0 && index<m_works.size()?m_works[index].id:0; }
int DvdShelfView::selectedIndex() const
{
    const auto id=currentWorkId();
    for(int i=0;i<m_works.size();++i) if(m_works[i].id==id) return i;
    return -1;
}
void DvdShelfView::selection_changed(int selected,int expanded)
{
    if(selected<0 && expanded<0) m_scene->returnToShelf();
}
void DvdShelfView::updateSelection()
{
    if(m_settingWorks) return;
    const int index=selectedIndex();
    const bool returning=m_scene->state()==dvd_scene::DvdInteractionController::State::Returning;
    if(index<0) { m_pendingOpen.clear(); clearExpandedMeta(); hideOverlays(); return; }
    refresh_expanded_work_meta(index);
    if(returning || m_scene->isExpanded()) {
        if(m_relationGraphContainer) m_relationGraphContainer->hide();
    } else if(m_relationGraph) {
        const auto workId=workIdAt(index);
        if(m_relationGraphWorkId!=workId) {
            m_relationGraphContainer->hide();
            m_relationGraphWorkId=workId;
            if (!m_graphManager->isInitialized()) m_graphManager->scheduleInitialize();
            // Start the snapshot, force layout and font build during pull-out.
            // The same hidden widget is revealed when the focus animation ends.
            m_relationGraph->setEgoGraph(QStringLiteral("w%1").arg(workId),3,true);
        }
        if(m_scene->state()==dvd_scene::DvdInteractionController::State::Focused)
            showRelationGraph(workId);
    }
    updateActions(); layoutOverlays();
}
void DvdShelfView::updateActions()
{
    m_favoriteAction->setText(m_favorited?QStringLiteral("取消收藏"):QStringLiteral("收藏"));
    m_openAction->setText(m_scene->isExpanded()?QStringLiteral("合盒"):QStringLiteral("开盒"));
    m_openAction->setEnabled(m_scene->state()==dvd_scene::DvdInteractionController::State::Focused);
    m_playAction->setEnabled(m_hasLocalVideo);
    m_coverFavorite->setIcon(QIcon(m_favorited?":/icons/dvd/love-on.svg":":/icons/dvd/love-off.svg"));
    m_coverFavorite->setToolTip(m_favoriteAction->text());
}
void DvdShelfView::updateCoverInfo()
{
    QVariantList signature{m_title,m_story,m_code,m_releaseDate,m_director,m_studio,m_label,m_series,
        m_makerId,m_labelId,m_seriesId,m_coverTextWidth,m_actresses,m_actors,m_tags};
    const auto key=QJsonDocument(QJsonArray::fromVariantList(signature)).toJson(QJsonDocument::Compact);
    if(key==m_coverInfoSignature) return;
    m_coverInfoSignature=key;
    delete m_coverInfo->takeWidget();
    m_coverContent=new QWidget;
    m_coverContent->setObjectName("ShelfCoverContent");
    m_coverContent->setStyleSheet(QStringLiteral(
        "QWidget#ShelfCoverContent, QWidget#ShelfCoverContent QWidget { background:transparent; }"
        "QLabel { color:#263140; background:transparent; border:0; font-size:14px; }"));
    auto *column=new QVBoxLayout(m_coverContent);
    column->setContentsMargins(12,12,12,12); column->setSpacing(8);
    column->setAlignment(Qt::AlignTop);
    auto titleFont=m_coverInfo->font(); titleFont.setPixelSize(18); titleFont.setBold(true);
    auto bodyFont=m_coverInfo->font(); bodyFont.setPixelSize(14); bodyFont.setBold(false);
    const auto label=[&](const QString &text,const QString &name,const QString &color=QStringLiteral("#263140")) {
        auto *widget=new QLabel(text,m_coverContent);
        widget->setObjectName(name); widget->setTextFormat(Qt::PlainText);
        widget->setFont(bodyFont);
        widget->setWordWrap(true); widget->setStyleSheet(QStringLiteral("color:%1;").arg(color));
        column->addWidget(widget); return widget;
    };
    auto *title=label(coverExcerpt(m_title,titleFont,m_coverTextWidth,2),"ShelfCoverTitle","#202938");
    title->setFont(titleFont); title->setStyleSheet("color:#202938;font-size:18px;font-weight:bold;");
    if(!m_story.isEmpty()) label(coverExcerpt(m_story,bodyFont,m_coverTextWidth,6),"ShelfCoverStory");
    const auto button=[&](const QString &text,const QString &name,auto handler,const QString &background=QString()) {
        auto *widget=new QToolButton(m_coverContent); widget->setText(text); widget->setObjectName(name);
        widget->setCursor(Qt::PointingHandCursor); widget->setFont(bodyFont);
        widget->setToolTip(text);
        const QString &fill=background;
        widget->setStyleSheet(fill.isEmpty()
            ? QStringLiteral("QToolButton { color:#263140;background:transparent;border:0;padding:0;font-size:14px; } QToolButton:hover { text-decoration:underline; }")
            : QStringLiteral("QToolButton { color:%1;background:%2;border:1px solid transparent;border-radius:4px;padding:2px 5px;font-size:14px; } QToolButton:hover { border-color:#a0a8b8; }")
                .arg(readableTextColor(fill).name(),fill));
        const QFontMetricsF metrics(bodyFont);
        // Reserve the chip border in both states so hovering cannot elide the text.
        const int padding=fill.isEmpty()?0:6;
        widget->setFixedHeight(qCeil(metrics.height())+padding);
        widget->setFixedWidth(std::min(m_coverTextWidth,qCeil(metrics.horizontalAdvance(text))+(fill.isEmpty()?0:12)));
        connect(widget,&QToolButton::clicked,this,handler); return widget;
    };
    const auto row=[&](const QString &prefix,QToolButton *value) {
        auto *line=new QHBoxLayout; line->setSpacing(4); line->setContentsMargins(0,0,0,0);
        auto *caption=new QLabel(prefix,m_coverContent); caption->setStyleSheet("color:#687080;");
        caption->setFont(bodyFont);
        value->setFixedWidth(std::min(value->width(),std::max(24,m_coverTextWidth-caption->sizeHint().width()-4)));
        line->addWidget(caption); line->addWidget(value); line->addStretch(); column->addLayout(line);
    };
    if(!m_code.isEmpty()) row(QStringLiteral("番号: "),button(m_code,"ShelfCode",[this]{copy_to_clipboard(m_code);}));
    if(!m_releaseDate.isEmpty()) label(QStringLiteral("发布日期: ")+m_releaseDate,"ShelfReleaseDate","#687080");
    const auto people=[&](const QVariantList &list,const QString &kind,const QString &caption,const QString &idKey,const QString &nameKey) {
        if(list.isEmpty()) return;
        auto *section=new QWidget(m_coverContent); auto *stack=new QVBoxLayout(section);
        stack->setContentsMargins(0,0,0,0); stack->setSpacing(4);
        auto *heading=new QLabel(caption,section); heading->setStyleSheet("color:#687080;"); stack->addWidget(heading);
        auto *chips=new QWidget(section); auto *flow=new FlowLayout(chips,0,4);
        for(const auto &entry:list) {
            const auto person=entry.toMap(); const auto id=person.value(idKey).toLongLong();
            const auto background=kind=="tag"?person.value("color").toString():QStringLiteral("#ffffff");
            flow->addWidget(button(person.value(nameKey).toString(),kind+QString::number(id),[this,kind,id] {
                if(kind=="actress") on_actress_clicked(id);
                else if(kind=="actor") on_actor_clicked(id);
                else on_tag_clicked(id);
            },QColor(background).isValid()?background:QStringLiteral("#cccccc")));
        }
        stack->addWidget(chips); column->addWidget(section);
    };
    people(m_actresses,"actress",QStringLiteral("女优"),"actress_id","actress_name");
    people(m_actors,"actor",QStringLiteral("男优"),"actor_id","actor_name");
    if(!m_director.isEmpty()) row(QStringLiteral("导演: "),button(m_director,"ShelfDirector",[this]{on_director_clicked();}));
    if(!m_studio.isEmpty()) row(QStringLiteral("片商: "),button(m_studio,"ShelfMaker",[this]{on_studio_clicked();}));
    if(!m_label.isEmpty()) row(QStringLiteral("厂牌: "),button(m_label,"ShelfLabel",[this]{on_label_clicked();}));
    if(!m_series.isEmpty()) row(QStringLiteral("系列: "),button(m_series,"ShelfSeries",[this]{on_series_clicked();}));
    people(m_tags,"tag",QStringLiteral("作品标签"),"tag_id","tag_name");
    m_coverInfo->setWidget(m_coverContent);
}
void DvdShelfView::layoutOverlays()
{
    // expanded becomes false as soon as closing starts. Reveal the prepared
    // graph only near the end of the actual lid animation.
    if(m_relationGraphContainer && m_relationGraphContainer->isHidden())
        showRelationGraph(m_relationGraphWorkId);
    const auto front=m_scene->selectedFrontRect();
    if(const auto anchor=m_scene->selectedInfoAnchor())
        set_force_view_overlay(anchor->x(),anchor->y());
    if(m_settingWorks || !m_scene->isFullyExpanded() || !front || front->width()<80
        || front->height()<80 || (m_code.isEmpty() && m_title.isEmpty())) {
        if(m_coverInfo) m_coverInfo->hide();
        if(m_coverActions) m_coverActions->hide();
        if(m_fanartContainer) m_fanartContainer->hide();
        return;
    }
    const auto offset=m_scene->renderWidget()->mapTo(this,QPoint());
    const QRectF cover=front->translated(offset);
    // Match the legacy overlay width and natural content height. Reserving a
    // scrollbar before it is needed forces extra text/chip rows.
    const int width=qRound(std::min(cover.width()-16,std::clamp(m_scene->height()*0.55,240.0,600.0)));
    const int maxHeight=m_scene->height()-24;
    const int fullTextWidth=std::max(40,width-24);
    const auto measureHeight=[&](int available) {
        const auto *contentLayout=m_coverContent->layout();
        return contentLayout->hasHeightForWidth()
            ?contentLayout->totalHeightForWidth(available):contentLayout->sizeHint().height();
    };
    const bool needsScroll=measureHeight(width)>maxHeight;
    m_coverInfo->setVerticalScrollBarPolicy(needsScroll?Qt::ScrollBarAsNeeded:Qt::ScrollBarAlwaysOff);
    const int scrollWidth=needsScroll?8:0;
    const int textWidth=std::max(40,fullTextWidth-scrollWidth);
    if(m_coverTextWidth!=textWidth) {
        m_coverTextWidth=textWidth;
        updateCoverInfo();
    }
    const int naturalHeight=measureHeight(width-scrollWidth);
    // QScrollArea's automatic sizing can add a scrollbar-width reflow even
    // when the measured content fits. Use the same explicit width and height.
    m_coverContent->resize(width-scrollWidth,naturalHeight);
    const int height=std::min(maxHeight,naturalHeight);
    auto center=m_scene->selectedInfoAnchor().value_or(front->center())+QPointF(offset);
    center.setX(std::clamp(center.x(),cover.left()+width*0.5,cover.right()-width*0.5));
    m_coverInfo->setGeometry(qRound(center.x()-width*0.5),qRound(center.y()-height*0.5),width,height);
    m_coverInfo->show(); m_coverInfo->raise();
    if(const auto spine=m_scene->selectedSpineCenter()) {
        m_coverActions->move(qRound(spine->x()+offset.x()-20),qRound(spine->y()+offset.y()-68));
        m_coverActions->show(); m_coverActions->raise();
    }
    if(const auto disc=m_scene->selectedDiscCenter())
        set_fanart_strip_layout(disc->x()+offset.x()-width*0.5,
            disc->y()+offset.y()+m_scene->height()*0.41-124,width);
}

void DvdShelfView::refresh_expanded_work_meta(int virtualIndex)
{
    const qint64 workId = workIdAt(virtualIndex);
    if (!workId)
    {
        clearExpandedMeta();
        clearFanartOverlay();
        return;
    }
    QString error;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), true, &error)) return;
    const auto details = WorkRepository(connection.database()).findDetailsById(workId, &error);
    if (!details.has_value()) { clearExpandedMeta(); return; }
    const WorkDetails &value = *details;
    m_title = !value.work.chineseTitle.isEmpty() ? value.work.chineseTitle
                                                  : value.work.japaneseTitle;
    if (m_title.isEmpty()) m_title = value.work.serialNumber;
    m_story = !value.work.chineseStory.isEmpty() ? value.work.chineseStory
                                                  : value.work.japaneseStory;
    m_code = value.work.serialNumber;
    m_releaseDate = value.work.releaseDate;
    m_director = value.work.director;
    m_studio = value.makerName;
    m_label = value.labelName;
    m_series = value.seriesName;
    m_makerId = value.work.makerId.value_or(-1);
    m_labelId = value.work.labelId.value_or(-1);
    m_seriesId = value.work.seriesId.value_or(-1);
    m_hasLocalVideo = !value.work.videoUrl.trimmed().isEmpty();
    // Metadata refresh also follows video imports/removals without rebuilding
    // the shelf or losing the opened case and camera position.
    const auto sceneItems=m_scene->items();
    for(auto item:sceneItems) {
        if(item.stableId!=QString::number(workId)) continue;
        if(item.hasLocalVideo!=m_hasLocalVideo) {
            item.hasLocalVideo=m_hasLocalVideo;
            m_scene->updateItem(item);
        }
        break;
    }
    m_actresses.clear(); m_actors.clear(); m_tags.clear();
    for (const auto &person : value.actresses)
        m_actresses.append(QVariantMap{{"actress_id", person.id}, {"actress_name", person.name}});
    for (const auto &person : value.actors)
        m_actors.append(QVariantMap{{"actor_id", person.id}, {"actor_name", person.name}});
    for (const auto &tag : value.tags)
        m_tags.append(QVariantMap{{"tag_id", tag.id}, {"tag_name", tag.name},
                                  {"color", tag.color},
                                  {"text_color", readableTextColor(tag.color).name()}});
    emit expandedWorkMetaChanged();
    refresh_expanded_favorite_state(virtualIndex);

    QList<FanartEntry> fanartEntries;
    QString fanartError;
    if (m_fanartStrip != nullptr &&
        FanartStripWidget::parseJson(value.work.fanartJson, &fanartEntries, &fanartError))
    {
        m_overlayWorkId = workId;
        m_fanartStrip->setEntries(fanartEntries);
    }
    else if (m_fanartStrip != nullptr)
    {
        m_fanartStrip->setEntries({});
    }
}

void DvdShelfView::refresh_expanded_favorite_state(int virtualIndex)
{
    const qint64 workId = workIdAt(virtualIndex);
    QString error;
    SqliteConnection connection;
    const bool favorite = workId && m_privateDatabase.isValid()
        && connection.open(m_privateDatabase.databaseName(), true, &error)
        && PrivateRepository(connection.database()).isFavoriteWork(workId, &error);
    if (m_favorited != favorite) { m_favorited = favorite; emit expandedWorkFavoritedChanged(); }
}

void DvdShelfView::on_heart_clicked(int virtualIndex)
{
    const qint64 workId = workIdAt(virtualIndex);
    if (!workId || !m_privateDatabase.isValid()) return;
    QString error;
    SqliteConnection connection;
    if (!connection.open(m_privateDatabase.databaseName(), false, &error))
    {
        ToastNotification::showError(window(), QStringLiteral("收藏操作失败：%1").arg(error));
        return;
    }
    PrivateRepository privateRepository(connection.database());
    // Like Python, read the persisted state at click time: another page may
    // have changed it since this DVD was expanded.
    const bool favorite = privateRepository.isFavoriteWork(workId, &error);
    if (!error.isEmpty())
    {
        ToastNotification::showError(window(), QStringLiteral("收藏操作失败：%1").arg(error));
        return;
    }
    const bool changed = favorite
        ? privateRepository.removeFavoriteWork(workId, &error)
        : privateRepository.addFavoriteWork(workId, m_works.at(virtualIndex).serialNumber, &error);
    if (!changed)
    {
        ToastNotification::showError(window(), QStringLiteral("收藏操作失败：%1").arg(error));
        return;
    }
    if (m_favorited != !favorite)
    {
        m_favorited = !favorite;
        emit expandedWorkFavoritedChanged();
    }
    emit favoriteChanged(workId, !favorite);
}

void DvdShelfView::on_cd_clicked(int virtualIndex)
{
    const qint64 workId = workIdAt(virtualIndex);
    if (!workId) return;
    QString error;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), true, &error)) return;
    const auto work = WorkRepository(connection.database()).findById(workId, &error);
    if (!work.has_value()) return;
    QStringList paths;
    for (const QString &raw : work->videoUrl.split(',', Qt::SkipEmptyParts))
    {
        const QString path = raw.trimmed();
        if (!path.isEmpty()) paths.append(path);
    }
    if (paths.isEmpty())
    {
        ToastNotification::showMessage(window(), QStringLiteral("没有可播放的视频"),
                                       ToastNotification::Level::Info, 2500);
        return;
    }
    const auto play = [this](const QString &path) {
        if (!utils::playVideo(path, settings::app().localVideoPlayer))
            ToastNotification::showMessage(window(), QStringLiteral("无法播放视频：%1").arg(path),
                                           ToastNotification::Level::Error, 3500);
    };
    if (paths.size() == 1)
    {
        play(paths.constFirst());
        return;
    }
    QMenu menu(this);
    for (const QString &path : std::as_const(paths))
    {
        QAction *action = menu.addAction(QFileInfo(path).fileName());
        action->setData(path);
    }
    if (QAction *selected = menu.exec(QCursor::pos())) play(selected->data().toString());
}

void DvdShelfView::on_edit_clicked(int virtualIndex)
{
    const qint64 workId = workIdAt(virtualIndex);
    if (workId) emit editRequested(workId);
}

void DvdShelfView::on_delete_clicked(int virtualIndex)
{
    const qint64 workId = workIdAt(virtualIndex);
    if (!workId || QMessageBox::question(this, QStringLiteral("确认删除"),
        QStringLiteral("确定要删除该作品吗？")) != QMessageBox::Yes) return;
    QString error;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), false, &error)) return;
    if (WorkRepository(connection.database()).setDeleted(workId, true, &error))
    {
        ToastNotification::showSuccess(window(), QStringLiteral("已标记删除"));
        emit workDeleted(workId);
    }
}

void DvdShelfView::copy_to_clipboard(const QString &text) const
{
    if (text.isEmpty() || !QApplication::clipboard()) return;
    QApplication::clipboard()->setText(text);
    ToastNotification::showSuccess(window(), QStringLiteral("番号已复制"), nullptr, 2000);
}

void DvdShelfView::on_actress_clicked(qint64 actressId)
{
    if (actressId > 0) emit actressRequested(actressId);
}

void DvdShelfView::on_actor_clicked(qint64 actorId)
{
    if (actorId > 0) emit actorRequested(actorId);
}

void DvdShelfView::on_tag_clicked(qint64 tagId)
{
    if (tagId > 0) emit tagRequested(tagId);
}

void DvdShelfView::on_director_clicked()
{
    if (!m_director.isEmpty()) emit directorRequested(m_director);
}

void DvdShelfView::on_studio_clicked()
{
    if (m_makerId > 0) emit makerRequested(m_makerId);
}

void DvdShelfView::on_label_clicked()
{
    if (m_labelId > 0) emit labelRequested(m_labelId);
}

void DvdShelfView::on_series_clicked()
{
    if (m_seriesId > 0) emit seriesRequested(m_seriesId);
}

void DvdShelfView::clearExpandedMeta()
{
    m_title.clear(); m_story.clear(); m_code.clear(); m_releaseDate.clear(); m_director.clear();
    m_studio.clear(); m_label.clear(); m_series.clear(); m_actresses.clear(); m_actors.clear(); m_tags.clear();
    m_makerId = m_labelId = m_seriesId = -1; m_hasLocalVideo = false; m_favorited = false;
    emit expandedWorkMetaChanged(); emit expandedWorkFavoritedChanged();
}

void DvdShelfView::clearFanartOverlay()
{
    m_fanartLeft = m_fanartTop = -1;
    m_fanartWidth = 0;
    if (m_fanartStrip != nullptr) m_fanartStrip->setEntries({});
    if (m_fanartContainer != nullptr) m_fanartContainer->hide();
}

qint64 DvdShelfView::currentWorkId() const { return m_scene->selectedId().toLongLong(); }

bool DvdShelfView::openWork(qint64 workId)
{
    int index=-1;
    for(int i=0;i<m_works.size();++i) if(m_works[i].id==workId) { index=i; break; }
    if(index<0) return false;
    m_useInitialCamera=false;
    hideOverlays();
    m_scene->setCameraX(float(index*dvdSpacing),true);
    m_pendingOpen=QString::number(workId);
    if(!m_scene->focusItem(m_pendingOpen)) { m_pendingOpen.clear(); return false; }
    if(m_scene->state()==dvd_scene::DvdInteractionController::State::Focused) {
        m_pendingOpen.clear();
        if(!m_scene->isExpanded()) m_scene->toggleOpen();
    }
    updateSelection();
    return true;
}

void DvdShelfView::showRelationGraph(qint64 workId)
{
    if (m_relationGraph == nullptr || m_relationGraphContainer == nullptr || workId <= 0 ||
        workId!=currentWorkId() || !m_scene->isNearlyClosed()
        || m_scene->state()!=dvd_scene::DvdInteractionController::State::Focused)
        return;
    if (m_relationGraphCenterX >= 0 && m_relationGraphCenterY >= 0)
        updateRelationGraphGeometry(m_relationGraphCenterX, m_relationGraphCenterY);
    m_relationGraphContainer->show();
    m_relationGraphContainer->raise();
}

void DvdShelfView::hideOverlays()
{
    m_overlayWorkId = 0;
    m_relationGraphWorkId = 0;
    m_relationGraphCenterX = m_relationGraphCenterY = -1;
    if (m_relationGraphContainer != nullptr) m_relationGraphContainer->hide();
    clearFanartOverlay();
}

void DvdShelfView::set_force_view_overlay(qreal centerX, qreal centerY)
{
    m_relationGraphCenterX = centerX;
    m_relationGraphCenterY = centerY;
    if (m_relationGraphContainer != nullptr && m_relationGraphContainer->isVisible())
        updateRelationGraphGeometry(centerX, centerY);
}

void DvdShelfView::updateRelationGraphGeometry(qreal centerX, qreal centerY)
{
    if (m_relationGraphContainer == nullptr || m_scene == nullptr ||
        !std::isfinite(centerX) || !std::isfinite(centerY) ||
        centerX < -90000.0 || centerY < -90000.0)
        return;

    // Match the original shelf: anchor under spine, then shift left by 42%
    // of the viewport height. The transparent graph may overlap the 3D view.
    const int viewWidth=m_scene->renderWidget()->width();
    const int viewHeight=m_scene->renderWidget()->height();
    if(viewWidth<1 || viewHeight<1) return;
    const int graphWidth=std::min(std::max(40,qRound(viewHeight*0.61)),viewWidth);
    const int graphHeight=std::min(std::max(40,qRound(viewHeight*0.88)),std::max(40,viewHeight-4));
    const int left=std::clamp(qRound(centerX-graphWidth*0.5-viewHeight*0.42),0,std::max(0,viewWidth-graphWidth));
    const int top=std::clamp(qRound(centerY-graphHeight*0.5),0,std::max(0,viewHeight-graphHeight));
    const auto offset=m_scene->renderWidget()->mapTo(this,QPoint());
    m_relationGraphContainer->setGeometry(left+offset.x(), top+offset.y(), graphWidth, graphHeight);
    m_relationGraphContainer->raise();
}

void DvdShelfView::set_fanart_strip_layout(qreal left, qreal top, qreal width)
{
    m_fanartLeft = left;
    m_fanartTop = top;
    m_fanartWidth = width;
    if (m_fanartStrip == nullptr || m_fanartStrip->entries().isEmpty()) return;
    m_fanartContainer->show();
    updateFanartGeometry(left, top, width);
}

void DvdShelfView::updateFanartGeometry(qreal left, qreal top, qreal width)
{
    if (m_fanartContainer == nullptr) return;
    constexpr int stripHeight = 124;
    int stripWidth = qMax(160, qRound(width));
    int stripLeft = qBound(0, qRound(left), qMax(0, this->width() - stripWidth));
    stripWidth = qMin(stripWidth, this->width() - stripLeft);
    const int stripTop = qBound(0, qRound(top), qMax(0, height() - stripHeight));
    m_fanartContainer->setGeometry(stripLeft, stripTop, stripWidth, stripHeight);
    m_fanartContainer->raise();
}

void DvdShelfView::persistFanartPreview()
{
    if (m_overlayWorkId <= 0 || m_fanartStrip == nullptr) return;
    QString error;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), false, &error)) return;
    WorkRepository repository(connection.database());
    const auto work = repository.findById(m_overlayWorkId, &error);
    if (!work.has_value()) return;
    Work updated = *work;
    updated.fanartJson = FanartStripWidget::toJson(m_fanartStrip->entries());
    repository.updateDetails(updated, &error);
}

void DvdShelfView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateInitialCamera();
    layoutOverlays();
}

void DvdShelfView::showContextMenu(const QPoint &position)
{
    if(currentWorkId()>0) {
        refresh_expanded_favorite_state(selectedIndex());
        m_contextMenu->exec(position);
    } else {
        QMenu menu(this);
        menu.addAction(m_scene->renderWidget()->statisticsAction());
        menu.exec(position);
    }
}

bool DvdShelfView::eventFilter(QObject *object,QEvent *event)
{
    if(object==m_scene->renderWidget()) {
        if(event->type()==QEvent::MouseButtonPress) {
            const auto *mouse=static_cast<QMouseEvent *>(event);
            if(mouse->button()==Qt::RightButton) { m_rightPress=mouse->position(); m_rightDragged=false; }
        }
        if(event->type()==QEvent::MouseMove) {
            const auto *mouse=static_cast<QMouseEvent *>(event);
            if(mouse->buttons().testFlag(Qt::RightButton)
                && (mouse->position()-m_rightPress).manhattanLength()>QApplication::startDragDistance()) m_rightDragged=true;
        }
        if(event->type()==QEvent::MouseButtonRelease) {
            const auto *mouse=static_cast<QMouseEvent *>(event);
            if(mouse->button()==Qt::RightButton
                && !m_rightDragged
                && (mouse->position()-m_rightPress).manhattanLength()<=QApplication::startDragDistance()) {
                const QPoint position=mouse->globalPosition().toPoint();
                // Let the scene finish its release handling before opening a modal menu.
                QTimer::singleShot(0,this,[this,position] { showContextMenu(position); });
            }
        }
        if(event->type()==QEvent::ContextMenu) {
            const auto *context=static_cast<QContextMenuEvent *>(event);
            // macOS sends mouse context menus on press. Handle mouse menus on
            // release ourselves so they cannot interrupt right-button rotation.
            if(context->reason()==QContextMenuEvent::Keyboard)
                showContextMenu(context->globalPos());
            return true;
        }
        if(event->type()==QEvent::Wheel || event->type()==QEvent::MouseButtonPress
            || event->type()==QEvent::KeyPress) m_useInitialCamera=false;
        if(event->type()==QEvent::Resize) {
            QTimer::singleShot(0,this,[this] { updateInitialCamera(); layoutOverlays(); });
        }
    }
    return QWidget::eventFilter(object,event);
}

} // namespace darkeye
