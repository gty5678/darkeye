#include "render3d/Lighting.h"
#include <QFile>
#include <QVector3D>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace darkeye::render3d {
namespace {
constexpr float pi=3.14159265358979323846f;
QVector3D pixel(const QImage &image,int x,int y)
{
    const auto *p=reinterpret_cast<const float *>(image.constScanLine(y))+4*x;
    return {p[0],p[1],p[2]};
}
void put(QImage &image,int x,int y,QVector3D color)
{
    auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
    p[0]=color.x(); p[1]=color.y(); p[2]=color.z(); p[3]=1;
}
QVector3D direction(float u,float v)
{
    const float phi=(u-0.5f)*2*pi,theta=v*pi;
    return {std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)};
}
QVector3D sample(const QImage &image,const QVector3D &n)
{
    const float u=std::atan2(n.z(),n.x())/(2*pi)+0.5f;
    const float v=std::acos(std::clamp(n.y(),-1.0f,1.0f))/pi;
    const float x=u*image.width()-0.5f,y=v*image.height()-0.5f;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const auto texel=[&](int a,int b) { return pixel(image,(a%image.width()+image.width())%image.width(),
        std::clamp(b,0,image.height()-1)); };
    const float fx=x-std::floor(x),fy=y-std::floor(y);
    return (texel(ix,iy)*(1-fx)+texel(ix+1,iy)*fx)*(1-fy)
        +(texel(ix,iy+1)*(1-fx)+texel(ix+1,iy+1)*fx)*fy;
}
float radicalInverse(unsigned bits)
{
    bits=(bits<<16)|(bits>>16);
    bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);
    bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
    return float(bits)*2.3283064365386963e-10f;
}
QVector3D localToWorld(QVector3D local,QVector3D n)
{
    const auto up=std::abs(n.y())<0.999f?QVector3D(0,1,0):QVector3D(1,0,0);
    const auto t=QVector3D::crossProduct(up,n).normalized();
    return t*local.x()+QVector3D::crossProduct(n,t)*local.y()+n*local.z();
}
QVector3D ggx(float x,float y,float roughness)
{
    const float a=roughness*roughness;
    const float cosine=std::sqrt((1-y)/(1+(a*a-1)*y));
    const float sine=std::sqrt(std::max(0.0f,1-cosine*cosine)),phi=2*pi*x;
    return {std::cos(phi)*sine,std::sin(phi)*sine,cosine};
}
QImage decode(const QString &path,QString &error)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) { error=file.errorString(); return {}; }
    if(file.size()>64*1024*1024) { error="HDR exceeds 64 MiB"; return {}; }
    const auto bytes=file.readAll();
    qsizetype cursor=0;
    const auto line=[&]() {
        const auto end=bytes.indexOf('\n',cursor);
        if(end<0) return QByteArray{};
        auto result=bytes.mid(cursor,end-cursor).trimmed(); cursor=end+1; return result;
    };
    const auto signature=line();
    if(signature!="#?RADIANCE" && signature!="#?RGBE") { error="Not a Radiance HDR file"; return {}; }
    bool format=false;
    while(cursor<bytes.size()) {
        const auto header=line();
        if(header.isEmpty()) break;
        if(header=="FORMAT=32-bit_rle_rgbe") format=true;
    }
    const auto resolution=line().simplified().split(' ');
    bool widthOk=false,heightOk=false;
    const int height=resolution.size()==4?resolution[1].toInt(&heightOk):0;
    const int width=resolution.size()==4?resolution[3].toInt(&widthOk):0;
    if(!format || !widthOk || !heightOk || resolution[0]!="-Y" || resolution[2]!="+X"
        || width<1 || height<1 || width>4096 || height>2048) {
        error="Unsupported HDR format/orientation/size (requires -Y +X, max 4096x2048)"; return {};
    }
    QImage result(width,height,QImage::Format_RGBA32FPx4);
    if(result.isNull()) { error="Cannot allocate HDR pixels"; return {}; }
    QByteArray scanline(width*4,0);
    const auto read=[&](int &value) {
        if(cursor>=bytes.size()) return false;
        value=static_cast<unsigned char>(bytes[cursor++]); return true;
    };
    for(int y=0;y<height;++y) {
        if(cursor+4>bytes.size()) { error="Truncated HDR scanline"; return {}; }
        const auto *head=reinterpret_cast<const unsigned char *>(bytes.constData()+cursor);
        const bool rle=width>=8 && head[0]==2 && head[1]==2 && !(head[2]&128);
        if(rle) {
            if((int(head[2])<<8|head[3])!=width) { error="HDR scanline width mismatch"; return {}; }
            cursor+=4;
            for(int channel=0;channel<4;++channel) {
                int x=0;
                while(x<width) {
                    int count=0,value=0;
                    if(!read(count) || !count) { error="Invalid HDR run"; return {}; }
                    const bool repeated=count>128;
                    if(repeated) count-=128;
                    if(x+count>width || (repeated && !read(value))) { error="Invalid HDR run length"; return {}; }
                    for(int i=0;i<count;++i) {
                        if(!repeated && !read(value)) { error="Truncated HDR literal"; return {}; }
                        scanline[4*x+channel]=char(value); ++x;
                    }
                }
            }
        } else {
            if(cursor+width*4>bytes.size()) { error="Truncated uncompressed HDR"; return {}; }
            std::memcpy(scanline.data(),bytes.constData()+cursor,std::size_t(width)*4); cursor+=width*4;
        }
        for(int x=0;x<width;++x) {
            const auto *p=reinterpret_cast<const unsigned char *>(scanline.constData())+4*x;
            const float scale=p[3]?std::ldexp(1.0f,int(p[3])-136):0;
            put(result,x,y,{std::min(65504.0f,p[0]*scale),std::min(65504.0f,p[1]*scale),std::min(65504.0f,p[2]*scale)});
        }
    }
    return result;
}
}
std::shared_ptr<const EnvironmentMap> EnvironmentMap::loadHdr(const QString &path,QString *error)
{
    QString failure;
    auto radiance=decode(path,failure);
    if(radiance.isNull()) { if(error) *error=path+": "+failure; return {}; }
    auto result=std::make_shared<EnvironmentMap>();
    result->source=path;
    result->radiance=std::move(radiance);
    result->irradiance=QImage(32,16,QImage::Format_RGBA32FPx4);
    constexpr unsigned samples=128;
    for(int y=0;y<16;++y) for(int x=0;x<32;++x) {
        const auto n=direction((x+0.5f)/32,(y+0.5f)/16);
        QVector3D sum;
        for(unsigned i=0;i<samples;++i) {
            const float r=std::sqrt((i+0.5f)/samples),phi=2*pi*radicalInverse(i);
            sum+=sample(result->radiance,localToWorld({r*std::cos(phi),r*std::sin(phi),std::sqrt(1-r*r)},n));
        }
        put(result->irradiance,x,y,sum/float(samples));
    }
    for(int level=0,width=128,height=64;level<8;++level,width=std::max(1,width/2),height=std::max(1,height/2)) {
        QImage image(width,height,QImage::Format_RGBA32FPx4);
        const float roughness=level/7.0f;
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            const auto n=direction((x+0.5f)/width,(y+0.5f)/height);
            QVector3D sum; float weight=0;
            for(unsigned i=0;i<(level? samples:1u);++i) {
                const auto h=localToWorld(ggx(float(i)/samples,radicalInverse(i),roughness),n);
                const auto l=(h*(2*QVector3D::dotProduct(n,h))-n).normalized();
                const float ndl=std::max(0.0f,QVector3D::dotProduct(n,l));
                sum+=sample(result->radiance,l)*ndl; weight+=ndl;
            }
            put(image,x,y,sum/std::max(weight,0.001f));
        }
        result->specular.push_back(std::move(image));
    }
    result->brdf=QImage(64,64,QImage::Format_RGBA32FPx4);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const float ndv=(x+0.5f)/64,roughness=(y+0.5f)/64;
        const QVector3D v(std::sqrt(1-ndv*ndv),0,ndv);
        float a=0,b=0;
        for(unsigned i=0;i<samples;++i) {
            const auto h=ggx(float(i)/samples,radicalInverse(i),roughness);
            const float vdh=std::max(QVector3D::dotProduct(v,h),0.0f);
            const auto l=h*(2*vdh)-v;
            const float ndl=std::max(l.z(),0.0f),ndh=std::max(h.z(),0.001f);
            if(ndl<=0) continue;
            const float k=roughness*roughness/2;
            const float g=(ndv/(ndv*(1-k)+k))*(ndl/(ndl*(1-k)+k));
            const float visibility=g*vdh/(ndh*ndv),f=std::pow(1-vdh,5.0f);
            a+=(1-f)*visibility; b+=f*visibility;
        }
        put(result->brdf,x,y,{a/samples,b/samples,0});
    }
    if(error) error->clear();
    return result;
}
} // namespace darkeye::render3d
