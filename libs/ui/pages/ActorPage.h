#pragma once

#include "ui/pages/PersonPage.h"

#include <utility>

namespace darkeye
{

class ActorPage final : public PersonPage
{
public:
    ActorPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
              ThemeService &themeService, QString imageDirectory = {},
              QWidget *parent = nullptr)
        : PersonPage(PersonKind::Actor, std::move(publicDatabase), std::move(privateDatabase),
                     themeService, std::move(imageDirectory), parent)
    {
    }
};

} // namespace darkeye
