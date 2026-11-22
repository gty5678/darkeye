#pragma once

#include <QList>
#include <QString>
#include <QUrl>
#include <QWidget>

class QHBoxLayout;
class QPushButton;
class QWidget;

namespace darkeye
{

class ImageFetchService;

struct FanartEntry final
{
    QString url;
    QString file;
    QString localPath;

    bool operator==(const FanartEntry &) const = default;
};

class FanartStripWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit FanartStripWidget(
        QString fanartDirectory, QString legacyCoverDirectory = {},
        QUrl imageFetchEndpoint = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/image")),
        QWidget *parent = nullptr);

    void setEntries(const QList<FanartEntry> &entries);
    [[nodiscard]] QList<FanartEntry> entries() const;
    void setUrlList(const QStringList &urls);
    void setCanAdd(bool canAdd);
    [[nodiscard]] bool canAdd() const noexcept;
    enum class Mode { Edit, Display };
    void setMode(Mode mode);
    [[nodiscard]] Mode mode() const noexcept { return m_mode; }

    bool addLocalImage(const QString &path, QString *errorMessage = nullptr);
    bool addImageUrl(const QString &url, QString *errorMessage = nullptr);
    bool removeEntry(int index);
    bool moveEntry(int from, int to);
    bool updateEntry(int index, const QString &url, const QString &file = {});
    bool downloadEntry(int index);
    [[nodiscard]] bool downloadInProgress() const noexcept;

    [[nodiscard]] bool finalizedEntries(const QString &serialNumber,
                                        QList<FanartEntry> *finalEntries, QStringList *createdFiles,
                                        QString *errorMessage = nullptr) const;

    static bool parseJson(const QString &json, QList<FanartEntry> *entries,
                          QString *errorMessage = nullptr);
    [[nodiscard]] static QString toJson(const QList<FanartEntry> &entries);

signals:
    void fanartChanged(const QList<darkeye::FanartEntry> &entries);
    void imageRejected(const QString &message);
    void downloadStateChanged(bool downloading);

private:
    void chooseImage();
    void showPreview(int index);
    void rebuild();
    [[nodiscard]] QString resolvedPath(const FanartEntry &entry) const;
    [[nodiscard]] static bool saveAsJpeg(const QString &sourcePath, const QString &targetPath,
                                         QString *errorMessage);

    QString m_fanartDirectory;
    QString m_legacyCoverDirectory;
    ImageFetchService *m_imageFetch = nullptr;
    QList<FanartEntry> m_entries;
    QHBoxLayout *m_stripLayout = nullptr;
    bool m_canAdd = true;
    Mode m_mode = Mode::Edit;
    int m_activeDownloadIndex = -1;
    quint64 m_activeRequestId = 0;
};

} // namespace darkeye

Q_DECLARE_METATYPE(darkeye::FanartEntry)
Q_DECLARE_METATYPE(QList<darkeye::FanartEntry>)
