#pragma once
#include <QtCore>
#include <optional>

namespace mdedit {
// All positions are UTF-16 code units, matching QString and QTextCursor.
struct SourceRange { int begin=0, end=0; };
enum class BlockKind { Heading, Paragraph, List, Code, Quote, Table, Resource, Rule };
struct ResourceRef { QString uri, alt, mode; int width=0; };
struct BlockViewNode {
    BlockKind kind=BlockKind::Paragraph;
    SourceRange source;
    std::optional<ResourceRef> resource;
};
class MarkdownParser {
public:
    static QList<BlockViewNode> parse(const QString &source);
};
}
