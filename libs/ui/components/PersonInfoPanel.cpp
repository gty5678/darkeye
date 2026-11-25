#include "ui/components/PersonInfoPanel.h"

#include "darkeye_ui/theme/ChamferPath.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/Charts.h"
#include "darkeye_ui/components/CollapsibleSection.h"
#include "darkeye_ui/components/HeartLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/OctImage.h"
#include "darkeye_ui/components/TokenControls.h"
#include "darkeye_ui/components/TokenViews.h"
#include "ui/components/ClickableLabel.h"
#include "utils/GeneralUtils.h"

#include <QFontDatabase>
#include <QGridLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

namespace
{

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

QString valueOrDash(const QString &value)
{
    return value.trimmed().isEmpty() ? QStringLiteral("----") : value.trimmed();
}

QString valueOrDash(const std::optional<int> &value)
{
    return value.has_value() ? QString::number(*value) : QStringLiteral("----");
}

QString fixedDateFontFamily()
{
    const QString systemFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    if (QFontDatabase::isFixedPitch(systemFamily))
        return systemFamily;

    const auto families = QFontDatabase::families(QFontDatabase::Latin);
    for (const QString &family : families)
        if (QFontDatabase::isFixedPitch(family))
            return family;
    return systemFamily;
}

QPair<double, double> linearFit(const QList<QPair<double, double>> &pairs)
{
    if (pairs.size() < 2)
        return {0.0, pairs.isEmpty() ? 0.0 : pairs.first().second};
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    for (const auto &[x, y] : pairs)
    {
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }
    const double count = static_cast<double>(pairs.size());
    const double denominator = count * sumXX - sumX * sumX;
    if (qFuzzyIsNull(denominator)) return {0.0, sumY / count};
    const double slope = (count * sumXY - sumX * sumY) / denominator;
    return {slope, (sumY - slope * sumX) / count};
}

QList<QPair<double, double>> measurementPairs(
    const QList<darkeye::PersonBodyMetrics> &reference,
    const std::function<std::optional<int>(const darkeye::PersonBodyMetrics &)> &measurement)
{
    QList<QPair<double, double>> pairs;
    for (const auto &item : reference)
    {
        const auto value = measurement(item);
        if (item.height.has_value() && value.has_value())
            pairs.append({static_cast<double>(*item.height), static_cast<double>(*value)});
    }
    return pairs;
}

} // namespace

