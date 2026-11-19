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
                      QWidget *parent = nullptr)
        : PersonDetailPage(std::move(publicDatabase),
                           std::move(privateDatabase), themes, std::move(imageDirectory), parent)
    {
    }
};

} // namespace darkeye
