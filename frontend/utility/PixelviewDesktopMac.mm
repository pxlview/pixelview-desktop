#include "PixelviewDesktopConnection.hpp"
#include "PixelviewNoninteractiveKeychain.hpp"
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <QtCore/QPointer>
@interface PVDesktopSocket : NSObject <NSURLSessionWebSocketDelegate> {
@public
 QPointer<pixelview::DesktopConnection> owner;
}
@property NSURLSession *session;
@property NSURLSessionWebSocketTask *task;
@property NSString *auth;
-(void)receive;
-(void)failed:(NSInteger)code;
@end
@implementation PVDesktopSocket
-(void)failed:(NSInteger)code {
 dispatch_async(dispatch_get_main_queue(), ^{ if(self->owner) self->owner->disconnected((int)code); });
}
-(void)receive {
 if(!owner) return;
 [self.task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *message,NSError *error){
  dispatch_async(dispatch_get_main_queue(), ^{
   if(!self->owner) return;
   if(error) { [self failed:self.task.closeCode]; return; }
   if(message.type!=NSURLSessionWebSocketMessageTypeString) { [self failed:4400]; return; }
   QByteArray body=QString::fromNSString(message.string).toUtf8();
   auto callback=self->owner->message; callback(body);
   // The callback may detach this attempt and open a new connection.
   if(self->owner) [self receive];
  });
 }];
}
-(void)URLSession:(NSURLSession *)session webSocketTask:(NSURLSessionWebSocketTask *)task didOpenWithProtocol:(NSString *)protocol {
 if(!owner) return; // A cancelled attempt must not send a delayed authentication.
 [task sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:self.auth] completionHandler:^(NSError *error){ if(error) [self failed:0]; }];
 self.auth=nil;
 [self receive];
}
-(void)URLSession:(NSURLSession *)session webSocketTask:(NSURLSessionWebSocketTask *)task didCloseWithCode:(NSURLSessionWebSocketCloseCode)code reason:(NSData *)reason { [self failed:code]; }
-(void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error { if(error) [self failed:0]; }
-(void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request completionHandler:(void (^)(NSURLRequest *))handler { handler(nil); }
@end
namespace pixelview {
void DesktopConnection::openSocket(QUrl url,QString token) {
 closeSocket();
 PVDesktopSocket *s=[PVDesktopSocket new]; s->owner=this;
 NSURLSessionConfiguration *config=[NSURLSessionConfiguration ephemeralSessionConfiguration];
 config.timeoutIntervalForRequest=10; config.HTTPCookieStorage=nil; config.URLCredentialStorage=nil; config.URLCache=nil;
 // Owner, delegate lifecycle and receive-loop decisions share Qt/Cocoa main.
 s.session=[NSURLSession sessionWithConfiguration:config delegate:s delegateQueue:NSOperationQueue.mainQueue];
 s.task=[s.session webSocketTaskWithURL:[NSURL URLWithString:url.toString().toNSString()]];
 s.task.maximumMessageSize=16384;
 s.auth=QString::fromUtf8(QJsonDocument(QJsonObject{{"type","auth"},{"device_token",token}}).toJson(QJsonDocument::Compact)).toNSString();
 socket=(__bridge_retained void *)s;
 [s.task resume];
}
void DesktopConnection::sendSocket(QByteArray body) {
 if(!socket || body.size()>16384) return;
 PVDesktopSocket *s=(__bridge PVDesktopSocket *)socket;
 [s.task sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:QString::fromUtf8(body).toNSString()] completionHandler:^(NSError *error){if(error) [s failed:0];}];
}
void DesktopConnection::closeSocket() {
 if(!socket) return;
 PVDesktopSocket *s=CFBridgingRelease(socket); socket=nullptr;
 s->owner=nullptr;
 s.auth=nil;
 [s.task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
 [s.session invalidateAndCancel];
}
}

namespace pixelview {
static NSMutableDictionary *query(const QString &origin) {
 return [@{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
  (__bridge id)kSecAttrService:@"com.pixelview.desktop.device",
  (__bridge id)kSecAttrAccount:origin.toNSString(),
  (__bridge id)kSecAttrSynchronizable:@NO} mutableCopy];
}
bool saveDevice(const QString &origin,const QString &token) {
 NoninteractiveKeychain interaction;
 if(!interaction.ready) return false;
 auto q=query(origin);
 NSData *data=[token.toNSString() dataUsingEncoding:NSUTF8StringEncoding];
 OSStatus result=SecItemUpdate((__bridge CFDictionaryRef)q,(__bridge CFDictionaryRef)@{(__bridge id)kSecValueData:data});
 if(result==errSecItemNotFound) {
  q[(__bridge id)kSecValueData]=data;
  q[(__bridge id)kSecAttrAccessible]=(__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
  result=SecItemAdd((__bridge CFDictionaryRef)q,nullptr);
 }
 return keychainStatus("sender-save",result)==errSecSuccess && loadDevice(origin)==token;
}
QString loadDevice(const QString &origin) {
 NoninteractiveKeychain interaction;
 if(!interaction.ready) return {};
 auto q=query(origin); q[(__bridge id)kSecReturnData]=@YES;
 CFTypeRef value=nullptr;
 if(keychainStatus("sender-read",SecItemCopyMatching((__bridge CFDictionaryRef)q,&value))!=errSecSuccess) return {};
 NSData *data=CFBridgingRelease(value);
 return QString::fromUtf8(static_cast<const char *>(data.bytes),data.length);
}
bool removeDevice(const QString &origin) {
 NoninteractiveKeychain interaction;
 if(!interaction.ready) return false;
 auto status=SecItemDelete((__bridge CFDictionaryRef)query(origin));
 keychainStatus("sender-remove",status);
 if(status!=errSecSuccess && status!=errSecItemNotFound) return false;
 auto q=query(origin); q[(__bridge id)kSecReturnData]=@YES;
 CFTypeRef value=nullptr;
 status=SecItemCopyMatching((__bridge CFDictionaryRef)q,&value);
 if(value) CFRelease(value);
 return keychainStatus("sender-remove-verification",status)==errSecItemNotFound;
}
}
