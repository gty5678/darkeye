#pragma once

#include "ui/pages/PersonEditorPage.h"

namespace darkeye
{

class ModifyActorPage final : public PersonEditorPage
{
    Q_OBJECT

public:
    explicit ModifyActorPage(QSqlDatabase database, ThemeService &themes,
                             QString imageDirectory, QWidget *parent = nullptr,
                             QString coverDirectory = {});

    bool loadActor(qint64 actorId);
};

} // namespace darkeye
