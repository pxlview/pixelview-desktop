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
static constexpr unsigned W=1024,H=32;
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
 vi.output_format=!strcmp(argv[3],"P010")?VIDEO_FORMAT_P010:VIDEO_FORMAT_NV12;
 vi.colorspace=VIDEO_CS_709;vi.range=VIDEO_RANGE_PARTIAL;vi.gpu_conversion=true;vi.scale_type=OBS_SCALE_DISABLE;
 if(obs_reset_video(&vi)!=OBS_VIDEO_SUCCESS) return 4;
 obs_source_info si={};si.id="precision-ramp";si.type=OBS_SOURCE_TYPE_INPUT;si.output_flags=OBS_SOURCE_ASYNC_VIDEO;
 si.get_name=name;si.create=create;si.destroy=destroy;obs_register_source(&si);
 auto *s=obs_source_create_private(si.id,"ramp",nullptr); if(!s) return 5;
 obs_set_output_source(0,s);
 obs_add_main_rendered_callback(rendered,nullptr);
 bool planar=!strcmp(argv[4],"I210");
 std::vector<uint16_t> y(W*H), uv(W*H,512), u(W*H/2,512),v(W*H/2,512);
 for(unsigned j=0;j<H;j++)for(unsigned x=0;x<W;x++)y[j*W+x]=(64+x%877)*(planar?1:64);
 if(!planar) for(auto &c:uv)c=512*64;
 obs_source_frame2 f={};f.width=W;f.height=H;f.format=planar?VIDEO_FORMAT_I210:VIDEO_FORMAT_P010;
 f.range=VIDEO_RANGE_PARTIAL;f.trc=VIDEO_TRC_SRGB;
 f.data[0]=(uint8_t*)y.data();f.linesize[0]=W*2;
 f.data[1]=(uint8_t*)(planar?u.data():uv.data());f.linesize[1]=planar?W:W*2;
 if(planar){f.data[2]=(uint8_t*)v.data();f.linesize[2]=W;}
 if(!video_format_get_parameters_for_format(VIDEO_CS_709,f.range,f.format,f.color_matrix,f.color_range_min,f.color_range_max))return 6;
 for(unsigned i=0;i<90;i++){
  f.timestamp=os_gettime_ns();obs_source_output_video2(s,&f);
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1./30.]];
 }
 obs_remove_main_rendered_callback(rendered,nullptr);
 unsigned n=levels.load(),r=reads.load();
 printf("RESULT input=%s canvas=%s transfer=SDR709 unique_red=%u input_legal_codes=877 readbacks=%u\n",argv[4],argv[3],n,r);
 obs_set_output_source(0,nullptr);obs_source_release(s);
 obs_enter_graphics();gs_stagesurface_destroy(stage);obs_leave_graphics();obs_shutdown();
 return r>0 && (vi.output_format==VIDEO_FORMAT_P010?n>256:n<=256 && n>1)?0:7;
}}
