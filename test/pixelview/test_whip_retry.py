"""Execute producer branches and the frontend dispatcher, offline."""
import pathlib
import unittest
import test_desktop_retry
ROOT = pathlib.Path(__file__).resolve().parents[2]

class WhipRetry(unittest.TestCase):
    def run_branch(self, body, parameters, cases):
        stream = (ROOT/'frontend/widgets/OBSBasic_Streaming.cpp').read_text()
        dispatch = stream[stream.index('\t\t\tconst bool transient ='):stream.index('\n\t\t}', stream.index('\t\t\tconst bool transient ='))]
        whip = (ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
        cleanup = whip[whip.index('\tauto doCleanup ='):whip.index('\n\tauto displayError =')]
        source = r'''
#include "frontend/utility/PixelviewDesktop.hpp"
#include <cassert>
#include "libobs/obs-defs.h"
#include <curl/curl.h>
#define LOG_ERROR 0
#define LOG_INFO 0
#define do_log(...) ((void)0)
struct Producer {
 int output=0, code=OBS_OUTPUT_SUCCESS;
 void Stop(bool) {}
 void obs_output_signal_stop(int,int value) {code=value;}
 int c=0, headers=0, url_builder=0;
 std::string resource_url="https://other.invalid/";
 struct dstr {const char *array="";};
 void curl_easy_cleanup(int) {} void curl_slist_free_all(int) {} void curl_url_cleanup(int) {}
 void dstr_init_copy(dstr*,const char*) {} void dstr_replace(dstr*,const char*,const char*) {} void dstr_free(dstr*) {}
 const char *obs_module_text(const char *s) {return s;}
 void obs_output_set_last_error(int,const char*) {}
 bool run(PARAMETERS) {CLEANUP (void)doCleanup; BODY return true;}
};
void check(Producer &p, bool retry) {
 pixelview::Desktop pixelviewLease;
 pixelviewLease.ready=true;pixelviewLease.deadline=30000;
 assert(pixelviewLease.requestStart(0));
 int code=p.code;
 DISPATCH
 assert(pixelviewLease.intent==retry);
 assert((pixelviewLease.retryAt>=0)==retry);
 if(retry) {
  assert(!pixelviewLease.authorized(0));
  assert(pixelviewLease.takeRetry(2000,true));
  pixelviewLease.receive({{"type","ready"},{"heartbeat_interval",15},{"lease_seconds",45}},2000);
  assert(pixelviewLease.pending && !pixelviewLease.authorized(2000));
 }
}
int main(){ CASES }
'''.replace('PARAMETERS',parameters).replace('CLEANUP',cleanup).replace('BODY',body).replace('DISPATCH',dispatch).replace('CASES',cases)
        test_desktop_retry.DesktopRetry().compile_run(source)

    def test_http_temporary_failure_reacquires_authority(self):
        whip=(ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
        body=whip[whip.index('\tif (response_code != 201)'):whip.index('\n\tif (read_buffer.empty())')]
        self.run_branch(body, 'long response_code', '''
for(int status : {502,503,504,401,403,400,404,409,422,500,501,301,307}) {
 Producer p;p.run(status);check(p,status==502 || status==503 || status==504);
}
''')

    def test_only_transient_curl_failures_retry(self):
        whip=(ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
        body=whip[whip.index('\tif (res != CURLE_OK)'):whip.index('\n\tlong response_code;')]
        self.run_branch(body, 'CURLcode res', '''
for(auto error : {CURLE_COULDNT_RESOLVE_HOST, CURLE_COULDNT_RESOLVE_PROXY, CURLE_COULDNT_CONNECT,
                 CURLE_OPERATION_TIMEDOUT, CURLE_SEND_ERROR, CURLE_RECV_ERROR, CURLE_GOT_NOTHING}) {
 Producer p;p.run(error);check(p,true);
}
for(auto error : {CURLE_URL_MALFORMAT, CURLE_UNSUPPORTED_PROTOCOL, CURLE_PEER_FAILED_VERIFICATION,
                 CURLE_SSL_CERTPROBLEM, CURLE_LOGIN_DENIED, CURLE_OUT_OF_MEMORY}) {
 Producer p;p.run(error);check(p,false);
}
''')

    def test_invalid_response_is_terminal(self):
        whip=(ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
        for marker in ('Connect failed: No data returned', 'WHIP server did not provide a resource URL',
                       'Failed to build Resource URL', 'WHIP server provided a invalid resource URL',
                       'WHIP resource origin rejected', 'WHIP server responded with invalid SDP',
                       'Failed to set remote description'):
            with self.subTest(marker=marker):
                start=whip.rindex('do_log(', 0, whip.index(marker))
                end=whip.index('return false;',start)+len('return false;')
                self.run_branch(whip[start:end], '', 'Producer p;p.run();check(p,false);')

    def test_encoder_failure_remains_terminal(self):
        whip=(ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
        body=whip.split('if (!packet) {',1)[1].split('\n\t}',1)[0].replace('return;', 'return false;')
        self.run_branch(body, '', 'Producer p;p.run();check(p,false);assert(p.code==OBS_OUTPUT_ENCODE_ERROR);')

    def test_peer_failure_reacquires_authority(self):
        whip=(ROOT/'plugins/obs-webrtc/whip-output.cpp').read_text()
        body=whip.split('case rtc::PeerConnection::State::Failed:',1)[1].split('break;',1)[0]
        self.run_branch(body, '', 'Producer p;p.run();check(p,true);')
