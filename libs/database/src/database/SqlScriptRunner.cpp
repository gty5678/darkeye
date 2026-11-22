#include "database/SqlScriptRunner.h"

#include "app/Resources.h"

#include <QFile>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

namespace darkeye {

QStringList SqlScriptRunner::splitStatements(const QString &script)
{
    enum class State {
        Normal,
        SingleQuote,
        DoubleQuote,
        Backtick,
        Bracket,
        LineComment,
        BlockComment,
    };

    State state = State::Normal;
    QString current;
    QStringList statements;

    const QRegularExpression createTrigger(
        QStringLiteral("\\bCREATE\\s+(?:(?:TEMP|TEMPORARY)\\s+)?TRIGGER\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression triggerEnd(
        QStringLiteral("\\bEND\\s*$"), QRegularExpression::CaseInsensitiveOption);

    for (qsizetype index = 0; index < script.size(); ++index) {
        const QChar character = script.at(index);
        const QChar next = index + 1 < script.size() ? script.at(index + 1) : QChar();
        current.append(character);

        switch (state) {
        case State::Normal:
            if (character == QLatin1Char('\'')) {
                state = State::SingleQuote;
            } else if (character == QLatin1Char('"')) {
                state = State::DoubleQuote;
            } else if (character == QLatin1Char('`')) {
                state = State::Backtick;
            } else if (character == QLatin1Char('[')) {
                state = State::Bracket;
            } else if (character == QLatin1Char('-') && next == QLatin1Char('-')) {
                current.append(next);
                ++index;
                state = State::LineComment;
            } else if (character == QLatin1Char('/') && next == QLatin1Char('*')) {
                current.append(next);
                ++index;
                state = State::BlockComment;
            } else if (character == QLatin1Char(';')) {
                const QString withoutTerminator = current.left(current.size() - 1).trimmed();
                const bool isTrigger = createTrigger.match(withoutTerminator).hasMatch();
                if (isTrigger && !triggerEnd.match(withoutTerminator).hasMatch()) {
                    break;
                }
                if (!withoutTerminator.isEmpty()) {
                    statements.append(withoutTerminator);
                }
                current.clear();
            }
            break;
        case State::SingleQuote:
            if (character == QLatin1Char('\'') && next == QLatin1Char('\'')) {
                current.append(next);
                ++index;
            } else if (character == QLatin1Char('\'')) {
                state = State::Normal;
            }
            break;
        case State::DoubleQuote:
            if (character == QLatin1Char('"') && next == QLatin1Char('"')) {
                current.append(next);
                ++index;
            } else if (character == QLatin1Char('"')) {
                state = State::Normal;
            }
            break;
        case State::Backtick:
            if (character == QLatin1Char('`')) {
                state = State::Normal;
            }
            break;
        case State::Bracket:
            if (character == QLatin1Char(']')) {
                state = State::Normal;
            }
            break;
        case State::LineComment:
            if (character == QLatin1Char('\n')) {
                state = State::Normal;
            }
            break;
        case State::BlockComment:
            if (character == QLatin1Char('*') && next == QLatin1Char('/')) {
                current.append(next);
                ++index;
                state = State::Normal;
            }
            break;
        }
    }

    const QString tail = current.trimmed();
    if (!tail.isEmpty()) {
        statements.append(tail);
    }
    return statements;
}

bool SqlScriptRunner::execute(QSqlDatabase database, const QString &script,
                              QString *errorMessage)
{
    const QStringList statements = splitStatements(script);
    for (qsizetype index = 0; index < statements.size(); ++index) {
        QSqlQuery query(database);
        if (!query.exec(statements.at(index))) {
            database.rollback();
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("SQL 语句 %1 执行失败：%2")
                                    .arg(index + 1)
                                    .arg(query.lastError().text());
            }
            return false;
        }
    }
    return true;
}

bool SqlScriptRunner::executeResource(QSqlDatabase database, const QString &resourcePath,
                                      QString *errorMessage)
{
    Resources::ensureInitialized();
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取 SQL 资源：%1").arg(resourcePath);
        }
        return false;
    }
    return execute(database, QString::fromUtf8(file.readAll()), errorMessage);
}

} // namespace darkeye
