#ifndef MSDFFONTATLAS_H
#define MSDFFONTATLAS_H

#include <QString>
#include <QStringList>

#include <cstdint>
#include <unordered_map>
#include <vector>

class MsdfFontAtlas
{
public:
    struct Config {
        QString fontPath;
        int atlasWidth = 1024;
        int atlasHeight = 1024;
        float pxRange = 6.0f;
        // 0: automatic (at most 8 workers and half the logical CPUs); 1: serial.
        int maxGenerationThreads = 0;
    };

    struct GlyphInfo {
        uint32_t codepoint = 0;
        float advance = 0.0f;
        bool drawable = false;
        float planeLeft = 0.0f;
        float planeBottom = 0.0f;
        float planeRight = 0.0f;
        float planeTop = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
    };

    struct AtlasData {
        Config config;
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        std::unordered_map<uint32_t, GlyphInfo> glyphs;
        double ascender = 0.0;
        double descender = 0.0;
        double lineHeight = 1.0;
        int generation = 0;
    };

    MsdfFontAtlas() = default;
    ~MsdfFontAtlas();
    MsdfFontAtlas(const MsdfFontAtlas&) = delete;
    MsdfFontAtlas& operator=(const MsdfFontAtlas&) = delete;
    MsdfFontAtlas(MsdfFontAtlas&&) = delete;
    MsdfFontAtlas& operator=(MsdfFontAtlas&&) = delete;

    bool initialize(const Config& config, QString* errorMessage = nullptr);
    bool buildForLabels(const QStringList& labels, QString* errorMessage = nullptr);
    bool applyAtlasData(const AtlasData& data);
    bool applyAtlasData(AtlasData&& data);
    static bool BuildAtlasStandalone(const Config& cfg,
                                     const QStringList& labels,
                                     AtlasData& out,
                                     QString* errorMessage = nullptr);

    const GlyphInfo* findGlyph(uint32_t codepoint) const;
    bool hasGlyph(uint32_t codepoint) const;
    float kerning(uint32_t previousCodepoint, uint32_t currentCodepoint) const;
    float measureText(const QString& text) const;

    void clear();

    const std::vector<unsigned char>& atlasPixels() const { return m_atlasPixels; }
    int atlasWidth() const { return m_atlasWidth; }
    int atlasHeight() const { return m_atlasHeight; }
    int generation() const { return m_generation; }
    bool isReady() const { return m_ready; }
    float pxRange() const { return m_config.pxRange; }
    double ascender() const { return m_ascender; }
    double descender() const { return m_descender; }
    double lineHeight() const { return m_lineHeight; }
    float targetInnerPixels() const { return 32.0f; }

private:
    bool buildAtlasData(const QStringList& labels, AtlasData& out, QString* errorMessage);
    bool queryFontMetrics();

    void* m_freetype = nullptr;
    void* m_font = nullptr;
    Config m_config;
    std::unordered_map<uint32_t, GlyphInfo> m_glyphs;
    mutable std::unordered_map<uint64_t, float> m_kerningCache;
    std::vector<unsigned char> m_atlasPixels;
    int m_atlasWidth = 0;
    int m_atlasHeight = 0;
    int m_generation = 0;
    bool m_ready = false;
    double m_ascender = 0.0;
    double m_descender = 0.0;
    double m_lineHeight = 1.0;
    double m_emSize = 1.0;
};

#endif // MSDFFONTATLAS_H

