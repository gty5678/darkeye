#include "resource_manager.h"
#if !MDEDIT_LITE
#include <QPdfDocument>
#include <QSvgRenderer>
#endif
#include <QStringDecoder>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <memory>
namespace mdedit {
namespace {
bool supportedImageFormat(QImageReader &reader){
#if MDEDIT_LITE
    const auto format=reader.format().toLower();
    return format=="png"||format=="jpeg"||format=="jpg"||format=="bmp"||format=="gif";
#else
    Q_UNUSED(reader);
    return true;
#endif
}
}

bool ResourceResolver::isImageResource(const QUrl &url) {
    static const QSet<QString> extensions={"png","jpg","jpeg","bmp","gif"
#if !MDEDIT_LITE
        ,"svg","webp","tif","tiff"
#endif
    };
    if(url.scheme()=="http"||url.scheme()=="https")return true;
    return extensions.contains(QFileInfo(url.path()).suffix().toLower());
}
bool ResourceResolver::isPreviewResource(const QUrl &url) {
    const QString suffix=QFileInfo(url.path()).suffix().toLower();
    return isImageResource(url)||isTextResource(url)||(!MDEDIT_LITE&&suffix=="pdf")||suffix=="csv";
}
bool ResourceResolver::isTextResource(const QUrl &url) {
    static const QSet<QString> extensions={"txt","log","json","jsonl","xml","yaml","yml","toml","ini","cfg","conf","md","markdown","rst","text","tsv","html","htm","css","js","ts","py","cpp","h","c","sh","sql"};
    return extensions.contains(QFileInfo(url.path()).suffix().toLower());
}
QUrl ResourceResolver::resolve(const QString &uri,const QUrl &base) {
    QUrl url(uri);
    if(url.isRelative())url=base.resolved(url);
    return url.adjusted(QUrl::NormalizePathSegments);
}
ResourceManager::ResourceManager(QObject *parent):QObject(parent){pool.setMaxThreadCount(2);}
ResourceManager::~ResourceManager(){pool.clear();pool.waitForDone();}
void ResourceManager::clear(){
    ++revision;cache.clear();imageSizes.clear();
    for(const auto &flag:cancelled)*flag=true;
    emit ready();
}
QSet<QUrl> ResourceManager::referencedUrls(const QString &markdown,const QUrl &base) {
    QSet<QUrl> urls;
    static const QRegularExpression image(R"(!\[[^\]]*\]\(\s*(?:<([^>]+)>|([^\s)]+))[^)]*\))");
    auto matches=image.globalMatch(markdown);
    while(matches.hasNext()){
        const auto match=matches.next();
        urls.insert(ResourceResolver::resolve(match.captured(1).isEmpty()?match.captured(2):match.captured(1),base));
    }
    return urls;
}
bool ResourceManager::setActiveUrls(const QSet<QUrl> &urls) {
    if(windowed&&activeUrls==urls)return false;
    windowed=true;activeUrls=urls;
    for(auto it=keyUrls.begin();it!=keyUrls.end();){
        if(!accepts(it.value())){cache.remove(it.key());if(cancelled.contains(it.key()))*cancelled.value(it.key())=true;it=keyUrls.erase(it);}else ++it;
    }
    for(auto *reply:network.findChildren<QNetworkReply *>())
        if(!reply->isFinished()&&!accepts(reply->request().url()))reply->abort();
    return true;
}
QSize ResourceManager::imageSize(const QUrl &url) {
    if(imageSizes.contains(url))return imageSizes.value(url);
    QSize size;
    // Header inspection only: never decode pixels to lay out an offscreen image.
    if(url.isLocalFile()){
        QImageReader reader(url.toLocalFile());reader.setAutoTransform(true);size=reader.size();
        if(reader.transformation()&QImageIOHandler::TransformationRotate90)size.transpose();
        if(size.width()>1600||size.height()>1200)size.scale(1600,1200,Qt::KeepAspectRatio);
    }
    if(size.isValid())imageSizes.insert(url,size);
    return size.isValid()?size:QSize(400,300);
}
QString ResourceManager::request(const QUrl &url,int page) {
    const QFileInfo info(url.toLocalFile());
    const QString key=url.toString()+QString("|%1|%2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch())+QString("|%1|page=%2").arg(revision).arg(page);
    if(!accepts(url))return key;
    keyUrls.insert(key,url);
    if(cache.contains(key)||pending.contains(key))return key;
    pending.insert(key);
    const auto cancel=std::make_shared<std::atomic_bool>(false);cancelled.insert(key,cancel);
    if(url.scheme()=="http"||url.scheme()=="https"){
        QNetworkRequest request(url);
        request.setTransferTimeout(15000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        auto *reply=network.get(request);
        auto bytes=std::make_shared<QByteArray>();
        auto oversized=std::make_shared<bool>(false);
        connect(reply,&QIODevice::readyRead,this,[reply,bytes,oversized]{
            bytes->append(reply->readAll());
            if(bytes->size()>32*1024*1024){*oversized=true;reply->abort();}
        });
        const auto generation=revision;
        connect(reply,&QNetworkReply::finished,this,[this,url,reply,bytes,oversized,key,generation,cancel]{
            bytes->append(reply->readAll());
            pending.remove(key);cancelled.remove(key);
            if(*cancel){reply->deleteLater();if(accepts(url))emit ready();return;}
            ResourceSnapshot data;
            if(*oversized||bytes->size()>32*1024*1024)data.error="图片超过 32 MiB 限制";
            else if(reply->error()!=QNetworkReply::NoError)data.error="图片加载失败："+reply->errorString();
            else {
                QBuffer buffer(bytes.get());buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);reader.setDecideFormatFromContent(true);reader.setAutoTransform(true);
#if !MDEDIT_LITE
                if(reader.format()=="svg"||reader.format()=="svgz"){
                    QSvgRenderer svg(*bytes);
                    QSize size=svg.defaultSize();
                    if(!svg.isValid()||size.isEmpty())data.error="SVG 图片无效";
                    else {
                        if(size.width()>1600||size.height()>1200)size.scale(1600,1200,Qt::KeepAspectRatio);
                        data.image=QImage(size,QImage::Format_ARGB32_Premultiplied);data.image.fill(Qt::transparent);
                        QPainter painter(&data.image);svg.render(&painter);
                    }
                }else
#endif
                {
                    if(!supportedImageFormat(reader))data.error="此构建不支持该图片格式";
                    else {
                        const QSize size=reader.size();
                        if(size.isValid()&&qint64(size.width())*size.height()>100000000)data.error="图片像素数超过限制";
                        else {
                            if(size.width()>1600||size.height()>1200)reader.setScaledSize(size.scaled(1600,1200,Qt::KeepAspectRatio));
                            data.image=reader.read();
                            if(data.image.isNull())data.error="无法读取图片："+reader.errorString();
                        }
                    }
                }
            }
            reply->deleteLater();
            if(generation!=revision||!accepts(url))return;
            if(!data.image.isNull())imageSizes.insert(url,data.image.size());
            const int cost=qMax(1,int(data.image.sizeInBytes()/1024));
            cache.insert(key,new ResourceSnapshot(std::move(data)),cost);emit ready();
        });
        return key;
    }
    const auto generation=revision;
    pool.start([this,url,key,page,generation,cancel]{
        ResourceSnapshot data;
        const QString path=url.toLocalFile();QFileInfo file(path);
        if(*cancel){}
        else if(!url.isLocalFile())data.error="不支持的资源地址："+url.toString();
        else if(!file.isFile())data.error="资源不存在："+path;
        else if(file.size()>32*1024*1024)data.error="资源超过 32 MiB 限制："+path;
        else if(file.suffix().compare("pdf",Qt::CaseInsensitive)==0) {
#if !MDEDIT_LITE
            QPdfDocument pdf;
            const auto error=pdf.load(path);
            if(error==QPdfDocument::Error::IncorrectPassword)data.error="PDF 已加密，请在外部阅读器中打开";
            else if(error!=QPdfDocument::Error::None)data.error="无法读取 PDF（文件损坏或格式不受支持）："+path;
            else if(pdf.pageCount()<1)data.error="PDF 没有可显示的页面";
            else {
                data.pageCount=pdf.pageCount();data.page=qBound(0,page,data.pageCount-1);
                const QSizeF points=pdf.pagePointSize(data.page);
                if(points.isEmpty())data.error="PDF 页面尺寸无效";
                else {
                    const QSize size=points.scaled(QSizeF(1600,1600),Qt::KeepAspectRatio).toSize();
                    data.image=pdf.render(data.page,size);
                    if(data.image.isNull())data.error="PDF 页面渲染失败";
                    else {QImage opaque(data.image.size(),QImage::Format_RGB32);opaque.fill(Qt::white);
                        QPainter painter(&opaque);painter.drawImage(0,0,data.image);painter.end();data.image=opaque;}
                }
            }
#else
            data.error="此构建未启用 PDF 预览";
#endif
        } else if(file.suffix().compare("csv",Qt::CaseInsensitive)==0) {
            QFile input(path);
            if(!input.open(QIODevice::ReadOnly))data.error=input.errorString();
            else {
                const QString text=QString::fromUtf8(input.readAll());QString field;QStringList row;bool quoted=false;
                for(int i=0;i<text.size()&&data.rows.size()<51;++i){
                    const QChar c=text[i];
                    if(c=='"'){if(quoted&&i+1<text.size()&&text[i+1]=='"'){field+='"';++i;}else quoted=!quoted;}
                    else if(!quoted&&(c==','||c=='\n')){row.append(field);field.clear();if(c=='\n'){data.rows.append(row);row.clear();}}
                    else if(c!='\r'||quoted)field+=c;
                }
                if(!row.isEmpty()||!field.isEmpty()){row.append(field);data.rows.append(row);}
                if(quoted)data.error="CSV 引号未闭合";
            }
        } else if(ResourceResolver::isTextResource(url)) {
            QFile input(path);
            if(!input.open(QIODevice::ReadOnly))data.error=input.errorString();
            else {
                const QByteArray bytes=input.readAll();
                const auto encoding=QStringDecoder::encodingForData(bytes).value_or(QStringConverter::Utf8);
                QStringDecoder decoder(encoding);
                QString text=decoder.decode(bytes);
                if(decoder.hasError()||text.contains(QChar(0)))data.error="无法解码文本，请使用 UTF-8 或带 BOM 的 UTF-16/UTF-32 编码";
                else {
                    text.replace("\r\n","\n");text.replace('\r','\n');
                    const auto lines=text.split('\n');
                    QString preview=lines.mid(0,12).join('\n');
                    data.textTruncated=lines.size()>12||preview.size()>8000;
                    data.text=preview.left(8000);
                }
            }
        } else if(!ResourceResolver::isImageResource(url)) {
            data.error="不支持预览此文件类型";
        } else {
            QImageReader reader(path);reader.setAutoTransform(true);
            const QSize size=reader.size();
            if(!supportedImageFormat(reader))data.error="此构建不支持该图片格式";
            else if(size.isValid()&&qint64(size.width())*size.height()>100000000)data.error="图片像素数超过限制";
            else {if(size.width()>1600||size.height()>1200)reader.setScaledSize(size.scaled(1600,1200,Qt::KeepAspectRatio));
                data.image=reader.read();if(data.image.isNull())data.error="无法预览该资源："+reader.errorString();}
        }
        QMetaObject::invokeMethod(this,[this,url,key,generation,cancel,data=std::move(data)]() mutable {
            pending.remove(key);cancelled.remove(key);
            if(*cancel){if(accepts(url))emit ready();return;}
            if(generation!=revision||!accepts(url))return;
            if(!data.image.isNull())imageSizes.insert(url,data.image.size());
            int cost=qMax(1,int(data.image.sizeInBytes()/1024));
            if(data.text)cost+=data.text->size()*2/1024+1;
            for(const auto &row:data.rows)for(const auto &cell:row)cost+=cell.size()*2/1024+1;
            cache.insert(key,new ResourceSnapshot(std::move(data)),cost);emit ready();
        },Qt::QueuedConnection);
    });
    return key;
}
}
