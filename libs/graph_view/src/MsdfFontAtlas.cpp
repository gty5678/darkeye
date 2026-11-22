/**
 * MsdfFontAtlas.cpp
 *
 * MSDF（多通道有符号距离场）字体图集实现。
 * 将 TrueType/OpenType 字体字形渲染为 MSDF 位图，打包到一张纹理图集中，
 */

#include "MsdfFontAtlas.h"

#include <QFile>

#include <msdfgen.h>
#include <msdfgen-ext.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <future>
#include <thread>
#include <unordered_set>
#include <utility>

namespace {

/// 将错误信息写入可选输出参数
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

/// 返回 >= value 的最小 2 的幂（用于纹理尺寸对齐）
int nextPowerOfTwo(int value)
{
    if (value <= 1) return 1;
    int p = 1;
    while (p < value && p < (1 << 30)) {
        p <<= 1;
    }
    return p;
}

/// 将浮点值钳制到 [0, 1] 区间（MSDF 转 8 位像素时使用）
float clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

} // namespace

MsdfFontAtlas::~MsdfFontAtlas()
{
    clear();
}

bool MsdfFontAtlas::applyAtlasData(const AtlasData& data)
{
    if (data.width <= 0 || data.height <= 0) {
        return false;
    }

    m_config = data.config;
    m_atlasWidth = data.width;
    m_atlasHeight = data.height;
    m_atlasPixels = data.pixels;
    m_glyphs = data.glyphs;
    m_kerningCache.clear();
    // 每次应用新图集时递增 generation，便于外部检测变化
    m_generation += 1;
    m_ascender = data.ascender;
    m_descender = data.descender;
    m_lineHeight = data.lineHeight;
    m_ready = true;
    return true;
}

bool MsdfFontAtlas::applyAtlasData(AtlasData&& data)
{
    if (data.width <= 0 || data.height <= 0) {
        return false;
    }

    m_config = std::move(data.config);
    m_atlasWidth = data.width;
    m_atlasHeight = data.height;
    m_atlasPixels = std::move(data.pixels);
    m_glyphs = std::move(data.glyphs);
    m_kerningCache.clear();
    // 每次应用新图集时递增 generation，便于外部检测变化
    m_generation += 1;
    m_ascender = data.ascender;
    m_descender = data.descender;
    m_lineHeight = data.lineHeight;
    m_ready = true;
    return true;
}

/// 初始化：加载字体、初始化 FreeType，并查询字体度量
bool MsdfFontAtlas::initialize(const Config& config, QString* errorMessage)
{
    clear();

    // 校验配置
    if (config.fontPath.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("MSDF font path is empty."));
        return false;
    }
    if (config.atlasWidth <= 0 || config.atlasHeight <= 0) {
        setError(errorMessage, QStringLiteral("MSDF atlas size must be positive."));
        return false;
    }

    // 初始化 FreeType 库（msdfgen 依赖）
    m_freetype = msdfgen::initializeFreetype();
    if (!m_freetype) {
        setError(errorMessage, QStringLiteral("Failed to initialize FreeType for MSDF atlas."));
        return false;
    }

    // 加载字体文件
    const QByteArray fontPathBytes = QFile::encodeName(config.fontPath);
    m_font = msdfgen::loadFont(static_cast<msdfgen::FreetypeHandle*>(m_freetype), fontPathBytes.constData());
    if (!m_font) {
        setError(errorMessage, QStringLiteral("Failed to load font for MSDF atlas: %1").arg(config.fontPath));
        clear();
        return false;
    }

    m_config = config;
    m_atlasWidth = config.atlasWidth;
    m_atlasHeight = 1;
    m_atlasPixels.assign(static_cast<size_t>(m_atlasWidth) * 3, 0);  // 临时占位，buildForLabels 会重建
    queryFontMetrics();
    m_ready = true;
    return true;
}

