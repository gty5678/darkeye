#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QVariantMap>

namespace darkeye::utils {

[[nodiscard]] QList<int> loadIds(const QString &jsonValue);
[[nodiscard]] double imageMse(const QString &firstPath, const QString &secondPath);
[[nodiscard]] QString textColorForBackground(const QColor &color);
[[nodiscard]] QString hoverColorForBackground(const QColor &color);
[[nodiscard]] QColor invertColor(const QColor &color);
[[nodiscard]] double rankPosition(double value, const QList<double> &data,
                                  bool reverse = false);
[[nodiscard]] QString convertDate(const QString &date);
[[nodiscard]] QImage mosaic(const QImage &image, int pixelSize = 20);
[[nodiscard]] QString replaceSensitive(QString text, const QStringList &words,
                                       const QString &replacement = QStringLiteral("**"));
[[nodiscard]] QList<QVariantMap> orderMapKeys(const QList<QVariantMap> &data,
                                              const QStringList &keyOrder);

} // namespace darkeye::utils
