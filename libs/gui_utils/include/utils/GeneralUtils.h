#pragma once

#include <QList>
#include <QString>

namespace darkeye::utils {

[[nodiscard]] double rankPosition(double value, const QList<double> &data,
                                  bool reverse = false);
[[nodiscard]] QString convertDate(const QString &date);

} // namespace darkeye::utils