namespace darkeye
{

PersonInfoPanel::PersonInfoPanel(ThemeService &themes, QString imageDirectory, QWidget *parent)
    : QWidget(parent), m_themes(themes), m_imageDirectory(std::move(imageDirectory))
{
    setObjectName(QStringLiteral("PersonInfoPanel"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setMaximumWidth(380);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(12);

    auto *portraitRow = new QGridLayout;
    portraitRow->setContentsMargins(0, 0, 0, 0);
    m_avatar = new OctImage({}, m_imageDirectory, 150, true, this);
    m_avatar->setObjectName(QStringLiteral("PersonDetailAvatar"));
    m_avatar->installEventFilter(this);
    m_heart = new HeartLabel(this);
    m_heart->setObjectName(QStringLiteral("PersonFavoriteButton"));
    m_heart->setToolTip(QStringLiteral("收藏"));
    m_edit = new IconButton(QStringLiteral("square_pen"), &m_themes, this);
    m_edit->setObjectName(QStringLiteral("PersonEditButton"));
    m_edit->setToolTip(QStringLiteral("修改人物"));
    portraitRow->addWidget(m_avatar, 0, 0, Qt::AlignHCenter);
    portraitRow->addWidget(m_heart, 0, 0, Qt::AlignTop | Qt::AlignRight);
    portraitRow->addWidget(m_edit, 0, 0, Qt::AlignTop | Qt::AlignRight);
    root->addLayout(portraitRow);

    m_names = new QWidget(this);
    auto *namesLayout = new QVBoxLayout(m_names);
    namesLayout->setContentsMargins(0, 0, 0, 0);
    namesLayout->setSpacing(4);
    root->addWidget(m_names);

    m_facts = new QWidget(this);
    auto *factsLayout = new QGridLayout(m_facts);
    factsLayout->setContentsMargins(0, 0, 0, 0);
    factsLayout->setHorizontalSpacing(14);
    factsLayout->setVerticalSpacing(6);
    factsLayout->setColumnStretch(0, 1);
    factsLayout->setColumnStretch(1, 1);
    root->addWidget(m_facts);

    m_radar = new RadarChartWidget({}, {}, {}, 5, &m_themes, this);
    m_radar->setFixedSize(250, 220);
    root->addWidget(m_radar, 0, Qt::AlignHCenter);

    m_actressAliases = new TokenCollapsibleSection(QStringLiteral("别名（0）"), &m_themes, this);
    // Paint the resizing list directly.
    m_actressAliases->setObjectName(QStringLiteral("PersonAliasSection"));
    m_actressAliases->contentLayout()->setSpacing(10);
    root->addWidget(m_actressAliases);
    connect(m_actressAliases, &TokenCollapsibleSection::toggled, this, [this](bool expanded) {
        m_actressAliases->layout()->activate();
        emit aliasesToggled(expanded);
    });

    m_notes = new DesignLabel({}, this);
    m_notes->setObjectName(QStringLiteral("PersonNotes"));
    m_notes->setWordWrap(true);
    root->addWidget(m_notes);

    m_aliasesGroup = new TokenGroupBox(QStringLiteral("姓名与别名"), this);
    auto *aliasesLayout = new QVBoxLayout(m_aliasesGroup);
    m_aliases = new TokenTableWidget(0, 4, m_aliasesGroup);
    m_aliases->setObjectName(QStringLiteral("PersonAliasTable"));
    m_aliases->setHorizontalHeaderLabels({QStringLiteral("中文"), QStringLiteral("日文"),
                                          QStringLiteral("英文"), QStringLiteral("假名")});
    m_aliases->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_aliases->setEditTriggers(QAbstractItemView::NoEditTriggers);
    aliasesLayout->addWidget(m_aliases);
    root->addWidget(m_aliasesGroup);

    m_worksGroup = new TokenGroupBox(QStringLiteral("关联作品"), this);
    auto *worksLayout = new QVBoxLayout(m_worksGroup);
    m_works = new TokenTableWidget(0, 3, m_worksGroup);
    m_works->setObjectName(QStringLiteral("PersonWorkTable"));
    m_works->setHorizontalHeaderLabels(
        {QStringLiteral("番号"), QStringLiteral("标题"), QStringLiteral("发行日期")});
    m_works->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_works->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_works->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_works->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_works->setSelectionBehavior(QAbstractItemView::SelectRows);
    worksLayout->addWidget(m_works);
    root->addWidget(m_worksGroup, 1);

    connect(m_heart, &HeartLabel::clicked, this,
            [this](bool favorite) { emit favoriteChanged(favorite); });
    connect(m_edit, &QPushButton::clicked, this,
            [this]
            {
                if (m_personId > 0)
                    emit editRequested(m_personId);
            });
    connect(m_works, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int)
            {
                const auto *item = m_works->item(row, 0);
                if (item != nullptr)
                    emit workRequested(item->data(Qt::UserRole).toLongLong());
            });
}

void PersonInfoPanel::setDetails(const PersonDetails &details, bool favorite)
{
    if (m_personId != details.id || m_kind != details.kind)
        m_actressAliases->collapse();
    m_kind = details.kind;
    m_personId = details.id;
    m_avatar->updateImage(details.imagePath);
    m_heart->setVisible(details.kind == PersonKind::Actress);
    m_edit->setVisible(details.kind == PersonKind::Actor);
    m_notes->setVisible(details.kind == PersonKind::Actor);
    m_actressAliases->setVisible(details.kind == PersonKind::Actress);
    m_aliasesGroup->setVisible(details.kind == PersonKind::Actor);
    m_worksGroup->setVisible(details.kind == PersonKind::Actor);
    setFavorite(favorite);
    m_notes->setText(details.notes.trimmed().isEmpty() ? QStringLiteral("暂无备注")
                                                       : details.notes.trimmed());
    rebuildNames(details);
    rebuildFacts(details);
    rebuildRadar(details);
    rebuildWorks(details);
}

void PersonInfoPanel::setFavorite(bool favorite)
{
    m_heart->setState(favorite);
}

bool PersonInfoPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_avatar
        && (event->type() == QEvent::ContextMenu
            || (event->type() == QEvent::MouseButtonPress
                && static_cast<QMouseEvent *>(event)->button() == Qt::RightButton)))
    {
        // Keep the avatar's edit gesture from opening a context menu on a
        // parent or overlapping widget.
        event->accept();
        return true;
    }
    if (watched == m_avatar && event->type() == QEvent::MouseButtonRelease)
    {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton && m_personId > 0)
        {
            const qint64 personId = m_personId;
            QTimer::singleShot(0, this, [this, personId] { emit editRequested(personId); });
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PersonInfoPanel::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    constexpr qreal borderWidth = 1;
    constexpr qreal corner = 6;
    const QRectF bounds(rect());

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(m_themes.currentTokens().border), borderWidth));
    const qreal inset = borderWidth / 2;
    painter.drawPath(chamferPath(bounds.adjusted(inset, inset, -inset, -inset), corner));
}

qint64 PersonInfoPanel::personId() const noexcept
{
    return m_personId;
}