/// 从字体获取上行高度、下行高度、行高等度量
bool MsdfFontAtlas::queryFontMetrics()
{
    if (!m_font) return false;

    msdfgen::FontMetrics metrics;
    const bool ok = msdfgen::getFontMetrics(metrics, static_cast<msdfgen::FontHandle*>(m_font));
    if (ok) {
        // msdfgen 1.10 returns font-unit coordinates. Keep the public atlas
        // metrics in em units, matching the former EM_NORMALIZED API.
        m_emSize = metrics.emSize > 0.0 ? metrics.emSize : 1.0;
        m_ascender = metrics.ascenderY / m_emSize;//字体的高点一般为正
        m_descender = metrics.descenderY / m_emSize;//字体低点一般为负
        m_lineHeight = metrics.lineHeight / m_emSize;
    }
    // 若字体未提供度量，使用合理默认值
    if (!(m_lineHeight > 0.0)) m_lineHeight = 1.0;
    if (!(m_ascender > 0.0)) m_ascender = 0.8;
    if (!(m_descender < 0.0)) m_descender = -0.2;
    return ok;
}

/**
 * 根据标签列表构建 MSDF 图集（同步版本，直接写入当前实例）。
 * 后台线程请使用 BuildAtlasStandalone，然后在 UI 线程中通过 applyAtlasData 应用结果。
 */
bool MsdfFontAtlas::buildForLabels(const QStringList& labels, QString* errorMessage)
{
    AtlasData data;
    if (!buildAtlasData(labels, data, errorMessage))
        return false;

    // 使用新的 atlas 数据更新当前实例
    // generation 使用递增策略，便于外部检测变化
    data.generation = m_generation + 1;
    return applyAtlasData(std::move(data));
}

