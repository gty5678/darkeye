#include "darkeye_ui/components/MakerSelector.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QStringListModel>
#include <QThreadPool>

namespace darkeye {

MakerSelector::MakerSelector(const QList<MakerOption> &makers, QWidget *parent)
    : DesignComboBox(parent)
{
    setObjectName(QStringLiteral("DesignMakerSelector"));
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    setMaxVisibleItems(15);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(8);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lineEdit()->setFrame(false);
    m_completionModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completionModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->popup()->setObjectName(QStringLiteral("DesignComboBoxPopup"));
    setCompleter(m_completer);
    connect(lineEdit(), &QLineEdit::textEdited, this, &MakerSelector::filterCompletions);
    connect(this, &QComboBox::currentTextChanged, this, &MakerSelector::synchronizeSelection);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated), this,
            &MakerSelector::synchronizeSelection);
    connect(this, &MakerSelector::makersLoaded, this,
            [this](int sequence, const QList<MakerOption> &loaded) {
                if (sequence != m_reloadSequence) return;
                const auto selected = m_selectedId;
                const QString current = currentText();
                setMakers(loaded);
                if (selected.has_value()) setMaker(selected);
                else if (!current.isEmpty()) setEditText(current);
            });
    setMakers(makers);
}

void MakerSelector::setMakers(const QList<MakerOption> &makers)
{
    m_records.clear();
    m_records.append({std::nullopt, {}, {}});
    for (const MakerOption &maker : makers) {
        const QString display = !maker.chineseName.trimmed().isEmpty()
                                    ? maker.chineseName.trimmed()
                                    : maker.japaneseName.trimmed();
        if (display.isEmpty()) continue;
        QStringList search{maker.chineseName, maker.japaneseName};
        search.append(maker.aliases);
        m_records.append({maker.id, display, search.join(QLatin1Char(' ')).toLower()});
    }
    clear();
    QStringList completions;
    for (const Record &record : m_records) {
        addItem(record.displayName,
                record.id.has_value() ? QVariant::fromValue(*record.id) : QVariant());
        completions.append(record.displayName);
    }
    m_completionModel->setStringList(completions);
    setCurrentIndex(0);
    m_selectedId.reset();
}

void MakerSelector::setMaker(std::optional<qint64> makerId)
{
    m_selectedId.reset();
    setCurrentIndex(0);
    setEditText({});
    if (!makerId.has_value()) return;
    for (const Record &record : m_records) {
        if (record.id == makerId) {
            setCurrentIndex(findData(QVariant::fromValue(*makerId)));
            setEditText(record.displayName);
            m_selectedId = record.id;
            return;
        }
    }
}

std::optional<qint64> MakerSelector::maker() const { return m_selectedId; }
void MakerSelector::setLoader(Loader loader) { m_loader = std::move(loader); }

void MakerSelector::reloadMakers()
{
    if (!m_loader) return;
    const int sequence = ++m_reloadSequence;
    const Loader loader = m_loader;
    QPointer<MakerSelector> guard(this);
    QThreadPool::globalInstance()->start([guard, loader, sequence] {
        QList<MakerOption> loaded;
        try { loaded = loader(); } catch (...) { loaded.clear(); }
        if (guard.isNull()) return;
        QMetaObject::invokeMethod(guard, [guard, sequence, loaded] {
            if (!guard.isNull()) emit guard->makersLoaded(sequence, loaded);
        }, Qt::QueuedConnection);
    });
}

void MakerSelector::filterCompletions(const QString &text)
{
    const QString keyword = text.trimmed().toLower();
    QStringList matches;
    for (const Record &record : m_records) {
        if (keyword.isEmpty() || record.searchText.contains(keyword))
            matches.append(record.displayName);
    }
    m_completionModel->setStringList(matches);
    if (hasFocus()) m_completer->complete();
}

void MakerSelector::synchronizeSelection(const QString &text)
{
    m_selectedId.reset();
    const QString key = text.trimmed();
    for (const Record &record : m_records) {
        if (record.displayName == key) {
            m_selectedId = record.id;
            return;
        }
    }
}

} // namespace darkeye
