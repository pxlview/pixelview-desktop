#include "PixelviewReceiveCredentialStore.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include "PixelviewNoninteractiveKeychain.hpp"
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <LocalAuthentication/LocalAuthentication.h>

namespace pixelview {
class KeychainReceiveStore final : public ReceiveCredentialStore {
 QString service;
 NSMutableDictionary *query() const
 {
  LAContext *context=[LAContext new]; context.interactionNotAllowed=YES;
  return [@{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
   (__bridge id)kSecAttrService:service.toNSString(),
   (__bridge id)kSecAttrAccount:@"latest-session",
   (__bridge id)kSecAttrSynchronizable:@NO,
   (__bridge id)kSecUseAuthenticationContext:context} mutableCopy];
 }
public:
 explicit KeychainReceiveStore(QString s) : service(std::move(s)) {}
 Result load(const QString &origin, const QString &session, const QString &revision) override
 {
  NoninteractiveKeychain interaction;
  if(!interaction.ready) return {State::Error,{}};
  auto q=query(); q[(__bridge id)kSecReturnData]=@YES;
  CFTypeRef value=nullptr;
  auto status=SecItemCopyMatching((__bridge CFDictionaryRef)q,&value);
  if(status==errSecItemNotFound) return {};
  if(status!=errSecSuccess) return {State::Error,{}};
  NSData *data=CFBridgingRelease(value);
  auto document=QJsonDocument::fromJson(QByteArray(static_cast<const char *>(data.bytes),data.length));
  auto object=document.object();
  if(!document.isObject() || object["version"]!=1 || !object["password"].isString()) return {State::Error,{}};
  if(revision.isEmpty() || object["origin"]!=origin || object["session"]!=session || object["revision"]!=revision) return {};
  return {State::Found,object["password"].toString()};
 }
 bool save(const QString &origin,const QString &session,const QString &revision,const QString &password) override
 {
  NoninteractiveKeychain interaction;
  if(!interaction.ready) return false;
  if(password.isEmpty()) return clear();
  if(origin.isEmpty() || session.isEmpty() || revision.isEmpty()) return false;
  auto bytes=QJsonDocument(QJsonObject{{"version",1},{"origin",origin},{"session",session},
   {"revision",revision},{"password",password}}).toJson(QJsonDocument::Compact);
  NSData *data=[NSData dataWithBytes:bytes.constData() length:bytes.size()];
  auto q=query();
  auto status=SecItemUpdate((__bridge CFDictionaryRef)q,(__bridge CFDictionaryRef)@{(__bridge id)kSecValueData:data});
  if(status==errSecItemNotFound) {
   q[(__bridge id)kSecValueData]=data;
   q[(__bridge id)kSecAttrAccessible]=(__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
   status=SecItemAdd((__bridge CFDictionaryRef)q,nullptr);
  }
  if(status!=errSecSuccess) return false;
  auto check=load(origin,session,revision);
  return check.state==State::Found && check.password==password;
 }
 bool clear() override
 {
  NoninteractiveKeychain interaction;
  if(!interaction.ready) return false;
  auto status=SecItemDelete((__bridge CFDictionaryRef)query());
  if(status!=errSecSuccess && status!=errSecItemNotFound) return false;
  auto q=query(); q[(__bridge id)kSecReturnData]=@YES;
  CFTypeRef value=nullptr;
  status=SecItemCopyMatching((__bridge CFDictionaryRef)q,&value);
  if(value) CFRelease(value);
  return status==errSecItemNotFound;
 }
};
std::unique_ptr<ReceiveCredentialStore> makeReceiveCredentialStore(const QString &service)
{
 return std::make_unique<KeychainReceiveStore>(service);
}
}