bool MsdfFontAtlas::buildAtlasData(const QStringList& labels, AtlasData& out, QString* errorMessage)
{
    if (!m_ready || !m_font) {
        setError(errorMessage, QStringLiteral("MSDF font atlas is not initialized."));
        return false;
    }

    out = AtlasData{};
    out.config = m_config;

    // 待打包字形：含 MSDF 位图及在图集中的位置
    struct PendingGlyph {
        GlyphInfo info;
        msdfgen::Bitmap<float, 3> bitmap;
        int width = 0;
        int height = 0;
        int x = 0;
        int y = 0;
        bool loaded = false;
    };

    // 收集所有需要的 Unicode 码点（去重）
    std::unordered_set<uint32_t> charset;
    charset.reserve(256);
    charset.insert(static_cast<uint32_t>('?'));   // 缺失字形后备
    charset.insert(static_cast<uint32_t>(' '));  // 空格
    for (const QString& label : labels) {
        const QList<uint> codepoints = label.toUcs4();
        for (const uint cp : codepoints) {
            charset.insert(static_cast<uint32_t>(cp));
        }
    }

    std::vector<uint32_t> codepoints;
    codepoints.reserve(charset.size());
    for (uint32_t cp : charset) codepoints.push_back(cp);
    std::sort(codepoints.begin(), codepoints.end());

    std::unordered_map<uint32_t, GlyphInfo> newGlyphs;
    newGlyphs.reserve(codepoints.size());
    std::vector<PendingGlyph> pending;
    pending.reserve(codepoints.size());

    // SDF 边缘填充像素数，保证采样时不会越界
    const int sdfPadding = std::max(2, static_cast<int>(std::ceil(m_config.pxRange)) + 1);
    const double targetInnerPixels = 32.0;  // 字形主体目标像素数（控制分辨率）

    // Each task writes only its own slot. Merge in codepoint order afterwards
    // so parallel generation preserves the serial atlas packing and UVs.
    std::vector<PendingGlyph> generated(codepoints.size());
    auto generateGlyph = [&](size_t index, MsdfFontAtlas& font) {
        const uint32_t cp = codepoints[index];
        // 获取字形索引
        msdfgen::GlyphIndex glyphIndex;
        const bool hasGlyph = msdfgen::getGlyphIndex(glyphIndex, static_cast<msdfgen::FontHandle*>(font.m_font), cp);
        if (!hasGlyph) {
            return;
        }

        // 加载字形轮廓和 advance（水平推进量）
        msdfgen::Shape shape;
        double advance = 0.0;
        const bool loaded = msdfgen::loadGlyph(shape, static_cast<msdfgen::FontHandle*>(font.m_font), glyphIndex, &advance);
        if (!loaded) {
            return;
        }

        GlyphInfo info;
        info.codepoint = cp;
        info.advance = static_cast<float>(advance / m_emSize);
        info.drawable = false;
        PendingGlyph& pg = generated[index];
        pg.info = info;
        pg.loaded = true;

        if (shape.contours.empty()) {
            return;  // 空格等无轮廓字形，只保留 advance
        }

        // 规范化轮廓并做 MSDF 边着色（三通道分配）
        shape.normalize();
        msdfgen::edgeColoringSimple(shape, 3.0);

        // 根据字形尺寸计算位图大小，使主体约 targetInnerPixels 像素
        const msdfgen::Shape::Bounds bounds = shape.getBounds();
        const double glyphW = std::max(1e-6, bounds.r - bounds.l);
        const double glyphH = std::max(1e-6, bounds.t - bounds.b);
        const double glyphMax = std::max(glyphW, glyphH);
        const double fontUnitsToPx = std::max(1e-6, targetInnerPixels / glyphMax);
        const double rangeFontUnits = std::max(1e-6, m_config.pxRange / fontUnitsToPx);

        const int bitmapW = std::max(4, static_cast<int>(std::ceil(glyphW * fontUnitsToPx)) + 2 * sdfPadding);
        const int bitmapH = std::max(4, static_cast<int>(std::ceil(glyphH * fontUnitsToPx)) + 2 * sdfPadding);

        pg.bitmap = msdfgen::Bitmap<float, 3>(bitmapW, bitmapH);
        pg.width = bitmapW;
        pg.height = bitmapH;
        pg.info = info;
        pg.info.drawable = true;

        // 生成 MSDF 位图（scale + translate 将字体内部单位映射到像素）
        // Projection 约定: pixel = scale * (shape + translate)
        // 要使 shape bounds.l 映射到 pixel sdfPadding:
        //   sdfPadding = fontUnitsToPx * (bounds.l + translate.x)
        //   translate.x = sdfPadding / fontUnitsToPx - bounds.l
        const msdfgen::Vector2 scale(fontUnitsToPx, fontUnitsToPx);
        const msdfgen::Vector2 translate(
            static_cast<double>(sdfPadding) / fontUnitsToPx - bounds.l,
            static_cast<double>(sdfPadding) / fontUnitsToPx - bounds.b
        );
        msdfgen::generateMSDF(pg.bitmap, shape, rangeFontUnits, scale, translate);

        // 记录平面坐标（em 单位，用于着色器中的纹理坐标映射）
        const double paddingFontUnits = static_cast<double>(sdfPadding) / fontUnitsToPx;
        pg.info.planeLeft = static_cast<float>((bounds.l - paddingFontUnits) / m_emSize);
        pg.info.planeBottom = static_cast<float>((bounds.b - paddingFontUnits) / m_emSize);
        pg.info.planeRight = static_cast<float>((bounds.r + paddingFontUnits) / m_emSize);
        pg.info.planeTop = static_cast<float>((bounds.t + paddingFontUnits) / m_emSize);
    };

    // Leave CPU capacity for the force simulation and UI. Small character sets
    // stay serial to avoid paying for additional font loads and thread startup.
    const unsigned cpuBudget = std::max(1u, std::thread::hardware_concurrency() / 2);
    const unsigned requested = m_config.maxGenerationThreads > 0
        ? static_cast<unsigned>(m_config.maxGenerationThreads) : 8u;
    const unsigned workBudget = static_cast<unsigned>(std::max<size_t>(1, codepoints.size() / 32));
    const unsigned workerCount = std::min({8u, cpuBudget, requested, workBudget});
    std::atomic<size_t> nextGlyph{0};
    auto generateRemaining = [&](MsdfFontAtlas& font) {
        for (;;) {
            const size_t index = nextGlyph.fetch_add(1, std::memory_order_relaxed);
            if (index >= codepoints.size()) {
                return;
            }
            generateGlyph(index, font);
        }
    };

    try {
        // Futures join even on exceptions, before any captured buffers die.
        std::vector<std::future<QString>> workers;
        workers.reserve(workerCount - 1);
        for (unsigned i = 1; i < workerCount; ++i) {
            workers.push_back(std::async(std::launch::async, [&]() -> QString {
                // FreeType library and font face are never shared by workers.
                MsdfFontAtlas workerFont;
                QString error;
                if (!workerFont.initialize(m_config, &error)) {
                    return error;
                }
                generateRemaining(workerFont);
                return {};
            }));
        }
        // The existing background build thread also participates in generation.
        generateRemaining(*this);
        for (auto& worker : workers) {
            const QString error = worker.get();
            if (!error.isEmpty()) {
                setError(errorMessage, error);
                return false;
            }
        }
    } catch (const std::exception& error) {
        setError(errorMessage, QStringLiteral("MSDF glyph generation failed: %1")
            .arg(QString::fromUtf8(error.what())));
        return false;
    }

    for (auto& pg : generated) {
        if (!pg.loaded) {
            continue;
        }
        newGlyphs[pg.info.codepoint] = pg.info;
        if (pg.info.drawable) {
            pending.push_back(std::move(pg));
        }
    }

    // 矩形装箱：按行从左到右放置字形，行满则换行
    const int atlasW = std::max(64, m_config.atlasWidth);
    int cursorX = 1;
    int cursorY = 1;
    int rowH = 0;
    for (auto& pg : pending) {
        if (cursorX + pg.width + 1 > atlasW) {
            cursorX = 1;
            cursorY += rowH + 1;
            rowH = 0;
        }
        if (cursorY + pg.height + 1 > m_config.atlasHeight) {
            setError(errorMessage, QStringLiteral("MSDF atlas overflow: too many glyphs for %1x%2 atlas.")
                .arg(m_config.atlasWidth).arg(m_config.atlasHeight));
            return false;
        }
        pg.x = cursorX;
        pg.y = cursorY;
        cursorX += pg.width + 1;
        rowH = std::max(rowH, pg.height);
    }

    // 图集高度取实际使用高度与配置上限的较小值，并向上取 2 的幂
    int usedH = cursorY + rowH + 1;
    usedH = std::max(8, usedH);
    const int atlasH = std::min(m_config.atlasHeight, nextPowerOfTwo(usedH));
    if (atlasH < usedH) {
        setError(errorMessage, QStringLiteral("MSDF atlas height overflow after power-of-two adjustment."));
        return false;
    }

    // 创建 RGB 像素缓冲区（0 = "完全在字形外部"，防止双线性过滤泄漏出半透明边缘）
    std::vector<unsigned char> pixels(static_cast<size_t>(atlasW) * atlasH * 3, 0);
    for (const auto& pg : pending) {
        for (int y = 0; y < pg.height; ++y) {
            for (int x = 0; x < pg.width; ++x) {
                const float* src = pg.bitmap(x, y);
                const size_t dstIndex = static_cast<size_t>(3) * (static_cast<size_t>(atlasW) * (pg.y + y) + (pg.x + x));
                pixels[dstIndex + 0] = static_cast<unsigned char>(std::lround(clamp01(src[0]) * 255.0f));
                pixels[dstIndex + 1] = static_cast<unsigned char>(std::lround(clamp01(src[1]) * 255.0f));
                pixels[dstIndex + 2] = static_cast<unsigned char>(std::lround(clamp01(src[2]) * 255.0f));
            }
        }
    }

    // 为每个字形计算归一化 UV 坐标并写回 GlyphInfo
    for (auto& pg : pending) {
        pg.info.u0 = static_cast<float>(pg.x) / static_cast<float>(atlasW);
        pg.info.v0 = static_cast<float>(pg.y) / static_cast<float>(atlasH);
        pg.info.u1 = static_cast<float>(pg.x + pg.width) / static_cast<float>(atlasW);
        pg.info.v1 = static_cast<float>(pg.y + pg.height) / static_cast<float>(atlasH);
        newGlyphs[pg.info.codepoint] = pg.info;
    }

    out.width = atlasW;
    out.height = atlasH;
    out.pixels.swap(pixels);
    out.glyphs.swap(newGlyphs);
    out.ascender = m_ascender;
    out.descender = m_descender;
    out.lineHeight = m_lineHeight;
    return true;
}

