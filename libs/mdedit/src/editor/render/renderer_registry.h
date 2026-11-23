#pragma once
#include <QtGui>
#include "../theme_palette.h"
#include "../resources/resource_manager.h"
namespace mdedit {
// CPU-only render boundary: loaders never own a QPainter or viewport.
class IResourceRenderer {
public:
    virtual ~IResourceRenderer()=default;
    virtual bool supports(const ResourceSnapshot &) const=0;
    virtual void paint(QPainter &,const ResourceSnapshot &,const QRectF &, const std::optional<QPalette> &) const=0;
};
class RendererRegistry {
public:
    static void paint(QPainter &,const ResourceSnapshot &,const QRectF &, const std::optional<QPalette> &theme = {});
};
}
