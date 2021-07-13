/*
 * RISC-V ACLINT (Advanced Core Local Interruptor)
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017 SiFive, Inc.
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 *
 * This provides real-time clock, timer and interprocessor interrupts.
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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/intc/riscv_aclint.h"
#include "qemu/timer.h"
#include "hw/irq.h"

typedef struct riscv_aclint_mtimer_callback {
    RISCVAclintMTimerState *s;
    int num;
} riscv_aclint_mtimer_callback;

static uint64_t cpu_riscv_read_rtc(uint32_t timebase_freq)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
        timebase_freq, NANOSECONDS_PER_SECOND);
}

/*
 * Called when timecmp is written to update the QEMU timer or immediately
 * trigger timer interrupt if mtimecmp <= current timer value.
 */
static void riscv_aclint_mtimer_write_timecmp(RISCVAclintMTimerState *mtimer,
                                       RISCVCPU *cpu,
                                       int hartid,
                                       uint64_t value,
                                       uint32_t timebase_freq)
{
    uint64_t next;
    uint64_t diff;

    uint64_t rtc_r = cpu_riscv_read_rtc(timebase_freq);

    cpu->env.timecmp = value;
    if (cpu->env.timecmp <= rtc_r) {
        /*
         * If we're setting an MTIMECMP value in the "past",
         * immediately raise the timer interrupt
         */
        qemu_irq_raise(mtimer->timer_irqs[hartid - mtimer->hartid_base]);
        return;
    }

    /* otherwise, set up the future timer interrupt */
    qemu_irq_lower(mtimer->timer_irqs[hartid - mtimer->hartid_base]);
    diff = cpu->env.timecmp - rtc_r;
    /* back to ns (note args switched in muldiv64) */
    next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
        muldiv64(diff, NANOSECONDS_PER_SECOND, timebase_freq);
    timer_mod(cpu->env.timer, next);
}

/*
 * Callback used when the timer set using timer_mod expires.
 * Should raise the timer interrupt line
 */
static void riscv_aclint_mtimer_cb(void *opaque)
{
    riscv_aclint_mtimer_callback *state = opaque;

    qemu_irq_raise(state->s->timer_irqs[state->num]);
}

/* CPU read MTIMER register */
static uint64_t riscv_aclint_mtimer_read(void *opaque, hwaddr addr,
    unsigned size)
{
    RISCVAclintMTimerState *mtimer = opaque;

    if (addr < (mtimer->num_harts << 3)) {
        size_t hartid = mtimer->hartid_base + (addr >> 3);
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            error_report("aclint-mtimer: invalid hartid: %zu", hartid);
        } else if ((addr & 0x7) == 0) {
            /* timecmp_lo */
            uint64_t timecmp = env->timecmp;
            return timecmp & 0xFFFFFFFF;
        } else if ((addr & 0x7) == 4) {
            /* timecmp_hi */
            uint64_t timecmp = env->timecmp;
            return (timecmp >> 32) & 0xFFFFFFFF;
        } else {
            error_report("aclint-mtimer: invalid read: %08x", (uint32_t)addr);
            return 0;
        }
    } else if (addr == RISCV_ACLINT_MTIMER_MTIME) {
        /* time_lo */
        return cpu_riscv_read_rtc(mtimer->timebase_freq) & 0xFFFFFFFF;
    } else if (addr == RISCV_ACLINT_MTIMER_MTIME + 4) {
        /* time_hi */
        return (cpu_riscv_read_rtc(mtimer->timebase_freq) >> 32) & 0xFFFFFFFF;
    }

    error_report("aclint-mtimer: invalid read: %08x", (uint32_t)addr);
    return 0;
}

