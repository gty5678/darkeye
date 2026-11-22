#pragma once

#include "darkeye_ui/components/DesignButton.h"

namespace darkeye {

class Chip final : public DesignButton
{
public:
    explicit Chip(const QString &text = {}, const QString &tone = QStringLiteral("default"),
                  bool checkable = false, bool checked = false,
                  QWidget *parent = nullptr);

    void setTone(const QString &tone);
    QString tone() const;
};

using Tag = Chip;

} // namespace darkeye
