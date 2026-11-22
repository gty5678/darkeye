#include "darkeye_ui/components/SearchBar.h"

#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignInput.h"

#include <QHBoxLayout>
#include <QKeyEvent>

namespace darkeye {

SearchBar::SearchBar(const QString &placeholder, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("DesignSearchBar"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    m_lineEdit = new DesignLineEdit(this);
    m_lineEdit->setPlaceholderText(placeholder);
    m_lineEdit->installEventFilter(this);
    auto *clear = new DesignButton(QStringLiteral("Clear"), this);
    clear->setObjectName(QStringLiteral("DesignSearchClear"));
    auto *filter = new DesignButton(QStringLiteral("Filter"), this);
    filter->setObjectName(QStringLiteral("DesignSearchFilter"));
    layout->addWidget(m_lineEdit, 1);
    layout->addWidget(clear);
    layout->addWidget(filter);
    connect(m_lineEdit, &QLineEdit::textChanged, this, &SearchBar::searchChanged);
    connect(m_lineEdit, &QLineEdit::returnPressed, this,
            [this] { emit searchSubmitted(text()); });
    connect(clear, &QPushButton::clicked, this, &SearchBar::clearSearch);
    connect(filter, &QPushButton::clicked, this, &SearchBar::filterRequested);
}

QString SearchBar::text() const { return m_lineEdit->text(); }
void SearchBar::setText(const QString &text) { m_lineEdit->setText(text); }
DesignLineEdit *SearchBar::lineEdit() const { return m_lineEdit; }

bool SearchBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_lineEdit && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && !text().isEmpty()) {
            clearSearch();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SearchBar::clearSearch()
{
    if (text().isEmpty()) return;
    m_lineEdit->clear();
    m_lineEdit->setFocus();
    emit clearRequested();
}

} // namespace darkeye
