#include "darkeye_ui/components/CompleterLineEdit.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QMetaObject>
#include <QPointer>
#include <QStringListModel>
#include <QThreadPool>

namespace darkeye {

CompleterLineEdit::CompleterLineEdit(Loader loader, QWidget *parent)
    : DesignLineEdit(parent), m_loader(std::move(loader))
{
    m_model = new QStringListModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->popup()->setObjectName(QStringLiteral("DesignCompleterPopup"));
    setCompleter(m_completer);
    connect(this, &CompleterLineEdit::itemsLoaded, this,
            &CompleterLineEdit::applyItems);
    loadItems();
}

QStringList CompleterLineEdit::items() const { return m_items; }

void CompleterLineEdit::setLoader(Loader loader)
{
    m_loader = std::move(loader);
    reloadItems();
}

void CompleterLineEdit::loadItems()
{
    if (!m_loader) return;
    const int sequence = ++m_loadSequence;
    const Loader loader = m_loader;
    QPointer<CompleterLineEdit> guard(this);
    QThreadPool::globalInstance()->start([guard, loader, sequence] {
        QStringList loaded;
        try {
            loaded = loader();
        } catch (...) {
            loaded.clear();
        }
        if (guard.isNull()) return;
        QMetaObject::invokeMethod(
            guard, [guard, sequence, loaded] {
                if (!guard.isNull()) emit guard->itemsLoaded(sequence, loaded);
            }, Qt::QueuedConnection);
    });
}

void CompleterLineEdit::reloadItems() { loadItems(); }

void CompleterLineEdit::tryShowCompleter()
{
    m_completer->setCompletionPrefix(text());
    if (!m_completer->popup()->isVisible()
        && (text().isEmpty() || m_completer->completionCount() > 0)) {
        m_completer->complete(rect());
    }
}

void CompleterLineEdit::focusInEvent(QFocusEvent *event)
{
    DesignLineEdit::focusInEvent(event);
    tryShowCompleter();
}

void CompleterLineEdit::keyPressEvent(QKeyEvent *event)
{
    DesignLineEdit::keyPressEvent(event);
    tryShowCompleter();
}

void CompleterLineEdit::mousePressEvent(QMouseEvent *event)
{
    DesignLineEdit::mousePressEvent(event);
    tryShowCompleter();
}

void CompleterLineEdit::applyItems(int sequence, const QStringList &items)
{
    if (sequence != m_loadSequence) return;
    m_items = items;
    m_model->setStringList(m_items);
}

} // namespace darkeye
