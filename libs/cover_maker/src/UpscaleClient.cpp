#include "cover_maker/UpscaleClient.h"
#include <QBuffer>
#include <QHttpMultiPart>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QFile>

namespace darkeye::cover_maker {
namespace { constexpr qsizetype maxResponse = 256 * 1024 * 1024; }
UpscaleClient::UpscaleClient(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}
bool UpscaleClient::validEndpoint(const QUrl &url)
{
    if(!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment())return false;
    const QString host=url.host().toLower();
    return url.scheme()==QStringLiteral("https") || (url.scheme()==QStringLiteral("http") &&
        (host==QStringLiteral("localhost") || host==QStringLiteral("127.0.0.1") || host==QStringLiteral("::1")));
}
void UpscaleClient::start(const QUrl &url,const QString &token,const QImage &image)
{
    if(busy())return;
    if(!validEndpoint(url)){emit failed(QStringLiteral("服务地址须使用 HTTPS；仅本机调试允许 HTTP。"));return;}
    if(image.isNull() || image.width()>8192/2 || image.height()>8192/2) {
        emit failed(QStringLiteral("图片为空或增强后尺寸超过 8192 像素。"));return;
    }
    QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"PNG")){emit failed(QStringLiteral("无法编码图片。"));return;}
    if(png.size()>20*1024*1024){emit failed(QStringLiteral("上传图片超过 20 MB。"));return;}
    auto *multipart=new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart file;file.setHeader(QNetworkRequest::ContentDispositionHeader,QStringLiteral("form-data; name=\"image\"; filename=\"image.png\""));
    file.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("image/png"));file.setBody(png);multipart->append(file);
    QNetworkRequest request(url);request.setTransferTimeout(240000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    if(!token.isEmpty())request.setRawHeader("Authorization",("Bearer "+token).toUtf8());
    const QString caPath=qEnvironmentVariable("DARKEYE_SR_CA_FILE");
    if(!caPath.isEmpty()) {
        auto ssl=request.sslConfiguration();auto certs=ssl.caCertificates();certs.append(QSslCertificate::fromPath(caPath));ssl.setCaCertificates(certs);request.setSslConfiguration(ssl);
    }
    m_data.clear();m_cancelled=false;m_tooLarge=false;m_expected={image.width()*2,image.height()*2};
    auto *reply=m_network->post(request,multipart);multipart->setParent(reply);m_reply=reply;
    connect(reply,&QNetworkReply::readyRead,this,[this,reply]{
        m_data.append(reply->readAll());if(m_data.size()>maxResponse){m_tooLarge=true;reply->abort();}
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        if(reply->isOpen())m_data.append(reply->readAll());const auto data=std::move(m_data);const auto expected=m_expected;
        const bool cancelled=m_cancelled,tooLarge=m_tooLarge || data.size()>maxResponse;
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error=reply->error();const QString errorString=reply->errorString();
        reply->deleteLater();m_reply=nullptr;emit busyChanged(false);
        if(cancelled)return;
        if(tooLarge){emit failed(QStringLiteral("超分返回数据超过限制。"));return;}
        if(error!=QNetworkReply::NoError || status!=200) {
            const auto object=QJsonDocument::fromJson(data).object();const QString message=object.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            emit failed(message.isEmpty()?QStringLiteral("超分请求失败（HTTP %1）：%2").arg(status).arg(errorString):message);return;
        }
        QBuffer input;input.setData(data);input.open(QIODevice::ReadOnly);QImageReader reader(&input,"PNG");
        if(reader.size()!=expected || qint64(expected.width())*expected.height()>64000000) {
            emit failed(QStringLiteral("服务返回的图片尺寸不符合请求。"));return;
        }
        const QImage result=reader.read();if(result.isNull())emit failed(QStringLiteral("服务返回的 PNG 无法读取。"));else emit completed(result);
    });
    emit busyChanged(true);
}
void UpscaleClient::cancel() {if(m_reply){m_cancelled=true;m_reply->abort();}}
}
