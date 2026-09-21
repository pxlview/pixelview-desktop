#pragma once
#include <curl/curl.h>
#include <string>
// Resource DELETE carries the node bearer: require the original exact origin.
inline bool pixelviewWhipSameOrigin(const std::string &endpoint, const std::string &resource)
{
 CURLU *a=curl_url(), *b=curl_url();
 auto part=[](CURLU *u,CURLUPart p,unsigned flags=0U){
  char *v=nullptr; std::string result;
  if(curl_url_get(u,p,&v,flags)==CURLUE_OK) {result=v;curl_free(v);} return result;
 };
 bool ok=a && b && curl_url_set(a,CURLUPART_URL,endpoint.c_str(),0)==CURLUE_OK &&
  curl_url_set(b,CURLUPART_URL,resource.c_str(),0)==CURLUE_OK;
 if(ok) ok=part(a,CURLUPART_SCHEME)==part(b,CURLUPART_SCHEME) &&
  part(a,CURLUPART_HOST)==part(b,CURLUPART_HOST) &&
  part(a,CURLUPART_PORT,CURLU_DEFAULT_PORT)==part(b,CURLUPART_PORT,CURLU_DEFAULT_PORT) &&
  part(b,CURLUPART_USER).empty() && part(b,CURLUPART_PASSWORD).empty() && part(b,CURLUPART_FRAGMENT).empty();
 curl_url_cleanup(a);curl_url_cleanup(b);return ok;
}
// Log-safe origin (scheme://host:port): never a path, query, fragment or userinfo.
inline std::string pixelviewWhipOrigin(const std::string &url)
{
 CURLU *u=curl_url();
 auto part=[u](CURLUPart p,unsigned flags=0U){
  char *v=nullptr; std::string result;
  if(u && curl_url_get(u,p,&v,flags)==CURLUE_OK) {result=v;curl_free(v);} return result;
 };
 std::string origin="invalid";
 if(u && curl_url_set(u,CURLUPART_URL,url.c_str(),0)==CURLUE_OK)
  origin=part(CURLUPART_SCHEME)+"://"+part(CURLUPART_HOST)+":"+part(CURLUPART_PORT,CURLU_DEFAULT_PORT);
 if(u) curl_url_cleanup(u);
 return origin;
}
// The WHIP resource receives the node bearer (DELETE). It may live on the
// endpoint's own origin or on the origin that issued the bearer (the paired
// Pixelview backend, which fronts engines under /ingress and is what they name
// in an absolute Location). Any other origin is refused.
inline bool pixelviewWhipResourceAllowed(const std::string &endpoint, const std::string &resource, const std::string &issuer)
{
 return pixelviewWhipSameOrigin(endpoint, resource) || (!issuer.empty() && pixelviewWhipSameOrigin(issuer, resource));
}
