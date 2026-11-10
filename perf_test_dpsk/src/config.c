/*
 * config.c - 最小化 JSON 解析器 + 测试配置加载
 *
 * 零依赖：仅支持本框架配置子集（对象/数组/字符串/整数/浮点/布尔/null）。
 * 支持嵌套 3 层。解析失败给出近似位置。
 */
#include "config.h"
#include "utils.h"

#include <opus.h>

/* ─── 最小 JSON 值模型 ──────────────────────────────────────────── */
typedef enum { J_OBJ, J_ARR, J_STR, J_NUM, J_BOOL, J_NULL } JType;

typedef struct JVal JVal;
struct JVal {
    JType type;
    char *key;          /* 顶层对象成员的 key */
    char *str;          /* J_STR */
    double num;         /* J_NUM */
    int    boolean;     /* J_BOOL */
    int    nchild;
    JVal **child;
};

static const char *jp_cur;
static int jp_line;

static void jp_error(const char *msg)
{
    fprintf(stderr, "[config] JSON 解析错误 (line ~%d): %s\n", jp_line, msg);
}

static void jp_skip_ws(void)
{
    while (*jp_cur) {
        if (*jp_cur == '\n') jp_line++;
        if (*jp_cur == ' ' || *jp_cur == '\t' || *jp_cur == '\r' || *jp_cur == '\n')
            jp_cur++;
        else
            break;
    }
}

static JVal *jval_new(JType t)
{
    JVal *v = (JVal *)xcalloc(1, sizeof(JVal));
    v->type = t;
    return v;
}

static char *jp_parse_string(void)
{
    /* 当前位于 '"' 之前 */
    if (*jp_cur != '"') return NULL;
    jp_cur++;
    /* 简单扫描到结束引号（不处理转义的复杂情况，框架配置足够） */
    const char *start = jp_cur;
    size_t len = 0;
    while (*jp_cur && *jp_cur != '"') {
        if (*jp_cur == '\\') jp_cur++;   /* 跳过转义 */
        jp_cur++;
        len++;
    }
    if (*jp_cur != '"') { jp_error("字符串未闭合"); return NULL; }
    len = (size_t)(jp_cur - start);
    jp_cur++; /* 跳过闭合引号 */
    char *out = (char *)xmalloc(len + 1);
    /* 复制并解转义 */
    size_t o = 0;
    const char *p = start;
    while (p < jp_cur - 1) {
        if (*p == '\\' && p + 1 < jp_cur - 1) {
            p++;
            switch (*p) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = '\t'; break;
                case 'r': out[o++] = '\r'; break;
                case '"': out[o++] = '"'; break;
                case '\\': out[o++] = '\\'; break;
                default:   out[o++] = *p; break;
            }
        } else {
            out[o++] = *p;
        }
        p++;
    }
    out[o] = '\0';
    return out;
}

static JVal *jp_parse_number(void)
{
    char *end;
    double v = strtod(jp_cur, &end);
    if (end == jp_cur) return NULL;
    jp_cur = end;
    JVal *n = jval_new(J_NUM);
    n->num = v;
    return n;
}

static JVal *jp_parse_value(void);

static JVal *jp_parse_array(void)
{
    jp_cur++; /* '[' */
    JVal *a = jval_new(J_ARR);
    jp_skip_ws();
    if (*jp_cur == ']') { jp_cur++; return a; }
    int cap = 8;
    a->child = (JVal **)xmalloc(sizeof(JVal *) * cap);
    while (*jp_cur) {
        jp_skip_ws();
        JVal *v = jp_parse_value();
        if (!v) { jp_error("数组元素解析失败"); break; }
        if (a->nchild == cap) {
            cap *= 2;
            a->child = (JVal **)realloc(a->child, sizeof(JVal *) * cap);
        }
        a->child[a->nchild++] = v;
        jp_skip_ws();
        if (*jp_cur == ',') { jp_cur++; continue; }
        if (*jp_cur == ']') { jp_cur++; break; }
        jp_error("数组缺逗号或右括号");
        break;
    }
    return a;
}

static JVal *jp_parse_object(void)
{
    jp_cur++; /* '{' */
    JVal *o = jval_new(J_OBJ);
    jp_skip_ws();
    if (*jp_cur == '}') { jp_cur++; return o; }
    int cap = 8;
    o->child = (JVal **)xmalloc(sizeof(JVal *) * cap);
    while (*jp_cur) {
        jp_skip_ws();
        char *key = jp_parse_string();
        if (!key) { jp_error("对象 key 解析失败"); break; }
        jp_skip_ws();
        if (*jp_cur != ':') { jp_error("缺冒号"); free(key); break; }
        jp_cur++;
        jp_skip_ws();
        JVal *v = jp_parse_value();
        if (!v) { free(key); break; }
        v->key = key;
        if (o->nchild == cap) {
            cap *= 2;
            o->child = (JVal **)realloc(o->child, sizeof(JVal *) * cap);
        }
        o->child[o->nchild++] = v;
        jp_skip_ws();
        if (*jp_cur == ',') { jp_cur++; continue; }
        if (*jp_cur == '}') { jp_cur++; break; }
        jp_error("对象缺逗号或右括号");
        break;
    }
    return o;
}

