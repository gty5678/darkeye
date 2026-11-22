#include "domain/SerialNumber.h"

#include <QRegularExpression>
#include <array>

namespace darkeye::serial {
namespace {

using Option = QRegularExpression::PatternOption;

QRegularExpression expression(const QString &pattern)
{
    return QRegularExpression(pattern, Option::CaseInsensitiveOption
                                           | Option::UseUnicodePropertiesOption);
}

QString stripTrailingTokens(QString value, bool stripBarePart)
{
    static const std::array<QRegularExpression, 8> patterns = {
        expression(QStringLiteral(R"([-_.\s]U$)")),
        expression(QStringLiteral(R"([-_.\s](?:CD|PART|EP)[-_\s]?\d{1,2}$)")),
        expression(QStringLiteral(R"([-_.\s](?:前番|前編|後番|後編)$)")),
        expression(QStringLiteral(R"([-_.\s]C[-_.\s]+C$)")),
        expression(QStringLiteral(R"([-_.\s]CH$)")),
        expression(QStringLiteral(R"((?<=\d)CH$)")),
        expression(QStringLiteral(R"([-_.\s]C$)")),
        expression(QStringLiteral(R"((?<=\d)C$)")),
    };
    static const QRegularExpression barePart =
        expression(QStringLiteral(R"([-_.\s][1-9]$)"));

    while (true) {
        QString next = value;
        for (const auto &pattern : patterns) {
            next.remove(pattern);
        }
        if (stripBarePart) {
            next.remove(barePart);
        }
        if (next == value) {
            return value;
        }
        value = next;
    }
}

struct Pattern
{
    enum class Kind
    {
        Single,
        Join,
    };
    Kind kind;
    QRegularExpression regex;
};

const QList<Pattern> &orderedPatterns()
{
    static const QList<Pattern> patterns = {
        {Pattern::Kind::Single, expression(R"((FC2-\d{5,}))")},
        {Pattern::Kind::Single, expression(R"((FC2\d{5,}))")},
        {Pattern::Kind::Single, expression(R"((HEYZO-\d{3,}))")},
        {Pattern::Kind::Single, expression(R"((HEYZO\d{3,}))")},
        {Pattern::Kind::Single, expression(R"((TH101-\d{3,}-\d{5,}))")},
        {Pattern::Kind::Single, expression(R"((T28-?\d{3,}))")},
        {Pattern::Kind::Single, expression(R"((S2M[BD]*-\d{3,}))")},
        {Pattern::Kind::Single, expression(R"((MCB3D[BD]*-\d{2,}))")},
        {Pattern::Kind::Single, expression(R"((KIN8(?:TENGOKU)?-?\d{3,}))")},
        {Pattern::Kind::Single, expression(R"((CW3D2D?BD-?\d{2,}))")},
        {Pattern::Kind::Single, expression(R"((MMR-?[A-Z]{2,}-?\d+[A-Z]*))")},
        {Pattern::Kind::Single, expression(R"((XXX-AV-\d{4,}))")},
        {Pattern::Kind::Single, expression(R"((MKY-[A-Z]+-\d{3,}))")},
        {Pattern::Kind::Join, expression(R"(([A-Z]{2,})00(\d{3}))")},
        {Pattern::Kind::Single, expression(R"((\d{2,}[A-Z]{2,}-\d{2,}[A-Z]?))")},
        {Pattern::Kind::Single, expression(R"((?:^|[^A-Z])(N\d{4})(?:[^A-Z]|$))")},
        {Pattern::Kind::Single, expression(R"(([A-Z]+-[A-Z]\d+))")},
        {Pattern::Kind::Single, expression(R"(([A-Z]{2,}-\d{2,}[A-Z]?))")},
        {Pattern::Kind::Join,
         expression(R"(([A-Z]{2,6})(\d{1,5})(?=$|[-_\s\[\](){}【】（）]|[A-Z]{2,}))")},
        {Pattern::Kind::Join, expression(R"(([A-Z]{3,}).*?(\d{2,}))")},
        {Pattern::Kind::Join, expression(R"(([A-Z]{2,}).*?(\d{3,}))")},
        {Pattern::Kind::Single, expression(R"((\d{3,}-[A-Z]{3,}))")},
        {Pattern::Kind::Join, expression(R"(H_\d{3,}([A-Z]{2,})(\d{2,}))")},
    };
    return patterns;
}

bool isSpaceJoinedWordNumber(const QString &serial, const QString &original)
{
    static const QRegularExpression serialPattern =
        expression(QStringLiteral(R"(^([A-Z]{2,})-(\d{2,})$)"));
    const auto match = serialPattern.match(serial.trimmed().toUpper());
    if (!match.hasMatch()) {
        return false;
    }
    const QRegularExpression originalPattern =
        expression(QStringLiteral(R"(%1\s+%2\b)")
                       .arg(QRegularExpression::escape(match.captured(1)),
                            QRegularExpression::escape(match.captured(2))));
    return originalPattern.match(original).hasMatch();
}

std::optional<QString> extractNormalized(const QString &normalized,
                                         const QString &original)
{
    for (const Pattern &pattern : orderedPatterns()) {
        const auto match = pattern.regex.match(normalized);
        if (!match.hasMatch()) {
            continue;
        }
        QString raw;
        if (pattern.kind == Pattern::Kind::Join) {
            raw = match.captured(1) + QLatin1Char('-') + match.captured(2);
        } else {
            raw = match.captured(1).isNull() ? match.captured(0)
                                             : match.captured(1);
        }
        const QString result = normalizeNumber(raw);
        if (!isSpaceJoinedWordNumber(result, original)) {
            return result;
        }
    }
    return std::nullopt;
}

std::optional<QString> fallback(const QString &normalized, const QString &original)
{
    const QList<QRegularExpression> patterns = {
        expression(QStringLiteral(R"([A-Z]{2,6}-\d{1,5})")),
        expression(QStringLiteral(R"([A-Z]{2,6}\d{1,5})")),
    };
    for (int index = 0; index < patterns.size(); ++index) {
        auto iterator = patterns.at(index).globalMatch(normalized);
        while (iterator.hasNext()) {
            QString candidate = iterator.next().captured(0);
            if (index == 1) {
                candidate.replace(expression(QStringLiteral(R"(^([A-Z]+)(\d+)$)")),
                                  QStringLiteral("\\1-\\2"));
            }
            candidate = normalizeNumber(candidate).toUpper();
            if (!isSpaceJoinedWordNumber(candidate, original)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

} // namespace

QString normalizeNumber(const QString &value)
{
    QString result = value;
    result.replace(QRegularExpression(QStringLiteral("FC-")),
                   QStringLiteral("FC2-"));
    result.replace(QRegularExpression(QStringLiteral("-+")), QStringLiteral("-"));
    result.remove(QRegularExpression(QStringLiteral(R"(^[-_.\s]+|[-_.\s]+$)"),
                                     Option::UseUnicodePropertiesOption));
    return result;
}

QString normalizeRawName(const QString &rawName, const QStringList &escapeStrings)
{
    QString normalized = rawName.normalized(QString::NormalizationForm_C).toUpper();
    for (const QString &token : escapeStrings) {
        if (!token.trimmed().isEmpty()) {
            normalized.replace(token.trimmed().toUpper(), QString());
        }
    }

    static const QStringList shortTokens = {
        QStringLiteral("4K"), QStringLiteral("4KS"), QStringLiteral("8K"),
        QStringLiteral("2160P"), QStringLiteral("1080P"), QStringLiteral("720P"),
        QStringLiteral("HD"), QStringLiteral("HEVC"), QStringLiteral("H264"),
        QStringLiteral("H265"), QStringLiteral("X264"), QStringLiteral("X265"),
        QStringLiteral("AAC"), QStringLiteral("DVD"), QStringLiteral("FULL"),
    };
    for (const QString &token : shortTokens) {
        normalized.replace(
            expression(QStringLiteral(R"((?:^|[-_.\s\[])%1(?=$|[-_.\s\]]))")
                           .arg(QRegularExpression::escape(token))),
            QStringLiteral("-"));
    }
    normalized.replace(expression(QStringLiteral(R"(FC2[-_ ]?PPV)")),
                       QStringLiteral("FC2-"));
    normalized.replace(expression(QStringLiteral("GACHIPPV")),
                       QStringLiteral("GACHI"));
    normalized.replace(QRegularExpression(QStringLiteral("-+")), QStringLiteral("-"));
    normalized.remove(expression(QStringLiteral(R"(\d{4}[-_.]\d{1,2}[-_.]\d{1,2})")));
    normalized.remove(expression(QStringLiteral(R"([-\[]\d{2}[-_.]\d{2}[-_.]\d{2}\]?)")));
    normalized.remove(expression(QStringLiteral(R"([-_.\s][A-Z0-9]\.$)")));
    normalized.remove(expression(QStringLiteral(R"(^\d+[-_.\s]*(?=[A-Z]))")));
    normalized = stripTrailingTokens(normalized, true);
    normalized.replace(expression(QStringLiteral(R"([-_.\s]+)")), QStringLiteral("-"));
    normalized.remove(expression(QStringLiteral(R"(^[-_.\s]+|[-_.\s]+$)")));
    return normalized;
}

bool isValid(const QString &code)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[A-Z]{2,6}-\d{1,5}$)"));
    return pattern.match(code.toUpper()).hasMatch();
}

QString convertFanza(const QString &serialNumber)
{
    QString converted = serialNumber.toLower();
    converted.replace(QLatin1Char('-'), QStringLiteral("00"));
    static const QStringList prefixed = {
        QStringLiteral("start"), QStringLiteral("stars"), QStringLiteral("star"),
        QStringLiteral("sdde"), QStringLiteral("kmhrs"), QStringLiteral("namh"),
        QStringLiteral("dldss"), QStringLiteral("fns"), QStringLiteral("fsdss"),
        QStringLiteral("boko"), QStringLiteral("sdam"), QStringLiteral("hawa"),
        QStringLiteral("moon"), QStringLiteral("mogi"), QStringLiteral("nhdtb"),
    };
    for (const QString &prefix : prefixed) {
        if (converted.startsWith(prefix)) {
            converted.prepend(QLatin1Char('1'));
            break;
        }
    }
    if (converted.startsWith(QStringLiteral("knmb"))) {
        converted.prepend(QStringLiteral("h_491"));
    }
    if (converted.startsWith(QStringLiteral("isrd"))) {
        converted.prepend(QStringLiteral("24"));
    }
    return converted;
}

bool equal(const QString &left, const QString &right)
{
    auto normalized = [](QString value) {
        value = value.toLower();
        value.replace(QLatin1Char('-'), QStringLiteral("00"));
        return value;
    };
    return normalized(left) == normalized(right);
}

QString convertSpecial(const QString &serialNumber)
{
    QString result = serialNumber.toLower();
    result.remove(QLatin1Char('-'));
    return result;
}

std::optional<QString> extract(const std::optional<QString> &text,
                               const QStringList &escapeStrings)
{
    if (!text.has_value() || text->trimmed().isEmpty()) {
        return std::nullopt;
    }
    const QString original = text->trimmed();
    static const QRegularExpression datedNumericCode =
        expression(QStringLiteral(R"((?:^|[^0-9])(\d{6}_\d{3})(?:[^0-9]|$))"));
    const QRegularExpressionMatch datedMatch = datedNumericCode.match(original);
    if (datedMatch.hasMatch()) {
        return datedMatch.captured(1);
    }
    const QString normalized = normalizeRawName(original, escapeStrings);
    if (normalized.isEmpty()) {
        return std::nullopt;
    }
    const auto matched = extractNormalized(normalized, original);
    return matched.has_value() ? std::optional<QString>(matched->toUpper())
                              : fallback(normalized, original);
}

} // namespace darkeye::serial
