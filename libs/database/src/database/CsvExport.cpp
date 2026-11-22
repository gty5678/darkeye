#include "database/CsvExport.h"

#include "database/SqliteConnection.h"

#include <QAbstractItemModel>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QStringConverter>

namespace darkeye {
namespace {
QString csvField(QString value)
{
    if (value.contains(QLatin1Char('"'))) value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"'))
        || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')))
        value = QLatin1Char('"') + value + QLatin1Char('"');
    return value;
}

bool openOutput(QSaveFile &file, QTextStream &stream, QString *errorMessage)
{
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    stream.setDevice(&file);
    stream.setEncoding(QStringConverter::Utf8);
    return true;
}

bool commitOutput(QSaveFile &file, QTextStream &stream, QString *errorMessage)
{
    // QTextStream may buffer the final write. Check it before publishing the file.
    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        if (errorMessage) *errorMessage = QStringLiteral("CSV 写入失败：%1").arg(file.errorString());
        return false;
    }
    if (!file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    return true;
}

void writeRow(QTextStream &stream, const QStringList &fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString &field : fields) escaped.append(csvField(field));
    stream << escaped.join(QLatin1Char(',')) << '\n';
}
}

bool exportModelToCsv(const QAbstractItemModel *model, const QString &csvFilePath,
                      QString *errorMessage)
{
    if (!model) {
        if (errorMessage) *errorMessage = QStringLiteral("当前没有可导出的表格数据。");
        return false;
    }
    QSaveFile file(csvFilePath);
    QTextStream stream;
    if (!openOutput(file, stream, errorMessage)) return false;
    QStringList headers;
    for (int column = 0; column < model->columnCount(); ++column)
        headers.append(model->headerData(column, Qt::Horizontal).toString());
    writeRow(stream, headers);
    for (int row = 0; row < model->rowCount(); ++row) {
        QStringList fields;
        for (int column = 0; column < model->columnCount(); ++column)
            fields.append(model->data(model->index(row, column)).toString());
        writeRow(stream, fields);
    }
    return commitOutput(file, stream, errorMessage);
}

bool exportSqlToCsv(const QString &sql, const QString &csvFilePath,
                    const QString &databasePath, QString *errorMessage)
{
    SqliteConnection connection;
    if (!connection.open(databasePath, true, errorMessage)) return false;
    QSqlQuery query(connection.database());
    if (!query.exec(sql)) {
        if (errorMessage) *errorMessage = query.lastError().text();
        return false;
    }
    QSaveFile file(csvFilePath);
    QTextStream stream;
    if (!openOutput(file, stream, errorMessage)) return false;
    QStringList headers;
    const QSqlRecord record = query.record();
    for (int column = 0; column < record.count(); ++column) headers.append(record.fieldName(column));
    if (!headers.isEmpty()) writeRow(stream, headers);
    while (query.next()) {
        QStringList fields;
        for (int column = 0; column < record.count(); ++column)
            fields.append(query.value(column).toString());
        writeRow(stream, fields);
    }
    if (query.lastError().isValid()) {
        if (errorMessage) *errorMessage = query.lastError().text();
        return false;
    }
    return commitOutput(file, stream, errorMessage);
}

} // namespace darkeye
