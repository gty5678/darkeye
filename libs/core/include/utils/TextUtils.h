#pragma once

#include <QSet>
#include <QStringList>
#include <QVariantMap>

namespace darkeye::utils {

[[nodiscard]] QStringList loadSensitiveWords(const QString &filePath,
                                             QString *errorMessage = nullptr);
[[nodiscard]] QVariantMap loadTagMap(const QString &filePath,
                                     QString *errorMessage = nullptr);
/// Returns tag names whose configured keywords occur in text. A key containing
/// `|` requires every segment to occur, matching the Python crawler contract.
[[nodiscard]] QStringList tagNamesFromText(const QString &text, const QString &filePath,
                                           QString *errorMessage = nullptr);
[[nodiscard]] QSet<QString> loadExcludedGenres(const QString &filePath,
                                               QString *errorMessage = nullptr);

} // namespace darkeye::utils
