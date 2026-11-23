#include "editor/live_editor.h"
#include "editor/resources/resource_manager.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <iostream>
#define CHECK(x) do {if(!(x)){std::cerr<<"failed line "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(false)
static bool waitFor(const std::function<bool()> &condition){
    QElapsedTimer timer;timer.start();
    while(!condition()&&timer.elapsed()<5000)QCoreApplication::processEvents(QEventLoop::AllEvents,20);
    return condition();
}
int main(int argc,char **argv){
    QApplication app(argc,argv);
    QTcpServer server;CHECK(server.listen(QHostAddress::LocalHost));
    int requests=0;
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{
        while(server.hasPendingConnections()){
            auto *socket=server.nextPendingConnection();
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[socket,&requests]{
                const auto request=socket->readAll();if(!request.contains("\r\n\r\n"))return;
                ++requests;
                if(request.startsWith("GET /missing"))socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                else {
                    const QByteArray svg="<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"20\"><rect width=\"100\" height=\"20\" fill=\"green\"/></svg>";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/svg+xml\r\nContent-Length: "+QByteArray::number(svg.size())+"\r\nConnection: close\r\n\r\n"+svg);
                }
                socket->disconnectFromHost();
            });
        }
    });
    const QString address=QString("http://127.0.0.1:%1/badge/build-passing-green").arg(server.serverPort());
    mdedit::ResourceManager resources;
    const auto key=resources.request(QUrl(address));
    CHECK(resources.request(QUrl(address))==key);
    CHECK(waitFor([&]{return resources.lookup(key)!=nullptr;}));
    CHECK(resources.lookup(key)->error.isEmpty());
    CHECK(resources.lookup(key)->image.size()==QSize(100,20));
    CHECK(requests==1);
    CHECK(resources.request(QUrl(address))==key);CHECK(requests==1);
    const auto missing=resources.request(QUrl(address).resolved(QUrl("/missing")));
    CHECK(waitFor([&]{return resources.lookup(missing)!=nullptr;}));
    CHECK(!resources.lookup(missing)->error.isEmpty());
    LiveEditor editor;
    const QString source="intro\n\n[![build]("+address+")](https://github.com/example/project) [![version]("+address+")](https://github.com/example/project/releases)\n\nend";
    editor.setSource(source);
    CHECK(waitFor([&]{return requests>=3;}));
    bool rendered=waitFor([&]{
        int images=0;
        for(auto block=editor.document()->begin();block.isValid();block=block.next())
            for(auto it=block.begin();!it.atEnd();++it){
                const auto f=it.fragment().charFormat();
                if(f.isImageFormat()&&f.anchorHref().startsWith("https://github.com/example/project")){
                    const auto image=qvariant_cast<QImage>(editor.document()->resource(QTextDocument::ImageResource,QUrl(f.toImageFormat().name())));
                    if(image.size()==QSize(100,20))++images;
                }
            }
        return images==2;
    });
    if(!rendered)std::cerr<<editor.document()->toHtml().toStdString();
    CHECK(rendered);
    CHECK(editor.markdown()==source);
    resources.clear();
    const auto fresh=resources.request(QUrl(address));CHECK(fresh!=key);
    CHECK(waitFor([&]{return resources.lookup(fresh)!=nullptr;}));
    CHECK(resources.lookup(fresh)->error.isEmpty());
    return 0;
}
