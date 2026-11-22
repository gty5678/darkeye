#pragma once

#include "darkeye_ui/components/DesignComboBox.h"

#include <functional>
#include <optional>
#include <QStringList>

class QCompleter;
class QStringListModel;

namespace darkeye {

struct MakerOption
{
    std::optional<qint64> id;
    QString chineseName;
    QString japaneseName;
    QStringList aliases;
};

// Searchable, asynchronously reloadable maker picker.  This deliberately has
// no business-layer dependency so it can be consumed as a darkeye_ui component.
class MakerSelector final : public DesignComboBox
{
    Q_OBJECT

public:
    using Loader = std::function<QList<MakerOption>()>;

    explicit MakerSelector(const QList<MakerOption> &makers = {},
                           QWidget *parent = nullptr);
    void setMakers(const QList<MakerOption> &makers);
    void setMaker(std::optional<qint64> makerId);
    std::optional<qint64> maker() const;
    void setLoader(Loader loader);
    void reloadMakers();

signals:
    void makersLoaded(int sequence, const QList<darkeye::MakerOption> &makers);

private:
    struct Record {
        std::optional<qint64> id;
        QString displayName;
        QString searchText;
    };
    void filterCompletions(const QString &text);
    void synchronizeSelection(const QString &text);

    QList<Record> m_records;
    std::optional<qint64> m_selectedId;
    QStringListModel *m_completionModel = nullptr;
    QCompleter *m_completer = nullptr;
    Loader m_loader;
    int m_reloadSequence = 0;
};

} // namespace darkeye

Q_DECLARE_METATYPE(darkeye::MakerOption)