PersonKind PersonInfoPanel::kind() const noexcept
{
    return m_kind;
}

void PersonInfoPanel::rebuildNames(const PersonDetails &details)
{
    auto *nameLayout = m_names->layout();
    clearLayout(nameLayout);
    const auto addName = [this, nameLayout](const QString &name, bool actressJump, bool primary)
    {
        auto *label = new ClickableLabel(name, actressJump, m_names);
        connect(label, &ClickableLabel::actressJumpRequested,
                this, &PersonInfoPanel::actressExternalSearchRequested);
        QFont font = label->font();
        font.setPointSize(primary ? 26 : 14);
        font.setBold(primary);
        label->setFont(font);
        label->setAlignment(Qt::AlignCenter);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setWordWrap(true);
        nameLayout->addWidget(label);
    };
    const PersonName primary = details.names.isEmpty() ? PersonName{} : details.names.first();
    addName(valueOrDash(primary.chinese), false, true);
    const QList<QPair<QString, bool>> otherNames{
        {primary.japanese, details.kind == PersonKind::Actress},
        {primary.kana, false}, {primary.english, false}};
    for (const auto &[name, actressJump] : otherNames)
    {
        if (!name.trimmed().isEmpty())
            addName(name.trimmed(), actressJump, false);
    }

    if (details.kind == PersonKind::Actress)
    {
        rebuildActressAliases(details.names);
        return;
    }

    m_aliases->setRowCount(details.names.size());
    for (qsizetype row = 0; row < details.names.size(); ++row)
    {
        const PersonName &name = details.names.at(row);
        const QStringList values{name.chinese, name.japanese, name.english, name.kana};
        for (int column = 0; column < values.size(); ++column)
            m_aliases->setItem(row, column, new QTableWidgetItem(values.at(column)));
    }
}

void PersonInfoPanel::rebuildActressAliases(const QList<PersonName> &names)
{
    auto *list = m_actressAliases->contentLayout();
    clearLayout(list);
    int aliasCount = 0;
    for (qsizetype index = 1; index < names.size(); ++index)
    {
        const PersonName &name = names.at(index);
        const QString japanese = name.japanese.trimmed();
        QStringList variants{japanese, name.chinese.trimmed(), name.kana.trimmed(),
                             name.english.trimmed()};
        variants.removeAll(QString());
        variants.removeDuplicates();
        if (variants.isEmpty())
            continue;

        auto *entry = new QWidget(m_actressAliases->contentWidget());
        auto *entryLayout = new QVBoxLayout(entry);
        entryLayout->setContentsMargins(0, 0, 0, 0);
        entryLayout->setSpacing(3);
        for (const QString &variant : variants)
        {
            auto *label = new ClickableLabel(variant, variant == japanese, entry);
            label->setTextFormat(Qt::PlainText);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            label->setWordWrap(true);
            if (entryLayout->count() == 0)
            {
                QFont font = label->font();
                font.setBold(true);
                label->setFont(font);
            }
            else
            {
                label->setTone(QStringLiteral("muted"));
            }
            connect(label, &ClickableLabel::actressJumpRequested,
                    this, &PersonInfoPanel::actressExternalSearchRequested);
            entryLayout->addWidget(label);
        }
        list->addWidget(entry);
        ++aliasCount;
    }
    m_actressAliases->setTitle(QStringLiteral("别名（%1）").arg(aliasCount));
    if (aliasCount == 0)
        list->addWidget(new DesignLabel(QStringLiteral("暂无别名"), QStringLiteral("muted"),
                                        m_actressAliases->contentWidget()));
}