static JVal *jp_parse_value(void)
{
    jp_skip_ws();
    if (*jp_cur == '{') return jp_parse_object();
    if (*jp_cur == '[') return jp_parse_array();
    if (*jp_cur == '"') {
        char *s = jp_parse_string();
        if (!s) return NULL;
        JVal *v = jval_new(J_STR);
        v->str = s;
        return v;
    }
    if (*jp_cur == 't' || *jp_cur == 'f') {
        if (strncmp(jp_cur, "true", 4) == 0) { jp_cur += 4; JVal *v = jval_new(J_BOOL); v->boolean = 1; return v; }
        if (strncmp(jp_cur, "false", 5) == 0) { jp_cur += 5; JVal *v = jval_new(J_BOOL); v->boolean = 0; return v; }
    }
    if (*jp_cur == 'n' && strncmp(jp_cur, "null", 4) == 0) { jp_cur += 4; return jval_new(J_NULL); }
    return jp_parse_number();
}

static void jval_free(JVal *v)
{
    if (!v) return;
    for (int i = 0; i < v->nchild; i++) jval_free(v->child[i]);
    if (v->child) free(v->child);
    if (v->str) free(v->str);
    if (v->key) free(v->key);
    free(v);
}

static JVal *jobj_get(JVal *obj, const char *key)
{
    if (!obj || obj->type != J_OBJ) return NULL;
    for (int i = 0; i < obj->nchild; i++)
        if (obj->child[i]->key && strcmp(obj->child[i]->key, key) == 0)
            return obj->child[i];
    return NULL;
}

/* ─── 字段取值辅助 ─────────────────────────────────────────────── */
static int jval_to_int(JVal *v, int def)
{
    if (!v) return def;
    if (v->type == J_NUM) return (int)v->num;
    if (v->type == J_STR) return atoi(v->str);
    return def;
}

static const char *jval_to_str(JVal *v, const char *def)
{
    if (!v || v->type != J_STR) return def;
    return v->str;
}

/* ─── 应用常量映射 ─────────────────────────────────────────────── */
int app_string_to_code(const char *s)
{
    if (!s) return OPUS_APPLICATION_AUDIO;
    if (str_ieq(s, "VOIP") || str_ieq(s, "OPUS_APPLICATION_VOIP")) return OPUS_APPLICATION_VOIP;
    if (str_ieq(s, "AUDIO") || str_ieq(s, "OPUS_APPLICATION_AUDIO")) return OPUS_APPLICATION_AUDIO;
    if (str_ieq(s, "LOWDELAY") || str_ieq(s, "OPUS_APPLICATION_RESTRICTED_LOWDELAY")) return OPUS_APPLICATION_RESTRICTED_LOWDELAY;
    if (str_ieq(s, "SILK") || str_ieq(s, "RESTRICTED_SILK") || str_ieq(s, "OPUS_APPLICATION_RESTRICTED_SILK")) return OPUS_APPLICATION_RESTRICTED_SILK;
    if (str_ieq(s, "CELT") || str_ieq(s, "RESTRICTED_CELT") || str_ieq(s, "OPUS_APPLICATION_RESTRICTED_CELT")) return OPUS_APPLICATION_RESTRICTED_CELT;
    return OPUS_APPLICATION_AUDIO;
}

int bandwidth_string_to_code(const char *s)
{
    if (!s) return 0;
    if (str_ieq(s, "NARROWBAND") || str_ieq(s, "OPUS_BANDWIDTH_NARROWBAND")) return OPUS_BANDWIDTH_NARROWBAND;
    if (str_ieq(s, "MEDIUMBAND") || str_ieq(s, "OPUS_BANDWIDTH_MEDIUMBAND")) return OPUS_BANDWIDTH_MEDIUMBAND;
    if (str_ieq(s, "WIDEBAND") || str_ieq(s, "OPUS_BANDWIDTH_WIDEBAND")) return OPUS_BANDWIDTH_WIDEBAND;
    if (str_ieq(s, "SUPERWIDEBAND") || str_ieq(s, "OPUS_BANDWIDTH_SUPERWIDEBAND")) return OPUS_BANDWIDTH_SUPERWIDEBAND;
    if (str_ieq(s, "FULLBAND") || str_ieq(s, "OPUS_BANDWIDTH_FULLBAND")) return OPUS_BANDWIDTH_FULLBAND;
    return 0; /* 0 = OPUS_AUTO */
}

