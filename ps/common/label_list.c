#include "label_list.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static label_def_t s_labels[LABEL_NUM];
static int s_loaded = 0;

/* 在 buf 中找 "key" 后的字符串值，拷入 out（截断安全）。返回 0 找到。 */
static int json_str(const char *buf, const char *key, char *out, size_t outsz) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(buf, pat);
    if (!p) return -1;
    p = strchr(p + strlen(pat), '"');
    if (!p) return -1;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < outsz) out[n++] = *p++;
    out[n] = 0;
    return 0;
}

int label_list_load(const char *json_path) {
    FILE *fp = fopen(json_path, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > (1 << 20)) { fclose(fp); return -1; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[rd] = 0;

    /* 按 label 逐条切分，简单可靠 */
    int n = 0;
    const char *p = buf;
    while (n < LABEL_NUM) {
        p = strstr(p, "\"label\"");
        if (!p) break;
        /* 以本条 "label" 到下一条 "label"（或结尾）为该条作用域 */
        const char *next = strstr(p + 7, "\"label\"");
        size_t span = next ? (size_t)(next - p) : strlen(p);
        char scope[512];
        if (span >= sizeof scope) span = sizeof scope - 1;
        memcpy(scope, p, span); scope[span] = 0;

        label_def_t *L = &s_labels[n];
        memset(L, 0, sizeof *L);
        json_str(scope, "label",   L->label,   sizeof L->label);
        json_str(scope, "cn",      L->cn,      sizeof L->cn);
        json_str(scope, "kind",    L->kind,    sizeof L->kind);
        json_str(scope, "verdict", L->verdict, sizeof L->verdict);
        json_str(scope, "action",  L->action,  sizeof L->action);
        json_str(scope, "dest",    L->dest,    sizeof L->dest);
        L->id = (uint8_t)n;
        n++;
        p += 7;
    }
    free(buf);
    if (n != LABEL_NUM) return -1;
    s_loaded = 1;
    return 0;
}

const label_def_t *label_list_get(void) { return s_loaded ? s_labels : 0; }
const label_def_t *label_by_id(uint8_t id) {
    return (s_loaded && id < LABEL_NUM) ? &s_labels[id] : 0;
}
