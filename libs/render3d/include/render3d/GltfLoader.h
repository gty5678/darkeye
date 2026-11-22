#pragma once

#include "ModelAsset.h"
#include <memory>

namespace darkeye::render3d {

struct ModelLoadResult {
    std::shared_ptr<const ModelAsset> asset;
    QString error;
    explicit operator bool() const { return asset != nullptr && error.isEmpty(); }
};

class GltfLoader {
public:
    GltfLoader();
    ~GltfLoader();
    GltfLoader(const GltfLoader &) = delete;
    GltfLoader &operator=(const GltfLoader &) = delete;
    // CPU-only; each loader is used by one thread. Unicode paths are handled
    // by Qt file callbacks even on Windows. Cache entries hold weak assets.
    [[nodiscard]] ModelLoadResult load(const QString &path);
    void clearCache();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace darkeye::render3d
