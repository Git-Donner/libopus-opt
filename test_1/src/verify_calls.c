/* Untimed, separately linked coverage proof. Never linked into the timed worker.
 * Headers and ABI definitions must match the selected libopus build. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>
#include <string.h>
#include "celt.h"
#include "API.h"

static uint64_t calls[4];
void bench_probe_reset(void) { memset(calls, 0, sizeof(calls)); }
void bench_probe_read(uint64_t out[4]) { memcpy(out, calls, sizeof(calls)); }

opus_int __real_silk_Encode(void *, silk_EncControlStruct *, const opus_res *,
                           opus_int, ec_enc *, opus_int32 *, const opus_int, int);
opus_int __wrap_silk_Encode(void *state, silk_EncControlStruct *ctl,
                           const opus_res *pcm, opus_int n, ec_enc *ec,
                           opus_int32 *bytes, const opus_int prefill, int activity)
{
    calls[0]++;
    return __real_silk_Encode(state, ctl, pcm, n, ec, bytes, prefill, activity);
}

int __real_celt_encode_with_ec(OpusCustomEncoder *, const opus_res *, int,
                              unsigned char *, int, ec_enc *);
int __wrap_celt_encode_with_ec(OpusCustomEncoder *state, const opus_res *pcm,
                              int frame, unsigned char *packet, int size, ec_enc *ec)
{
    calls[1]++;
    return __real_celt_encode_with_ec(state, pcm, frame, packet, size, ec);
}

opus_int __real_silk_Decode(void *, silk_DecControlStruct *, opus_int, opus_int,
                          ec_dec *, opus_res *, opus_int32 *,
#ifdef ENABLE_DEEP_PLC
                          LPCNetPLCState *,
#endif
                          int);
opus_int __wrap_silk_Decode(void *state, silk_DecControlStruct *ctl,
                          opus_int lost, opus_int first, ec_dec *ec,
                          opus_res *pcm, opus_int32 *n,
#ifdef ENABLE_DEEP_PLC
                          LPCNetPLCState *plc,
#endif
                          int arch)
{
    calls[2]++;
    return __real_silk_Decode(state, ctl, lost, first, ec, pcm, n,
#ifdef ENABLE_DEEP_PLC
                             plc,
#endif
                             arch);
}

int __real_celt_decode_with_ec(OpusCustomDecoder *, const unsigned char *, int,
                              opus_res *, int, ec_dec *, int);
int __wrap_celt_decode_with_ec(OpusCustomDecoder *state, const unsigned char *packet,
                              int size, opus_res *pcm, int frame, ec_dec *ec, int accum)
{
    calls[3]++;
    return __real_celt_decode_with_ec(state, packet, size, pcm, frame, ec, accum);
}

/* Current Opus uses this entry point for ordinary CELT/HYBRID decode as well
 * as DRED-capable builds; only wrapping celt_decode_with_ec misses that path. */
int __real_celt_decode_with_ec_dred(OpusCustomDecoder *, const unsigned char *, int,
                                   opus_res *, int, ec_dec *, int
#ifdef ENABLE_DEEP_PLC
                                   , LPCNetPLCState *
#endif
                                   ARG_QEXT(const unsigned char *) ARG_QEXT(int));
int __wrap_celt_decode_with_ec_dred(OpusCustomDecoder *state,
                                   const unsigned char *packet, int size,
                                   opus_res *pcm, int frame, ec_dec *ec, int accum
#ifdef ENABLE_DEEP_PLC
                                   , LPCNetPLCState *plc
#endif
                                   ARG_QEXT(const unsigned char *qext)
                                   ARG_QEXT(int qext_size))
{
    calls[3]++;
    return __real_celt_decode_with_ec_dred(state, packet, size, pcm, frame, ec, accum
#ifdef ENABLE_DEEP_PLC
                                          , plc
#endif
                                          ARG_QEXT(qext) ARG_QEXT(qext_size));
}
