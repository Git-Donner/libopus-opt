/* Native measurement worker. The Python frontend validates JSON and schedules
 * independent runs. No Python, allocation or PCM generation is timed here. */
#define _GNU_SOURCE
#include <opus.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_PACKET 7656
#define MAX_MEMORY (256u*1024u*1024u)
#define FORCE_MODE_REQUEST 11002
#define FNV_INIT UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)

/* Supplied by verify_calls.c only in the separate coverage executable. */
__attribute__((weak)) void bench_probe_reset(void) {}
__attribute__((weak)) void bench_probe_read(uint64_t c[4]) { memset(c,0,4*sizeof(*c)); }

typedef struct {
    int mode, decode, fs, channels, frame, bitrate, complexity, bandwidth;
    int frames, warmup, cpu, signal, prepare, verify;
    uint32_t seed;
    const char *fixture;
} Options;
typedef struct { unsigned char *bytes; uint32_t len, range; } Packet;
static char error_text[512];
static void fail(const char *fmt,...) { va_list a;va_start(a,fmt);vsnprintf(error_text,sizeof(error_text),fmt,a);va_end(a); }
static uint64_t hash_bytes(uint64_t h,const void *p,size_t n) {
    const unsigned char *b=p;while(n--){h^=*b++;h*=FNV_PRIME;}return h;
}
static uint64_t hash_u32(uint64_t h,uint32_t x) {
    unsigned char b[4]={(unsigned char)x,(unsigned char)(x>>8),(unsigned char)(x>>16),(unsigned char)(x>>24)};
    return hash_bytes(h,b,4);
}
static uint64_t now_ns(clockid_t id) { struct timespec t;if(clock_gettime(id,&t)){perror("clock_gettime");exit(1);}return(uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec; }
static const char *mode_name(int x){return x==1000?"silk":x==1001?"hybrid":"celt";}
static int packet_mode(const unsigned char *p){return p[0]&0x80?1002:(p[0]&0x60)==0x60?1001:1000;}
static int number(const char *s,int *out){char *e;errno=0;long v=strtol(s,&e,10);if(errno||!s[0]||*e||v<0||v>INT_MAX)return 0;*out=(int)v;return 1;}
static int parse(int argc,char **argv,Options *o){
    *o=(Options){.mode=1000,.fs=48000,.channels=1,.frame=960,.bitrate=16000,.complexity=5,.bandwidth=OPUS_BANDWIDTH_WIDEBAND,.frames=1000,.warmup=100,.cpu=-1,.seed=42};
    for(int i=1;i<argc;i++){
        const char *k=argv[i];
        if(!strcmp(k,"--prepare")){o->prepare=1;continue;}
        if(!strcmp(k,"--verify-only")){o->verify=1;continue;}
        if(i+1==argc){fail("missing value for %s",k);return 0;}
        const char *v=argv[++i];int *dst=NULL;
        if(!strcmp(k,"--mode")){if(!strcmp(v,"silk"))o->mode=1000;else if(!strcmp(v,"hybrid"))o->mode=1001;else if(!strcmp(v,"celt"))o->mode=1002;else goto bad;}
        else if(!strcmp(k,"--operation")){if(!strcmp(v,"encode"))o->decode=0;else if(!strcmp(v,"decode"))o->decode=1;else goto bad;}
        else if(!strcmp(k,"--bandwidth")){if(!strcmp(v,"WIDEBAND"))o->bandwidth=OPUS_BANDWIDTH_WIDEBAND;else if(!strcmp(v,"SUPERWIDEBAND"))o->bandwidth=OPUS_BANDWIDTH_SUPERWIDEBAND;else if(!strcmp(v,"FULLBAND"))o->bandwidth=OPUS_BANDWIDTH_FULLBAND;else goto bad;}
        else if(!strcmp(k,"--signal")){if(!strcmp(v,"mixed"))o->signal=0;else if(!strcmp(v,"sine"))o->signal=1;else if(!strcmp(v,"noise"))o->signal=2;else if(!strcmp(v,"impulse"))o->signal=3;else goto bad;}
        else if(!strcmp(k,"--fixture"))o->fixture=v;
        else if(!strcmp(k,"--sample-rate"))dst=&o->fs;
        else if(!strcmp(k,"--channels"))dst=&o->channels;
        else if(!strcmp(k,"--frame-size"))dst=&o->frame;
        else if(!strcmp(k,"--bitrate"))dst=&o->bitrate;
        else if(!strcmp(k,"--complexity"))dst=&o->complexity;
        else if(!strcmp(k,"--frames"))dst=&o->frames;
        else if(!strcmp(k,"--warmup"))dst=&o->warmup;
        else if(!strcmp(k,"--cpu"))dst=&o->cpu;
        else if(!strcmp(k,"--seed")){char *e;errno=0;unsigned long long n=strtoull(v,&e,10);if(errno||v[0]<'0'||v[0]>'9'||*e||n<1||n>UINT32_MAX)goto bad;o->seed=(uint32_t)n;}
        else {fail("unknown worker option %s",k);return 0;}
        if(dst&&!number(v,dst))goto bad;
        continue;
    bad:fail("invalid value %s for %s",v,k);return 0;
    }
    if(o->cpu<0||o->cpu>=CPU_SETSIZE){fail("--cpu is required and must fit CPU_SETSIZE");return 0;}
    if(o->fs!=8000&&o->fs!=12000&&o->fs!=16000&&o->fs!=24000&&o->fs!=48000){fail("unsupported sample rate");return 0;}
    if(o->channels<1||o->channels>2||o->frames<1||o->frames>100000||o->warmup<1||o->warmup>100000||o->complexity>10||o->bitrate<6000||o->bitrate>256000){fail("case parameters outside supported bounds");return 0;}
    if(o->frame<o->fs/400||o->frame>3*o->fs/50||
       (400*o->frame!=o->fs&&200*o->frame!=o->fs&&100*o->frame!=o->fs&&50*o->frame!=o->fs&&25*o->frame!=o->fs&&50*o->frame!=3*o->fs)){
        fail("unsupported frame duration");return 0;
    }
    if(o->mode!=1002&&o->frame<o->fs/100){fail("SILK/HYBRID require >=10ms frames");return 0;}
    if((o->bandwidth==OPUS_BANDWIDTH_WIDEBAND&&o->fs<16000)||(o->bandwidth==OPUS_BANDWIDTH_SUPERWIDEBAND&&o->fs<24000)||(o->bandwidth==OPUS_BANDWIDTH_FULLBAND&&o->fs<48000)){fail("bandwidth exceeds sample-rate Nyquist limit");return 0;}
    if(o->mode==1000&&o->bandwidth!=OPUS_BANDWIDTH_WIDEBAND){fail("SILK requires WIDEBAND");return 0;}
    if(o->mode==1001&&(o->fs<24000||o->frame>o->fs/50||o->bandwidth==OPUS_BANDWIDTH_WIDEBAND||o->bitrate<16000)){fail("HYBRID requires >=24kHz, 10/20ms, SWB/FB and bitrate >=16000");return 0;}
    if((o->decode||o->prepare)&&(!o->fixture||!o->fixture[0])){fail("--fixture is required for decode/prepare");return 0;}
    size_t n=(size_t)o->frames+(size_t)o->warmup;
    if(n*MAX_PACKET>MAX_MEMORY||n*(size_t)o->frame*(size_t)o->channels*sizeof(opus_int16)>MAX_MEMORY){fail("case exceeds 256 MiB buffer limit; reduce frames/warmup");return 0;}
    return 1;
}
static int check_cpu(int cpu){
    cpu_set_t s,got;CPU_ZERO(&s);CPU_SET(cpu,&s);
    if(sched_getaffinity(0,sizeof(got),&got)||!CPU_EQUAL(&s,&got)||sched_getcpu()!=cpu){fail("CPU affinity verification failed");return 0;}
    return 1;
}
static int bind_cpu(int cpu){
    cpu_set_t s;CPU_ZERO(&s);CPU_SET(cpu,&s);
    if(sched_setaffinity(0,sizeof(s),&s)){fail("cannot bind CPU %d: %s",cpu,strerror(errno));return 0;}
    return check_cpu(cpu);
}
static uint32_t rnd(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
static opus_int16 *make_pcm(const Options *o,uint64_t *digest){
    size_t frames=(size_t)o->warmup+(size_t)o->frames;
    size_t count=frames*(size_t)o->frame*(size_t)o->channels;
    opus_int16 *p=malloc(count*sizeof(*p));if(!p){fail("PCM allocation failed");return NULL;}
    uint32_t state=o->seed;uint64_t h=FNV_INIT;
    for(size_t i=0;i<frames*(size_t)o->frame;i++){
        double t=(double)i/o->fs;
        for(int ch=0;ch<o->channels;ch++){
            double n=(double)(rnd(&state)>>8)/16777216.0*2.0-1.0;
            double f=180.0+ch*67.0;
            double x;
            if(o->signal==1)x=.45*sin(2*M_PI*(440.0+ch*113)*t);
            else if(o->signal==2)x=.30*n;
            else if(o->signal==3)x=(i%(size_t)(o->fs/50+ch*13)==0)?.65:0;
            else {
                double env=.65+.25*sin(2*M_PI*3*t+ch*.7);
                x=env*(.34*sin(2*M_PI*f*t+.4*sin(2*M_PI*1.1*t))+.12*sin(2*M_PI*(1100+ch*173)*t))
                    +.07*sin(2*M_PI*(o->fs>=24000?6500:2700)*t+ch)+.025*n;
            }
            int v=(int)lrint(x*32767.0);if(v>32767)v=32767;if(v< -32768)v=-32768;
            p[i*(size_t)o->channels+(size_t)ch]=(opus_int16)v;
            unsigned char b[2]={(unsigned char)v,(unsigned char)((unsigned)v>>8)};h=hash_bytes(h,b,2);
        }
    }
    *digest=h;return p;
}
static int ctl_check(int e,const char *name){if(e!=OPUS_OK){fail("encoder %s failed: %s",name,opus_strerror(e));return 0;}return 1;}
static OpusEncoder *make_encoder(const Options *o){
    int e,app=o->mode==1000?OPUS_APPLICATION_RESTRICTED_SILK:o->mode==1002?OPUS_APPLICATION_RESTRICTED_CELT:OPUS_APPLICATION_VOIP;
    OpusEncoder *enc=opus_encoder_create(o->fs,o->channels,app,&e);
    if(!enc||e){fail("encoder creation failed: %s",opus_strerror(e));return NULL;}
#define CTL(x) do{if(!ctl_check((x),#x)){opus_encoder_destroy(enc);return NULL;}}while(0)
    CTL(opus_encoder_ctl(enc,OPUS_SET_BITRATE(o->bitrate)));
    CTL(opus_encoder_ctl(enc,OPUS_SET_COMPLEXITY(o->complexity)));
    CTL(opus_encoder_ctl(enc,OPUS_SET_FORCE_CHANNELS(o->channels)));
    CTL(opus_encoder_ctl(enc,OPUS_SET_BANDWIDTH(o->bandwidth)));
    CTL(opus_encoder_ctl(enc,OPUS_SET_MAX_BANDWIDTH(o->bandwidth)));
    CTL(opus_encoder_ctl(enc,OPUS_SET_VBR(0)));
    CTL(opus_encoder_ctl(enc,OPUS_SET_DTX(0)));
    CTL(opus_encoder_ctl(enc,OPUS_SET_INBAND_FEC(0)));
    CTL(opus_encoder_ctl(enc,FORCE_MODE_REQUEST,o->mode));
#undef CTL
    return enc;
}
static int write32(FILE *f,uint32_t x){unsigned char b[4]={(unsigned char)x,(unsigned char)(x>>8),(unsigned char)(x>>16),(unsigned char)(x>>24)};return fwrite(b,1,4,f)==4;}
static int read32(FILE *f,uint32_t *x){unsigned char b[4];if(fread(b,1,4,f)!=4)return 0;*x=(uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;return 1;}
static void metadata(const Options *o,uint32_t a[13]){
    uint32_t b[13]={1,(uint32_t)o->mode,(uint32_t)o->fs,(uint32_t)o->channels,(uint32_t)o->frame,(uint32_t)o->bitrate,(uint32_t)o->complexity,(uint32_t)o->bandwidth,(uint32_t)o->frames,(uint32_t)o->warmup,o->seed,(uint32_t)o->signal,MAX_PACKET};memcpy(a,b,sizeof(b));
}
static uint64_t packet_digest(const Packet *p,int n){uint64_t h=FNV_INIT;for(int i=0;i<n;i++){h=hash_u32(h,p[i].len);h=hash_u32(h,p[i].range);h=hash_bytes(h,p[i].bytes,p[i].len);}return h;}
static int save_fixture(const Options *o,const Packet *p,uint64_t pcm_h,uint64_t pkt_h){
    int fd=open(o->fixture,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0){fail("cannot create new fixture %s: %s",o->fixture,strerror(errno));return 0;}
    FILE *f=fdopen(fd,"wb");int ok=1;if(!f){close(fd);unlink(o->fixture);fail("fdopen fixture failed");return 0;}
    uint32_t m[13];metadata(o,m);ok=fwrite("OPUSBEN2",1,8,f)==8;
    for(int i=0;i<13;i++)ok=write32(f,m[i])&&ok;
    ok=write32(f,(uint32_t)pcm_h)&&ok;ok=write32(f,(uint32_t)(pcm_h>>32))&&ok;
    ok=write32(f,(uint32_t)pkt_h)&&ok;ok=write32(f,(uint32_t)(pkt_h>>32))&&ok;
    for(int i=0;i<o->frames+o->warmup&&ok;i++)ok=write32(f,p[i].len)&&write32(f,p[i].range)&&fwrite(p[i].bytes,1,p[i].len,f)==p[i].len;
    if(fclose(f))ok=0;
    if(!ok){unlink(o->fixture);fail("fixture write failed");}return ok;
}
static int load_fixture(const Options *o,Packet *p,unsigned char *storage,uint64_t *pcm_h,uint64_t *pkt_h){
    FILE *f=fopen(o->fixture,"rb");if(!f){fail("cannot open fixture %s: %s",o->fixture,strerror(errno));return 0;}
    char magic[8];uint32_t m[13],expected[13],lo,hi;int ok=fread(magic,1,8,f)==8&&!memcmp(magic,"OPUSBEN2",8);
    metadata(o,expected);
    for(int i=0;i<13&&ok;i++)ok=read32(f,&m[i])&&m[i]==expected[i];
    if(ok){ok=read32(f,&lo)&&read32(f,&hi);if(ok)*pcm_h=((uint64_t)hi<<32)|lo;}
    if(ok){ok=read32(f,&lo)&&read32(f,&hi);if(ok)*pkt_h=((uint64_t)hi<<32)|lo;}
    for(int i=0;i<o->frames+o->warmup&&ok;i++){
        p[i].bytes=storage+(size_t)i*MAX_PACKET;
        ok=read32(f,&p[i].len)&&read32(f,&p[i].range)&&p[i].len>0&&p[i].len<=MAX_PACKET;
        if(ok)ok=fread(p[i].bytes,1,p[i].len,f)==p[i].len;
    }
    if(ok)ok=fgetc(f)==EOF&&!ferror(f);
    fclose(f);
    if(ok)ok=packet_digest(p,o->frames+o->warmup)==*pkt_h;
    if(!ok)fail("fixture is truncated, corrupt, or does not match the complete case parameters");
    return ok;
}
static int check_packets(const Options *o,const Packet *p,int count){
    for(int i=0;i<count;i++){
        if(p[i].len<1||packet_mode(p[i].bytes)!=o->mode){fail("packet %d has wrong mode (expected %s)",i,mode_name(o->mode));return 0;}
        if(opus_packet_get_nb_samples(p[i].bytes,(opus_int32)p[i].len,o->fs)!=o->frame||opus_packet_get_nb_channels(p[i].bytes)!=o->channels||opus_packet_get_bandwidth(p[i].bytes)!=o->bandwidth){fail("packet %d duration/channels/bandwidth mismatch",i);return 0;}
    }return 1;
}
static int check_decode(OpusDecoder *dec,const Options *o,const Packet *p,opus_int16 *out,int i,uint64_t *h,int *nonzero){
    int n=opus_decode(dec,p->bytes,(opus_int32)p->len,out,o->frame,0);opus_uint32 range=0;
    if(n!=o->frame){fail("decode frame %d returned %d, expected %d",i,n,o->frame);return 0;}
    if(opus_decoder_ctl(dec,OPUS_GET_FINAL_RANGE(&range))!=OPUS_OK||range!=p->range){fail("decode final range mismatch at frame %d",i);return 0;}
    if(h){for(int j=0;j<n*o->channels;j++){if(out[j]&&nonzero)*nonzero=1;unsigned v=(uint16_t)out[j];unsigned char b[2]={(unsigned char)v,(unsigned char)(v>>8)};*h=hash_bytes(*h,b,2);}}
    return 1;
}
static int probes_ok(const Options *o,const uint64_t p[4]){
    if(o->prepare) return 1; /* Corpus generation is not a measured phase. */
#ifdef BENCH_PROBES
    int offset=o->decode?2:0;
    if(p[o->decode?0:2]||p[o->decode?1:3]){fail("unexpected opposite-phase operator calls in measurement");return 0;}
    if((o->mode!=1002&&p[offset]<(uint64_t)o->frames)||(o->mode!=1000&&p[offset+1]<(uint64_t)o->frames)){fail("required codec operator was not observed; wrapping may be unavailable with LTO");return 0;}
    if((o->mode==1000&&p[offset+1])||(o->mode==1002&&p[offset])){fail("unexpected codec operator in exclusive mode");return 0;}
#else
    (void)o;(void)p;
#endif
    return 1;
}
static void print_error(void){fputs("{\"status\":\"failed\",\"error\":\"",stdout);for(const char*p=error_text;*p;p++){if(*p=='"'||*p=='\\')putchar('\\');if((unsigned char)*p>=32)putchar(*p);}puts("\"}");fprintf(stderr,"bench_worker: %s\n",error_text);}
int main(int argc,char **argv){
    Options o;Packet *pkts=NULL;unsigned char *storage=NULL;opus_int16 *pcm=NULL,*out=NULL;
    OpusEncoder *enc=NULL;OpusDecoder *dec=NULL;uint64_t *samples=NULL,*ranges=NULL;
    uint64_t pcm_h=FNV_INIT,pkt_h=FNV_INIT,out_h=FNV_INIT,probes[4]={0},total_ns=0,loop_ns=0,cpu_ns=0;int rc=1,n=0,nonzero_output=0;
    if(!parse(argc,argv,&o)||!bind_cpu(o.cpu))goto done;
#ifndef BENCH_PROBES
    if(o.verify){fail("--verify-only requires the operator-probe worker");goto done;}
#endif
    n=o.frames+o.warmup;
    pkts=calloc((size_t)n,sizeof(*pkts));storage=malloc((size_t)n*MAX_PACKET);
    samples=calloc((size_t)o.frames,sizeof(*samples));ranges=calloc((size_t)n,sizeof(*ranges));
    out=malloc((size_t)o.frame*(size_t)o.channels*sizeof(*out));
    if(!pkts||!storage||!samples||!ranges||!out){fail("measurement buffer allocation failed");goto done;}
    for(int i=0;i<n;i++)pkts[i].bytes=storage+(size_t)i*MAX_PACKET;
    if(o.decode&&!o.prepare){
        if(!load_fixture(&o,pkts,storage,&pcm_h,&pkt_h)||!check_packets(&o,pkts,n))goto done;
    }else{
        pcm=make_pcm(&o,&pcm_h);if(!pcm)goto done;
        enc=make_encoder(&o);if(!enc)goto done;
    }
    if(o.decode&&!o.prepare){int e;dec=opus_decoder_create(o.fs,o.channels,&e);if(!dec||e){fail("decoder creation failed");goto done;}}
    /* Warmup processes the initial prefix of the same continuous stream. */
    for(int i=0;i<o.warmup;i++){
        if(o.decode&&!o.prepare){if(!check_decode(dec,&o,&pkts[i],out,i,NULL,NULL))goto done;}
        else{
            int nb=opus_encode(enc,pcm+(size_t)i*o.frame*o.channels,o.frame,pkts[i].bytes,MAX_PACKET);
            if(nb<=0){fail("warmup encode failed: %s",opus_strerror(nb));goto done;}pkts[i].len=(uint32_t)nb;
            if(opus_encoder_ctl(enc,OPUS_GET_FINAL_RANGE(&pkts[i].range))!=OPUS_OK){fail("GET_FINAL_RANGE failed");goto done;}
        }
    }
    bench_probe_reset();
    uint64_t cpu0=now_ns(CLOCK_THREAD_CPUTIME_ID),loop0=now_ns(CLOCK_MONOTONIC);
    for(int k=0;k<o.frames;k++){
        int i=k+o.warmup;
        if(o.decode&&!o.prepare){
            uint64_t t=o.verify?0:now_ns(CLOCK_MONOTONIC);
            int nf=opus_decode(dec,pkts[i].bytes,(opus_int32)pkts[i].len,out,o.frame,0);
            if(!o.verify)samples[k]=now_ns(CLOCK_MONOTONIC)-t;
            if(nf!=o.frame){fail("decode returned %d instead of %d at frame %d",nf,o.frame,i);goto done;}
            opus_uint32 range;
            if(opus_decoder_ctl(dec,OPUS_GET_FINAL_RANGE(&range))!=OPUS_OK||range!=pkts[i].range){fail("decode final range mismatch at frame %d",i);goto done;}
            ranges[i]=range;
            for(int j=0;j<nf*o.channels;j++){if(out[j])nonzero_output=1;unsigned v=(uint16_t)out[j];unsigned char b[2]={(unsigned char)v,(unsigned char)(v>>8)};out_h=hash_bytes(out_h,b,2);}
        }else{
            uint64_t t=o.verify?0:now_ns(CLOCK_MONOTONIC);
            int nb=opus_encode(enc,pcm+(size_t)i*o.frame*o.channels,o.frame,pkts[i].bytes,MAX_PACKET);
            if(!o.verify)samples[k]=now_ns(CLOCK_MONOTONIC)-t;
            if(nb<=0){fail("encode failed at frame %d: %s",i,opus_strerror(nb));goto done;}pkts[i].len=(uint32_t)nb;
            if(opus_encoder_ctl(enc,OPUS_GET_FINAL_RANGE(&pkts[i].range))!=OPUS_OK){fail("GET_FINAL_RANGE failed");goto done;}
        }
    }
    loop_ns=now_ns(CLOCK_MONOTONIC)-loop0;cpu_ns=now_ns(CLOCK_THREAD_CPUTIME_ID)-cpu0;
    bench_probe_read(probes);
    if(!check_cpu(o.cpu)||!probes_ok(&o,probes)||!check_packets(&o,pkts,n))goto done;
    if(!o.decode||o.prepare){
        pkt_h=packet_digest(pkts,n);
        /* Independent correctness pass, explicitly outside timers/probe snapshot. */
        int e;dec=opus_decoder_create(o.fs,o.channels,&e);if(!dec||e){fail("validation decoder creation failed");goto done;}
        for(int i=0;i<n;i++)if(!check_decode(dec,&o,&pkts[i],out,i,i>=o.warmup?&out_h:NULL,&nonzero_output))goto done;
    }
    if(!nonzero_output){fail("decoded output is entirely zero for non-silent input");goto done;}
    if(o.prepare&&!save_fixture(&o,pkts,pcm_h,pkt_h))goto done;
    uint64_t payload=0;for(int i=o.warmup;i<n;i++)payload+=pkts[i].len;
    for(int i=0;i<o.frames;i++)total_ns+=samples[i];
    printf("{\"status\":\"passed\",\"operation\":\"%s\",\"mode\":\"%s\",\"frames\":%d,\"warmup\":%d,\"cpu\":%d,\"sample_rate\":%d,\"frame_size\":%d,\"channels\":%d,",o.decode?"decode":"encode",mode_name(o.mode),o.frames,o.warmup,o.cpu,o.fs,o.frame,o.channels);
    printf("\"packet_mode_counts\":{\"silk\":%d,\"hybrid\":%d,\"celt\":%d},",o.mode==1000?o.frames:0,o.mode==1001?o.frames:0,o.mode==1002?o.frames:0);
    printf("\"total_ns\":%"PRIu64",\"loop_ns\":%"PRIu64",\"thread_cpu_ns\":%"PRIu64",\"pcm_hash\":\"%016"PRIx64"\",\"packet_hash\":\"%016"PRIx64"\",\"output_hash\":\"%016"PRIx64"\",",total_ns,loop_ns,cpu_ns,pcm_h,pkt_h,out_h);
    printf("\"actual_kbps\":%.6f,\"measured_encode_calls\":%d,\"measured_decode_calls\":%d,\"operator_calls\":{\"silk_encode\":%"PRIu64",\"celt_encode\":%"PRIu64",\"silk_decode\":%"PRIu64",\"celt_decode\":%"PRIu64"},",(double)payload*8*o.fs/(o.frames*(double)o.frame*1000),o.decode&&!o.prepare?0:o.frames,o.decode&&!o.prepare?o.frames:0,probes[0],probes[1],probes[2],probes[3]);
    printf("\"samples_ns\":[");for(int i=0;i<o.frames;i++)printf("%s%"PRIu64,i?",":"",samples[i]);puts("]}");rc=0;
done:
    if(rc)print_error();
    if(enc) opus_encoder_destroy(enc);
    if(dec) opus_decoder_destroy(dec);
    free(pkts);free(storage);free(samples);free(ranges);free(pcm);free(out);return rc;
}
