#pragma once

#include <QDialog>

namespace darkeye
{

class TermsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit TermsDialog(QWidget *parent = nullptr);
};

} // namespace darkeye
