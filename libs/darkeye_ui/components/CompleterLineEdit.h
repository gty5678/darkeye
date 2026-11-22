#pragma once

#include "darkeye_ui/components/DesignInput.h"

#include <functional>

class QCompleter;
class QStringListModel;

namespace darkeye {

class CompleterLineEdit final : public DesignLineEdit
{
    Q_OBJECT

public:
    using Loader = std::function<QStringList()>;

    explicit CompleterLineEdit(Loader loader = {}, QWidget *parent = nullptr);
    QStringList items() const;
    void setLoader(Loader loader);
    void loadItems();
    void reloadItems();
    void tryShowCompleter();

signals:
    void itemsLoaded(int sequence, const QStringList &items);

protected:
    void focusInEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void applyItems(int sequence, const QStringList &items);

    Loader m_loader;
    QStringList m_items;
    int m_loadSequence = 0;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_model = nullptr;
};

} // namespace darkeye
