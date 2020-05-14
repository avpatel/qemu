/*
 * Spike machine interface
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_SPIKE_H
#define HW_RISCV_SPIKE_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"

#define SPIKE_SOCKETS_MAX 4
#define SPIKE_CPUS_PER_SOCKET_MIN 2
#define SPIKE_CPUS_PER_SOCKET_MAX 4
#define SPIKE_CPUS_MAX (SPIKE_SOCKETS_MAX * SPIKE_CPUS_PER_SOCKET_MAX)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    unsigned int num_socs;
    RISCVHartArrayState soc[SPIKE_SOCKETS_MAX];
    void *fdt;
    int fdt_size;
} SpikeState;

enum {
    SPIKE_MROM,
    SPIKE_CLINT,
    SPIKE_DRAM
};

#if defined(TARGET_RISCV32)
#define SPIKE_V1_09_1_CPU TYPE_RISCV_CPU_RV32GCSU_V1_09_1
#define SPIKE_V1_10_0_CPU TYPE_RISCV_CPU_RV32GCSU_V1_10_0
#elif defined(TARGET_RISCV64)
#define SPIKE_V1_09_1_CPU TYPE_RISCV_CPU_RV64GCSU_V1_09_1
#define SPIKE_V1_10_0_CPU TYPE_RISCV_CPU_RV64GCSU_V1_10_0
#endif

#endif
