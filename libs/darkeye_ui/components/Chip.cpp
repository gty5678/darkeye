#include "darkeye_ui/components/Chip.h"

#include <QStyle>

namespace darkeye {

Chip::Chip(const QString &text, const QString &tone, bool checkable, bool checked,
           QWidget *parent)
    : DesignButton(text, parent)
{
    setObjectName(QStringLiteral("DesignChip"));
    setCheckable(checkable);
    if (checkable) setChecked(checked);
    setTone(tone);
}

void Chip::setTone(const QString &tone)
{
    setProperty("tone", tone.trimmed().isEmpty() ? QStringLiteral("default")
                                                  : tone.trimmed());
    style()->unpolish(this);
    style()->polish(this);
    update();
}

QString Chip::tone() const { return property("tone").toString(); }

} // namespace darkeye
