#include "editor/markdown_editor_widget.h"
#include "editor/tables/table_data.h"
#include <iostream>
#define CHECK(x) do{if(!(x)){std::cerr<<"Table edit failed "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
static bool waitFor(const std::function<bool()> &predicate){QElapsedTimer elapsed;elapsed.start();while(!predicate()&&elapsed.elapsed()<5000){QApplication::processEvents();QThread::msleep(1);}return predicate();}
int runTableEditTests(){
    const QString original="Before.\n\n| Name | Value |\n| :--- | ---: |\n| alpha | 10 |\n| beta | 20 |\n\nAfter.\n";
    MarkdownEditorWidget widget;widget.resize(900,600);widget.setMarkdown(original);widget.show();
    auto *live=static_cast<LiveEditor *>(widget.findChild<QTextBrowser *>("markdownPreview"));CHECK(live);
    QApplication::processEvents();
    widget.setMarkdown("Before.\n\nAfter.");
    QTextCursor insertion(live->document()->lastBlock());
    insertion.movePosition(QTextCursor::Right,QTextCursor::MoveAnchor,3);
    const QPoint insertionPoint=live->cursorRect(insertion).center();
    QContextMenuEvent insertContext(QContextMenuEvent::Mouse,insertionPoint,live->viewport()->mapToGlobal(insertionPoint));
    QApplication::sendEvent(live->viewport(),&insertContext);
    auto *insertMenu=live->findChild<QMenu *>("editorContextMenu");CHECK(insertMenu);
    for(auto *action:insertMenu->actions()){
        const auto label=action->text().remove('&').section('\t',0,0).trimmed();
        CHECK(!label.startsWith("Undo"));CHECK(!label.startsWith("Redo"));CHECK(!label.startsWith("Select All"));
        CHECK(!label.contains("撤销"));CHECK(!label.contains("重做"));CHECK(!label.contains("全选"));
    }
    auto *insertAction=insertMenu->findChild<QAction *>("insertTable");CHECK(insertAction);CHECK(insertAction->isEnabled());
    insertAction->trigger();insertMenu->close();
    QTextTable *insertedTable=nullptr;
    for(auto block=live->document()->begin();block.isValid();block=block.next())
        if(auto *table=QTextCursor(block).currentTable()){insertedTable=table;break;}
    CHECK(insertedTable);CHECK(insertedTable->rows()==2);CHECK(insertedTable->columns()==2);
    for(int row=0;row<2;++row)for(int column=0;column<2;++column){
        auto cell=insertedTable->cellAt(row,column);auto content=cell.firstCursorPosition();
        content.setPosition(cell.lastPosition(),QTextCursor::KeepAnchor);CHECK(content.selectedText().trimmed().isEmpty());
    }
    CHECK(widget.markdown()=="Before.\n\n|  |  |\n| --- | --- |\n|  |  |\n\nAfter.");
    widget.document()->undo();widget.render();CHECK(widget.markdown()=="Before.\n\nAfter.");
    for(const auto &sample:QList<QPair<QString,int>>{{"",0},{"Text",2},{"Before.\n\nAfter.",8},{"Before.\n",8}}){
        widget.setMarkdown(sample.first);
        const int start=sample.second>0?sample.first.lastIndexOf('\n',sample.second-1)+1:0;
        const int end=start<sample.first.size()&&sample.first.at(start)=='\n'?start+1:start;
        QString expected=sample.first;expected.replace(start,end-start,"|  |  |\n| --- | --- |\n|  |  |\n\n");
        live->insertTableRequested(sample.second);CHECK(widget.markdown()==expected);
        widget.document()->undo();widget.render();CHECK(widget.markdown()==sample.first);
    }
    widget.setMarkdown(original);
    QApplication::processEvents();QTextCursor cell;
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){cell=QTextCursor(block);break;}
    CHECK(!cell.isNull());const QPoint tablePoint=live->cursorRect(cell).center();
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,tablePoint,live->viewport()->mapToGlobal(tablePoint),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(live->viewport(),&doubleClick);
    CHECK(!live->findChild<QDialog *>());
    CHECK(live->textCursor().currentTable());
    auto *inlineTable=live->textCursor().currentTable();
    live->setTextCursor(inlineTable->cellAt(1,0).lastCursorPosition());
    QKeyEvent typeCell(QEvent::KeyPress,0,Qt::NoModifier,QString::fromUtf8("新|值"));QApplication::sendEvent(live,&typeCell);
    CHECK(widget.markdown().contains(QString::fromUtf8("alpha新\\|值")));
    CHECK(live->textCursor().currentTable());CHECK(!widget.markdown().contains("<table"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    live->setTextCursor(inlineTable->cellAt(2,1).lastCursorPosition());
    QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(live,&tab);
    CHECK(live->textCursor().currentTable()->rows()==4);CHECK(live->textCursor().currentTable()->cellAt(live->textCursor()).row()==3);
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    // IME, multiline paste and formatting stay in the native table and source.
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    live->setTextCursor(inlineTable->cellAt(1,0).lastCursorPosition());
    QInputMethodEvent ime;ime.setCommitString(QString::fromUtf8("中文"));QApplication::sendEvent(live,&ime);
    CHECK(widget.markdown().contains(QString::fromUtf8("alpha中文")));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    auto selection=inlineTable->cellAt(1,0).firstCursorPosition();selection.setPosition(inlineTable->cellAt(1,0).lastPosition(),QTextCursor::KeepAnchor);live->setTextCursor(selection);
    widget.formatSelection("**","**");CHECK(widget.markdown().contains("**alpha**"));CHECK(live->textCursor().currentTable());
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    live->setTextCursor(inlineTable->cellAt(1,0).firstCursorPosition());
    widget.clipboardParagraph(false);CHECK(QApplication::clipboard()->text()=="alpha");
    QApplication::clipboard()->setText("two\nlines");
    QKeyEvent paste(QEvent::KeyPress,Qt::Key_V,Qt::ControlModifier);QApplication::sendEvent(live,&paste);
    CHECK(widget.markdown().contains("<br>"));CHECK(live->textCursor().currentTable());
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    // Enter inserts a soft break; at the end of the final row it appends a row.
    auto findTable=[&]()->QTextTable *{
        for(auto block=live->document()->begin();block.isValid();block=block.next())
            if(auto *table=QTextCursor(block).currentTable())return table;
        return nullptr;
    };
    // Removing the blank line before a table must not expose the table source.
    for(const QString &prefix:QStringList{"Before.","**Before.**"}){
        widget.setMarkdown(prefix+"\n\n| Name | Value |\n| --- | --- |\n| alpha | 10 |\n");
        QTextCursor above(live->document()->begin());
        above.movePosition(QTextCursor::NextBlock);
        live->setTextCursor(above);
        QKeyEvent backspace(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);
        QApplication::sendEvent(live,&backspace);
        CHECK(findTable());
        CHECK(!live->toPlainText().contains("| --- |"));
        CHECK(widget.markdown()==prefix+"\n| Name | Value |\n| --- | --- |\n| alpha | 10 |\n");
        QApplication::sendEvent(live,&backspace);
        CHECK(findTable());
        CHECK(!live->toPlainText().contains("| --- |"));
    }
    widget.setMarkdown(original);
    inlineTable=findTable();CHECK(inlineTable);
    live->setTextCursor(inlineTable->cellAt(1,0).lastCursorPosition());
    QKeyEvent enterCell(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QApplication::sendEvent(live,&enterCell);
    CHECK(live->textCursor().currentTable()->rows()==3);
    CHECK(live->textCursor().block().text().contains(QChar::LineSeparator));
    CHECK(widget.markdown().contains("<br>"));
    QKeyEvent afterBreak(QEvent::KeyPress,0,Qt::NoModifier,"next");QApplication::sendEvent(live,&afterBreak);
    CHECK(widget.markdown().contains("alpha<br>next"));
    widget.setMarkdown(original);inlineTable=findTable();CHECK(inlineTable);
    live->setTextCursor(inlineTable->cellAt(2,1).lastCursorPosition());
    QApplication::sendEvent(live,&enterCell);
    CHECK(live->textCursor().currentTable()->rows()==4);
    CHECK(live->textCursor().currentTable()->cellAt(live->textCursor()).row()==3);
    CHECK(live->textCursor().currentTable()->cellAt(live->textCursor()).column()==1);
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    inlineTable=findTable();CHECK(inlineTable);
    live->setTextCursor(inlineTable->cellAt(2,0).lastCursorPosition());
    QKeyEvent shiftEnter(QEvent::KeyPress,Qt::Key_Return,Qt::ShiftModifier);QApplication::sendEvent(live,&shiftEnter);
    CHECK(live->textCursor().currentTable()->rows()==3);
    CHECK(widget.markdown().contains("beta<br>"));
    const QString formulaSource="Before.\n\n| Formula | Other |\n| --- | --- |\n| $x^2$ | **keep** |\n\nAfter.\n";
    widget.setMarkdown(formulaSource);
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    live->setTextCursor(inlineTable->cellAt(1,0).lastCursorPosition());
    QKeyEvent addText(QEvent::KeyPress,0,Qt::NoModifier," text");QApplication::sendEvent(live,&addText);
    CHECK(widget.markdown().contains("$x^2$"));CHECK(!widget.markdown().contains("math:/"));CHECK(widget.markdown().contains("**keep**"));
    CHECK(live->textCursor().block().text().contains("$x^2$"));
    live->setTextCursor(inlineTable->cellAt(1,1).firstCursorPosition());
    CHECK(live->textCursor().block().text()=="**keep**");
    CHECK(!inlineTable->cellAt(1,0).firstCursorPosition().block().text().contains("$x^2$"));
    CHECK(widget.markdown().contains("$x^2$"));
    auto rawBold=live->textCursor();rawBold.setPosition(inlineTable->cellAt(1,1).lastPosition(),QTextCursor::KeepAnchor);
    rawBold.insertText("**changed**");
    CHECK(widget.markdown().contains("**changed**"));
    live->setTextCursor(inlineTable->cellAt(1,0).firstCursorPosition());
    CHECK(inlineTable->cellAt(1,1).firstCursorPosition().block().text()=="changed");
    CHECK(live->textCursor().block().text().contains("$x^2$"));
    live->setTextCursor(QTextCursor(live->document()->lastBlock()));
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
    CHECK(!inlineTable->cellAt(1,0).firstCursorPosition().block().text().contains("$x^2$"));
    CHECK(widget.markdown().contains("**changed**"));

    // Native arrows may carry KeypadModifier. Every bottom-row cell must
    // allow Down to leave, and every top-row cell must allow Up to leave.
    for(const QString &navigationSource:QStringList{
        original,"| Name | Value |\n| --- | --- |\n| alpha | 10 |"})
    for(const auto modifiers:{Qt::NoModifier,Qt::KeypadModifier}){
        for(const bool down:{true,false})for(int column=0;column<2;++column){
            widget.setMarkdown(navigationSource);
            QTextTable *table=nullptr;
            for(auto block=live->document()->begin();block.isValid();block=block.next())
                if((table=QTextCursor(block).currentTable()))break;
            CHECK(table);
            live->setTextCursor(down?table->cellAt(table->rows()-1,column).lastCursorPosition():
                table->cellAt(0,column).firstCursorPosition());
            QKeyEvent arrow(QEvent::KeyPress,down?Qt::Key_Down:Qt::Key_Up,modifiers);
            QApplication::sendEvent(live,&arrow);
            if(live->textCursor().currentTable())qWarning()<<"Exit failed"<<down<<column<<modifiers<<navigationSource;
            CHECK(!live->textCursor().currentTable());
            CHECK(widget.markdown()==navigationSource);
        }
    }

    // Selecting every native cell removes the table as one undoable edit.
    for(int key:{Qt::Key_Backspace,Qt::Key_Delete}){
        widget.setMarkdown(original);
        for(auto block=live->document()->begin();block.isValid();block=block.next())
            if(QTextCursor(block).currentTable()){inlineTable=QTextCursor(block).currentTable();break;}
        auto allCells=inlineTable->firstCursorPosition();
        allCells.setPosition(inlineTable->lastPosition(),QTextCursor::KeepAnchor);
        live->setTextCursor(allCells);
        QKeyEvent remove(QEvent::KeyPress,key,Qt::NoModifier);QApplication::sendEvent(live,&remove);
        CHECK(!widget.markdown().contains("|"));
        CHECK(widget.markdown().contains("Before."));CHECK(widget.markdown().contains("After."));
        widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
        live->selectAll();QApplication::sendEvent(live,&remove);
        CHECK(widget.markdown().isEmpty());
        widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    }

    widget.setMarkdown(original);
    live->restoreSourceSelection(original.indexOf("alpha"),original.indexOf("alpha"));CHECK(live->textCursor().currentTable());
    inlineTable=live->textCursor().currentTable();live->setTextCursor(inlineTable->cellAt(0,0).firstCursorPosition());
    QKeyEvent backtab(QEvent::KeyPress,Qt::Key_Backtab,Qt::ShiftModifier);QApplication::sendEvent(live,&backtab);
    CHECK(!live->textCursor().currentTable());CHECK(widget.markdown()==original);
    live->restoreSourceSelection(original.indexOf("| Name"),original.indexOf("| Name"));CHECK(live->textCursor().currentTable());
    QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);QApplication::sendEvent(live,&up);CHECK(!live->textCursor().currentTable());CHECK(widget.markdown()==original);
    CHECK(live->moveTableBlockAt(original.indexOf("| Name"),-1));CHECK(widget.markdown().startsWith("| Name"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->moveTableBlockAt(original.indexOf("| Name"),1));CHECK(widget.markdown().indexOf("After.")<widget.markdown().indexOf("| Name"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    // Native Markdown menus operate on the cell that was right-clicked.
    widget.setMarkdown(original);QTextTable *native=nullptr;
    for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){native=QTextCursor(block).currentTable();break;}
    CHECK(native);const auto targetCell=native->cellAt(1,1).firstCursorPosition();const QPoint contextPoint=live->cursorRect(targetCell).center();
    QContextMenuEvent context(QContextMenuEvent::Mouse,contextPoint,live->viewport()->mapToGlobal(contextPoint));
    QApplication::sendEvent(live->viewport(),&context);
    auto *menu=live->findChild<QMenu *>("tableContextMenu");CHECK(menu);
    auto *insertRow=menu->findChild<QAction *>("insertRowAfter");CHECK(insertRow&&insertRow->isEnabled());insertRow->trigger();menu->close();
    auto nativeData=mdedit::readMarkdownTable(live->markdown().mid(live->markdown().indexOf("| Name"),live->markdown().indexOf("\n\nAfter")-live->markdown().indexOf("| Name")));
    CHECK(nativeData.rows.size()==4);CHECK(nativeData.rows[1][0]=="alpha");CHECK(nativeData.rows[2][0].isEmpty());CHECK(nativeData.rows[3][0]=="beta");
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,1,"insertColumnBefore"));
    CHECK(widget.markdown().contains("| alpha |  | 10 |"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,1,"moveColumnLeft"));CHECK(widget.markdown().contains("| Value | Name |"));CHECK(widget.markdown().contains("| ---: | :--- |"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,0,"moveRowDown"));CHECK(widget.markdown().indexOf("beta")<widget.markdown().indexOf("alpha"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,0,"removeRow"));CHECK(!widget.markdown().contains("alpha"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(live->changeMarkdownTableAt(original.indexOf("| Name"),1,1,"removeColumn"));CHECK(widget.markdown().contains("| Name |"));CHECK(!widget.markdown().contains("Value"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    widget.setMarkdown(original);QApplication::processEvents();
    auto sendMouse=[&](QEvent::Type type,const QPoint &point,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
        QMouseEvent event(type,point,live->viewport()->mapToGlobal(point),button,buttons,modifiers);QApplication::sendEvent(live->viewport(),&event);
    };
    CHECK(live->moveMarkdownTableTo(original.indexOf("| Name"),0));CHECK(widget.markdown().startsWith("| Name"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(!live->moveMarkdownTableTo(original.indexOf("| Name"),1));CHECK(widget.markdown()==original);
    // Native row/column grips reorder in place; edge buttons append axes.
    widget.setMarkdown(original);QApplication::processEvents();
    auto geometry=[&]{QTextTable *table=nullptr;for(auto block=live->document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){table=QTextCursor(block).currentTable();break;}return table;};
    auto rowPoint=[&](int row){auto *table=geometry();const auto frame=live->document()->documentLayout()->frameBoundingRect(table);const auto rect=live->document()->documentLayout()->blockBoundingRect(table->cellAt(row,0).firstCursorPosition().block());return QPoint(qRound(frame.left()-12)-live->horizontalScrollBar()->value(),qRound(rect.center().y())-live->verticalScrollBar()->value());};
    auto colPoint=[&](int column){auto *table=geometry();const auto frame=live->document()->documentLayout()->frameBoundingRect(table);const auto rect=live->document()->documentLayout()->blockBoundingRect(table->cellAt(0,column).firstCursorPosition().block());return QPoint(qRound(rect.center().x())-live->horizontalScrollBar()->value(),qRound(frame.top()-12)-live->verticalScrollBar()->value());};
    const QPoint hover=rowPoint(1),interior=colPoint(0)+QPoint(0,40);
    sendMouse(QEvent::MouseMove,interior,Qt::NoButton,Qt::NoButton);CHECK(live->viewport()->cursor().shape()!=Qt::OpenHandCursor);
    auto gripColor=[&]{auto image=live->viewport()->grab().toImage();const qreal scale=image.devicePixelRatio();return image.pixelColor(qRound(hover.x()*scale),qRound(hover.y()*scale));};
    const QColor hidden=gripColor();sendMouse(QEvent::MouseMove,hover,Qt::NoButton,Qt::NoButton);CHECK(live->viewport()->cursor().shape()==Qt::OpenHandCursor);CHECK(gripColor()!=hidden);
    sendMouse(QEvent::MouseMove,interior,Qt::NoButton,Qt::NoButton);CHECK(gripColor()==hidden);
    const QImage nativeGrid=live->viewport()->grab().toImage();
    QPoint select=rowPoint(1);sendMouse(QEvent::MouseButtonPress,select,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,select,Qt::LeftButton,Qt::NoButton);
    CHECK(!live->textCursor().hasSelection());
    // Compare the painted selection edges to the native grid, rather than
    // relying on frameBoundingRect (which includes a first-cell offset).
    const QImage selectedRow=live->viewport()->grab().toImage();
    CHECK(!selectedRow.isNull());
    auto horizontalEdges=[&](const QImage &image,bool selection){
        QList<int> edges;const qreal scale=image.devicePixelRatio();
        const auto frame=live->document()->documentLayout()->frameBoundingRect(geometry()).translated(-live->horizontalScrollBar()->value(),-live->verticalScrollBar()->value());
        const int left=qMax(0,qRound((frame.left()-12)*scale)),right=qMin(image.width()-1,qRound((frame.right()+12)*scale));
        for(int y=qMax(0,qRound((frame.top()-16)*scale));y<qMin(image.height(),qRound(frame.bottom()*scale));++y){
            int count=0;
            for(int x=left;x<=right;++x){const auto color=image.pixelColor(x,y);
                if(selection?color.red()>90&&color.red()<180&&color.blue()>140&&color.blue()>color.red()+20&&color.green()<150:
                    qAbs(color.red()-color.green())<3&&qAbs(color.green()-color.blue())<3&&color.red()>90&&color.red()<230)++count;
            }
            if(count>(right-left-24*scale)*0.8)edges.append(y);
        }return edges;
    };
    const auto gridEdges=horizontalEdges(nativeGrid,false),selectionEdges=horizontalEdges(selectedRow,true);
    CHECK(!selectionEdges.isEmpty());
    for(int edge:selectionEdges){bool aligned=false;for(int grid:gridEdges)aligned|=qAbs(edge-grid)<=2*selectedRow.devicePixelRatio();CHECK(aligned);}

    widget.clipboardParagraph(false);CHECK(QApplication::clipboard()->text()=="alpha\t10");
    QKeyEvent deleteRow(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(live,&deleteRow);CHECK(!widget.markdown().contains("alpha"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    QPoint start=rowPoint(1),target=rowPoint(2);target.setY(target.y()+10);
    sendMouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseMove,target,Qt::NoButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,target,Qt::LeftButton,Qt::NoButton);
    CHECK(widget.markdown().indexOf("beta")<widget.markdown().indexOf("alpha"));widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    start=colPoint(1);target=colPoint(0);target.setX(target.x()-30);
    sendMouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseMove,target,Qt::NoButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,target,Qt::LeftButton,Qt::NoButton);
    CHECK(widget.markdown().contains("| Value | Name |"));CHECK(widget.markdown().contains("| ---: | :--- |"));widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    // Drag inside the grid selects a rectangle, independent of text offsets.
    auto cellPoint=[&](int row,int column){
        const auto rect=live->document()->documentLayout()->blockBoundingRect(geometry()->cellAt(row,column).firstCursorPosition().block());
        return rect.center().toPoint()-QPoint(live->horizontalScrollBar()->value(),live->verticalScrollBar()->value());
    };
    auto rectangle=[&](int r1,int c1,int r2,int c2){
        const auto from=cellPoint(r1,c1),to=cellPoint(r2,c2);
        sendMouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
        sendMouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
        return !live->textCursor().hasSelection();
    };
    CHECK(rectangle(1,0,2,1));
    const auto rectangleEdges=horizontalEdges(live->viewport()->grab().toImage(),true);
    CHECK(!rectangleEdges.isEmpty());
    for(int edge:rectangleEdges){
        bool aligned=false;for(int grid:gridEdges)aligned|=qAbs(edge-grid)<=2*selectedRow.devicePixelRatio();
        CHECK(aligned);
    }
    CHECK(live->clipboardNativeCell(false));CHECK(QApplication::clipboard()->text()=="alpha\t10\nbeta\t20");
    CHECK(widget.markdown()==original);
    CHECK(rectangle(2,1,0,1));
    CHECK(live->clipboardNativeCell(false));CHECK(QApplication::clipboard()->text()=="Value\n10\n20");
    QKeyEvent clearCells(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(live,&clearCells);
    CHECK(geometry()->rows()==3&&geometry()->columns()==2);
    CHECK(widget.markdown().contains("alpha")&&widget.markdown().contains("beta"));
    CHECK(!widget.markdown().contains("Value")&&!widget.markdown().contains("10")&&!widget.markdown().contains("20"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(rectangle(2,1,1,0));CHECK(live->clipboardNativeCell(true));
    CHECK(QApplication::clipboard()->text()=="alpha\t10\nbeta\t20");
    CHECK(geometry()->rows()==3&&geometry()->columns()==2);
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    // Selecting every cell deletes the table block, including in reverse drags.
    for(int key:{Qt::Key_Delete,Qt::Key_Backspace}){
        CHECK(rectangle(2,1,0,0));
        QKeyEvent removeTable(QEvent::KeyPress,key,Qt::NoModifier);QApplication::sendEvent(live,&removeTable);
        CHECK(!widget.markdown().contains("|"));
        CHECK(widget.markdown().contains("Before")&&widget.markdown().contains("After"));
        widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    }
    CHECK(rectangle(0,0,2,1));CHECK(live->clipboardNativeCell(true));
    CHECK(QApplication::clipboard()->text()=="Name\tValue\nalpha\t10\nbeta\t20");
    CHECK(!widget.markdown().contains("|"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    // Rectangles can be projected onto either axis for grouped movement.
    CHECK(rectangle(1,0,2,1));
    start=rowPoint(1);target=rowPoint(0);target.setY(target.y()-10);
    sendMouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    sendMouse(QEvent::MouseMove,target,Qt::NoButton,Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease,target,Qt::LeftButton,Qt::NoButton);
    CHECK(widget.markdown().indexOf("alpha")<widget.markdown().indexOf("beta"));
    CHECK(widget.markdown().indexOf("beta")<widget.markdown().indexOf("Name"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    const QString wide="Before.\n\n| H0 | H1 | H2 | H3 |\n| --- | --- | --- | --- |\n| a0 | a1 | a2 | a3 |\n| b0 | b1 | b2 | b3 |\n| c0 | c1 | c2 | c3 |\n\nAfter.\n";
    auto selectedAction=[&](const char *name){
        const auto point=cellPoint(1,1);
        QContextMenuEvent event(QContextMenuEvent::Mouse,point,live->viewport()->mapToGlobal(point));
        QApplication::sendEvent(live->viewport(),&event);
        const auto menus=live->findChildren<QMenu *>("tableContextMenu");
        if(menus.isEmpty())return false;
        auto *popup=menus.last();auto *action=popup->findChild<QAction *>(name);
        const bool available=action&&action->isEnabled();
        if(available)action->trigger();popup->close();QApplication::processEvents();return available;
    };
    widget.setMarkdown(wide);CHECK(rectangle(1,1,2,2));
    CHECK(selectedAction("moveSelectedColumnsRight"));CHECK(widget.markdown().contains("| H0 | H3 | H1 | H2 |"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==wide);
    CHECK(rectangle(1,1,2,2));CHECK(selectedAction("moveSelectedRowsDown"));
    CHECK(widget.markdown().indexOf("c0")<widget.markdown().indexOf("a0"));
    CHECK(widget.markdown().indexOf("a0")<widget.markdown().indexOf("b0"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==wide);
    CHECK(rectangle(1,1,2,2));CHECK(selectedAction("removeSelectedRows"));
    CHECK(geometry()->rows()==2&&geometry()->columns()==4);
    CHECK(!widget.markdown().contains("a0")&&!widget.markdown().contains("b0"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==wide);
    CHECK(rectangle(1,1,2,2));CHECK(selectedAction("removeSelectedColumns"));
    CHECK(geometry()->columns()==2&&geometry()->rows()==4);
    CHECK(widget.markdown().contains("| H0 | H3 |"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==wide);
    CHECK(rectangle(1,1,2,2));start=colPoint(1);target=colPoint(3);target.setX(target.x()+30);
    sendMouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    sendMouse(QEvent::MouseMove,target,Qt::NoButton,Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease,target,Qt::LeftButton,Qt::NoButton);
    CHECK(widget.markdown().contains("| H0 | H3 | H1 | H2 |"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==wide);
    widget.setMarkdown(original);
    const auto clickCell=cellPoint(1,0);
    sendMouse(QEvent::MouseButtonPress,clickCell,Qt::LeftButton,Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease,clickCell,Qt::LeftButton,Qt::NoButton);
    CHECK(live->textCursor().currentTable());CHECK(!live->textCursor().hasSelection());
    // Shift-click extends structural selections without selecting cell text.
    auto chooseAxis=[&](QPoint point,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
        sendMouse(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,modifiers);
        sendMouse(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton,modifiers);
        return !live->textCursor().hasSelection();
    };
    CHECK(chooseAxis(rowPoint(1)));CHECK(chooseAxis(rowPoint(2),Qt::ShiftModifier));
    CHECK(live->clipboardNativeCell(false));CHECK(QApplication::clipboard()->text()=="alpha\t10\nbeta\t20");
    QKeyEvent deleteAxes(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(live,&deleteAxes);
    CHECK(!widget.markdown().contains("alpha")&&!widget.markdown().contains("beta"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(chooseAxis(colPoint(1)));CHECK(chooseAxis(colPoint(0),Qt::ShiftModifier));
    CHECK(live->clipboardNativeCell(false));CHECK(QApplication::clipboard()->text()=="Name\tValue\nalpha\t10\nbeta\t20");
    CHECK(live->clipboardNativeCell(true));CHECK(!widget.markdown().contains("|"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    CHECK(chooseAxis(rowPoint(1)));CHECK(chooseAxis(rowPoint(2),Qt::ShiftModifier));
    start=rowPoint(1);target=rowPoint(0);target.setY(target.y()-10);
    sendMouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    sendMouse(QEvent::MouseMove,target,Qt::NoButton,Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease,target,Qt::LeftButton,Qt::NoButton);
    CHECK(!live->textCursor().hasSelection());
    CHECK(widget.markdown().indexOf("alpha")<widget.markdown().indexOf("beta"));
    CHECK(widget.markdown().indexOf("beta")<widget.markdown().indexOf("Name"));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    auto frame=live->document()->documentLayout()->frameBoundingRect(geometry()).translated(-live->horizontalScrollBar()->value(),-live->verticalScrollBar()->value());
    QPoint plus(qRound(frame.center().x()),qRound(frame.bottom()+10));sendMouse(QEvent::MouseMove,plus,Qt::NoButton,Qt::NoButton);sendMouse(QEvent::MouseButtonPress,plus,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,plus,Qt::LeftButton,Qt::NoButton);
    CHECK(geometry()->rows()==4);widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    frame=live->document()->documentLayout()->frameBoundingRect(geometry()).translated(-live->horizontalScrollBar()->value(),-live->verticalScrollBar()->value());
    plus=QPoint(qRound(frame.right()+10),qRound(frame.center().y()));sendMouse(QEvent::MouseMove,plus,Qt::NoButton,Qt::NoButton);sendMouse(QEvent::MouseButtonPress,plus,Qt::LeftButton,Qt::LeftButton);sendMouse(QEvent::MouseButtonRelease,plus,Qt::LeftButton,Qt::NoButton);
    CHECK(geometry()->columns()==3);
    const auto emptyColumn=live->document()->documentLayout()->blockBoundingRect(geometry()->cellAt(0,2).firstCursorPosition().block());
    CHECK(emptyColumn.width()>=70);
    widget.document()->undo();widget.render();CHECK(widget.markdown()==original);
    {
        LiveEditor responsive;responsive.resize(640,500);responsive.show();responsive.setSource(original);QApplication::processEvents();
        auto table=[&]()->QTextTable *{for(auto block=responsive.document()->begin();block.isValid();block=block.next())if(auto *grid=QTextCursor(block).currentTable())return grid;return nullptr;};
        CHECK(table());
        auto width=[&]{return responsive.document()->documentLayout()->frameBoundingRect(table()).width();};
        auto cellWidth=[&](int column){return responsive.document()->documentLayout()->blockBoundingRect(table()->cellAt(0,column).firstCursorPosition().block()).width();};
        CHECK(cellWidth(0)>=65);CHECK(qAbs(cellWidth(0)-cellWidth(1))<2);
        for(auto block=responsive.document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable())CHECK(block.blockFormat().alignment()==Qt::AlignLeft);
        CHECK(responsive.markdown()==original);
        const qreal initial=width();
        responsive.setTextCursor(table()->cellAt(1,0).lastCursorPosition());
        QInputMethodEvent composing(QStringLiteral("中文输入候选文字"),{});QApplication::sendEvent(&responsive,&composing);QApplication::processEvents();
        CHECK(responsive.markdown()==original);CHECK(width()>initial);
        CHECK(table()->cellAt(1,0).firstCursorPosition().block().layout()->lineCount()==1);
        QInputMethodEvent cancelComposition;QApplication::sendEvent(&responsive,&cancelComposition);QApplication::processEvents();
        CHECK(responsive.markdown()==original);CHECK(qAbs(width()-initial)<2);
        QInputMethodEvent composingAgain(QStringLiteral("中文输入候选文字"),{});QApplication::sendEvent(&responsive,&composingAgain);
        const qreal composingWidth=width();
        QInputMethodEvent commitComposition;commitComposition.setCommitString(QStringLiteral("中文输入候选文字"));QApplication::sendEvent(&responsive,&commitComposition);QApplication::processEvents();
        CHECK(responsive.markdown().contains(QStringLiteral("alpha中文输入候选文字")));
        CHECK(qAbs(width()-composingWidth)<3);CHECK(table()->cellAt(1,0).firstCursorPosition().block().layout()->lineCount()==1);
        responsive.setSource(original);QApplication::processEvents();responsive.setTextCursor(table()->cellAt(1,0).lastCursorPosition());
        QKeyEvent grow(QEvent::KeyPress,0,Qt::NoModifier,"abcdefghijklm");QApplication::sendEvent(&responsive,&grow);QApplication::processEvents();
        CHECK(width()>initial);CHECK(cellWidth(0)>cellWidth(1)+20);CHECK(table()->cellAt(1,0).firstCursorPosition().block().layout()->lineCount()==1);
        QKeyEvent overflow(QEvent::KeyPress,0,Qt::NoModifier,QString(200,'W'));QApplication::sendEvent(&responsive,&overflow);QApplication::processEvents();
        CHECK(width()<=responsive.viewport()->width()-2*responsive.document()->documentMargin()+20);
        CHECK(cellWidth(0)>cellWidth(1)+20);
        CHECK(table()->cellAt(1,0).firstCursorPosition().block().layout()->lineCount()>1);
        CHECK(responsive.markdown().contains("| :--- | ---: |"));
        const qreal wide=width();const QString beforeResize=responsive.markdown();
        responsive.resize(400,500);QApplication::processEvents();QApplication::processEvents();
        CHECK(width()<wide);CHECK(responsive.markdown()==beforeResize);
    }
    {
        LiveEditor separated;separated.resize(640,600);separated.show();
        const QString two="| A | B |\n| --- | --- |\n| one | two |\n\n| C | D |\n| --- | --- |\n| three | four |\n";
        separated.setSource(two);QApplication::processEvents();
        QList<QTextTable *> tables;
        for(auto block=separated.document()->begin();block.isValid();block=block.next())if(auto *table=QTextCursor(block).currentTable();table&&!tables.contains(table))tables.append(table);
        CHECK(tables.size()==2);
        const auto first=separated.document()->documentLayout()->frameBoundingRect(tables[0]);
        const auto second=separated.document()->documentLayout()->frameBoundingRect(tables[1]);
        CHECK(second.top()-first.bottom()>=16);CHECK(separated.markdown()==two);
    }
    {
        LiveEditor emptyCells;emptyCells.resize(900,600);emptyCells.show();
        const QString source="| 负责人 | 状态 |  | **的三啊** |\n| --- | --- | --- | --- |\n| 李四 | 待验证 |  |  |\n| 张三 | 待验证 |  | 内容 |\n| 王五 | 待验证 | 待验证 | 待验证 |\n|  |  |  |  |\n";
        emptyCells.setSource(source);QApplication::processEvents();
        QTextTable *table=nullptr;
        for(auto block=emptyCells.document()->begin();block.isValid();block=block.next())if(QTextCursor(block).currentTable()){table=QTextCursor(block).currentTable();break;}
        CHECK(table);CHECK(table->rows()==5);CHECK(table->columns()==4);
        for(int row=0;row<5;++row)for(int column=0;column<4;++column){
            const auto cell=table->cellAt(row,column);
            CHECK(cell.row()==row);CHECK(cell.column()==column);CHECK(cell.rowSpan()==1);CHECK(cell.columnSpan()==1);
        }
        CHECK(emptyCells.markdown()==source);
        emptyCells.setTextCursor(table->cellAt(1,3).firstCursorPosition());
        QKeyEvent editEmpty(QEvent::KeyPress,0,Qt::NoModifier,QStringLiteral("独立单元格"));QApplication::sendEvent(&emptyCells,&editEmpty);
        const auto data=mdedit::readMarkdownTable(emptyCells.markdown());
        CHECK(data.error.isEmpty());CHECK(data.rows[1].size()==4);CHECK(data.rows[1][1]=="待验证");CHECK(data.rows[1][2].isEmpty());CHECK(data.rows[1][3]=="独立单元格");
    }
    QTemporaryDir dir;CHECK(dir.isValid());const QString path=dir.filePath("full.csv");QByteArray bytes="name,value\n";
    for(int i=0;i<80;++i)bytes+=QByteArray::number(i)+",value"+QByteArray::number(i)+"\n";
    QFile file(path);CHECK(file.open(QIODevice::WriteOnly));CHECK(file.write(bytes)==bytes.size());file.close();
    const QString ref="Before.\n\n![Full CSV](full.csv)\n\nAfter.\n";
    widget.setBaseUrl(QUrl::fromLocalFile(dir.path()+"/"));widget.setMarkdown(ref);
    QTextCursor csvCell;
    CHECK(waitFor([&]{
        for(auto block=live->document()->begin();block.isValid();block=block.next())
            for(auto it=block.begin();!it.atEnd();++it){
                const auto format=it.fragment().charFormat();
                if(format.isImageFormat()&&QUrl(format.toImageFormat().name()).scheme()=="csv-ready"){
                    csvCell=QTextCursor(live->document());csvCell.setPosition(it.fragment().position());return true;
                }
            }
        return false;
    }));
    const QPoint csvPoint=live->cursorRect(csvCell).center();
    QMouseEvent csvDoubleClick(QEvent::MouseButtonDblClick,csvPoint,live->viewport()->mapToGlobal(csvPoint),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(live->viewport(),&csvDoubleClick);
    CHECK(!live->findChild<QDialog *>());
    QContextMenuEvent csvMenuEvent(QContextMenuEvent::Mouse,csvPoint,live->viewport()->mapToGlobal(csvPoint));
    QApplication::sendEvent(live->viewport(),&csvMenuEvent);
    auto *csvMenu=live->findChild<QMenu *>("tableContextMenu");CHECK(csvMenu);
    for(auto *action:csvMenu->actions())CHECK(!action->text().contains("编辑 CSV"));
    csvMenu->close();
    CHECK(file.open(QIODevice::ReadOnly));CHECK(file.readAll()==bytes);file.close();
    CHECK(widget.markdown()==ref);CHECK(!widget.document()->isModified());
    CHECK(live->moveTableBlockAt(ref.indexOf("!["),1));CHECK(widget.markdown().indexOf("After.")<widget.markdown().indexOf("!["));
    widget.document()->undo();widget.render();CHECK(widget.markdown()==ref);
    // Starting a grid drag after clicking a formatted cell must not scroll
    // through the temporary document-start caret used to restore its preview.
    const QString scrollSource=QString("Paragraph above.\n\n").repeated(60)+
        "| Name | Value |\n| --- | --- |\n| **alpha** | 10 |\n| beta | 20 |\n\n"+
        QString("Paragraph below.\n\n").repeated(60);
    widget.setMarkdown(scrollSource);QApplication::processEvents();
    inlineTable=findTable();CHECK(inlineTable);
    live->setTextCursor(inlineTable->cellAt(1,0).firstCursorPosition());
    live->ensureCursorVisible();QApplication::processEvents();
    const auto from=cellPoint(1,0),to=cellPoint(2,1);
    sendMouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
    const int scrollBeforeDrag=live->verticalScrollBar()->value();CHECK(scrollBeforeDrag>0);
    sendMouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);
    CHECK(live->verticalScrollBar()->value()==scrollBeforeDrag);
    sendMouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
    QApplication::processEvents();
    CHECK(live->verticalScrollBar()->value()==scrollBeforeDrag);
    CHECK(live->clipboardNativeCell(false));
    CHECK(QApplication::clipboard()->text()=="**alpha**\t10\nbeta\t20");
    CHECK(widget.markdown()==scrollSource);
    return 0;
}
