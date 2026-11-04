#pragma once

#include "ui/pages/PersonDetailPage.h"

#include <utility>

namespace darkeye
{

class ActressDetailPage final : public PersonDetailPage
{
public:
    ActressDetailPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                      ThemeService &themes, QString imageDirectory = {},
                      QWidget *parent = nullptr, QString coverDirectory = {},
                      graph::GraphManager *graphManager = nullptr)
        : PersonDetailPage(PersonKind::Actress, std::move(publicDatabase),
                           std::move(privateDatabase), themes, std::move(imageDirectory), parent,
                           std::move(coverDirectory), graphManager)
    {
    }
};

} // namespace darkeye
