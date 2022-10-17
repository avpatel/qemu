/*
 * RISC-V timer helper implementation.
 *
 * Copyright (c) 2022 Rivos Inc.
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu_bits.h"
#include "time_helper.h"

#define RISCV_TIMER_CMP_NS 2000000UL

void riscv_stimer_update(RISCVCPU *cpu)
{
    CPURISCVState *env = &cpu->env;

    if (!(env->menvcfg & MENVCFG_STCE)) {
        return;
    }

    if (env->stimecmp <= env->rdtime_fn(env->rdtime_fn_arg)) {
        if (!(env->mip & MIP_STIP)) {
            riscv_cpu_update_mip(cpu, MIP_STIP, BOOL_TO_MASK(1));
        }
    } else {
        if (env->mip & MIP_STIP) {
            riscv_cpu_update_mip(cpu, MIP_STIP, BOOL_TO_MASK(0));
        }
    }
}

void riscv_vstimer_update(RISCVCPU *cpu)
{
    CPURISCVState *env = &cpu->env;

    if (!(env->menvcfg & MENVCFG_STCE) || !(env->henvcfg & HENVCFG_STCE)) {
        return;
    }

    if (env->vstimecmp <=
        (env->rdtime_fn(env->rdtime_fn_arg) + env->htimedelta)) {
        if (!env->vstime_irq) {
            env->vstime_irq = true;
            /* Dummy MIP update to invoke cpu_interrupt() */
            riscv_cpu_update_mip(cpu, 0, BOOL_TO_MASK(0));
        }
    } else {
        if (env->vstime_irq) {
            env->vstime_irq = false;
            /* Dummy MIP update to invoke cpu_interrupt() */
            riscv_cpu_update_mip(cpu, 0, BOOL_TO_MASK(0));
        }
    }
}

static void riscv_timer_cb(void *opaque)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;

    /* Update timer interrupts */
    riscv_stimer_update(cpu);
    riscv_vstimer_update(cpu);

    /* Re-start the timer */
    if (cpu->cfg.ext_sstc) {
        timer_mod(env->timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + RISCV_TIMER_CMP_NS);
    }
}

void riscv_timer_init(RISCVCPU *cpu)
{
    CPURISCVState *env;

    if (!cpu) {
        return;
    }

    env = &cpu->env;
    env->stimecmp = UINT64_MAX;
    env->vstimecmp = UINT64_MAX;
    env->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &riscv_timer_cb, cpu);
    env->vstime_irq = false;

    /* Start the timer */
    if (cpu->cfg.ext_sstc) {
        timer_mod(env->timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + RISCV_TIMER_CMP_NS);
    }
}
