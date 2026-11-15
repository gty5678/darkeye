#include "darkeye_ui/components/DesignLabel.h"

#include <QEvent>
#include <QFontMetrics>
#include <QStyle>

namespace darkeye {

DesignLabel::DesignLabel(const QString &text, QWidget *parent) : QLabel(text, parent)
{
    setObjectName(QStringLiteral("DesignLabel"));
    setProperty("darkeyeStyle", QStringLiteral("DesignLabel"));
    setAttribute(Qt::WA_TranslucentBackground);
}

DesignLabel::DesignLabel(const QString &text, const QString &tone, QWidget *parent)
    : DesignLabel(text, parent)
{
    setTone(tone);
}

void DesignLabel::setTone(const QString &tone)
{
    setProperty("tone", tone.trimmed().isEmpty() ? QStringLiteral("default")
                                                  : tone.trimmed());
    style()->unpolish(this);
    style()->polish(this);
    update();
}

QString DesignLabel::tone() const { return property("tone").toString(); }

void DesignLabel::setFormLabelColumn(int cjkGlyphs, const QString &suffix)
{
    m_formGlyphs = qMax(1, cjkGlyphs);
    m_formSuffix = suffix;
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    refreshFormWidth();
}

bool DesignLabel::event(QEvent *event)
{
    if (m_formGlyphs > 0 && (event->type() == QEvent::FontChange ||
                             event->type() == QEvent::ApplicationFontChange ||
                             event->type() == QEvent::StyleChange)) {
        refreshFormWidth();
    }
    return QLabel::event(event);
}

void DesignLabel::refreshFormWidth()
{
    const QFontMetrics metrics(font());
    setMinimumWidth(metrics.horizontalAdvance(QString(m_formGlyphs, QChar(u'中'))
                                               + m_formSuffix));
}

} // namespace darkeye
