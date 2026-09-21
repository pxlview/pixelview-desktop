#include "PixelviewDesktopConnection.hpp"
#include "PixelviewNoninteractiveKeychain.hpp"
#include "PixelviewSocketWatchdog.hpp"
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <QtCore/QPointer>
#include <QtCore/QUrlQuery>
@interface PVDesktopSocket : NSObject <NSURLSessionWebSocketDelegate> {
@public
 QPointer<pixelview::DesktopConnection> owner;
 std::unique_ptr<QTimer> watchdog;
}
@property NSURLSession *session;
@property NSURLSessionWebSocketTask *task;
-(void)receive;
-(void)failed:(NSInteger)code;
@end
@implementation PVDesktopSocket
-(void)failed:(NSInteger)code {
 // Send/receive/ping failures can race the authoritative close/HTTP/TLS event.
 // Let the main-queue delegates classify first; never turn 4401 into a retry.
 const bool transient=pixelview::Desktop::transientClose((int)code);
 dispatch_after(dispatch_time(DISPATCH_TIME_NOW,transient ? 100*NSEC_PER_MSEC : 0),dispatch_get_main_queue(), ^{
  if(!self->owner) return;
  NSInteger result=code;
  if(transient) {
   NSInteger status=[(NSHTTPURLResponse *)self.task.response statusCode];
   if(status==401) result=4401;
   else if(status==403 || (status>=300 && status<400)) result=4403;
   else if(self.task.closeCode!=NSURLSessionWebSocketCloseCodeInvalid) result=self.task.closeCode;
  }
  auto callback=self->owner->disconnected;callback((int)result);
 });
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
 if(!owner) return; // A cancelled attempt must not arm a receive loop.
 __weak PVDesktopSocket *weakSelf=self;
 watchdog=pixelview::watchControlSocket(task,[weakSelf]{PVDesktopSocket *s=weakSelf;if(s) [s failed:0];});
 [self receive];
}
-(void)URLSession:(NSURLSession *)session webSocketTask:(NSURLSessionWebSocketTask *)task didCloseWithCode:(NSURLSessionWebSocketCloseCode)code reason:(NSData *)reason { [self failed:code]; }
-(void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
 if(error) [self failed:(error.code<=NSURLErrorSecureConnectionFailed && error.code>=NSURLErrorClientCertificateRequired) ? 4403 : 0];
}
-(void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request completionHandler:(void (^)(NSURLRequest *))handler { handler(nil); }
@end
namespace pixelview {
void DesktopConnection::openSocket(QUrl url,QString token) {
 closeSocket();
 // The device token authenticates the upgrade itself; there is no first message.
 QUrlQuery query; query.addQueryItem("token",token);
 url.setQuery(query);
 PVDesktopSocket *s=[PVDesktopSocket new]; s->owner=this;
 NSURLSessionConfiguration *config=[NSURLSessionConfiguration ephemeralSessionConfiguration];
 config.timeoutIntervalForRequest=10; config.HTTPCookieStorage=nil; config.URLCredentialStorage=nil; config.URLCache=nil;
 // Owner, delegate lifecycle and receive-loop decisions share Qt/Cocoa main.
 s.session=[NSURLSession sessionWithConfiguration:config delegate:s delegateQueue:NSOperationQueue.mainQueue];
 s.task=[s.session webSocketTaskWithURL:[NSURL URLWithString:url.toString().toNSString()]];
 s.task.maximumMessageSize=16384;
 socket=(__bridge_retained void *)s;
 [s.task resume];
}
void DesktopConnection::sendSocket(QByteArray body) {
 if(!socket || body.size()>16384) return;
 PVDesktopSocket *s=(__bridge PVDesktopSocket *)socket;
 [s.task sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:QString::fromUtf8(body).toNSString()] completionHandler:^(NSError *error){if(error) [s failed:0];}];
}
void DesktopConnection::closeSocket(bool normal) {
 if(!socket) return;
 PVDesktopSocket *s=CFBridgingRelease(socket); socket=nullptr;
 s->owner=nullptr;
 s->watchdog.reset();
 if(normal) [s.task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
 else [s.task cancel];
 [s.session invalidateAndCancel];
}
void setKeychainLog(void (*sink)(const char *line)) { keychainLogSink=sink; }
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
