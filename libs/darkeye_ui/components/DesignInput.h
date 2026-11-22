#pragma once

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace darkeye {

class DesignLineEdit : public QLineEdit
{
public:
    explicit DesignLineEdit(QWidget *parent = nullptr);
};

class DesignTextEdit : public QTextEdit
{
public:
    explicit DesignTextEdit(QWidget *parent = nullptr);
};

class DesignPlainTextEdit final : public QPlainTextEdit
{
public:
    explicit DesignPlainTextEdit(QWidget *parent = nullptr);
};

using LineEdit = DesignLineEdit;
using TextEdit = DesignTextEdit;
using PlainTextEdit = DesignPlainTextEdit;

} // namespace darkeye