void PersonInfoPanel::rebuildRadar(const PersonDetails &details)
{
    m_radar->setVisible(details.kind == PersonKind::Actress);
    if (details.kind != PersonKind::Actress) return;
    const QStringList categories{QStringLiteral("身高"), QStringLiteral("罩杯"),
                                 QStringLiteral("胸围"), QStringLiteral("腰围"),
                                 QStringLiteral("臀围")};
    const QStringList displayed{valueOrDash(details.height), valueOrDash(details.cup),
                                valueOrDash(details.bust), valueOrDash(details.waist),
                                valueOrDash(details.hip)};
    if (!details.height.has_value() || !details.bust.has_value()
        || !details.waist.has_value() || !details.hip.has_value()
        || details.cup.trimmed().isEmpty())
    {
        m_radar->updateChart(categories, {0, 0, 0, 0, 0}, displayed);
        return;
    }

    QList<double> heights;
    for (const auto &item : details.bodyReference)
        if (item.height.has_value()) heights.append(*item.height);
    const auto residualRank = [&details](
        const std::function<std::optional<int>(const PersonBodyMetrics &)> &measurement,
        int current, bool reverse = false)
    {
        const auto pairs = measurementPairs(details.bodyReference, measurement);
        const auto [slope, intercept] = linearFit(pairs);
        QList<double> residuals;
        for (const auto &[height, value] : pairs)
            residuals.append(value - (slope * height + intercept));
        const double residual = current - (slope * *details.height + intercept);
        return utils::rankPosition(residual, residuals, reverse);
    };
    const QHash<QString, double> cupDifference{
        {QStringLiteral("AA"), 7.5}, {QStringLiteral("A"), 10.0},
        {QStringLiteral("B"), 12.5}, {QStringLiteral("C"), 15.0},
        {QStringLiteral("D"), 17.5}, {QStringLiteral("E"), 20.0},
        {QStringLiteral("F"), 22.5}, {QStringLiteral("G"), 25.0},
        {QStringLiteral("H"), 27.5}, {QStringLiteral("I"), 30.0},
        {QStringLiteral("J"), 32.5}, {QStringLiteral("K"), 35.0},
        {QStringLiteral("L"), 37.5}, {QStringLiteral("M"), 40.0},
        {QStringLiteral("N"), 42.5}, {QStringLiteral("O"), 45.0},
        {QStringLiteral("P"), 47.5}};
    QList<double> bustRatios;
    for (const auto &item : details.bodyReference)
    {
        const double difference = cupDifference.value(item.cup.trimmed().toUpper(), 0.0);
        if (!item.bust.has_value() || difference <= 0.0 || *item.bust == difference) continue;
        bustRatios.append(*item.bust / (*item.bust - difference));
    }
    const double currentDifference = cupDifference.value(
        details.cup.trimmed().left(1).toUpper(), 0.0);
    const double currentRatio = currentDifference > 0.0
        ? *details.bust / (*details.bust - currentDifference) : 0.0;
    QVector<qreal> values{
        utils::rankPosition(*details.height, heights),
        utils::rankPosition(currentRatio, bustRatios),
        residualRank([](const PersonBodyMetrics &item) { return item.bust; }, *details.bust),
        residualRank([](const PersonBodyMetrics &item) { return item.waist; }, *details.waist, true),
        residualRank([](const PersonBodyMetrics &item) { return item.hip; }, *details.hip)};
    for (qreal &value : values) value = std::sqrt(qBound(0.0, value, 1.0));
    m_radar->updateChart(categories, values, displayed);
}

void PersonInfoPanel::rebuildFacts(const PersonDetails &details)
{
    clearLayout(m_facts->layout());
    auto *grid = static_cast<QGridLayout *>(m_facts->layout());
    const bool isActress = details.kind == PersonKind::Actress;
    const QString dateFontFamily = isActress ? fixedDateFontFamily() : QString{};
    int row = 0;
    const auto addFact = [this, grid, &row, &dateFontFamily](const QString &label,
                                                            const QString &value,
                                                            bool fixedWidth = false)
    {
        auto *field = new DesignLabel(label, QStringLiteral("muted"), m_facts);
        field->setAlignment(Qt::AlignRight | Qt::AlignTop);
        auto *content = new DesignLabel(valueOrDash(value), m_facts);
        if (fixedWidth)
            content->setStyleSheet(QStringLiteral("font-family: \"%1\";").arg(dateFontFamily));
        content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        content->setWordWrap(true);
        grid->addWidget(field, row, 0);
        grid->addWidget(content, row, 1);
        ++row;
    };
    addFact(QStringLiteral("生日"), utils::convertDate(details.birthday), isActress);
    if (isActress)
    {
        addFact(QStringLiteral("出道日期"), utils::convertDate(details.debutDate), true);
    }
    else
    {
        addFact(QStringLiteral("身高"), valueOrDash(details.height));
        addFact(QStringLiteral("外貌"), valueOrDash(details.handsome));
        addFact(QStringLiteral("体型"), valueOrDash(details.fat));
    }
    if (details.kind == PersonKind::Actor)
        addFact(QStringLiteral("需要更新"),
                details.needUpdate ? QStringLiteral("是") : QStringLiteral("否"));
}

void PersonInfoPanel::rebuildWorks(const PersonDetails &details)
{
    m_works->setVisible(details.kind == PersonKind::Actor);
    m_works->setRowCount(details.works.size());
    for (qsizetype row = 0; row < details.works.size(); ++row)
    {
        const PersonWorkSummary &work = details.works.at(row);
        auto *serial = new QTableWidgetItem(work.serialNumber);
        serial->setData(Qt::UserRole, work.id);
        m_works->setItem(row, 0, serial);
        m_works->setItem(row, 1, new QTableWidgetItem(work.title));
        m_works->setItem(row, 2, new QTableWidgetItem(work.releaseDate));
    }
}

} // namespace darkeye
