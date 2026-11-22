#pragma once

#include <QAbstractButton>

namespace darkeye {

class ThemeService;

class ChamferButton final : public QAbstractButton
{
public:
    explicit ChamferButton(const QString &text = {}, const QString &iconName = {},
                           int iconSize = 0, int outerSize = 40,
                           qreal chamferRatio = 0.22,
                           ThemeService *themes = nullptr,
                           QWidget *parent = nullptr);
    void setChamferRatio(qreal ratio);
    qreal chamferRatio() const;
    void setSelected(bool selected);
    bool isSelected() const;
    void setIconName(const QString &name);
    void setIconPath(const QString &path);
    void setHoverable(bool hoverable);
    bool isHoverable() const;
    void setMenuId(const QString &menuId);
    QString menuId() const;
    void setUseNativeTooltip(bool enabled);
    bool usesNativeTooltip() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString m_iconName;
    int m_iconSize = 22;
    qreal m_chamferRatio = 0.22;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_hoverable = true;
    bool m_useNativeTooltip = true;
    QString m_menuId;
    ThemeService *m_themes = nullptr;
};

} // namespace darkeye
