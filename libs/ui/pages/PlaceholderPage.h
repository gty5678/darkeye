#pragma once

#include "darkeye_ui/base/LazyWidget.h"

namespace darkeye {

class PlaceholderPage final : public LazyWidget
{
public:
    PlaceholderPage(const QString &title, const QString &routeName, QWidget *parent = nullptr);

private:
    void lazyLoad() override;

    QString m_title;
    QString m_routeName;
};

} // namespace darkeye
