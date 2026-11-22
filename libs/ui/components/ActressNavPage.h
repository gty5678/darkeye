#pragma once

#include <QWidget>
#include <QJsonObject>

class QGridLayout;

namespace darkeye
{

// External actress lookup links compatible with data/actress_nav_buttons.json.
class ActressNavPage final : public QWidget
{
    Q_OBJECT

public:
    explicit ActressNavPage(QString configFile, QWidget *parent = nullptr);

    void setNames(QString japaneseName, QString chineseName);

private:
    void loadButtons(QGridLayout *layout);
    void openLink(const QJsonObject &config) const;

    QString m_configFile;
    QString m_japaneseName;
    QString m_chineseName;
};

} // namespace darkeye
