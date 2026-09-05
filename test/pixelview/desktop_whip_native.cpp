// Real libobs + real WHIP plugin regression; no encoders/capture/network start.
#include <obs.h>
#include <cassert>
#include <unistd.h>
int main(int argc,char **argv) {
 assert(argc==3);assert(obs_startup("en-US",nullptr,nullptr));
 obs_module_t *module=nullptr;
 assert(obs_open_module(&module,argv[1],argv[2])==MODULE_SUCCESS);
 assert(obs_init_module(module));
 obs_post_load_modules();
 auto settings=obs_data_create();
 obs_data_set_string(settings,"server","http://127.0.0.1:1/not-used");
 auto service=obs_service_create("whip_custom","test-service",settings,nullptr);
 auto output=obs_output_create("whip_output","test-output",nullptr,nullptr);
 assert(service && output);obs_output_set_service(output,service);
 // Native force-stop must complete even before any encoder/media started.
 obs_output_force_stop(output);
 alarm(3);
 assert(!obs_output_start(output)); // Expected: no encoders. Must not deadlock.
 alarm(0);
 obs_output_release(output);obs_service_release(service);obs_data_release(settings);
 obs_shutdown();
}
