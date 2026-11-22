#pragma once

#include "darkeye_ui/base/LazyWidget.h"

#include <QHash>
#include <QString>

class QFileSystemWatcher;
class QPlainTextEdit;
class QShowEvent;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QUrl;
namespace darkeye { class DesignButton; }

namespace darkeye
{

class AvPage final : public LazyWidget
{
public:
    explicit AvPage(QWidget *parent = nullptr);
    explicit AvPage(QString wikiDirectory, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void lazyLoad() override;
    [[nodiscard]] QString resolveWikiDirectory() const;
    void rebuildTree();
    void restoreSelection();
    void selectFirstDocument();
    [[nodiscard]] QTreeWidgetItem *firstDocument(QTreeWidgetItem *item) const;
    void openItem(QTreeWidgetItem *item);
    void loadDocument(const QString &filePath);
    void loadEditor(const QString &filePath);
    void autoSave();
    void switchToPreview();
    void switchToEdit();
    void watchDirectory();
    void handleLink(const QUrl &url);

    QString m_requestedWikiDirectory;
    QString m_wikiDirectory;
    QString m_currentPath;
    QTreeWidget *m_tree = nullptr;
    QTextBrowser *m_browser = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QStackedWidget *m_contentStack = nullptr;
    DesignButton *m_previewButton = nullptr;
    DesignButton *m_editButton = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_saveTimer = nullptr;
    QHash<QString, QString> m_stemToPath;
    QHash<QString, QTreeWidgetItem *> m_pathToItem;
    bool m_editDirty = false;
    bool m_loadingEditor = false;
};

} // namespace darkeye
