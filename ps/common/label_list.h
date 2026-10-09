// label_list.h — 从 models/label_list.json 加载类别表（唯一源，禁止手抄）
// 轻量解析：只依赖 C 标准库；字段固定，改 JSON 结构须同步本解析。
#ifndef LABEL_LIST_H
#define LABEL_LIST_H

#include <stdint.h>

#define LABEL_NUM 8
#define LABEL_STR 32

typedef struct {
    uint8_t  id;
    char     label[LABEL_STR]; /* e.g. "gear_ok" */
    char     cn[64];           /* 中文名 */
    char     kind[8];          /* gear | rack */
    char     verdict[8];       /* OK | NG */
    char     action[16];       /* arm_pick | flap */
    char     dest[16];         /* gear_box | rack_box | scrap | rework | recheck */
} label_def_t;

/* 成功返回 0 并填充表；文件缺失/格式错返回 -1。 */
int  label_list_load(const char *json_path);
const label_def_t *label_list_get(void);
const label_def_t *label_by_id(uint8_t id);

#endif
