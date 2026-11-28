#include "utils/GeneralUtils.h"

#include <QDate>
#include <algorithm>

namespace darkeye::utils {
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

} // namespace darkeye::utils
