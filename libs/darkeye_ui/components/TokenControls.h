#pragma once

#include <QCheckBox>
#include <QGroupBox>
#include <QProgressBar>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QWidget>

namespace darkeye {

class TokenCheckBox final : public QCheckBox
{
public:
    explicit TokenCheckBox(const QString &text = {}, QWidget *parent = nullptr);
};

class TokenRadioButton final : public QRadioButton
{
public:
    explicit TokenRadioButton(const QString &text = {}, QWidget *parent = nullptr);
};

class TokenSpinBox final : public QSpinBox
{
public:
    explicit TokenSpinBox(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

class TokenGroupBox final : public QGroupBox
{
public:
    explicit TokenGroupBox(const QString &title = {}, QWidget *parent = nullptr);
};

class TokenTabWidget final : public QTabWidget
{
public:
    explicit TokenTabWidget(QWidget *parent = nullptr);
};

class ProgressBar : public QProgressBar
{
public:
    explicit ProgressBar(QWidget *parent = nullptr);
};

class IndeterminateProgressBar final : public ProgressBar
{
public:
    explicit IndeterminateProgressBar(QWidget *parent = nullptr);
    void start();
    void stop(int value = 0);
};

using TokenProgressBar = ProgressBar;

class TransparentWidget final : public QWidget
{
public:
    explicit TransparentWidget(QWidget *parent = nullptr);
};

} // namespace darkeye
