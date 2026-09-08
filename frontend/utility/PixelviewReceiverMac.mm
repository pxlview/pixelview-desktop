#include "PixelviewReceiver.hpp"
#import <Foundation/Foundation.h>

// Each login attempt owns a separate ephemeral session. Delegate callbacks and
// event access are serialized on main; WebSocket completion handlers bridge there.
@interface PVReceiverSession : NSObject <NSURLSessionDataDelegate, NSURLSessionWebSocketDelegate> {
@public
 pixelview::ReceiverTransport::Events events;
 BOOL active;
 NSInteger httpCode;
}
@property(nonatomic,strong) NSURLSession *session;
@property(nonatomic,strong) NSURLSessionDataTask *http;
@property(nonatomic,strong) NSURLSessionWebSocketTask *socket;
@property(nonatomic,strong) NSMutableData *body;
-(void)receive;
-(void)closed:(NSInteger)code;
@end
@implementation PVReceiverSession
-(void)closed:(NSInteger)code {
 if(active && events.closed) {auto callback=events.closed; callback((int)code);}
}
-(void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request completionHandler:(void (^)(NSURLRequest *))handler {
 handler(nil); // Never forward either password or client_token on redirects.
}
-(void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveResponse:(NSURLResponse *)response completionHandler:(void (^)(NSURLSessionResponseDisposition))handler {
 httpCode=[(NSHTTPURLResponse *)response statusCode];
 if(response.expectedContentLength>262144) {httpCode=413;handler(NSURLSessionResponseCancel);return;}
 handler(NSURLSessionResponseAllow);
}
-(void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data {
 if(self.body.length+data.length>262144) {httpCode=413;[task cancel];return;}
 [self.body appendData:data];
}
-(void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
 if(!active) return;
 if(task==self.http) {
  QByteArray body(static_cast<const char *>(self.body.bytes),self.body.length);
  self.body=nil; self.http=nil;
  // TLS errors are terminal; generic network errors may retry.
  int code=(int)httpCode;
  if(error && code!=413) code=(error.code<=NSURLErrorSecureConnectionFailed && error.code>=NSURLErrorClientCertificateRequired) ? 495 : 0;
  if(events.login) {auto callback=events.login;callback(code,body);}
 } else if(task==self.socket && error) {
  NSInteger status=[(NSHTTPURLResponse *)task.response statusCode];
  NSInteger code=self.socket.closeCode;
  if(status==401 || status==403 || (status>=300 && status<400)) code=1008;
  if(error.code<=NSURLErrorSecureConnectionFailed && error.code>=NSURLErrorClientCertificateRequired) code=4403;
  [self closed:code];
 }
}
-(void)URLSession:(NSURLSession *)session webSocketTask:(NSURLSessionWebSocketTask *)task didOpenWithProtocol:(NSString *)protocol {
 if(!active) return;
 if(events.opened) {auto callback=events.opened;callback();}
 if(active) [self receive];
}
-(void)receive {
 if(!active || !self.socket) return;
 [self.socket receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *message,NSError *error) {
  dispatch_async(dispatch_get_main_queue(), ^{
   if(!self->active) return;
   if(error) { // Let task/close delegates classify auth and TLS before fallback.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,100*NSEC_PER_MSEC),dispatch_get_main_queue(), ^{if(self->active)[self closed:self.socket.closeCode];});return;
   }
   if(message.type!=NSURLSessionWebSocketMessageTypeString) {[self closed:1008];return;}
   QByteArray bytes=QString::fromNSString(message.string).toUtf8();
   if(self->events.message) {auto callback=self->events.message;callback(bytes);}
   if(self->active) [self receive];
  });
 }];
}
-(void)URLSession:(NSURLSession *)session webSocketTask:(NSURLSessionWebSocketTask *)task didCloseWithCode:(NSURLSessionWebSocketCloseCode)code reason:(NSData *)reason {
 [self closed:code];
}
-(void)URLSession:(NSURLSession *)session didBecomeInvalidWithError:(NSError *)error {
 self.session=nil;self.http=nil;self.socket=nil;self.body=nil;
}
@end

namespace pixelview {
class MacReceiverTransport final : public ReceiverTransport {
 PVReceiverSession *__strong connection=nil;
public:
 ~MacReceiverTransport() override {cancel();}
 void login(const QUrl &url,const QByteArray &body,Events e) override {
  cancel();
  PVReceiverSession *s=[PVReceiverSession new]; connection=s; s->active=YES;s->events=std::move(e);
  s.body=[NSMutableData data];
  auto config=[NSURLSessionConfiguration ephemeralSessionConfiguration];
  // This session also owns the long-lived WebSocket: a 30-second resource
  // timeout terminates it even while valid server heartbeats are arriving.
  // The controller separately bounds login (30s), registration (15s), and
  // missing heartbeats (65s), and refreshes authorization after 23 hours.
  config.timeoutIntervalForRequest=15;config.timeoutIntervalForResource=24*60*60;
  config.HTTPCookieStorage=nil;config.URLCredentialStorage=nil;config.URLCache=nil;
  config.HTTPShouldSetCookies=NO;config.requestCachePolicy=NSURLRequestReloadIgnoringLocalCacheData;
  s.session=[NSURLSession sessionWithConfiguration:config delegate:s delegateQueue:[NSOperationQueue mainQueue]];
  auto request=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:QString::fromUtf8(url.toEncoded()).toNSString()]];
  request.HTTPMethod=@"POST";[request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
  request.HTTPBody=[NSData dataWithBytes:body.constData() length:body.size()];
  s.http=[s.session dataTaskWithRequest:request];[s.http resume];
 }
 void open(const QUrl &url) override {
  auto s=connection;if(!s || !s->active)return;
  s.socket=[s.session webSocketTaskWithURL:[NSURL URLWithString:QString::fromUtf8(url.toEncoded()).toNSString()]];
  s.socket.maximumMessageSize=262144;[s.socket resume];
 }
 void send(const QByteArray &body) override {
  auto s=connection;if(!s || !s->active || !s.socket)return;
  auto message=[[NSURLSessionWebSocketMessage alloc] initWithString:QString::fromUtf8(body).toNSString()];
  [s.socket sendMessage:message completionHandler:^(NSError *error){
   if(error)dispatch_async(dispatch_get_main_queue(), ^{if(s->active)[s closed:s.socket.closeCode];});
  }];
 }
 void cancel() override {
  auto s=connection;connection=nil;if(!s)return;
  s->active=NO;s->events={};s.body=nil;
  [s.http cancel];s.http=nil;
  // Backend removes this viewer on websocket disconnect. There is no REMOVE
  // mutation. Allow the normal close frame to flush before bounded teardown.
  [s.socket cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
  [s.session finishTasksAndInvalidate];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(), ^{[s.session invalidateAndCancel];});
 }
};
std::unique_ptr<ReceiverTransport> makeReceiverTransport() {return std::make_unique<MacReceiverTransport>();}
}
