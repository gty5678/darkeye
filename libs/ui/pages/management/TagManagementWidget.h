#pragma once

#include "database/repositories/ReferenceRepository.h"

#include <QWidget>

class QComboBox;
class QGroupBox;
class QLineEdit;
class QTableWidget;

namespace darkeye
{

class ColorPicker;
class ThemeService;
class TokenTableWidget;
class TagDisplayPreview;
class WorkTagSelector;

class TagManagementWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TagManagementWidget(QSqlDatabase database, ThemeService &themes,
                                 QWidget *parent = nullptr);

    void refresh();
    void refreshTagTypes();

signals:
    void tagsChanged();
    void tagTypesChanged();

private:
    void buildUi();
    void selectTag(qint64 tagId);
    void beginNewTag();
    void saveCurrent();
    void removeCurrent();
    void changeSelectedColors();
    void redirectSelectedTags();
    void openTagTypeManager();
    void updateSelectionState();
    void updatePreview();
    [[nodiscard]] QList<qint64> selectedTagIds() const;
    [[nodiscard]] TagRecord editorRecord() const;

    QSqlDatabase m_database;
    ReferenceRepository m_repository;
    ThemeService &m_themes;
    QList<TagRecord> m_records;
    qint64 m_currentId = 0;
    WorkTagSelector *m_tagSelector = nullptr;
    QLineEdit *m_name = nullptr;
    QComboBox *m_type = nullptr;
    ColorPicker *m_color = nullptr;
    QLineEdit *m_detail = nullptr;
    TokenTableWidget *m_aliases = nullptr;
    TagDisplayPreview *m_preview = nullptr;
    QGroupBox *m_multiSelectGroup = nullptr;
    ColorPicker *m_multiSelectColor = nullptr;
};

class TagTypeManagementWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TagTypeManagementWidget(QSqlDatabase database, ThemeService &themes,
                                     QWidget *parent = nullptr);

    void refresh();

signals:
    void typesChanged();

private:
    void selectRow(int row);
    void beginNewType();
    void saveCurrent();
    void removeCurrent();
    void moveCurrent(int offset);
    void moveRow(int sourceRow, int destinationRow);

    ReferenceRepository m_repository;
    ThemeService &m_themes;
    QList<TagTypeRecord> m_records;
    qint64 m_currentId = 0;
    TokenTableWidget *m_table = nullptr;
    QLineEdit *m_name = nullptr;
};

} // namespace darkeye
