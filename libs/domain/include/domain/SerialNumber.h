#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace darkeye::serial {

QString normalizeNumber(const QString &value);
QString normalizeRawName(const QString &rawName,
                         const QStringList &escapeStrings = {});
bool isValid(const QString &code);
QString convertFanza(const QString &serialNumber);
bool equal(const QString &left, const QString &right);
QString convertSpecial(const QString &serialNumber);
std::optional<QString> extract(const std::optional<QString> &text,
                               const QStringList &escapeStrings = {});

} // namespace darkeye::serial
