#pragma once

#include "domain/Work.h"

#include <QHash>
#include <QSet>
#include <QWidget>
#include <functional>

class QLineEdit;
class QLabel;
class QGraphicsScene;
class QGraphicsView;
class QTabWidget;

namespace darkeye
{

class IconButton;
class TagCard;
class TagFlowScene;
class TagGraphicsView;
class ThemeService;

// A single-card preview that shares WorkTagSelector's TagCard renderer.  It is
// used by tag management so a live preview cannot drift from the selector.
class TagDisplayPreview final : public QWidget
{
    Q_OBJECT

  public:
    explicit TagDisplayPreview(ThemeService *themes = nullptr, QWidget *parent = nullptr);
    void setTag(const QString &name, const QString &color, const QString &detail = {});

  private:
    QGraphicsView *m_view = nullptr;
    QGraphicsScene *m_scene = nullptr;
    TagCard *m_card = nullptr;
    ThemeService *m_themes = nullptr;
};

class WorkTagSelector final : public QWidget
{
    Q_OBJECT

  public:
    using Loader = std::function<QList<TagOption>()>;

    explicit WorkTagSelector(const QList<TagOption> &tags, ThemeService *themes = nullptr, QWidget *parent = nullptr);

    [[nodiscard]] QList<qint64> selectedIds() const;
    // 对应 Python TagSelector5.load_with_ids：由作品编辑器加载关联标签时同步左侧已选区。
    void setSelectedIds(const QList<qint64> &ids);
    void clearSelection();
    void setModified(bool modified);
    void setAvailablePanelWidth(int width);
    enum class SelectedColumnLayout { Compact, ThreeColumns };
    void setSelectedColumnLayout(SelectedColumnLayout selectedLayout);
    void useResponsiveLayout(int availableMaximum = 700);
    void setLoader(Loader loader);
    void setMutexCheckEnabled(bool enabled) { m_mutexCheckEnabled = enabled; }
    void reloadTags();

  signals:
    void selectionChanged();

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void updateSelectedWidth();
    void updateWidth();
    void setTagSelected(qint64 tagId, bool selected, bool switchTab = true);
    void rebuildSearch(const QString &text);
    void navigateSearch(int offset);
    void showSearchResult();
    void updateAvailableVisibility();
    void setTags(const QList<TagOption> &tags);
    [[nodiscard]] const TagOption *tagById(qint64 tagId) const;
    void applyViewStyle(TagGraphicsView *view) const;
    void applyTabStyle() const;

    QList<TagOption> m_tags;
    QHash<qint64, TagCard *> m_items;
    QHash<qint64, TagCard *> m_selectedItems;
    QHash<QString, TagFlowScene *> m_flows;
    QHash<QString, TagGraphicsView *> m_views;
    QSet<qint64> m_selectedIds;
    QList<qint64> m_selectedOrder;
    TagFlowScene *m_selectedFlow = nullptr;
    TagGraphicsView *m_selectedView = nullptr;
    QWidget *m_panel = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_searchResult = nullptr;
    IconButton *m_searchPrevious = nullptr;
    IconButton *m_searchNext = nullptr;
    bool m_threeSelectedColumns = false;
    bool m_updating = false;
    bool m_mutexCheckEnabled = true;
    QList<qint64> m_searchMatches;
    int m_searchIndex = -1;
    Loader m_loader;
    ThemeService *m_themes = nullptr;
};

} // namespace darkeye
