#pragma once
#include "PixelviewDesktop.hpp"
#include "PixelviewKeychainTask.hpp"
#include <QtCore/QObject>
#include <QtCore/QJsonDocument>
#include <QtCore/QSysInfo>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
namespace pixelview {
bool saveDevice(const QString &origin,const QString &token);
QString loadDevice(const QString &origin);
bool removeDevice(const QString &origin);
class DesktopConnection : public QObject {
public:
 QNetworkAccessManager http{this};
 QUrl origin;
 DesktopIdentity identity;
 bool development=false;
 bool exchanging=false;
 QString authorizedToken; // Process-held pairing credential; reused on reconnect, cleared on Unpair/revocation.
 std::function<void()> paired=[]{};
 std::function<void(QString)> status=[](QString){};
 std::function<void(QByteArray)> message=[](QByteArray){};
 std::function<void(int)> disconnected=[](int){};
 void *socket=nullptr;
 void openSocket(QUrl url,QString token);
 void sendSocket(QByteArray body);
 void closeSocket();
 ~DesktopConnection() override { closeSocket(); }
 explicit DesktopConnection(QObject *parent=nullptr):QObject(parent){}
 void pair(QUrl url,bool dev,QString code) {
  if(exchanging) {status("A pairing exchange is already in progress.");return;}
  if(!Desktop::validOrigin(url,dev) || code.isEmpty()) {status("Enter a valid HTTPS backend origin and pairing code. Local HTTP requires development mode.");return;}
  url.setPath(""); origin=url; development=dev;
  exchanging=true;
  const auto requestOrigin=origin;
  url.setPath("/desktop/exchange");
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(10000);
  auto reply=http.post(request,QJsonDocument(QJsonObject{{"pairing_token",code},{"hostname",QSysInfo::machineHostName()},{"label",QJsonValue::Null}}).toJson(QJsonDocument::Compact));
  reply->setReadBufferSize(16385);
  connect(reply,&QNetworkReply::finished,this,[this,reply,requestOrigin]{
   exchanging=false;
   const auto body=reply->read(16385);
   auto object=QJsonDocument::fromJson(body).object();
   bool ok=reply->error()==QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200 && body.size()<=16384;
   reply->deleteLater();
   if(!ok || object["desktop_id"].toString().isEmpty() || object["node_id"].toString().isEmpty() || object["device_token"].toString().isEmpty()) {status("Pairing failed. Create a new code in Pixelview admin.");return;}
   exchanging=true; // Includes the native Keychain prompt, not just HTTP.
   status("Allow Pixelview to save its credential in the macOS Keychain.");
   const QString token=object["device_token"].toString();
   runKeychainUserAction(this,[requestOrigin,token]{return saveDevice(requestOrigin.toString(),token);},
    [this,object,token](bool saved){
     exchanging=false;
     if(!saved) {status("Pairing incomplete: Keychain access was canceled or failed. Saved credentials were not removed. Pair with a new admin code to retry.");return;}
     authorizedToken=token;
     identity.clear(); identity.accept(object);
     paired();
    });
  });
 }
};
}
