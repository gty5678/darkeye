#pragma once
#include <QtWidgets>
#include <functional>
#include "../platform.h"
#include "theme_palette.h"

// Keep line numbers outside the document so they never enter copied/saved text.
class LineNumberArea : public QWidget {
public:
    std::function<void(QPaintEvent *)> paint;
    explicit LineNumberArea(QWidget *parent):QWidget(parent){setObjectName("lineNumberArea");setAttribute(Qt::WA_TransparentForMouseEvents);}
protected:
    void paintEvent(QPaintEvent *event)override{if(paint)paint(event);}
};

template<class Base> class NumberedEditor : public Base {
    LineNumberArea *numbers=new LineNumberArea(this);
    int gutterWidth=0;
public:
    void setThemePalette(const QPalette &palette){
        theme=palette;
        this->setPalette(palette);
        this->viewport()->setPalette(palette);
        numbers->update();
    }
    NumberedEditor(){
        numbers->paint=[this](QPaintEvent *event){paintNumbers(event);};
        QObject::connect(this,&Base::textChanged,this,[this]{refreshLineNumbers();});
        QObject::connect(this->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{numbers->update();});
        QObject::connect(this,&Base::cursorPositionChanged,this,[this]{numbers->update();});
        QObject::connect(this->document()->documentLayout(),&QAbstractTextDocumentLayout::update,this,[this]{numbers->update();});
        refreshLineNumbers();
    }
    int lineNumberForBlock(const QTextBlock &block)const{return lineNumberAt(QTextCursor(block));}
    virtual int lineNumberAt(const QTextCursor &cursor)const{return cursor.blockNumber()+1;}
    virtual int sourceLineCount()const{return this->document()->blockCount();}
    int gutterLineNumberAt(const QTextCursor &cursor)const{
        // Cells in a table row share the same vertical position. Only its
        // first cell contributes a gutter label, even if a cell has paragraphs.
        if(const auto table=cursor.currentTable()){
            const auto cell=table->cellAt(cursor);
            if(cell.column()!=0||cursor.block()!=cell.firstCursorPosition().block())return 0;
        }
        // Qt inserts an empty frame block before a table at the same height
        // as its header. It has no separate rendered source line.
        if(cursor.block().text().isEmpty()&&cursor.block().next().isValid()&&
           QTextCursor(cursor.block().next()).currentTable())return 0;
        return lineNumberAt(cursor);
    }
    void refreshLineNumbers(){
        QFontMetrics metrics(numberFont());
        int width=24+metrics.horizontalAdvance(QString::number(qMax(1,sourceLineCount())));
        if(width!=gutterWidth){gutterWidth=width;this->setViewportMargins(width,0,0,0);}
        const auto area=this->contentsRect();numbers->setGeometry(area.left(),area.top(),width,area.height());
        numbers->update();
    }
protected:
    std::optional<QPalette> theme;
    void resizeEvent(QResizeEvent *event)override{Base::resizeEvent(event);refreshLineNumbers();}
private:
    QFont numberFont() const {
        QFont result(editorFont);result.setPointSizeF(qMax(7.0,this->font().pointSizeF()-2));return result;
    }
    void paintNumbers(QPaintEvent *event){
        QPainter painter(numbers);painter.fillRect(event->rect(),mdedit::themeColor(theme,QPalette::Base,QColor("#ffffff")));
        painter.setFont(numberFont());
        const int current=lineNumberAt(this->textCursor());
        auto block=this->cursorForPosition(QPoint(0,0)).block();int previous=-1;
        while(block.isValid()){
            QTextCursor cursor(block);const auto rect=this->cursorRect(cursor);
            if(rect.top()>this->viewport()->height())break;
            const QString text=block.text();
            for(int start=0;;){
                cursor.setPosition(block.position()+start);
                const auto lineRect=this->cursorRect(cursor);
                if(lineRect.top()>this->viewport()->height())break;
                const int number=gutterLineNumberAt(cursor);
                if(lineRect.bottom()>=0&&number>0&&number!=previous){
                    painter.setPen(number==current
                        ?mdedit::themeColor(theme,QPalette::Link,QColor("#623e9c"))
                        :mdedit::themeColor(theme,QPalette::Text,QColor("#706978")));
                    painter.drawText(QRect(0,lineRect.top(),gutterWidth-10,qMax(lineRect.height(),painter.fontMetrics().height())),
                        Qt::AlignRight|Qt::AlignVCenter,QString::number(number));
                    previous=number;
                }
                const int separator=text.indexOf(QChar::LineSeparator,start);
                if(separator<0)break;
                start=separator+1;
            }
            block=block.next();
        }
    }
};

