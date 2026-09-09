/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <obs-module.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
int main(int argc, char **argv) {
 assert(argc==2 || argc==3);
 assert(obs_startup("en-US",NULL,NULL));
 struct obs_audio_info ai={.samples_per_sec=48000,.speakers=SPEAKERS_STEREO};
 assert(obs_reset_audio(&ai));
 obs_module_t *module=NULL;
 assert(obs_open_module(&module,argv[1],".")==MODULE_SUCCESS);
 if (argc==3) { assert(!obs_init_module(module)); obs_shutdown(); puts("PASS module refuses failed lifetime pin"); return 0; }
 assert(obs_init_module(module));
 void *handle=dlopen(argv[1],RTLD_NOW|RTLD_NOLOAD);assert(handle);
 void *load_fn=dlsym(handle,"obs_module_load");assert(load_fn);dlclose(handle);
 obs_source_t *s=obs_source_create_private("pixelview_whep_source","bundled-test",NULL);
 assert(s);
 calldata_t cd;calldata_init(&cd);
 proc_handler_t *ph=obs_source_get_proc_handler(s);
 assert(proc_handler_call(ph,"get_status",&cd));
 assert(!strcmp(calldata_string(&cd,"state"),"idle"));
 calldata_set_string(&cd,"endpoint","http://127.0.0.1:9/test-no-credentials");
 calldata_set_int(&cd,"latency",50);
 assert(proc_handler_call(ph,"connect",&cd));
 for(int i=0;i<100;i++) {
  usleep(50000);proc_handler_call(ph,"get_status",&cd);
  if(calldata_int(&cd,"jitter_latency")==50 && !strcmp(calldata_string(&cd,"state"),"error"))break;
 }
 printf("jitter_latency=%lld state=%s\n",(long long)calldata_int(&cd,"jitter_latency"),calldata_string(&cd,"state"));
 assert(calldata_int(&cd,"jitter_latency")==50);
 assert(!strcmp(calldata_string(&cd,"state"),"error"));
 assert(proc_handler_call(ph,"disconnect",&cd));
 calldata_free(&cd);obs_source_release(s);obs_shutdown();
 Dl_info retained;assert(dladdr(load_fn,&retained));
 puts("PASS module code remains resident after OBS unload for cached probe lifetime");
 puts("PASS production module + bundled WHEP + actual jitter readback + safe connection failure");
}
