#include <opus.h>
#include <stdio.h>
#include <stdint.h>
static unsigned long calls;
float __real_op_pvq_search_neon(float *,int *,int,int,int);
float __wrap_op_pvq_search_neon(float *x,int *iy,int k,int n,int arch) {
  calls++;
  return __real_op_pvq_search_neon(x,iy,k,n,arch);
}
int main(void) {
  OpusEncoder *enc; OpusDecoder *dec; int err,i,j,nb,n;
  opus_int16 pcm[960],out[960]; unsigned char packet[1500];
  uint32_t seed=42; opus_uint32 erange,drange;
  enc=opus_encoder_create(48000,1,OPUS_APPLICATION_RESTRICTED_CELT,&err);
  if(!enc||err) return 1;
  dec=opus_decoder_create(48000,1,&err); if(!dec||err)return 2;
  if(opus_encoder_ctl(enc,OPUS_SET_BITRATE(64000))||
     opus_encoder_ctl(enc,OPUS_SET_COMPLEXITY(5)))return 3;
  for(i=0;i<50;i++) {
    for(j=0;j<960;j++) {seed=1664525U*seed+1013904223U;pcm[j]=(opus_int16)((int)(seed>>16)/4-8192);}
    nb=opus_encode(enc,pcm,960,packet,sizeof(packet));
    if(nb<=0||!(packet[0]&0x80))return 4;
    n=opus_decode(dec,packet,nb,out,960,0);if(n!=960)return 5;
    if(opus_encoder_ctl(enc,OPUS_GET_FINAL_RANGE(&erange))||
       opus_decoder_ctl(dec,OPUS_GET_FINAL_RANGE(&drange))||erange!=drange)return 6;
  }
  opus_encoder_destroy(enc);opus_decoder_destroy(dec);
  printf("PASS: 50 actual CELT encoder/decoder frames; NEON PVQ calls=%lu; frame counts/final ranges match\n",calls);
  return calls?0:7;
}
