#pragma once

#include <QList>
#include <QWidget>

class QLabel;
class QPushButton;

namespace darkeye {

class DesignComboBox;

class Pagination final : public QWidget
{
    Q_OBJECT

public:
    explicit Pagination(int totalItems = 0, int pageSize = 10,
                        QWidget *parent = nullptr);

    int totalItems() const;
    int pageSize() const;
    int currentPage() const;
    int totalPages() const;
    QList<int> pageSizeOptions() const;
    void setPageSizeOptions(const QList<int> &options);
    void setTotalItems(int count);
    void setCurrentPage(int page, bool notify = true);
    void setPageSize(int size, bool notify = true);

signals:
    void pageChanged(int page);
    void pageSizeChanged(int size);

private:
    void refresh();
    void rebuildSizeSelector();

    int m_totalItems = 0;
    int m_pageSize = 10;
    int m_currentPage = 1;
    QList<int> m_options = {10, 20, 50, 100};
    DesignComboBox *m_sizeSelector = nullptr;
    QPushButton *m_previous = nullptr;
    QPushButton *m_next = nullptr;
    QLabel *m_info = nullptr;
};

} // namespace darkeye
