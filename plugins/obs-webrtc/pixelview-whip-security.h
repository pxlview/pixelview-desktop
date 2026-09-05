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
