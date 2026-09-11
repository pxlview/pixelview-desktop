/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Nonshipping observer: independently record native samples before packing. */
#include <CoreVideo/CoreVideo.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdatomic.h>
#include <CommonCrypto/CommonDigest.h>
#include <compression.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <mach-o/dyld.h>
#include <limits.h>
static pid_t observer_pid;
static int observer_owner(void) { return observer_pid && observer_pid==getpid(); }
static void select_observer_owner(void)
{
 const char *expected=getenv("PV_NATIVE422_OBSERVER_EXECUTABLE");
 char executable[PATH_MAX];uint32_t size=sizeof(executable);struct stat a,b;
 if(!expected || _NSGetExecutablePath(executable,&size) || stat(executable,&a) || stat(expected,&b) ||
    a.st_dev!=b.st_dev || a.st_ino!=b.st_ino)return;
 observer_pid=getpid();
}
static uint64_t observer_clock(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC); }
/* Read-only independent reference; digest lookup is only an index. Acceptance
 * always memcmps every sample byte, including when hashes collide. Prepared
 * before main/receiver startup, stored as owned lossless LZ4 (256MiB cap),
 * never mmap/page in the multi-GB RAW reference on the decoding thread. */
#define MAX_REFERENCE_FRAMES 512
static uint8_t *reference[MAX_REFERENCE_FRAMES];
static size_t compressed_bytes[MAX_REFERENCE_FRAMES], total_reference_bytes;
static _Thread_local uint8_t *reference_frame;
static size_t reference_frame_bytes, reference_count;
static unsigned char reference_hashes[MAX_REFERENCE_FRAMES][CC_SHA256_DIGEST_LENGTH];
static atomic_uint reference_matches;
__attribute__((constructor)) static void load_reference(void)
{
 select_observer_owner();if(!observer_owner())return;
 const char *path=getenv("PV_NATIVE422_REFERENCE");if(!path)return;
 const char *size=getenv("PV_NATIVE422_REFERENCE_FRAME_BYTES");assert(size);
 reference_frame_bytes=strtoull(size,NULL,10);assert(reference_frame_bytes && reference_frame_bytes<=UINT32_MAX);
 int fd=open(path,O_RDONLY);assert(fd>=0);struct stat st;assert(!fstat(fd,&st) && st.st_size>0);
 size_t bytes=(size_t)st.st_size;assert(bytes%reference_frame_bytes==0);
 reference_count=bytes/reference_frame_bytes;assert(reference_count<=MAX_REFERENCE_FRAMES);
 FILE *file=fdopen(fd,"rb");assert(file);
 uint8_t *raw=malloc(reference_frame_bytes), *compressed=malloc(reference_frame_bytes+65536);assert(raw && compressed);
 for(size_t i=0;i<reference_count;i++) {
  assert(fread(raw,1,reference_frame_bytes,file)==reference_frame_bytes);
  assert(CC_SHA256(raw,(CC_LONG)reference_frame_bytes,reference_hashes[i]));
  size_t n=compression_encode_buffer(compressed,reference_frame_bytes+65536,raw,reference_frame_bytes,NULL,COMPRESSION_LZ4);
  assert(n && total_reference_bytes+n<=256*1024*1024);
  reference[i]=malloc(n);assert(reference[i]);memcpy(reference[i],compressed,n);
  compressed_bytes[i]=n;total_reference_bytes+=n;
 }
 free(raw);free(compressed);assert(!fclose(file));
 fprintf(stderr,"NATIVE_REFERENCE frames=%zu owned_compressed_bytes=%zu\n",reference_count,total_reference_bytes);
}
/* Only one previous owned native image per decoding thread, never a disk-sized
 * RAW backlog. Two production read locks must still compare byte-for-byte. */
