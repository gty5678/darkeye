#pragma once

#include "darkeye_ui/base/LazyWidget.h"

#include <QString>

class QLabel;
class MarkdownEditorWidget;
class QTimer;

namespace mdedit { class ProjectFileTree; class ProjectFilesPanel; }

namespace darkeye
{

class ThemeService;

class AvPage final : public LazyWidget
{
public:
    explicit AvPage(QWidget *parent = nullptr);
    explicit AvPage(QString wikiDirectory, QWidget *parent = nullptr);
    AvPage(QString wikiDirectory, ThemeService &themes, QWidget *parent = nullptr);

private:
    void lazyLoad() override;
    void applyTheme();
    [[nodiscard]] QString resolveWikiDirectory() const;
    void openDocument(const QString &filePath);
    void createDocument(const QString &filePath);
    void closeDocument();
    bool loadEditor(const QString &filePath);
    bool autoSave();

    QString m_requestedWikiDirectory;
    QString m_wikiDirectory;
    QString m_currentPath;
    mdedit::ProjectFileTree *m_tree = nullptr;
    mdedit::ProjectFilesPanel *m_filesPanel = nullptr;
    MarkdownEditorWidget *m_editor = nullptr;
    QLabel *m_emptyMessage = nullptr;
    QTimer *m_saveTimer = nullptr;
    ThemeService *m_themes = nullptr;
};

} // namespace darkeye
