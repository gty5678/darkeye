#pragma once

#include "ui/pages/PersonPage.h"

#include <utility>

namespace darkeye
{

class ActressPage final : public PersonPage
{
public:
    ActressPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                ThemeService &themeService, QString imageDirectory = {},
                QWidget *parent = nullptr)
        : PersonPage(PersonKind::Actress, std::move(publicDatabase), std::move(privateDatabase),
                     themeService, std::move(imageDirectory), parent)
    {
    }
};

} // namespace darkeye