bool MsdfFontAtlas::BuildAtlasStandalone(const Config& cfg,
                                         const QStringList& labels,
                                         AtlasData& out,
                                         QString* errorMessage)
{
    MsdfFontAtlas tmp;
    if (!tmp.initialize(cfg, errorMessage)) {
        return false;
    }
    if (!tmp.buildAtlasData(labels, out, errorMessage)) {
        return false;
    }
    // generation 由调用方在应用数据时决定，这里保持默认值 0
    out.generation = 0;
    return true;
}

/// 查找字形信息，缺失时回退到 '?'
const MsdfFontAtlas::GlyphInfo* MsdfFontAtlas::findGlyph(uint32_t codepoint) const
{
    auto it = m_glyphs.find(codepoint);
    if (it != m_glyphs.end()) return &it->second;
    it = m_glyphs.find(static_cast<uint32_t>('?'));
    if (it != m_glyphs.end()) return &it->second;
    return nullptr;
}

/// 检查图集中是否包含指定码点的字形
bool MsdfFontAtlas::hasGlyph(uint32_t codepoint) const
{
    return m_glyphs.find(codepoint) != m_glyphs.end();
}

/// 获取相邻两字形的字距（kerning），带缓存
float MsdfFontAtlas::kerning(uint32_t previousCodepoint, uint32_t currentCodepoint) const
{
    if (!m_font || previousCodepoint == 0 || currentCodepoint == 0) return 0.0f;
    const uint64_t key = (static_cast<uint64_t>(previousCodepoint) << 32) | static_cast<uint64_t>(currentCodepoint);
    auto it = m_kerningCache.find(key);
    if (it != m_kerningCache.end()) return it->second;

    double value = 0.0;
    const bool ok = msdfgen::getKerning(value, static_cast<msdfgen::FontHandle*>(m_font), previousCodepoint, currentCodepoint);
    const float kerningValue = ok ? static_cast<float>(value / m_emSize) : 0.0f;
    m_kerningCache[key] = kerningValue;
    return kerningValue;
}

