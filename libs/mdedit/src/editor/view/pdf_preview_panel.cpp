#include "pdf_preview_panel.h"
#include <QPdfView>
#include <QPdfDocument>
#include <QPdfPageNavigator>

namespace {
class NestedPdfView final : public QPdfView {
    QPointer<QAbstractScrollArea> outerArea;
protected:
    bool eventFilter(QObject *watched,QEvent *event) override {
        if(watched==verticalScrollBar()&&event->type()==QEvent::Wheel){wheelEvent(static_cast<QWheelEvent*>(event));return true;}
        return QPdfView::eventFilter(watched,event);
    }
    void wheelEvent(QWheelEvent *event) override {
        const int delta=event->pixelDelta().isNull()?event->angleDelta().y():event->pixelDelta().y();
        const auto *bar=verticalScrollBar();
        const bool outward=(delta>0&&bar->value()<=bar->minimum())||(delta<0&&bar->value()>=bar->maximum());
        if(outward&&outerArea&&event->modifiers()==Qt::NoModifier){
            const qreal distance=event->pixelDelta().isNull()
    ?event->angleDelta().y()/120.0*QApplication::wheelScrollLines()*QFontMetricsF(outerArea->font()).height()
    :event->pixelDelta().y();
            auto *outerBar=outerArea->verticalScrollBar();outerBar->setValue(outerBar->value()-qRound(distance));
            event->accept();return;
        }
        if(!event->pixelDelta().isNull()&&event->modifiers()==Qt::NoModifier){
            verticalScrollBar()->setValue(verticalScrollBar()->value()-event->pixelDelta().y());
        }else QPdfView::wheelEvent(event);
        // Never let an unconsumed event bubble to the document accidentally.
        event->accept();
    }
public:
    NestedPdfView(QAbstractScrollArea *outer,QWidget *parent):QPdfView(parent),outerArea(outer){verticalScrollBar()->installEventFilter(this);}
    ~NestedPdfView() override {setDocument(nullptr);}
};
}

QWidget *mdedit::createPdfPreviewPanel(QAbstractScrollArea *outer,QWidget *parent,const QUrl &file){
    auto *panel=new QWidget(parent);panel->setObjectName("pdfPreviewPanel");panel->setAutoFillBackground(true);
    auto palette=panel->palette();palette.setColor(QPalette::Window,QColor("#f5f2f8"));panel->setPalette(palette);
    auto *layout=new QVBoxLayout(panel);layout->setContentsMargins(0,0,0,0);layout->setSpacing(2);
    auto *view=new NestedPdfView(outer,panel);view->setObjectName("pdfViewer");
    auto *doc=new QPdfDocument(view);
    if(doc->load(file.toLocalFile())!=QPdfDocument::Error::None){delete panel;return nullptr;}
    view->setDocument(doc);view->setPageMode(QPdfView::PageMode::MultiPage);view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    auto *toolbar=new QHBoxLayout;
    auto *previous=new QPushButton("上一页",panel);auto *next=new QPushButton("下一页",panel);
    auto *page=new QSpinBox(panel);page->setObjectName("pdfPageNumber");page->setRange(1,doc->pageCount());page->setPrefix("第 ");page->setSuffix(QString(" / %1 页").arg(doc->pageCount()));
    toolbar->addWidget(previous);toolbar->addWidget(page);toolbar->addWidget(next);toolbar->addStretch();
    layout->addLayout(toolbar);layout->addWidget(view);
    QObject::connect(page,qOverload<int>(&QSpinBox::valueChanged),view,[view](int n){view->pageNavigator()->jump(n-1,QPointF());});
    QObject::connect(view->pageNavigator(),&QPdfPageNavigator::currentPageChanged,page,[page](int n){QSignalBlocker guard(page);page->setValue(n+1);});
    QObject::connect(previous,&QPushButton::clicked,page,[page]{page->setValue(page->value()-1);});
    QObject::connect(next,&QPushButton::clicked,page,[page]{page->setValue(page->value()+1);});
    return panel;
}

mdedit::PdfPreviewPanels::~PdfPreviewPanels(){qDeleteAll(panels);}

void mdedit::PdfPreviewPanels::sync(QAbstractScrollArea *outer,const QList<PdfPreviewPlacement> &placements){
    QSet<QString> used;
    for(const auto &placement:placements){
        if(!placement.geometry.intersects(outer->viewport()->rect().adjusted(0,-outer->viewport()->height(),0,outer->viewport()->height())))continue;
        used.insert(placement.key);
        auto *panel=panels.value(placement.key);
        if(!placement.geometry.intersects(outer->viewport()->rect())){
            if(panel)panel->hide();
            continue;
        }
        if(!panel){
            if(!placement.ready)continue;
            panel=createPdfPreviewPanel(outer,outer->viewport(),placement.file);
            if(!panel)continue;
            panels.insert(placement.key,panel);
        }
        panel->setGeometry(placement.geometry);
        panel->show();
    }
    for(auto it=panels.begin();it!=panels.end();){
        if(!used.contains(it.key())){it.value()->deleteLater();it=panels.erase(it);}
        else ++it;
    }
}
