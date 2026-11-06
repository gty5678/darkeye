#include "dvd_scene/CoverTextureCache.h"
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QResource>
#include <algorithm>

static void initializeDvdDefaults()
{
    static const bool initialized=[] { Q_INIT_RESOURCE(dvd_scene_defaults); return true; }();
    Q_UNUSED(initialized);
}

namespace darkeye::dvd_scene {
CoverTextureCache::CoverTextureCache(QObject *parent):QObject(parent)
{
    initializeDvdDefaults();
    m_pool.setMaxThreadCount(2);
    auto texture=std::make_shared<render3d::TextureData>();
    texture->source=QStringLiteral("DVD placeholder");
    texture->mipmap=render3d::TextureData::Mipmap::Linear;
    texture->maxAnisotropy=8;
    // Reuse the QML artwork for loading, absent and undecodable covers.
    texture->image=QImage(QStringLiteral(":/dvd_scene/maps/0.png")).convertToFormat(QImage::Format_RGBA8888);
    m_placeholder=std::move(texture);
}
CoverTextureCache::~CoverTextureCache()
{
    // Discard obsolete queued decodes before joining the (at most two) workers.
    // Keep this QObject alive until running tasks have posted their callbacks;
    // QObject destruction then removes those queued callbacks safely.
    m_pool.clear();
    m_pool.waitForDone();
}

CoverTextureCache::Request CoverTextureCache::request(const QString &path,int pixels)
{
    if (path.isEmpty()) return {{},m_placeholder};
    pixels=std::clamp(pixels,64,4096);
    const QFileInfo file(path);
    const QString key=QStringLiteral("%1|%2|%3|%4")
        .arg(file.absoluteFilePath()).arg(file.lastModified().toMSecsSinceEpoch()).arg(file.size()).arg(pixels);
    if (auto texture=find(key)) return {key,std::move(texture)};
    m_obsolete.remove(key);
    if (!m_running.contains(key) && !m_queued.contains(key)) {
        m_queued.insert(key, {path, pixels});
        startDecodes();
    }
    return {key,m_placeholder};
}

void CoverTextureCache::retainRequests(const QSet<QString> &keys)
{
    for (auto it = m_queued.begin(); it != m_queued.end();) {
        if (keys.contains(it.key())) ++it;
        else it = m_queued.erase(it);
    }
    m_obsolete = m_running - keys;
}

void CoverTextureCache::startDecodes()
{
    // Count work until its result is consumed on the GUI thread, bounding
    // both active decodes and decoded images waiting for delivery.
    while (!m_queued.isEmpty() && m_running.size() < m_pool.maxThreadCount()) {
        const auto next = std::max_element(m_queued.cbegin(), m_queued.cend(),
            [](const DecodeRequest &a, const DecodeRequest &b) { return a.pixels < b.pixels; });
        const QString key = next.key();
        const DecodeRequest request = m_queued.take(key);
        m_running.insert(key);
        decode(key, request);
    }
}

void CoverTextureCache::decode(const QString &key, const DecodeRequest &request)
{
    const QString path = request.path;
    const int pixels = request.pixels;
    const QPointer<CoverTextureCache> context(this);
    m_pool.start(QRunnable::create([context,path,pixels,key] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto original=reader.size();
        QString error;
        QImage image;
        if (original.isValid() && original.width()<=32768 && original.height()<=32768) {
            if(std::max(original.width(),original.height())>pixels)
                reader.setScaledSize(original.scaled(pixels,pixels,Qt::KeepAspectRatio));
            image=reader.read();
        }
        if (image.isNull()) error=QStringLiteral("Cannot decode cover %1: %2").arg(path,reader.errorString());
        else {
            if(std::max(image.width(),image.height())>pixels)
                image=image.scaled(pixels,pixels,Qt::KeepAspectRatio,Qt::SmoothTransformation);
            image=image.convertToFormat(QImage::Format_RGBA8888);
        }
        if (!context) return;
        QMetaObject::invokeMethod(context,[context,image=std::move(image),error,path,key] {
            if (!context) return;
            context->m_running.remove(key);
            if (context->m_obsolete.remove(key)) {
                context->startDecodes();
                return;
            }
            Entry entry;
            entry.error=error;
            entry.lastUse=++context->m_clock;
            if (image.isNull()) entry.texture=context->m_placeholder;
            else {
                auto texture=std::make_shared<render3d::TextureData>();
                texture->source=path;
                texture->mipmap=render3d::TextureData::Mipmap::Linear;
                texture->maxAnisotropy=8;
                texture->image=image;
                entry.texture=std::move(texture);
                entry.bytes=quint64(image.sizeInBytes());
            }
            context->m_bytes+=entry.bytes;
            context->m_entries.insert(key,std::move(entry));
            // Receivers acquire a shared reference before cache eviction.
            emit context->textureReady(key);
            if (context) {
                context->evict();
                context->startDecodes();
            }
        },Qt::QueuedConnection);
    }));
}

std::shared_ptr<const render3d::TextureData> CoverTextureCache::find(const QString &key)
{
    const auto it=m_entries.find(key);
    if (it==m_entries.end()) return {};
    it->lastUse=++m_clock;
    return it->texture;
}
QString CoverTextureCache::error(const QString &key) const
{
    const auto it=m_entries.constFind(key);
    return it==m_entries.cend()?QString():it->error;
}
void CoverTextureCache::setBudget(quint64 bytes) { m_budget=bytes; evict(); }
void CoverTextureCache::evict()
{
    while((m_bytes>m_budget || m_entries.size()>512) && !m_entries.isEmpty()) {
        auto oldest=m_entries.begin();
        for(auto it=m_entries.begin();it!=m_entries.end();++it)
            if(it->lastUse<oldest->lastUse) oldest=it;
        m_bytes-=oldest->bytes;
        m_entries.erase(oldest);
    }
}
} // namespace darkeye::dvd_scene
