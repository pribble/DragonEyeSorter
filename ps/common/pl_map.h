// pl_map.h — DEPRECATED: Use des_hw.h instead
// 用法：pl_map(PL_BASE, 0x1000) → pl_rd/pl_wr；退出前 pl_unmap。
// 板上以 root 运行；/dev/mem 已实测可用（docs/log/2026-10-08）。
//
// DEPRECATED: This module has known bugs and lacks volatile semantics.
// For PL register access, use des_hw.h (servo transactions, stepper control, etc.)
// See ps/README.md "约定" section for migration guide.
#ifndef PL_MAP_H
#define PL_MAP_H

#include <stdint.h>
#include <stddef.h>

int      pl_map(uintptr_t phys, size_t len);
void     pl_unmap(void);
uint32_t pl_rd(uint32_t off);
void     pl_wr(uint32_t off, uint32_t val);

#endif
