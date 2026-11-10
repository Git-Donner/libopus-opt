/*
 * module_probe.c - 运行时模块探测实现
 */
#include "module_probe.h"
#include "utils.h"

#include <opus.h>

RouteModule probe_packet_mode(const unsigned char *data, int len)
{
    if (!data || len < 1) return ROUTE_UNKNOWN;
    if (data[0] & 0x80)
        return ROUTE_CELT;
    if ((data[0] & 0x60) == 0x60)
        return ROUTE_HYBRID;
    return ROUTE_SILK;
}

int probe_dnn_analysis_active(int application, int complexity, int sample_rate)
{
    if (application == OPUS_APPLICATION_RESTRICTED_SILK)
        return 0;
    if (sample_rate < 16000 || sample_rate > 48000)
        return 0;
    /* float 构建阈值 7；fixed 构建阈值 10。这里取宽松判断并标注 */
    if (complexity < 7)
        return 0;
    return 1;
}

int probe_dnn_dred_configurable(int dred_duration)
{
    return dred_duration > 0;
}

const char *route_label(RouteModule r)
{
    switch (r) {
        case ROUTE_CELT:   return "CELT";
        case ROUTE_HYBRID: return "HYBRID";
        case ROUTE_SILK:   return "SILK";
        default:           return "UNKNOWN";
    }
}
