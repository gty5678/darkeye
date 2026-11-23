#include "editor/live_editor.h"
#include "markdown/scanner.h"
#include "latex.h"
#include <iostream>

int main(int argc,char **argv){
    QApplication app(argc,argv);
    tex::LaTeX::init(formulaResourcePath().toStdString());
    for(const auto modifiers:{Qt::KeyboardModifiers(Qt::NoModifier),Qt::KeyboardModifiers(Qt::KeypadModifier)}){
        LiveEditor editor;editor.resize(640,360);editor.show();
        const QString text="plain\n\n# Heading\n\n> quote\n\n- item\n\nlast";
        QList<int> starts{0};
        for(int i=0;i<text.size();++i)if(text[i]=='\n')starts.append(i+1);
        editor.setSource(text);
        for(int direction:{1,-1}){
            const int first=direction==1?0:starts.size()-1;
            editor.restoreSourceSelection(starts[first],starts[first]);
            for(int i=first+direction;i>=0&&i<starts.size();i+=direction){
                QKeyEvent key(QEvent::KeyPress,direction==1?Qt::Key_Down:Qt::Key_Up,modifiers);
                QApplication::sendEvent(&editor,&key);
                if(editor.sourceSelection()!=qMakePair(starts[i],starts[i])||editor.markdown()!=text){
                    std::cerr<<"Block navigation failed at source "<<starts[i]<<" modifiers "<<int(modifiers)<<'\n';return 1;
                }
            }
        }
        // A short item must not reset the preferred column for the next item.
        const QString list="- aaaaaa\n- a\n- aaaaaa";
        editor.setSource(list);editor.restoreSourceSelection(5,5);
        for(int expected:{12,18}){
            QKeyEvent key(QEvent::KeyPress,Qt::Key_Down,modifiers);
            QApplication::sendEvent(&editor,&key);
            if(editor.sourceSelection()!=qMakePair(expected,expected))return 2;
        }
        for(int expected:{12,5}){
            QKeyEvent key(QEvent::KeyPress,Qt::Key_Up,modifiers);
            QApplication::sendEvent(&editor,&key);
            if(editor.sourceSelection()!=qMakePair(expected,expected)||editor.markdown()!=list)return 3;
        }
        // Prefix characters are editable, including the space after a marker.
        const QString tasks="- [ ] first\n- [x] second\n- [ ] third";
        editor.setSource(tasks);editor.restoreSourceSelection(3,3);
        const int second=tasks.indexOf('\n')+1,third=tasks.lastIndexOf('\n')+1;
        for(int expected:{second+3,third+3}){
            QKeyEvent key(QEvent::KeyPress,Qt::Key_Down,modifiers);
            QApplication::sendEvent(&editor,&key);
            if(editor.sourceSelection()!=qMakePair(expected,expected)||editor.markdown()!=tasks)return 4;
        }
        // Enter and leave inline source in both directions without getting stuck.
        for(const QString &token:{QString("**bold**"),QString("`code`"),QString("$x$"),QString("~~strike~~")}){
            const QString source="before "+token+" after";
            editor.setSource(source);editor.restoreSourceSelection(0,0);
            for(int direction:{1,-1}){
                int previous=editor.sourceSelection().second;
                const int end=direction==1?source.size():0;
                for(int steps=0;previous!=end&&steps<source.size()+4;++steps){
                    QKeyEvent key(QEvent::KeyPress,direction==1?Qt::Key_Right:Qt::Key_Left,modifiers);
                    QApplication::sendEvent(&editor,&key);
                    const int now=editor.sourceSelection().second;
                    if((now-previous)*direction<=0||editor.markdown()!=source)return 5;
                    previous=now;
                }
                if(previous!=end)return 6;
            }
        }
    }
    for(const QString &source:{QString("before **bold** after"),QString("# heading\n\nparagraph"),QString("- first\n- second")}){
        LiveEditor editor;editor.resize(640,360);editor.show();editor.setSource(source);
        for(bool backwards:{false,true}){
            const int anchor=backwards?source.size():0,position=backwards?0:source.size();
            editor.restoreSourceSelection(anchor,position);
            QKeyEvent shrink(QEvent::KeyPress,backwards?Qt::Key_Right:Qt::Key_Left,Qt::ShiftModifier);
            QApplication::sendEvent(&editor,&shrink);
            const int expected=position+(backwards?1:-1);
            if(editor.sourceSelection()!=qMakePair(anchor,expected)){
                qWarning()<<"shrink range"<<source<<backwards<<editor.sourceSelection();return 8;
            }
        }
    }
    for(const QString &source:{QString("before **bold** after"),QString("# heading\n\nparagraph"),
            QString("- first\n- second"),QString("before\n\n```cpp\ncode\n```\n\nafter")}){
        LiveEditor editor;editor.resize(640,360);editor.show();
        for(const auto range:{qMakePair(0,int(source.size())),qMakePair(2,int(source.size())-2),qMakePair(0,7)}){
            for(bool backwards:{false,true})for(int key:{int(Qt::Key_Backspace),int(Qt::Key_Delete),int(Qt::Key_X)}){
                editor.setSource(source);
                editor.restoreSourceSelection(backwards?range.second:range.first,backwards?range.first:range.second);
                QKeyEvent edit(QEvent::KeyPress,key,Qt::NoModifier,key==Qt::Key_X?"X":"");
                QApplication::sendEvent(&editor,&edit);
                QString expected=source;expected.replace(range.first,range.second-range.first,key==Qt::Key_X?"X":"");
                if(editor.markdown()!=expected){qWarning()<<"replace range"<<source<<range<<backwards<<key<<editor.markdown();return 9;}
            }
            for(bool backwards:{false,true})for(int key:{int(Qt::Key_Left),int(Qt::Key_Right)}){
                editor.setSource(source);
                editor.restoreSourceSelection(backwards?range.second:range.first,backwards?range.first:range.second);
                QKeyEvent collapse(QEvent::KeyPress,key,Qt::NoModifier);
                QApplication::sendEvent(&editor,&collapse);
                const int expected=key==Qt::Key_Left?range.first:range.second;
                if(editor.sourceSelection()!=qMakePair(expected,expected)){
                    qWarning()<<"collapse range"<<source<<range<<backwards<<key<<editor.sourceSelection();return 10;
                }
            }
        }
    }
    // Deleting the last code character must keep surrounding previews rendered.
    for(const QString &delimiter:{QString("`"),QString("``")}){
        for(int key:{Qt::Key_Backspace,Qt::Key_Delete}){
            LiveEditor editor;editor.resize(640,360);editor.show();
            const QString prefix="**before** ";
            const QString body="int sxd";
            editor.setSource(prefix+delimiter+body+delimiter+" **after**");
            const int start=prefix.size()+delimiter.size();
            const int caret=key==Qt::Key_Backspace?start+body.size():start;
            editor.restoreSourceSelection(caret,caret);
            for(int remaining=body.size();remaining>0;--remaining){
                QKeyEvent remove(QEvent::KeyPress,key,Qt::NoModifier);
                QApplication::sendEvent(&editor,&remove);
                const QString expectedBody=key==Qt::Key_Backspace?body.left(remaining-1):body.right(remaining-1);
                if(editor.markdown()!=prefix+delimiter+expectedBody+delimiter+" **after**"||
                   editor.toPlainText().contains("**")){
                    qWarning()<<"inline code deletion"<<delimiter<<key<<remaining<<editor.toPlainText();return 10;
                }
            }
            QKeyEvent type(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");
            QApplication::sendEvent(&editor,&type);
            if(editor.markdown()!=prefix+delimiter+"x"+delimiter+" **after**"||
               editor.sourceSelection()!=qMakePair(start+1,start+1)||editor.toPlainText().contains("**"))return 11;
        }
    }
    // Empty code delimiters remain local when a paragraph is activated again.
    for(const QString &paragraph:{QString("plain `` text"),QString("left **before** `` **after** right")}){
        LiveEditor editor;editor.resize(640,360);editor.show();
        const QString source=paragraph+"\n\nOther block";
        editor.setSource(source);
        for(int at:{0,int(paragraph.indexOf("``"))+1,int(paragraph.size())-1}){
            editor.restoreSourceSelection(source.size(),source.size());
            editor.restoreSourceSelection(at,at);
            if(editor.markdown()!=source||editor.toPlainText().contains("**")){
                qWarning()<<"empty code activation"<<at<<editor.toPlainText();return 12;
            }
        }
    }
    // Unmatched backticks cannot consume blank lines and following blocks.
    for(const QString &separator:{QString("\n\n"),QString("\n \t\n")}){
        const QString source="left `` right"+separator+"# Heading\n\n**bold**\n\nLater ``code`` text";
        const auto spans=scanSpans(source);
        if(spans.size()!=1||spans.first().start!=source.indexOf("``code``")||spans.first().length!=8){
            qWarning()<<"cross paragraph code span";return 13;
        }
        LiveEditor editor;editor.resize(640,360);editor.show();editor.setSource(source);
        editor.restoreSourceSelection(6,6);
        if(editor.toPlainText().contains("# Heading")||editor.toPlainText().contains("**bold**")||
           editor.markdown()!=source){qWarning()<<"cross paragraph preview"<<editor.toPlainText();return 14;}
    }
    // Soft line breaks within a paragraph still belong to the same code span.
    const auto wrapped=scanSpans("left ``one\ntwo`` right");
    if(wrapped.size()!=1||wrapped.first().length!=11)return 15;
    // Restoring a range must keep its exact endpoints and direction.
    for(const QString &source:{QString("before **bold** after"),QString("# heading\n\nparagraph"),
            QString("- first\n- second"),QString("before\n\n```cpp\ncode\n```\n\nafter")}){
        LiveEditor editor;editor.resize(640,360);editor.show();editor.setSource(source);
        for(int anchor=0;anchor<=source.size();++anchor)for(int position=0;position<=source.size();++position){
            editor.restoreSourceSelection(anchor,position);
            if(editor.sourceSelection()!=qMakePair(anchor,position)){
                qWarning()<<"restore range"<<source<<anchor<<position<<"got"<<editor.sourceSelection();return 7;
            }
        }
    }
    return 0;
}