/* CPU write MTIMER register */
static void riscv_aclint_mtimer_write(void *opaque, hwaddr addr,
    uint64_t value, unsigned size)
{
    RISCVAclintMTimerState *mtimer = opaque;

    if (addr < (mtimer->num_harts << 3)) {
        size_t hartid = mtimer->hartid_base + (addr >> 3);
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            error_report("aclint-mtimer: invalid hartid: %zu", hartid);
        } else if ((addr & 0x7) == 0) {
            /* timecmp_lo */
            uint64_t timecmp_hi = env->timecmp >> 32;
            riscv_aclint_mtimer_write_timecmp(mtimer, RISCV_CPU(cpu), hartid,
                timecmp_hi << 32 | (value & 0xFFFFFFFF),
                mtimer->timebase_freq);
            return;
        } else if ((addr & 0x7) == 4) {
            /* timecmp_hi */
            uint64_t timecmp_lo = env->timecmp;
            riscv_aclint_mtimer_write_timecmp(mtimer, RISCV_CPU(cpu), hartid,
                value << 32 | (timecmp_lo & 0xFFFFFFFF),
                mtimer->timebase_freq);
        } else {
            error_report("aclint-mtimer: invalid timecmp write: %08x",
                         (uint32_t)addr);
        }
        return;
    } else if (addr == RISCV_ACLINT_MTIMER_MTIME) {
        /* time_lo */
        error_report("aclint-mtimer: time_lo write not implemented");
        return;
    } else if (addr == RISCV_ACLINT_MTIMER_MTIME + 4) {
        /* time_hi */
        error_report("aclint-mtimer: time_hi write not implemented");
        return;
    }

    error_report("aclint-mtimer: invalid write: %08x", (uint32_t)addr);
}

static const MemoryRegionOps riscv_aclint_mtimer_ops = {
    .read = riscv_aclint_mtimer_read,
    .write = riscv_aclint_mtimer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8
    }
};

static Property riscv_aclint_mtimer_properties[] = {
    DEFINE_PROP_UINT32("hartid-base", RISCVAclintMTimerState,
        hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", RISCVAclintMTimerState, num_harts, 1),
    DEFINE_PROP_UINT32("timebase-freq", RISCVAclintMTimerState,
        timebase_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_aclint_mtimer_realize(DeviceState *dev, Error **errp)
{
    RISCVAclintMTimerState *s = RISCV_ACLINT_MTIMER(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &riscv_aclint_mtimer_ops,
                          s, TYPE_RISCV_ACLINT_MTIMER,
                          RISCV_ACLINT_MTIMER_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    s->timer_irqs = g_malloc(sizeof(qemu_irq) * s->num_harts);
    qdev_init_gpio_out(dev, s->timer_irqs, s->num_harts);
}

static void riscv_aclint_mtimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = riscv_aclint_mtimer_realize;
    device_class_set_props(dc, riscv_aclint_mtimer_properties);
}

static const TypeInfo riscv_aclint_mtimer_info = {
    .name          = TYPE_RISCV_ACLINT_MTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVAclintMTimerState),
    .class_init    = riscv_aclint_mtimer_class_init,
};

/*
 * Create ACLINT MTIMER device.
 */
DeviceState *riscv_aclint_mtimer_create(hwaddr addr, hwaddr size,
    uint32_t hartid_base, uint32_t num_harts, uint32_t timebase_freq,
    bool provide_rdtime)
{
    int i;

    DeviceState *dev = qdev_new(TYPE_RISCV_ACLINT_MTIMER);
    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "timebase-freq", timebase_freq);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    for (i = 0; i < num_harts; i++) {
        CPUState *cpu = qemu_get_cpu(hartid_base + i);
        RISCVCPU *rvcpu = RISCV_CPU(cpu);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        riscv_aclint_mtimer_callback *cb =
            g_malloc0(sizeof(riscv_aclint_mtimer_callback));

        if (!env) {
            g_free(cb);
            continue;
        }
        if (provide_rdtime) {
            riscv_cpu_set_rdtime_fn(env, cpu_riscv_read_rtc, timebase_freq);
        }

        cb->s = RISCV_ACLINT_MTIMER(dev);
        cb->num = i;
        env->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                  &riscv_aclint_mtimer_cb, cb);
        env->timecmp = 0;

        qdev_connect_gpio_out_named(dev, NULL, i,
                           qdev_get_gpio_in(DEVICE(rvcpu), IRQ_M_TIMER));
    }

    return dev;
}

