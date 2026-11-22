#include "ui/pages/ModifyActorPage.h"

namespace darkeye
{

ModifyActorPage::ModifyActorPage(QSqlDatabase database, ThemeService &themes,
                                 QString imageDirectory, QWidget *parent,
                                 QString coverDirectory)
    : PersonEditorPage(std::move(database), themes, std::move(imageDirectory), parent,
                       std::move(coverDirectory))
{
    setObjectName(QStringLiteral("ModifyActorPage"));
    configureAvatar(QStringLiteral("男优头像"), QStringLiteral("把男优头像拖进来"));
}

bool ModifyActorPage::loadActor(qint64 actorId)
{
    return loadPerson(PersonKind::Actor, actorId);
}

} // namespace darkeye