/// 测量文本总宽度（em 单位），含字距
float MsdfFontAtlas::measureText(const QString& text) const
{
    const QList<uint> codepoints = text.toUcs4();
    if (codepoints.empty()) return 0.0f;

    float width = 0.0f;
    uint32_t prev = 0;
    bool hasPrev = false;
    for (const uint cpQ : codepoints) {
        const uint32_t cp = static_cast<uint32_t>(cpQ);
        const GlyphInfo* glyph = findGlyph(cp);
        if (!glyph) continue;
        if (hasPrev) {
            width += kerning(prev, cp);
        }
        width += glyph->advance;
        prev = cp;
        hasPrev = true;
    }
    return width;
}

/// 释放字体和图集资源
void MsdfFontAtlas::clear()
{
    if (m_font) {
        msdfgen::destroyFont(static_cast<msdfgen::FontHandle*>(m_font));
        m_font = nullptr;
    }
    if (m_freetype) {
        msdfgen::deinitializeFreetype(static_cast<msdfgen::FreetypeHandle*>(m_freetype));
        m_freetype = nullptr;
    }

    m_glyphs.clear();
    m_kerningCache.clear();
    m_atlasPixels.clear();
    m_atlasWidth = 0;
    m_atlasHeight = 0;
    m_generation = 0;
    m_ready = false;
    m_emSize = 1.0;
}

