#include "darkeye_ui/components/Pagination.h"

#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignComboBox.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSet>
#include <QSignalBlocker>
#include <QtMath>
#include <algorithm>

namespace darkeye {

Pagination::Pagination(int totalItems, int pageSize, QWidget *parent)
    : QWidget(parent), m_totalItems(qMax(0, totalItems)), m_pageSize(qMax(1, pageSize))
{
    setObjectName(QStringLiteral("DesignPagination"));
    if (!m_options.contains(m_pageSize)) {
        m_options.append(m_pageSize);
        std::sort(m_options.begin(), m_options.end());
    }

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(new QLabel(QStringLiteral("每页："), this));
    m_sizeSelector = new DesignComboBox(this);
    m_sizeSelector->setObjectName(QStringLiteral("PaginationPageSize"));
    rebuildSizeSelector();
    m_previous = new DesignButton(QStringLiteral("上一页"), this);
    m_previous->setObjectName(QStringLiteral("PaginationPrevious"));
    m_next = new DesignButton(QStringLiteral("下一页"), this);
    m_next->setObjectName(QStringLiteral("PaginationNext"));
    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("PaginationInfo"));
    layout->addWidget(m_sizeSelector);
    layout->addSpacing(8);
    layout->addWidget(m_previous);
    layout->addWidget(m_next);
    layout->addSpacing(8);
    layout->addWidget(m_info, 1);

    connect(m_previous, &QPushButton::clicked, this,
            [this] { setCurrentPage(m_currentPage - 1); });
    connect(m_next, &QPushButton::clicked, this,
            [this] { setCurrentPage(m_currentPage + 1); });
    connect(m_sizeSelector, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) setPageSize(m_sizeSelector->itemData(index).toInt());
    });
    refresh();
}

int Pagination::totalItems() const { return m_totalItems; }
int Pagination::pageSize() const { return m_pageSize; }
int Pagination::currentPage() const { return m_currentPage; }
int Pagination::totalPages() const { return qMax(1, qCeil(qreal(m_totalItems) / m_pageSize)); }
QList<int> Pagination::pageSizeOptions() const { return m_options; }

void Pagination::setPageSizeOptions(const QList<int> &options)
{
    QSet<int> unique;
    for (const int option : options) {
        if (option > 0) unique.insert(option);
    }
    unique.insert(m_pageSize);
    m_options = unique.values();
    std::sort(m_options.begin(), m_options.end());
    rebuildSizeSelector();
}

void Pagination::setTotalItems(int count)
{
    m_totalItems = qMax(0, count);
    m_currentPage = qMin(m_currentPage, totalPages());
    refresh();
}

void Pagination::setCurrentPage(int page, bool notify)
{
    const int normalized = qBound(1, page, totalPages());
    if (m_currentPage == normalized) return;
    m_currentPage = normalized;
    refresh();
    if (notify) emit pageChanged(m_currentPage);
}

void Pagination::setPageSize(int size, bool notify)
{
    const int normalized = qMax(1, size);
    if (!m_options.contains(normalized)) {
        m_options.append(normalized);
        std::sort(m_options.begin(), m_options.end());
    }
    if (m_pageSize == normalized) return;
    m_pageSize = normalized;
    m_currentPage = qMin(m_currentPage, totalPages());
    rebuildSizeSelector();
    refresh();
    if (notify) {
        emit pageSizeChanged(m_pageSize);
        emit pageChanged(m_currentPage);
    }
}

void Pagination::rebuildSizeSelector()
{
    if (m_sizeSelector == nullptr) return;
    const QSignalBlocker blocker(m_sizeSelector);
    m_sizeSelector->clear();
    for (const int option : std::as_const(m_options)) {
        m_sizeSelector->addItem(QString::number(option), option);
    }
    m_sizeSelector->setCurrentIndex(m_sizeSelector->findData(m_pageSize));
}

void Pagination::refresh()
{
    m_previous->setEnabled(m_currentPage > 1);
    m_next->setEnabled(m_currentPage < totalPages());
    m_info->setText(QStringLiteral("第 %1/%2 页 · 共 %3 项")
                        .arg(m_currentPage).arg(totalPages()).arg(m_totalItems));
}

} // namespace darkeye
