#include "darkeye_ui/components/DesignInput.h"

namespace darkeye {

DesignLineEdit::DesignLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setObjectName(QStringLiteral("DesignInput"));
    setProperty("darkeyeStyle", QStringLiteral("DesignInput"));
}

DesignTextEdit::DesignTextEdit(QWidget *parent)
    : QTextEdit(parent)
{
    setObjectName(QStringLiteral("DesignTextEdit"));
    setProperty("darkeyeStyle", QStringLiteral("DesignTextEdit"));
}

DesignPlainTextEdit::DesignPlainTextEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("DesignPlainTextEdit"));
    setProperty("darkeyeStyle", QStringLiteral("DesignPlainTextEdit"));
}

} // namespace darkeye
