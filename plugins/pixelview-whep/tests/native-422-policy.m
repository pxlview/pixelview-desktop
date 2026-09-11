/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native-422.m"
#include <assert.h>
int main(void) {
 gst_init(NULL,NULL);struct pv_native422 *d=pv_native422_create();
 guint8 bytes[]={0,0,0,3,2,1,0x80,0,0,0,4,0x4c,1,0xff,0x80};
 GstMapInfo map={0};map.data=bytes;map.size=sizeof(bytes);
 assert(valid_access_unit(d,&map));
 bytes[13]=0xfe;assert(!valid_access_unit(d,&map));bytes[13]=0xff;
 bytes[14]=0;assert(!valid_access_unit(d,&map));
 assert(timing_matches(30000,1001,30000,1001,FALSE,0));
 assert(!timing_matches(30000,1001,30,1,FALSE,0));
 assert(!timing_matches(24,1,24,1,TRUE,UINT32_MAX));
 pv_native422_destroy(d);return 0;
}
