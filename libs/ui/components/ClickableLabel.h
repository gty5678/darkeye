#pragma once

#include "darkeye_ui/components/DesignLabel.h"

namespace darkeye {

class ClickableLabel final : public DesignLabel
{
    Q_OBJECT

public:
    explicit ClickableLabel(const QString &text = QStringLiteral("xxx"),
                            bool actressJump = false, QWidget *parent = nullptr);

    void setActressJumpEnabled(bool enabled) noexcept;
    [[nodiscard]] bool actressJumpEnabled() const noexcept;
    QSize sizeHint() const override;

signals:
    void clicked();
    void actressJumpRequested(const QString &name);

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool m_actressJump = false;
};

} // namespace darkeye