/* ─── 模块解析 ─────────────────────────────────────────────────── */
static ExpectedModule module_from_string(const char *s)
{
    if (!s) return MOD_EXPECT_AUTO;
    if (str_ieq(s, "celt"))   return MOD_EXPECT_CELT;
    if (str_ieq(s, "silk"))   return MOD_EXPECT_SILK;
    if (str_ieq(s, "hybrid")) return MOD_EXPECT_HYBRID;
    if (str_ieq(s, "dnn"))    return MOD_EXPECT_DNN;
    return MOD_EXPECT_AUTO;
}

const char *module_label(ExpectedModule m)
{
    switch (m) {
        case MOD_EXPECT_CELT:   return "CELT";
        case MOD_EXPECT_SILK:   return "SILK";
        case MOD_EXPECT_HYBRID: return "HYBRID";
        case MOD_EXPECT_DNN:    return "DNN";
        default:                return "AUTO";
    }
}

const char *module_machine_name(ExpectedModule m)
{
    switch (m) {
        case MOD_EXPECT_CELT:   return "celt";
        case MOD_EXPECT_SILK:   return "silk";
        case MOD_EXPECT_HYBRID: return "hybrid";
        case MOD_EXPECT_DNN:    return "dnn";
        default:                return "auto";
    }
}

/* ─── 操作解析 ─────────────────────────────────────────────────── */
static PerfOperation op_from_string(const char *s)
{
    if (!s) return OP_ENCODE_DECODE;
    if (str_ieq(s, "encode_only") || str_ieq(s, "encode")) return OP_ENCODE_ONLY;
    if (str_ieq(s, "decode_only") || str_ieq(s, "decode")) return OP_DECODE_ONLY;
    return OP_ENCODE_DECODE;
}

/* ─── 加载 ─────────────────────────────────────────────────────── */
int config_load(const char *filename, TestConfig *cfg)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "[config] 无法打开配置文件: %s\n", filename);
        return PERF_ERR;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)xmalloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    jp_cur = buf;
    jp_line = 1;
    JVal *root = jp_parse_value();
    free(buf);
    if (!root || root->type != J_OBJ) {
        fprintf(stderr, "[config] 配置文件根节点必须是对象\n");
        if (root) jval_free(root);
        return PERF_ERR;
    }

    /* meta.name */
    JVal *meta = jobj_get(root, "meta");
    if (meta) {
        JVal *name = jobj_get(meta, "name");
        const char *n = jval_to_str(name, "default");
        snprintf(cfg->config_name, sizeof(cfg->config_name), "%s", n);
    } else {
        snprintf(cfg->config_name, sizeof(cfg->config_name), "default");
    }

    JVal *cases = jobj_get(root, "test_cases");
    if (!cases || cases->type != J_ARR) {
        fprintf(stderr, "[config] 缺少 test_cases 数组\n");
        jval_free(root);
        return PERF_ERR;
    }

    int n = cases->nchild;
    cfg->cases = (TestCase *)xcalloc((size_t)(n ? n : 1), sizeof(TestCase));
    cfg->count = 0;
    for (int i = 0; i < n; i++) {
        JVal *o = cases->child[i];
        if (o->type != J_OBJ) continue;
        TestCase *tc = &cfg->cases[cfg->count];
        memset(tc, 0, sizeof(*tc));
        snprintf(tc->name, sizeof(tc->name), "%s",
                 jval_to_str(jobj_get(o, "name"), "unnamed"));
        snprintf(tc->description, sizeof(tc->description), "%s",
                 jval_to_str(jobj_get(o, "description"), ""));
        tc->module = module_from_string(jval_to_str(jobj_get(o, "module"), "auto"));
        tc->operation = op_from_string(jval_to_str(jobj_get(o, "operation"), "encode_decode"));
        tc->channels    = jval_to_int(jobj_get(o, "channels"), 2);
        tc->application = app_string_to_code(jval_to_str(jobj_get(o, "application"), "AUDIO"));
        tc->frame_size  = jval_to_int(jobj_get(o, "frame_size"), 960);
        tc->bitrate     = jval_to_int(jobj_get(o, "bitrate"), 64000);
        tc->complexity  = jval_to_int(jobj_get(o, "complexity"), -1000);
        tc->use_fec     = jval_to_int(jobj_get(o, "use_fec"), 0);
        tc->use_dtx     = jval_to_int(jobj_get(o, "use_dtx"), 0);
        tc->loss_perc   = jval_to_int(jobj_get(o, "loss_perc"), 0);
        tc->dred_duration = jval_to_int(jobj_get(o, "dred_duration"), 0);
        tc->bandwidth   = bandwidth_string_to_code(jval_to_str(jobj_get(o, "bandwidth"), NULL));
        tc->chunk_frames = jval_to_int(jobj_get(o, "chunk_frames"), 0);
        tc->iterations  = jval_to_int(jobj_get(o, "iterations"), 3000);
        tc->warmup      = jval_to_int(jobj_get(o, "warmup"), 100);
        cfg->count++;
    }

    jval_free(root);
    return PERF_OK;
}

void config_free(TestConfig *cfg)
{
    if (cfg->cases) free(cfg->cases);
    cfg->cases = NULL;
    cfg->count = 0;
}
