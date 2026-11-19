#pragma once

#include <QScrollArea>
#include <QVBoxLayout>

namespace darkeye
{

inline QWidget *addPageScrollArea(QWidget *page, QWidget *content = nullptr)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea(page);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    if (!content)
        content = new QWidget;
    scroll->setWidget(content);
    layout->addWidget(scroll);
    return content;
}

} // namespace darkeye
