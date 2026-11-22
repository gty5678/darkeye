#pragma once

#include <QComboBox>

namespace darkeye {

class DesignComboBox : public QComboBox
{
public:
    explicit DesignComboBox(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

using ComboBox = DesignComboBox;

} // namespace darkeye
