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

static uint64_t cpu_riscv_read_rtc(uint32_t timebase_freq)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
        timebase_freq, NANOSECONDS_PER_SECOND);
}

/*
 * Called when timecmp is written to update the QEMU timer or immediately
 * trigger timer interrupt if mtimecmp <= current timer value.
 */
static void riscv_aclint_mtimer_write_timecmp(RISCVCPU *cpu, uint64_t value,
    uint32_t timebase_freq)
{
    uint64_t next;
    uint64_t diff;

    uint64_t rtc_r = cpu_riscv_read_rtc(timebase_freq);

    cpu->env.timecmp = value;
    if (cpu->env.timecmp <= rtc_r) {
        /* if we're setting an MTIMECMP value in the "past",
           immediately raise the timer interrupt */
        riscv_cpu_update_mip(cpu, MIP_MTIP, BOOL_TO_MASK(1));
        return;
    }

    /* otherwise, set up the future timer interrupt */
    riscv_cpu_update_mip(cpu, MIP_MTIP, BOOL_TO_MASK(0));
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
    RISCVCPU *cpu = opaque;
    riscv_cpu_update_mip(cpu, MIP_MTIP, BOOL_TO_MASK(1));
}

/* CPU wants to read MTIMER register */
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
    } else if (addr == (mtimer->aperture_size - 8)) {
        /* time_lo */
        return cpu_riscv_read_rtc(mtimer->timebase_freq) & 0xFFFFFFFF;
    } else if (addr == (mtimer->aperture_size - 4)) {
        /* time_hi */
        return (cpu_riscv_read_rtc(mtimer->timebase_freq) >> 32) & 0xFFFFFFFF;
    }

    error_report("aclint-mtimer: invalid read: %08x", (uint32_t)addr);
    return 0;
}

/* CPU wrote MTIMER register */
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
            riscv_aclint_mtimer_write_timecmp(RISCV_CPU(cpu),
                timecmp_hi << 32 | (value & 0xFFFFFFFF),
                mtimer->timebase_freq);
            return;
        } else if ((addr & 0x7) == 4) {
            /* timecmp_hi */
            uint64_t timecmp_lo = env->timecmp;
            riscv_aclint_mtimer_write_timecmp(RISCV_CPU(cpu),
                value << 32 | (timecmp_lo & 0xFFFFFFFF),
                mtimer->timebase_freq);
        } else {
            error_report("aclint-mtimer: invalid timecmp write: %08x",
                         (uint32_t)addr);
        }
        return;
    } else if (addr == (mtimer->aperture_size - 8)) {
        /* time_lo */
        error_report("aclint-mtimer: time_lo write not implemented");
        return;
    } else if (addr == (mtimer->aperture_size - 4)) {
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
    DEFINE_PROP_UINT32("num-harts", RISCVAclintMTimerState, num_harts, 0),
    DEFINE_PROP_UINT32("aperture-size", RISCVAclintMTimerState,
        aperture_size, 0),
    DEFINE_PROP_UINT32("timebase-freq", RISCVAclintMTimerState,
        timebase_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_aclint_mtimer_realize(DeviceState *dev, Error **errp)
{
    int i;
    RISCVCPU *cpu;
    RISCVAclintMTimerState *s = RISCV_ACLINT_MTIMER(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &riscv_aclint_mtimer_ops, s,
                          TYPE_RISCV_ACLINT_MTIMER, s->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    /* Claim MTIP so that only MTIMER controls it. */
    for (i = 0; i < s->num_harts; i++) {
        cpu = RISCV_CPU(qemu_get_cpu(s->hartid_base + i));
        if (riscv_cpu_claim_interrupts(cpu, MIP_MTIP) < 0) {
            error_report("MTIP already claimed");
            exit(1);
        }
    }
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
    for (i = 0; i < num_harts; i++) {
        CPUState *cpu = qemu_get_cpu(hartid_base + i);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            continue;
        }
        if (provide_rdtime) {
            riscv_cpu_set_rdtime_fn(env, cpu_riscv_read_rtc, timebase_freq);
        }
        env->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                  &riscv_aclint_mtimer_cb, cpu);
        env->timecmp = 0;
    }

    DeviceState *dev = qdev_new(TYPE_RISCV_ACLINT_MTIMER);
    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "aperture-size", size);
    qdev_prop_set_uint32(dev, "timebase-freq", timebase_freq);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
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
            return (env->mip & swi->sirq_mask) > 0;
        } else {
            error_report("aclint-swi: invalid read: %08x", (uint32_t)addr);
            return 0;
        }
    }

    error_report("aclint-swi: invalid read: %08x", (uint32_t)addr);
    return 0;
}

/* CPU wrote [M|S]SWI register */
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
            riscv_cpu_update_mip(RISCV_CPU(cpu), swi->sirq_mask,
                                 BOOL_TO_MASK(value));
        } else {
            error_report("aclint-swi: invalid [M|S]SIP write: %08x",
                         (uint32_t)addr);
        }
        return;
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
    DEFINE_PROP_UINT32("num-harts", RISCVAclintSwiState, num_harts, 0),
    DEFINE_PROP_UINT32("aperture-size", RISCVAclintSwiState,
        aperture_size, 0),
    DEFINE_PROP_UINT32("sirq-mask", RISCVAclintSwiState, sirq_mask, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_aclint_swi_realize(DeviceState *dev, Error **errp)
{
    int i;
    RISCVCPU *cpu;
    RISCVAclintSwiState *s = RISCV_ACLINT_SWI(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &riscv_aclint_swi_ops, s,
                          TYPE_RISCV_ACLINT_SWI, s->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    /* Claim [M|S]SIP so that can SWI controls it. */
    for (i = 0; i < s->num_harts; i++) {
        cpu = RISCV_CPU(qemu_get_cpu(s->hartid_base + i));

        /* We don't claim SSIP bit of mip CSR because this bit is
         * writable by software as-per RISC-V privilege specification.
         */
        if ((s->sirq_mask != MIP_SSIP) &&
            riscv_cpu_claim_interrupts(cpu, s->sirq_mask) < 0) {
            error_report("%s already claimed",
                (s->sirq_mask == MIP_SSIP) ? "SSIP" : "MSIP");
            exit(1);
        }
    }
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
DeviceState *riscv_aclint_swi_create(hwaddr addr, hwaddr size,
    uint32_t hartid_base, uint32_t num_harts, bool smode_swi)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_ACLINT_SWI);
    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "aperture-size", size);
    qdev_prop_set_uint32(dev, "sirq-mask", smode_swi ? MIP_SSIP : MIP_MSIP);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}

static void riscv_aclint_register_types(void)
{
    type_register_static(&riscv_aclint_mtimer_info);
    type_register_static(&riscv_aclint_swi_info);
}

type_init(riscv_aclint_register_types)
