#pragma once
#include "PixelviewDesktop.hpp"
#include "PixelviewKeychainTask.hpp"
#include <QtCore/QObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QStringList>
#include <QtCore/QSysInfo>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
namespace pixelview {
bool saveDevice(const QString &origin,const QString &token);
QString loadDevice(const QString &origin);
bool removeDevice(const QString &origin);
// Keychain failure categories (never item contents) also go to this sink; may run on a worker thread.
void setKeychainLog(void (*sink)(const char *line));
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
 // Application-log diagnostics. Never receives a pairing code or device token.
 std::function<void(QString)> log=[](QString){};
 std::function<void(QByteArray)> message=[](QByteArray){};
 std::function<void(int)> disconnected=[](int){};
 void *socket=nullptr;
 void openSocket(QUrl url,QString token);
 void sendSocket(QByteArray body);
 void closeSocket(bool normal=true);
 ~DesktopConnection() override { closeSocket(); }
 explicit DesktopConnection(QObject *parent=nullptr):QObject(parent){}
 // FastAPI validation errors echo the submitted code under "input"; keep only
 // the field path and message so a pairing code can never reach the log.
 static QString exchangeDetail(const QJsonObject &object) {
  const auto detail=object["detail"];
  if(detail.isString()) return detail.toString().left(200);
  QStringList parts;
  const QJsonArray entries=detail.toArray();
  for(const QJsonValue entry : entries) {
   QStringList loc;
   const QJsonArray path=entry.toObject()["loc"].toArray();
   for(const QJsonValue part : path) loc<<part.toVariant().toString();
   parts<<loc.join('.')+": "+entry.toObject()["msg"].toString();
  }
  return parts.join("; ").left(200);
 }
 static QString exchangeMessage(int http,const QString &transport) {
  if(!http) return QStringLiteral("Pairing failed: could not reach the Pixelview backend (%1). Check the network connection and try again.").arg(transport);
  if(http==401 || http==422) return "Pairing failed: the code is invalid, expired or already used. Create a new code in Pixelview admin.";
  if(http==429) return "Pairing failed: too many attempts. Wait a while, then create a new code in Pixelview admin.";
  if(http==200) return "Pairing failed: unexpected response from the Pixelview backend.";
  return QStringLiteral("Pairing failed (HTTP %1). Try again, or create a new code in Pixelview admin.").arg(http);
 }
 void pair(QUrl url,bool dev,QString code) {
  code=code.trimmed();
  if(exchanging) {status("A pairing exchange is already in progress.");return;}
  if(!Desktop::validOrigin(url,dev) || code.isEmpty()) {status("Enter a valid HTTPS backend origin and pairing code. Local HTTP requires development mode.");return;}
  url.setPath(""); origin=url; development=dev;
  exchanging=true;
  const auto requestOrigin=origin;
  url.setPath("/desktop/exchange");
  log(QStringLiteral("pairing exchange with %1 (code length %2)").arg(requestOrigin.toString()).arg(code.size()));
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
   const int http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
   const QString transport=reply->error()==QNetworkReply::NoError ? QStringLiteral("none") : reply->errorString();
   reply->deleteLater();
   if(!ok || object["desktop_id"].toString().isEmpty() || object["node_id"].toString().isEmpty() || object["device_token"].toString().isEmpty()) {
    // Numbers first: the multi-argument form substitutes both texts in one pass.
    log(QStringLiteral("pairing exchange failed: HTTP %1, body %2 bytes, transport error: %3, backend detail: %4")
     .arg(http).arg(body.size()).arg(transport,ok ? QStringLiteral("response is missing desktop_id/node_id/device_token") : exchangeDetail(object)));
    status(exchangeMessage(http,transport));return;
   }
   log(QStringLiteral("pairing exchange succeeded: node %1, desktop %2").arg(object["node_id"].toString(),object["desktop_id"].toString()));
   exchanging=true; // Includes the native Keychain prompt, not just HTTP.
   status("Allow Pixelview to save its credential in the macOS Keychain.");
   const QString token=object["device_token"].toString();
   runKeychainUserAction(this,[requestOrigin,token]{return saveDevice(requestOrigin.toString(),token);},
    [this,object,token](bool saved){
     exchanging=false;
     if(!saved) {log(QStringLiteral("pairing incomplete: the device credential could not be saved to the Keychain (see Pixelview Keychain lines)"));status("Pairing incomplete: Keychain access was canceled or failed. Saved credentials were not removed. Pair with a new admin code to retry.");return;}
     authorizedToken=token;
     identity.clear(); identity.accept(object);
     paired();
    });
  });
 }
};
}
