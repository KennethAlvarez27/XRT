/**
 * Copyright (C) 2022 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
#ifndef __XGQ_MB_PLAT_H__
#define __XGQ_MB_PLAT_H__

#if defined(__KERNEL__)
# include <linux/types.h>
#else
# include <stdint.h>
#endif /* __KERNEL__ */

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

static inline void reg_write(uint32_t addr, uint32_t val)
{
	volatile uint32_t *a = (uint32_t *)(uintptr_t)addr;
	*a = val;
}

static inline uint32_t reg_read(uint32_t addr)
{
	volatile uint32_t *a = (uint32_t *)(uintptr_t)addr;
	return *a;
}

static inline void xgq_mem_write32(uint32_t io_hdl, uint32_t addr, uint32_t val)
{
	reg_write(addr, val);
}
#define xgq_reg_write32	xgq_mem_write32

static inline uint32_t xgq_mem_read32(uint32_t io_hdl, uint32_t addr)
{
	return reg_read(addr);
}
#define xgq_reg_read32	xgq_mem_read32

#define ____cacheline_aligned_in_smp
#define BRAM_COLLISION_WORKAROUND
#define XGQ_IMPL
#define XGQ_SERVER
#endif
