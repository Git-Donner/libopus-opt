/* Public API integration and bitstream regression test, not a benchmark. */
#include <opus.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <asm/hwcap.h>
#include <linux/prctl.h>
static unsigned long sve_entries, sve256_entries;
static int reference_mode;
float __real_op_pvq_search_sve256(float *,int *,int,int,int);
float op_pvq_search_neon(float *,int *,int,int,int);
float __wrap_op_pvq_search_sve256(float *x,int *iy,int k,int n,int arch) {
  if(reference_mode) return op_pvq_search_neon(x,iy,k,n,arch);
  sve_entries++;
  if((prctl(PR_SVE_GET_VL,0,0,0,0)&PR_SVE_VL_LEN_MASK)==32 && n>=8) sve256_entries++;
  return __real_op_pvq_search_sve256(x,iy,k,n,arch);
}
int main(int argc,char **argv) {
  int have_sve=!!(getauxval(AT_HWCAP)&HWCAP_SVE);
  int bytes=argc>1?atoi(argv[1]):0;
  int channels,scenario,i,j,err;
  uint32_t seed=42;
  if(have_sve && bytes && (prctl(PR_SVE_SET_VL,bytes,0,0,0)&PR_SVE_VL_LEN_MASK)!=bytes) return 77;
  for(channels=1;channels<=2;channels++) for(scenario=0;scenario<3;scenario++) {
    const int framesize[3]={120,480,960};
    OpusEncoder *enc[2]; OpusDecoder *dec[2];
    opus_int16 pcm[1920],out[2][1920]; unsigned char packet[2][1500];
    int frame=framesize[scenario],len[2];
    for(i=0;i<2;i++) {
      enc[i]=opus_encoder_create(48000,channels,OPUS_APPLICATION_RESTRICTED_CELT,&err);
      if(!enc[i]||err) return 1;
      dec[i]=opus_decoder_create(48000,channels,&err); if(!dec[i]||err)return 2;
      if(opus_encoder_ctl(enc[i],OPUS_SET_BITRATE(64000*channels))||
         opus_encoder_ctl(enc[i],OPUS_SET_COMPLEXITY(5)))return 3;
    }
    for(i=0;i<50;i++) {
      opus_uint32 erange[2],drange[2];
      for(j=0;j<frame*channels;j++) {seed=1664525U*seed+1013904223U;pcm[j]=(opus_int16)((int)(seed>>16)/4-8192);}
      for(j=0;j<2;j++) {
        reference_mode=j;
        len[j]=opus_encode(enc[j],pcm,frame,packet[j],sizeof(packet[j]));
        if(len[j]<=0||!(packet[j][0]&0x80))return 4;
        if(opus_decode(dec[j],packet[j],len[j],out[j],frame,0)!=frame)return 5;
        if(opus_encoder_ctl(enc[j],OPUS_GET_FINAL_RANGE(&erange[j]))||
           opus_decoder_ctl(dec[j],OPUS_GET_FINAL_RANGE(&drange[j]))||erange[j]!=drange[j])return 6;
      }
      if(len[0]!=len[1]||memcmp(packet[0],packet[1],len[0])||
         memcmp(out[0],out[1],frame*channels*sizeof(opus_int16))||erange[0]!=erange[1]) {
        fprintf(stderr,"FAIL: SVE/NEON packet/PCM/range mismatch ch=%d frame=%d run=%d\n",channels,frame,i); return 7;
      }
    }
    for(i=0;i<2;i++) {opus_encoder_destroy(enc[i]);opus_decoder_destroy(dec[i]);}
  }
  if(!have_sve && sve_entries) return 8;
  if(have_sve && bytes==32 && !sve256_entries) return 9;
  if((!have_sve||bytes==16||bytes==64)&&sve256_entries)return 10;
  printf("PASS: 300 paired CELT frames mono/stereo 2.5/10/20ms; packets, PCM, final ranges exactly match NEON; HWCAP_SVE=%d VL=%d bytes entries=%lu actual256=%lu\n",have_sve,bytes,sve_entries,sve256_entries);
  return 0;
}
