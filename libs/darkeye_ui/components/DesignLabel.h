#pragma once

#include <QLabel>

namespace darkeye {

class DesignLabel : public QLabel
{
public:
    explicit DesignLabel(const QString &text = {}, QWidget *parent = nullptr);
    DesignLabel(const QString &text, const QString &tone, QWidget *parent = nullptr);

    void setTone(const QString &tone);
    QString tone() const;
    void setFormLabelColumn(int cjkGlyphs = 4,
                            const QString &suffix = QStringLiteral("："));

protected:
    bool event(QEvent *event) override;

private:
    void refreshFormWidth();

    int m_formGlyphs = 0;
    QString m_formSuffix;
};

using Label = DesignLabel;

} // namespace darkeye
