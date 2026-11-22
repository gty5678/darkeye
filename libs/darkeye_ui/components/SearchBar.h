#pragma once

#include <QWidget>

namespace darkeye {

class DesignLineEdit;

class SearchBar final : public QWidget
{
    Q_OBJECT

public:
    explicit SearchBar(const QString &placeholder = QStringLiteral("Search..."),
                       QWidget *parent = nullptr);

    QString text() const;
    void setText(const QString &text);
    DesignLineEdit *lineEdit() const;

signals:
    void searchChanged(const QString &text);
    void searchSubmitted(const QString &text);
    void clearRequested();
    void filterRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void clearSearch();
    DesignLineEdit *m_lineEdit = nullptr;
};

} // namespace darkeye
