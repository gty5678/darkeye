#pragma once

#include <QCache>
#include <QObject>
#include <QString>

namespace darkeye {
// Antialiased cut corners, retaining the native style colours and border width.
class ChamferStyle final : public QObject
{
public:
    explicit ChamferStyle(QObject *parent);
    static QString flattenCorners(QString styleSheet);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    QCache<QString, QString> m_flattenedStyles{128};
};
} // namespace darkeye