static _Thread_local uint8_t *paired_bytes;
static _Thread_local size_t paired_size;
static _Thread_local CVPixelBufferRef paired_image;
static atomic_uint pending_pairs, lock_count, record_count;
#define MAX_RECORDS 4096
static unsigned char records[MAX_RECORDS][CC_SHA256_DIGEST_LENGTH];
static uint64_t campaign_times[MAX_RECORDS][5];
static _Thread_local uint64_t first_extract;
static atomic_bool campaign_complete[MAX_RECORDS],campaign_flushed;
/* Called at terminal error or after teardown, never per-frame I/O. Publication
 * flags prevent reading an in-progress record from another callback thread. */
void pv_native422_observer_flush(void)
{
 if(!observer_owner() || !getenv("PV_BOUNDED_CAMPAIGN") || atomic_exchange(&campaign_flushed,1))return;
 unsigned count=atomic_load(&record_count);assert(count<=2048);
 for(unsigned j=0;j<count;j++) if(atomic_load(&campaign_complete[j]))
  fprintf(stderr,"CAMPAIGN_REFERENCE %u %llu %llu %llu %llu %llu\n",j,campaign_times[j][0],campaign_times[j][1],campaign_times[j][2],campaign_times[j][3],campaign_times[j][4]);
}
__attribute__((destructor)) static void verify_pairs(void)
{
 if(!observer_owner())return;
 assert(!atomic_load(&pending_pairs));
 unsigned count=atomic_load(&record_count);
 const char *path=getenv("PV_NATIVE422_X422_HASHES");
 if(!path) return;
 assert(atomic_load(&lock_count)==2*count);
 /* Exclusive creation fails closed on duplicate owners or preexisting
  * evidence, including symlinks. Never truncate another process's record. */
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);assert(fd>=0);
 FILE *file=fdopen(fd,"wb");assert(file);
 for(unsigned j=0;j<count;j++) {
  for(unsigned i=0;i<CC_SHA256_DIGEST_LENGTH;i++) assert(fprintf(file,"%02x",records[j][i])==2);
  assert(fputc('\n',file)!=EOF);

 }
 assert(fclose(file)==0);pv_native422_observer_flush();
 assert(!reference_count || atomic_load(&reference_matches)==count);
 fprintf(stderr,"NATIVE_OBSERVER locks=%u pairs=%u pending=0 exact=%u\n",atomic_load(&lock_count),count,atomic_load(&reference_matches));
 for(size_t i=0;i<reference_count;i++)free(reference[i]);
}
static CVReturn observe_lock(CVPixelBufferRef image, CVPixelBufferLockFlags flags)
{
 if(!observer_owner())return CVPixelBufferLockBaseAddress(image,flags);
 int trace=getenv("PV_MATRIX_TRACE")!=NULL;
 uint64_t started=observer_clock(), extracted=0, hashed=0;
 if(trace) fprintf(stderr,"MATRIX_OBSERVER lock_enter\n");
 CVReturn status = CVPixelBufferLockBaseAddress(image, flags);
 if(trace) fprintf(stderr,"MATRIX_OBSERVER lock_exit\n");
 const char *path = getenv("PV_NATIVE422_X422_DUMP");
 const char *hash_path = getenv("PV_NATIVE422_X422_HASHES");
 if (!status && (path || hash_path) && CVPixelBufferGetPixelFormatType(image) == kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange) {
  assert(!(path && hash_path));
  assert(CVPixelBufferGetPlaneCount(image) == 2);
  size_t width = CVPixelBufferGetWidth(image), height = CVPixelBufferGetHeight(image);
  uint8_t *copy=malloc(width*height*4); assert(copy); size_t offset=0;
  for (unsigned component = 0; component < 3; component++) {
   size_t stride = CVPixelBufferGetBytesPerRowOfPlane(image, component ? 1 : 0);
   const uint8_t *base = CVPixelBufferGetBaseAddressOfPlane(image, component ? 1 : 0);
   for (size_t row = 0; row < height; row++) for (size_t x = 0; x < (component ? width / 2 : width); x++) {
    const uint8_t *p = base + row * stride + (component ? x * 4 + (component - 1) * 2 : x * 2);
    unsigned word = p[0] | (unsigned)p[1] << 8;
    assert(!(word & 63));
    uint8_t sample[2] = {(word >> 6) & 255, word >> 14};
    copy[offset++]=sample[0]; copy[offset++]=sample[1];
   }
  }
  assert(offset==width*height*4);
  if(hash_path) {
   atomic_fetch_add(&lock_count,1);
   extracted=observer_clock();
   if(!paired_bytes) {
    first_extract=extracted-started;
    if(getenv("PV_OBSERVER_TIMING"))fprintf(stderr,"OBSERVER_TIMING first_extract=%llu\n",(unsigned long long)(extracted-started));
    paired_bytes=copy; paired_size=offset; paired_image=image; atomic_fetch_add(&pending_pairs,1);
    return status;
   }
   assert(paired_image==image && paired_size==offset && !memcmp(paired_bytes,copy,offset));
   unsigned char digest[CC_SHA256_DIGEST_LENGTH];
   assert(offset<=UINT32_MAX && CC_SHA256(copy,(CC_LONG)offset,digest));
   hashed=observer_clock();
   if(reference_count) {
    assert(offset==reference_frame_bytes);int match=0;
    reference_frame=malloc(offset);assert(reference_frame);
    for(size_t i=0;i<reference_count;i++) {
     if(memcmp(digest,reference_hashes[i],sizeof(digest)))continue;
     assert(compression_decode_buffer(reference_frame,offset,reference[i],compressed_bytes[i],NULL,COMPRESSION_LZ4)==offset);
     if(!memcmp(copy,reference_frame,offset)) {
      if(getenv("PV_CAMPAIGN_REFERENCE_REPEAT")) {
       assert(compression_decode_buffer(reference_frame,offset,reference[i],compressed_bytes[i],NULL,COMPRESSION_LZ4)==offset);
       assert(!memcmp(copy,reference_frame,offset));
      }
      match=1;break;
     }
    }
    free(reference_frame);reference_frame=NULL;
    assert(match && "independent reference bytes differ");atomic_fetch_add(&reference_matches,1);
   }
   if(getenv("PV_OBSERVER_TIMING"))fprintf(stderr,"OBSERVER_TIMING second_extract=%llu pair_hash=%llu reference=%llu\n",(unsigned long long)(extracted-started),(unsigned long long)(hashed-extracted),(unsigned long long)(observer_clock()-hashed));
   unsigned slot=atomic_fetch_add(&record_count,1);assert(slot<MAX_RECORDS);
   assert(!getenv("PV_BOUNDED_CAMPAIGN") || slot<2048);
   uint64_t measured[]={observer_clock(),first_extract,extracted-started,hashed-extracted,observer_clock()-hashed};
   memcpy(campaign_times[slot],measured,sizeof(measured));
   memcpy(records[slot],digest,sizeof(digest));
   atomic_store(&campaign_complete[slot],1);
   free(paired_bytes);paired_bytes=NULL;paired_size=0;paired_image=NULL;free(copy);
   atomic_fetch_sub(&pending_pairs,1);
   return status;
  }
  FILE *file=fopen(path,"ab");assert(file);
  if(trace) fprintf(stderr,"MATRIX_OBSERVER write_enter\n");
  assert(fwrite(copy,offset,1,file)==1);
  if(trace) fprintf(stderr,"MATRIX_OBSERVER write_exit\n");
  free(copy);
  if(trace) fprintf(stderr,"MATRIX_OBSERVER close_enter\n");
  fclose(file);
  if(trace) fprintf(stderr,"MATRIX_OBSERVER close_exit\n");
 }
 return status;
}
__attribute__((used, section("__DATA,__interpose"))) static struct { const void *replacement, *original; } hook = {
 (const void *)observe_lock, (const void *)CVPixelBufferLockBaseAddress
};
