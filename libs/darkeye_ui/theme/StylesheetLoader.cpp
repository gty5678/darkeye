#include "darkeye_ui/theme/StylesheetLoader.h"

#include <QFile>

namespace darkeye
{

QString StylesheetLoader::load(const QString &templatePath, const ThemeTokens &tokens,
                               QString *errorMessage)
{
    QFile file(templatePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = file.errorString();
        }
        return {};
    }
    return render(QString::fromUtf8(file.readAll()), tokens);
}

QString StylesheetLoader::render(QString stylesheetTemplate, const ThemeTokens &tokens)
{
    const auto values = tokens.toMap();
    for (auto iterator = values.cbegin(); iterator != values.cend(); ++iterator)
    {
        stylesheetTemplate.replace(
            QStringLiteral("{{%1}}").arg(iterator.key()), iterator.value());
    }
    return stylesheetTemplate;
}

} // namespace darkeye
