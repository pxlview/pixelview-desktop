// Offline real-libobs async SDR ramp -> main GPU texture readback.
// No app, capture plugins, audio, network, or persisted configuration.
#import <Foundation/Foundation.h>
#include <obs.h>
#include <util/platform.h>
#include <set>
#include <vector>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <cassert>
struct CanvasPolicy {
 bool pixelviewReceivePrecision=false,pixelviewReceivePrecisionFault=false;
 obs_video_info pixelviewSendVideo={}; std::string pixelviewSendRenderModule;
 bool PixelviewReceiveVideoBusy() const { return obs_video_active(); }
 bool set(bool enabled) { /* PRODUCTION_PRECISION_BODY */ }
};
extern "C" void pixelview_precision_start(obs_source_t*,const char*);
extern "C" unsigned pixelview_precision_stop(void);
static constexpr unsigned W=1024,H=128;
static gs_stagesurf_t *stage;
static std::atomic<unsigned> levels{0}, reads{0};
static const char *name(void*) { return "Synthetic precision ramp"; }
static void *create(obs_data_t*,obs_source_t *s) { return s; }
static void destroy(void*) {}
static void rendered(void*) {
 auto *tex=obs_get_main_texture(); if(!tex) return;
 auto format=gs_texture_get_color_format(tex);
 if(!stage) stage=gs_stagesurface_create(W,H,format);
 gs_stage_texture(stage,tex);
 uint8_t *data; uint32_t stride;
 if(gs_stagesurface_map(stage,&data,&stride)) {
  std::set<unsigned> unique;
  for(unsigned x=0;x<877;x++) unique.insert(format==GS_RGBA16F ? ((uint16_t*)data)[x*4] : data[x*4+2]);
  levels=unique.size(); reads++;
  gs_stagesurface_unmap(stage);
 }
}
int main(int argc,char **argv) { @autoreleasepool {
 if(argc!=5) return 2;
 if(!obs_startup("en-US",nullptr,nullptr)) return 3;
 obs_add_data_path(argv[1]);
 obs_video_info vi={}; vi.graphics_module=argv[2]; vi.fps_num=30;vi.fps_den=1;
 vi.base_width=vi.output_width=W;vi.base_height=vi.output_height=H;
 vi.output_format=VIDEO_FORMAT_NV12;
 vi.colorspace=VIDEO_CS_709;vi.range=VIDEO_RANGE_PARTIAL;vi.gpu_conversion=true;vi.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS) return 4;
 CanvasPolicy policy; if(!strcmp(argv[3],"P010")) assert(policy.set(true));
 obs_source_info si={};si.id="precision-ramp";si.type=OBS_SOURCE_TYPE_INPUT;si.output_flags=OBS_SOURCE_ASYNC_VIDEO;
 si.get_name=name;si.create=create;si.destroy=destroy;obs_register_source(&si);
 auto *s=obs_source_create_private(si.id,"ramp",nullptr); if(!s) return 5;
 obs_set_output_source(0,s);
 obs_add_main_rendered_callback(rendered,nullptr);
 pixelview_precision_start(s,argv[4]);
 for(unsigned i=0;i<120;i++) [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1./30.]];
 unsigned delivered=pixelview_precision_stop();
 printf("CALLBACK frames=%u\n",delivered);
 obs_remove_main_rendered_callback(rendered,nullptr);
 unsigned n=levels.load(),r=reads.load();
 printf("RESULT input=%s canvas=%s transfer=SDR709 unique_red=%u input_legal_codes=877 readbacks=%u\n",argv[4],argv[3],n,r);
 obs_set_output_source(0,nullptr);obs_source_release(s);
 obs_enter_graphics();gs_stagesurface_destroy(stage);obs_leave_graphics();
 assert(policy.set(false)); obs_video_info restored={}; assert(obs_get_video_info(&restored) && restored.output_format==VIDEO_FORMAT_NV12 && restored.colorspace==VIDEO_CS_709);
 obs_shutdown();
 if (getenv("PIXELVIEW_EXPECT_REJECT")) return delivered==0?0:8;
 return delivered>0 && r>0 && (!strcmp(argv[3],"P010")?n==877:n<=256 && n>1)?0:7;
}}
