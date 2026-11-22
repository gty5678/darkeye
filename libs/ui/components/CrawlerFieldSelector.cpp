#include "ui/components/CrawlerFieldSelector.h"

#include "darkeye_ui/components/TokenControls.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHash>
#include <QSignalBlocker>

namespace darkeye
{

CrawlerFieldSelector::CrawlerFieldSelector(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("CrawlerFieldSelector"));
    m_layout = new QGridLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setHorizontalSpacing(12);
    m_layout->setVerticalSpacing(6);

    // This pane may be docked into a narrow workspace column.  Keep the
    // requested two-column layout so a third intrinsic column cannot force
    // the entire workspace wider.
    constexpr int columns = 2;
    const QList<CrawlerFieldDefinition> fields = availableFields();
    for (int index = 0; index < fields.size(); ++index)
    {
        const CrawlerFieldDefinition &field = fields.at(index);
        auto *checkBox = new TokenCheckBox(field.label, this);
        checkBox->setObjectName(QStringLiteral("CrawlerField_%1").arg(field.key));
        checkBox->setProperty("fieldKey", field.key);
        m_checkBoxes.insert(field.key, checkBox);
        m_layout->addWidget(checkBox, index / columns, index % columns);
        connect(checkBox, &QCheckBox::toggled, this,
                [this](bool) { emit selectionChanged(selectedFields()); });
    }
}

QList<CrawlerFieldDefinition> CrawlerFieldSelector::availableFields()
{
    return {
        {QStringLiteral("release_date"), QStringLiteral("发布日期")},
        {QStringLiteral("director"), QStringLiteral("导演")},
        {QStringLiteral("cover"), QStringLiteral("封面")},
        {QStringLiteral("cn_title"), QStringLiteral("中文标题")},
        {QStringLiteral("jp_title"), QStringLiteral("日文标题")},
        {QStringLiteral("actress"), QStringLiteral("女优")},
        {QStringLiteral("cn_story"), QStringLiteral("中文故事")},
        {QStringLiteral("jp_story"), QStringLiteral("日文故事")},
        {QStringLiteral("actor"), QStringLiteral("男优")},
        {QStringLiteral("tag"), QStringLiteral("标签")},
        {QStringLiteral("runtime"), QStringLiteral("时长")},
        {QStringLiteral("maker"), QStringLiteral("片商")},
        {QStringLiteral("label"), QStringLiteral("厂牌")},
        {QStringLiteral("series"), QStringLiteral("系列")},
        {QStringLiteral("fanart"), QStringLiteral("剧照")},
    };
}

QSet<QString> CrawlerFieldSelector::selectedFields() const
{
    QSet<QString> result;
    for (auto iterator = m_checkBoxes.constBegin(); iterator != m_checkBoxes.constEnd(); ++iterator)
    {
        if (iterator.value()->isChecked())
            result.insert(iterator.key());
    }
    return result;
}

void CrawlerFieldSelector::setSelectedFields(const QSet<QString> &fields)
{
    applySelection(fields);
}

bool CrawlerFieldSelector::setFieldChecked(const QString &field, bool checked)
{
    QCheckBox *checkBox = m_checkBoxes.value(field);
    if (checkBox == nullptr)
        return false;
    checkBox->setChecked(checked);
    return true;
}

bool CrawlerFieldSelector::isFieldChecked(const QString &field) const
{
    const QCheckBox *checkBox = m_checkBoxes.value(field);
    return checkBox != nullptr && checkBox->isChecked();
}

void CrawlerFieldSelector::selectAll()
{
    QSet<QString> fields;
    for (const CrawlerFieldDefinition &field : availableFields())
        fields.insert(field.key);
    applySelection(fields);
}

void CrawlerFieldSelector::clearSelection()
{
    applySelection({});
}

void CrawlerFieldSelector::invertSelection()
{
    QSet<QString> fields;
    for (const CrawlerFieldDefinition &field : availableFields())
    {
        if (!isFieldChecked(field.key))
            fields.insert(field.key);
    }
    applySelection(fields);
}

void CrawlerFieldSelector::appendRowWidget(QWidget *widget, int column, int columnSpan)
{
    if (widget == nullptr)
        return;
    m_layout->addWidget(widget, m_layout->rowCount(), qMax(0, column), 1, qMax(1, columnSpan));
}

void CrawlerFieldSelector::applySelection(const QSet<QString> &fields)
{
    const QSet<QString> oldSelection = selectedFields();
    for (auto iterator = m_checkBoxes.begin(); iterator != m_checkBoxes.end(); ++iterator)
    {
        const QSignalBlocker blocker(iterator.value());
        iterator.value()->setChecked(fields.contains(iterator.key()));
    }
    const QSet<QString> newSelection = selectedFields();
    if (newSelection != oldSelection)
        emit selectionChanged(newSelection);
}

} // namespace darkeye
