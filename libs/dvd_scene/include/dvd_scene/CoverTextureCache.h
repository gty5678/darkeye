#pragma once
#include "render3d/Material.h"
#include <QObject>
#include <QThreadPool>
#include <QMap>
#include <QSet>

namespace darkeye::dvd_scene {
class CoverTextureCache final: public QObject {
    Q_OBJECT
public:
    struct Request { QString key; std::shared_ptr<const render3d::TextureData> texture; };
    explicit CoverTextureCache(QObject *parent=nullptr);
    ~CoverTextureCache() override;
    Request request(const QString &path,int pixels=256);
    void retainRequests(const QSet<QString> &keys);
    [[nodiscard]] std::shared_ptr<const render3d::TextureData> find(const QString &key);
    [[nodiscard]] std::shared_ptr<const render3d::TextureData> placeholder() const { return m_placeholder; }
    [[nodiscard]] QString error(const QString &key) const;
    void setBudget(quint64 bytes);
    [[nodiscard]] quint64 bytes() const { return m_bytes; }
    [[nodiscard]] int pendingCount() const { return int(m_queued.size() + m_running.size()); }
signals:
    void textureReady(const QString &key);
private:
    struct Entry {
        std::shared_ptr<const render3d::TextureData> texture;
        QString error;
        quint64 bytes=0,lastUse=0;
    };
    struct DecodeRequest { QString path; int pixels; };
    void startDecodes();
    void decode(const QString &key, const DecodeRequest &request);
    void evict();
    QThreadPool m_pool;
    QMap<QString,Entry> m_entries;
    QMap<QString, DecodeRequest> m_queued;
    QSet<QString> m_running;
    QSet<QString> m_obsolete;
    std::shared_ptr<const render3d::TextureData> m_placeholder;
    quint64 m_budget=32*1024*1024,m_bytes=0,m_clock=0;
};
} // namespace darkeye::dvd_scene
