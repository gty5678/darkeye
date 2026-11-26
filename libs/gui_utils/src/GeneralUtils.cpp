#include "utils/GeneralUtils.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>

namespace darkeye::utils {
namespace {
double luminance(const QColor &color)
{
    return 0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue();
}
}

QList<int> loadIds(const QString &jsonValue)
{
    if (jsonValue == QStringLiteral("@Invalid()")) return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(jsonValue.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) return {};
    QList<int> result;
    for (const QJsonValue &value : document.array()) {
        QString digits;
        if (value.isString()) digits = value.toString();
        else if (value.isDouble() && value.toDouble() == qFloor(value.toDouble()))
            digits = QString::number(static_cast<qint64>(value.toDouble()));
        static const QRegularExpression onlyDigits(QStringLiteral(R"(^\d+$)"));
        if (onlyDigits.match(digits).hasMatch()) result.append(digits.toInt());
    }
    return result;
}

double imageMse(const QString &firstPath, const QString &secondPath)
{
    QImage first(firstPath);
    QImage second(secondPath);
    if (first.isNull() || second.isNull() || first.size() != second.size()) return 1.0;
    first = first.convertToFormat(QImage::Format_RGBA8888);
    second = second.convertToFormat(QImage::Format_RGBA8888);
    if (first.sizeInBytes() != second.sizeInBytes()) return 1.0;
    quint64 squared = 0;
    for (qsizetype index = 0; index < first.sizeInBytes(); ++index) {
        const int difference = static_cast<int>(first.constBits()[index])
            - static_cast<int>(second.constBits()[index]);
        squared += static_cast<quint64>(difference * difference);
    }
    return static_cast<double>(squared)
        / static_cast<double>(first.width() * first.height());
}

QString textColorForBackground(const QColor &color)
{
    return luminance(color) > 128.0 ? QStringLiteral("black") : QStringLiteral("white");
}

QString hoverColorForBackground(const QColor &color)
{
    return luminance(color) > 128.0 ? QStringLiteral("#646464") : QStringLiteral("#C5C5C5");
}

QColor invertColor(const QColor &color)
{
    return {255 - color.red(), 255 - color.green(), 255 - color.blue(), color.alpha()};
}

double rankPosition(double value, const QList<double> &data, bool reverse)
{
    qsizetype rank = std::count_if(data.cbegin(), data.cend(),
                                  [value](double item) { return item <= value; });
    rank = qMax<qsizetype>(1, rank);
    double position = data.size() > 1
        ? static_cast<double>(rank - 1) / static_cast<double>(data.size() - 1) : 0.0;
    return reverse ? 1.0 - position : position;
}

QString convertDate(const QString &date)
{
    if (date.isNull()) return {};
    const QDate parsed = QDate::fromString(date, QStringLiteral("yyyy-MM-dd"));
    return parsed.isValid() ? parsed.toString(QStringLiteral("yyyy年MM月dd日")) : date;
}

QImage mosaic(const QImage &image, int pixelSize)
{
    if (image.isNull()) return {};
    pixelSize = qMax(1, pixelSize);
    const QImage small = image.scaled(qMax(1, image.width() / pixelSize),
                                      qMax(1, image.height() / pixelSize),
                                      Qt::IgnoreAspectRatio, Qt::FastTransformation);
    return small.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QString replaceSensitive(QString text, const QStringList &words, const QString &replacement)
{
    for (const QString &word : words) {
        if (word.isEmpty()) continue;
        text.replace(QRegularExpression(QRegularExpression::escape(word),
                                        QRegularExpression::CaseInsensitiveOption),
                     replacement);
    }
    return text;
}

QList<QVariantMap> orderMapKeys(const QList<QVariantMap> &data, const QStringList &keyOrder)
{
    QList<QVariantMap> ordered;
    ordered.reserve(data.size());
    for (const QVariantMap &item : data) {
        QVariantMap row;
        for (const QString &key : keyOrder) row.insert(key, item.value(key));
        ordered.append(row);
    }
    return ordered;
}

} // namespace darkeye::utils
