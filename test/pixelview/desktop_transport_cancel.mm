// Exercise an actual cancelled NSURLSession delegate arriving late. No Keychain.
#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include <QtCore/QCoreApplication>
#include <QtNetwork/QTcpServer>
#import <Foundation/Foundation.h>
#include <cassert>
static int sends=0, receives=0;
@interface PVLateOpenTask : NSObject
-(void)sendMessage:(NSURLSessionWebSocketMessage *)message completionHandler:(void (^)(NSError *))handler;
-(void)receiveMessageWithCompletionHandler:(void (^)(NSURLSessionWebSocketMessage *,NSError *))handler;
@end
@implementation PVLateOpenTask
-(void)sendMessage:(NSURLSessionWebSocketMessage *)message completionHandler:(void (^)(NSError *))handler {++sends;}
-(void)receiveMessageWithCompletionHandler:(void (^)(NSURLSessionWebSocketMessage *,NSError *))handler {++receives;}
@end
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 QTcpServer listener; assert(listener.listen(QHostAddress::LocalHost));
 pixelview::DesktopConnection connection;
 connection.openSocket(QUrl("ws://127.0.0.1:"+QString::number(listener.serverPort())+"/desktop/ws"),"fixture-secret");
 id<NSURLSessionWebSocketDelegate> old=(__bridge id)connection.socket;
 NSURLSession *session=[(id)old valueForKey:@"session"];
 NSURLSessionWebSocketTask *task=[(id)old valueForKey:@"task"];
 assert([task.currentRequest.URL.query isEqualToString:@"token=fixture-secret"]);
 assert(session.configuration.timeoutIntervalForRequest==10);
 assert(session.configuration.timeoutIntervalForResource>65);
 printf("Desktop NSURLSession request=%.0fs resource=%.0fs\n",session.configuration.timeoutIntervalForRequest,session.configuration.timeoutIntervalForResource);
 connection.closeSocket();
 [old URLSession:NSURLSession.sharedSession webSocketTask:(NSURLSessionWebSocketTask *)[PVLateOpenTask new] didOpenWithProtocol:nil];
 assert(sends==0 && receives==0 && "detached delegate must neither send nor arm a receive loop after cancellation");
}
