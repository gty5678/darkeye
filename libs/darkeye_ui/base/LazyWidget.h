#pragma once

#include <QWidget>

namespace darkeye
{

// Reusable lazy-initialization base matching darkeye_ui.base.LazyWidget.
class LazyWidget : public QWidget
{
public:
    explicit LazyWidget(QWidget *parent = nullptr);
    [[nodiscard]] bool isInitialized() const noexcept;
    void initialize();

protected:
    void showEvent(QShowEvent *event) override;
    virtual void lazyLoad() = 0;

private:
    bool m_initialized = false;
    bool m_initializing = false;
};

} // namespace darkeye