/* CPU read [M|S]SWI register */
static uint64_t riscv_aclint_swi_read(void *opaque, hwaddr addr,
    unsigned size)
{
    RISCVAclintSwiState *swi = opaque;

    if (addr < (swi->num_harts << 2)) {
        size_t hartid = swi->hartid_base + (addr >> 2);
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            error_report("aclint-swi: invalid hartid: %zu", hartid);
        } else if ((addr & 0x3) == 0) {
            return (env->mip & ((swi->sswi) ? MIP_SSIP : MIP_MSIP)) > 0;
        }
    }

    error_report("aclint-swi: invalid read: %08x", (uint32_t)addr);
    return 0;
}

/* CPU write [M|S]SWI register */
static void riscv_aclint_swi_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    RISCVAclintSwiState *swi = opaque;

    if (addr < (swi->num_harts << 2)) {
        size_t hartid = swi->hartid_base + (addr >> 2);
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            error_report("aclint-swi: invalid hartid: %zu", hartid);
        } else if ((addr & 0x3) == 0) {
            if (value) {
                qemu_irq_raise(swi->soft_irqs[hartid - swi->hartid_base]);
            } else {
                qemu_irq_lower(swi->soft_irqs[hartid - swi->hartid_base]);
            }
            return;
        }
    }

    error_report("aclint-swi: invalid write: %08x", (uint32_t)addr);
}

static const MemoryRegionOps riscv_aclint_swi_ops = {
    .read = riscv_aclint_swi_read,
    .write = riscv_aclint_swi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static Property riscv_aclint_swi_properties[] = {
    DEFINE_PROP_UINT32("hartid-base", RISCVAclintSwiState, hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", RISCVAclintSwiState, num_harts, 1),
    DEFINE_PROP_UINT32("sswi", RISCVAclintSwiState, sswi, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_aclint_swi_realize(DeviceState *dev, Error **errp)
{
    RISCVAclintSwiState *swi = RISCV_ACLINT_SWI(dev);

    memory_region_init_io(&swi->mmio, OBJECT(dev), &riscv_aclint_swi_ops, swi,
                          TYPE_RISCV_ACLINT_SWI, RISCV_ACLINT_SWI_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &swi->mmio);

    swi->soft_irqs = g_malloc(sizeof(qemu_irq) * swi->num_harts);
    qdev_init_gpio_out(dev, swi->soft_irqs, swi->num_harts);
}

static void riscv_aclint_swi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = riscv_aclint_swi_realize;
    device_class_set_props(dc, riscv_aclint_swi_properties);
}

static const TypeInfo riscv_aclint_swi_info = {
    .name          = TYPE_RISCV_ACLINT_SWI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVAclintSwiState),
    .class_init    = riscv_aclint_swi_class_init,
};

/*
 * Create ACLINT [M|S]SWI device.
 */
DeviceState *riscv_aclint_swi_create(hwaddr addr, uint32_t hartid_base,
    uint32_t num_harts, bool sswi)
{
    int i;
    DeviceState *dev = qdev_new(TYPE_RISCV_ACLINT_SWI);

    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "sswi", sswi ? true : false);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    for (i = 0; i < num_harts; i++) {
        CPUState *cpu = qemu_get_cpu(hartid_base + i);
        RISCVCPU *rvcpu = RISCV_CPU(cpu);

        qdev_connect_gpio_out_named(dev, NULL, i,
                           qdev_get_gpio_in(DEVICE(rvcpu),
                           (sswi) ? IRQ_S_SOFT : IRQ_M_SOFT));
    }

    return dev;
}

static void riscv_aclint_register_types(void)
{
    type_register_static(&riscv_aclint_mtimer_info);
    type_register_static(&riscv_aclint_swi_info);
}

type_init(riscv_aclint_register_types)
