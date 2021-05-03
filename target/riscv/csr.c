/*
 * RISC-V Control and Status Registers.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
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
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"

/* CSR function table public API */
void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops)
{
    *ops = csr_ops[csrno & (CSR_TABLE_SIZE - 1)];
}

void riscv_set_csr_ops(int csrno, riscv_csr_operations *ops)
{
    csr_ops[csrno & (CSR_TABLE_SIZE - 1)] = *ops;
}

/* Predicates */
static int fs(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    /* loose check condition for fcsr in vector extension */
    if ((csrno == CSR_FCSR) && (env->misa & RVV)) {
        return 0;
    }
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    return 0;
}

static int vs(CPURISCVState *env, int csrno)
{
    if (env->misa & RVV) {
        return 0;
    }
    return -RISCV_EXCP_ILLEGAL_INST;
}

static int ctr(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    CPUState *cs = env_cpu(env);
    RISCVCPU *cpu = RISCV_CPU(cs);

    if (!cpu->cfg.ext_counters) {
        /* The Counters extensions is not enabled */
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    if (riscv_cpu_virt_enabled(env)) {
        switch (csrno) {
        case CSR_CYCLE:
            if (!get_field(env->hcounteren, HCOUNTEREN_CY) &&
                get_field(env->mcounteren, HCOUNTEREN_CY)) {
                return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_TIME:
            if (!get_field(env->hcounteren, HCOUNTEREN_TM) &&
                get_field(env->mcounteren, HCOUNTEREN_TM)) {
                return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_INSTRET:
            if (!get_field(env->hcounteren, HCOUNTEREN_IR) &&
                get_field(env->mcounteren, HCOUNTEREN_IR)) {
                return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_HPMCOUNTER3...CSR_HPMCOUNTER31:
            if (!get_field(env->hcounteren, 1 << (csrno - CSR_HPMCOUNTER3)) &&
                get_field(env->mcounteren, 1 << (csrno - CSR_HPMCOUNTER3))) {
                return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        }
        if (riscv_cpu_is_32bit(env)) {
            switch (csrno) {
            case CSR_CYCLEH:
                if (!get_field(env->hcounteren, HCOUNTEREN_CY) &&
                    get_field(env->mcounteren, HCOUNTEREN_CY)) {
                    return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_TIMEH:
                if (!get_field(env->hcounteren, HCOUNTEREN_TM) &&
                    get_field(env->mcounteren, HCOUNTEREN_TM)) {
                    return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_INSTRETH:
                if (!get_field(env->hcounteren, HCOUNTEREN_IR) &&
                    get_field(env->mcounteren, HCOUNTEREN_IR)) {
                    return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_HPMCOUNTER3H...CSR_HPMCOUNTER31H:
                if (!get_field(env->hcounteren, 1 << (csrno - CSR_HPMCOUNTER3H)) &&
                    get_field(env->mcounteren, 1 << (csrno - CSR_HPMCOUNTER3H))) {
                    return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            }
        }
    }
#endif
    return 0;
}

static int ctr32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return ctr(env, csrno);
}

#if !defined(CONFIG_USER_ONLY)
static int any(CPURISCVState *env, int csrno)
{
    return 0;
}

static int any32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return any(env, csrno);

}

static int aia_any(CPURISCVState *env, int csrno)
{
    if (!riscv_feature(env, RISCV_FEATURE_AIA)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return any(env, csrno);
}

static int aia_any32(CPURISCVState *env, int csrno)
{
    if (!riscv_feature(env, RISCV_FEATURE_AIA)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return any32(env, csrno);
}

static int smode(CPURISCVState *env, int csrno)
{
    return -!riscv_has_ext(env, RVS);
}

static int smode32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return smode(env, csrno);
}

static int aia_smode(CPURISCVState *env, int csrno)
{
    if (!riscv_feature(env, RISCV_FEATURE_AIA)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return smode(env, csrno);
}

static int aia_smode32(CPURISCVState *env, int csrno)
{
    if (!riscv_feature(env, RISCV_FEATURE_AIA)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return smode32(env, csrno);
}

static int hmode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS) &&
        riscv_has_ext(env, RVH)) {
        /* Hypervisor extension is supported */
        if ((env->priv == PRV_S && !riscv_cpu_virt_enabled(env)) ||
            env->priv == PRV_M) {
            return 0;
        } else {
            return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    return -RISCV_EXCP_ILLEGAL_INST;
}

static int hmode32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return hmode(env, csrno);
}

static int aia_hmode(CPURISCVState *env, int csrno)
{
    if (!riscv_feature(env, RISCV_FEATURE_AIA)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return hmode(env, csrno);
}

static int aia_hmode32(CPURISCVState *env, int csrno)
{
    if (!riscv_feature(env, RISCV_FEATURE_AIA)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    return hmode32(env, csrno);
}

static int pmp(CPURISCVState *env, int csrno)
{
    return -!riscv_feature(env, RISCV_FEATURE_PMP);
}
#endif

/* User Floating-Point CSRs */
static int read_fflags(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    *val = riscv_cpu_get_fflags(env);
    return 0;
}

static int write_fflags(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    riscv_cpu_set_fflags(env, val & (FSR_AEXC >> FSR_AEXC_SHIFT));
    return 0;
}

static int read_frm(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    *val = env->frm;
    return 0;
}

static int write_frm(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = val & (FSR_RD >> FSR_RD_SHIFT);
    return 0;
}

static int read_fcsr(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    *val = (riscv_cpu_get_fflags(env) << FSR_AEXC_SHIFT)
        | (env->frm << FSR_RD_SHIFT);
    if (vs(env, csrno) >= 0) {
        *val |= (env->vxrm << FSR_VXRM_SHIFT)
                | (env->vxsat << FSR_VXSAT_SHIFT);
    }
    return 0;
}

static int write_fcsr(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = (val & FSR_RD) >> FSR_RD_SHIFT;
    if (vs(env, csrno) >= 0) {
        env->vxrm = (val & FSR_VXRM) >> FSR_VXRM_SHIFT;
        env->vxsat = (val & FSR_VXSAT) >> FSR_VXSAT_SHIFT;
    }
    riscv_cpu_set_fflags(env, (val & FSR_AEXC) >> FSR_AEXC_SHIFT);
    return 0;
}

static int read_vtype(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vtype;
    return 0;
}

static int read_vl(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vl;
    return 0;
}

static int read_vxrm(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vxrm;
    return 0;
}

static int write_vxrm(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vxrm = val;
    return 0;
}

static int read_vxsat(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vxsat;
    return 0;
}

static int write_vxsat(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vxsat = val;
    return 0;
}

static int read_vstart(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstart;
    return 0;
}

static int write_vstart(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vstart = val;
    return 0;
}

/* User Timers and Counters */
static int read_instret(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (icount_enabled()) {
        *val = icount_get();
    } else {
        *val = cpu_get_host_ticks();
    }
#else
    *val = cpu_get_host_ticks();
#endif
    return 0;
}

static int read_instreth(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (icount_enabled()) {
        *val = icount_get() >> 32;
    } else {
        *val = cpu_get_host_ticks() >> 32;
    }
#else
    *val = cpu_get_host_ticks() >> 32;
#endif
    return 0;
}

#if defined(CONFIG_USER_ONLY)
static int read_time(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = cpu_get_host_ticks();
    return 0;
}

static int read_timeh(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = cpu_get_host_ticks() >> 32;
    return 0;
}

#else /* CONFIG_USER_ONLY */

static int read_time(CPURISCVState *env, int csrno, target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->rdtime_fn(env->rdtime_fn_arg) + delta;
    return 0;
}

static int read_timeh(CPURISCVState *env, int csrno, target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    *val = (env->rdtime_fn(env->rdtime_fn_arg) + delta) >> 32;
    return 0;
}

/* Machine constants */

#define M_MODE_INTERRUPTS  ((uint64_t)(MIP_MSIP | MIP_MTIP | MIP_MEIP))
#define S_MODE_INTERRUPTS  ((uint64_t)(MIP_SSIP | MIP_STIP | MIP_SEIP))
#define VS_MODE_INTERRUPTS ((uint64_t)(MIP_VSSIP | MIP_VSTIP | MIP_VSEIP))

#define TLOWBITS64         ((uint64_t)((target_ulong)-1))

static const uint64_t delegable_ints = S_MODE_INTERRUPTS |
                                       VS_MODE_INTERRUPTS;
static const uint64_t all_ints = M_MODE_INTERRUPTS | S_MODE_INTERRUPTS |
                                 VS_MODE_INTERRUPTS;
static const target_ulong delegable_excps =
    (1ULL << (RISCV_EXCP_INST_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_INST_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_ILLEGAL_INST)) |
    (1ULL << (RISCV_EXCP_BREAKPOINT)) |
    (1ULL << (RISCV_EXCP_LOAD_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_LOAD_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_AMO_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_STORE_AMO_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_U_ECALL)) |
    (1ULL << (RISCV_EXCP_S_ECALL)) |
    (1ULL << (RISCV_EXCP_VS_ECALL)) |
    (1ULL << (RISCV_EXCP_M_ECALL)) |
    (1ULL << (RISCV_EXCP_INST_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_LOAD_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_INST_GUEST_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_VIRT_INSTRUCTION_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT));
static const target_ulong sstatus_v1_10_mask = SSTATUS_SIE | SSTATUS_SPIE |
    SSTATUS_UIE | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS |
    SSTATUS_SUM | SSTATUS_MXR | SSTATUS_SD;
static const uint64_t sip_writable_mask = SIP_SSIP | MIP_USIP | MIP_UEIP;
static const uint64_t hip_writable_mask = MIP_VSSIP;
static const uint64_t hvip_writable_mask = MIP_VSSIP | MIP_VSTIP | MIP_VSEIP;
static const uint64_t vsip_writable_mask = MIP_VSSIP;

static const char valid_vm_1_10_32[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV32] = 1
};

static const char valid_vm_1_10_64[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV39] = 1,
    [VM_1_10_SV48] = 1,
    [VM_1_10_SV57] = 1
};

/* Machine Information Registers */
static int read_zero(CPURISCVState *env, int csrno, target_ulong *val)
{
    return *val = 0;
}

static int read_mhartid(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mhartid;
    return 0;
}

/* Machine Trap Setup */
static int read_mstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mstatus;
    return 0;
}

static int validate_vm(CPURISCVState *env, target_ulong vm)
{
    if (riscv_cpu_is_32bit(env)) {
        return valid_vm_1_10_32[vm & 0xf];
    } else {
        return valid_vm_1_10_64[vm & 0xf];
    }
}

static int write_mstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mstatus = env->mstatus;
    uint64_t mask = 0;
    int dirty;

    /* flush tlb on mstatus fields that affect VM */
    if ((val ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP | MSTATUS_MPV |
            MSTATUS_MPRV | MSTATUS_SUM)) {
        tlb_flush(env_cpu(env));
    }
    mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
        MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
        MSTATUS_MPP | MSTATUS_MXR | MSTATUS_TVM | MSTATUS_TSR |
        MSTATUS_TW;

    if (!riscv_cpu_is_32bit(env)) {
        /*
         * RV32: MPV and GVA are not in mstatus. The current plan is to
         * add them to mstatush. For now, we just don't support it.
         */
        mask |= MSTATUS_MPV | MSTATUS_GVA;
    }

    mstatus = (mstatus & ~mask) | (val & mask);

    dirty = ((mstatus & MSTATUS_FS) == MSTATUS_FS) |
            ((mstatus & MSTATUS_XS) == MSTATUS_XS);
    mstatus = set_field(mstatus, MSTATUS_SD, dirty);
    env->mstatus = mstatus;

    return 0;
}

static int read_mstatush(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mstatus >> 32;
    return 0;
}

static int write_mstatush(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t valh = (uint64_t)val << 32;
    uint64_t mask = MSTATUS_MPV | MSTATUS_GVA;

    if ((valh ^ env->mstatus) & (MSTATUS_MPV)) {
        tlb_flush(env_cpu(env));
    }

    env->mstatus = (env->mstatus & ~mask) | (valh & mask);

    return 0;
}

static int read_misa(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->misa;
    return 0;
}

static int write_misa(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MISA)) {
        /* drop write to misa */
        return 0;
    }

    /* 'I' or 'E' must be present */
    if (!(val & (RVI | RVE))) {
        /* It is not, drop write to misa */
        return 0;
    }

    /* 'E' excludes all other extensions */
    if (val & RVE) {
        /* when we support 'E' we can do "val = RVE;" however
         * for now we just drop writes if 'E' is present.
         */
        return 0;
    }

    /* Mask extensions that are not supported by this hart */
    val &= env->misa_mask;

    /* Mask extensions that are not supported by QEMU */
    val &= (RVI | RVE | RVM | RVA | RVF | RVD | RVC | RVS | RVU);

    /* 'D' depends on 'F', so clear 'D' if 'F' is not present */
    if ((val & RVD) && !(val & RVF)) {
        val &= ~RVD;
    }

    /* Suppress 'C' if next instruction is not aligned
     * TODO: this should check next_pc
     */
    if ((val & RVC) && (GETPC() & ~3) != 0) {
        val &= ~RVC;
    }

    /* misa.MXL writes are not supported by QEMU */
    val = (env->misa & MISA_MXL) | (val & ~MISA_MXL);

    /* flush translation cache */
    if (val != env->misa) {
        tb_flush(env_cpu(env));
    }

    env->misa = val;

    return 0;
}

static int read_medeleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->medeleg;
    return 0;
}

static int write_medeleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->medeleg = (env->medeleg & ~delegable_excps) | (val & delegable_excps);
    return 0;
}

static int read_mideleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mideleg;
    return 0;
}

static int write_mideleg(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = delegable_ints & TLOWBITS64;

    env->mideleg = (env->mideleg & ~mask) | (val & mask);
    if (riscv_has_ext(env, RVH)) {
        env->mideleg |= VS_MODE_INTERRUPTS;
    }
    return 0;
}

static int aia_xlate_vs_csrno(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_virt_enabled(env)) {
        return csrno;
    }

    switch (csrno) {
    case CSR_SISELECT:
        return CSR_VSISELECT;
    case CSR_SIREG:
        return CSR_VSIREG;
    case CSR_STOPI:
        return CSR_VSTOPI;
    case CSR_SSETEIPNUM:
        return CSR_VSSETEIPNUM;
    case CSR_SCLREIPNUM:
        return CSR_VSCLREIPNUM;
    case CSR_SSETEIENUM:
        return CSR_VSSETEIENUM;
    case CSR_SCLREIENUM:
        return CSR_VSCLREIENUM;
    default:
        return csrno;
    };
}

static int rmw_xiselect(CPURISCVState *env, int csrno, target_ulong *val,
                        target_ulong new_val, target_ulong write_mask)
{
    target_ulong *iselect;

    switch (csrno) {
    case CSR_MISELECT:
        iselect = &env->miselect;
        break;
    case CSR_SISELECT:
        iselect = riscv_cpu_virt_enabled(env) ?
                  &env->vsiselect : &env->siselect;
        break;
    case CSR_VSISELECT:
        iselect = &env->vsiselect;
        break;
    default:
         return -RISCV_EXCP_ILLEGAL_INST;
    };

    if (val) {
        *val = *iselect;
    }

    if (write_mask) {
        *iselect = (*iselect & ~write_mask) | (new_val & write_mask);
    }

    return 0;
}

static int rmw_iprio(target_ulong iselect, uint8_t *iprio,
                     target_ulong *val, target_ulong new_val,
                     target_ulong write_mask)
{
    int i, firq, nirqs;
    target_ulong old_val;

    if (iselect < ISELECT_IPRIO0 || ISELECT_IPRIO15 < iselect) {
        return -EINVAL;
    }
#if TARGET_LONG_BITS == 64
    if (iselect & 0x1) {
        return -EINVAL;
    }
#endif

    nirqs = 4 * (TARGET_LONG_BITS / 32);
    firq = ((iselect - ISELECT_IPRIO0) / (TARGET_LONG_BITS / 32)) * (nirqs);

    old_val = 0;
    for (i = 0; i < nirqs; i++) {
        old_val |= ((target_ulong)iprio[firq + i]) << (IPRIO_IRQ_BITS * i);
    }

    if (val) {
        *val = old_val;
    }

    if (write_mask) {
        new_val = (old_val & ~write_mask) | (new_val & write_mask);
        for (i = 0; i < nirqs; i++) {
            iprio[firq + i] = (new_val >> (IPRIO_IRQ_BITS * i)) & 0xff;
        }
    }

    return 0;
}

static int rmw_xireg(CPURISCVState *env, int csrno, target_ulong *val,
                     target_ulong new_val, target_ulong write_mask)
{
    bool virt;
    uint8_t *iprio;
    int ret = -EINVAL;
    target_ulong priv, isel, vgein;

    /* Translate CSR number for VS-mode */
    csrno = aia_xlate_vs_csrno(env, csrno);

    /* Decode register details from CSR number */
    virt = false;
    switch (csrno) {
    case CSR_MIREG:
        iprio = env->miprio;
        isel = env->miselect;
        priv = PRV_M;
        break;
    case CSR_SIREG:
        iprio = env->siprio;
        isel = env->siselect;
        priv = PRV_S;
        break;
    case CSR_VSIREG:
        iprio = env->hviprio;
        isel = env->vsiselect;
        priv = PRV_S;
        virt = true;
        break;
    default:
         goto done;
    };

    /* Find the selected guest interrupt file */
    vgein = (virt) ? (env->hstatus & HSTATUS_VGEIN) >> HSTATUS_VGEIN_SHIFT : 0;

    if (ISELECT_IPRIO0 <= isel && isel <= ISELECT_IPRIO15) {
        /* Local interrupt priority registers not available for VS-mode */
        if (!virt) {
            ret = rmw_iprio(isel, iprio, val, new_val, write_mask);
        }
    } else if (ISELECT_IMSIC_FIRST <= isel && isel <= ISELECT_IMSIC_LAST) {
        /* IMSIC registers only available when machine implements it. */
        if (env->imsic_rmw_fn[priv]) {
            /* Selected guest interrupt file should not be zero */
            if (virt && !vgein) {
                goto done;
            }
            /* Call machine specific IMSIC register emulation */
            ret = env->imsic_rmw_fn[priv](env->imsic_rmw_fn_arg[priv],
                                    IMSIC_MAKE_REG(isel, priv, virt, vgein),
                                    val, new_val, write_mask);
        }
    }

done:
    if (ret) {
        return (riscv_cpu_virt_enabled(env) && virt) ?
               -RISCV_EXCP_VIRT_INSTRUCTION_FAULT : -RISCV_EXCP_ILLEGAL_INST;
    }
    return 0;
}

static int read_mtopi(CPURISCVState *env, int csrno, target_ulong *val)
{
    int irq;

    irq = riscv_cpu_mirq_pending(env);
    if (irq <= 0 || irq > 63) {
       *val = 0;
    } else {
       *val = (irq & TOPI_IID_MASK) << TOPI_IID_SHIFT;
       *val |= env->miprio[irq];
    }

    return 0;
}

static int read_xsetclreinum(CPURISCVState *env, int csrno, target_ulong *val)
{
    bool virt;
    int ret = -EINVAL;
    target_ulong vgein, priv;

    /* Translate CSR number for VS-mode */
    csrno = aia_xlate_vs_csrno(env, csrno);

    /* Decode register details from CSR number */
    virt = false;
    switch (csrno) {
    case CSR_MSETEIPNUM:
    case CSR_MCLREIPNUM:
    case CSR_MSETEIENUM:
    case CSR_MCLREIENUM:
        priv = PRV_M;
        break;
    case CSR_SSETEIPNUM:
    case CSR_SCLREIPNUM:
    case CSR_SSETEIENUM:
    case CSR_SCLREIENUM:
        priv = PRV_S;
        break;
    case CSR_VSSETEIPNUM:
    case CSR_VSCLREIPNUM:
    case CSR_VSSETEIENUM:
    case CSR_VSCLREIENUM:
        priv = PRV_S;
        virt = true;
        break;
    default:
         goto done;
    };

    /* IMSIC CSRs only available when machine implements IMSIC. */
    if (!env->imsic_rmw_fn[priv]) {
        goto done;
    }

    /* Find the selected guest interrupt file */
    vgein = (virt) ?
            (env->hstatus & HSTATUS_VGEIN) >> HSTATUS_VGEIN_SHIFT : 0;

    /* Selected guest interrupt file should not be zero */
    if (virt && !vgein) {
        goto done;
    }

    /* Set/Clear CSRs always read zero */
    ret = 0;
    if (val) {
        *val = 0;
    }

done:
    if (ret) {
        return (riscv_cpu_virt_enabled(env) && virt) ?
               -RISCV_EXCP_VIRT_INSTRUCTION_FAULT : -RISCV_EXCP_ILLEGAL_INST;
    }
    return 0;
}

static int write_xsetclreinum(CPURISCVState *env, int csrno, target_ulong val)
{
    int ret = -EINVAL;
    bool set, pend, virt;
    target_ulong priv, isel, vgein;
    target_ulong new_val, write_mask;

    /* Translate CSR number for VS-mode */
    csrno = aia_xlate_vs_csrno(env, csrno);

    /* Decode register details from CSR number */
    virt = set = pend = false;
    switch (csrno) {
    case CSR_MSETEIPNUM:
        priv = PRV_M;
        set = true;
        break;
    case CSR_MCLREIPNUM:
        priv = PRV_M;
        pend = true;
        break;
    case CSR_MSETEIENUM:
        priv = PRV_M;
        set = true;
        break;
    case CSR_MCLREIENUM:
        priv = PRV_M;
        break;
    case CSR_SSETEIPNUM:
        priv = PRV_S;
        set = true;
        pend = true;
        break;
    case CSR_SCLREIPNUM:
        priv = PRV_S;
        pend = true;
        break;
    case CSR_SSETEIENUM:
        priv = PRV_S;
        set = true;
        break;
    case CSR_SCLREIENUM:
        priv = PRV_S;
        break;
    case CSR_VSSETEIPNUM:
        priv = PRV_S;
        virt = true;
        set = true;
        pend = true;
        break;
    case CSR_VSCLREIPNUM:
        priv = PRV_S;
        virt = true;
        pend = true;
        break;
    case CSR_VSSETEIENUM:
        priv = PRV_S;
        virt = true;
        set = true;
        break;
    case CSR_VSCLREIENUM:
        priv = PRV_S;
        virt = true;
        break;
    default:
         goto done;
    };

    /* IMSIC CSRs only available when machine implements IMSIC. */
    if (!env->imsic_rmw_fn[priv]) {
        goto done;
    }

    /* Find target interrupt pending/enable register */
    isel = (val / TARGET_LONG_BITS);
    isel *= (TARGET_LONG_BITS / IMSIC_EIPx_BITS);
    isel += (pend) ? ISELECT_IMSIC_EIP0 : ISELECT_IMSIC_EIE0;

    /* Find the interrupt bit to be set/clear */
    write_mask = ((target_ulong)1) << (val % TARGET_LONG_BITS);
    new_val = (set) ? write_mask : 0;

    /* Find the selected guest interrupt file */
    vgein = (virt) ?
            (env->hstatus & HSTATUS_VGEIN) >> HSTATUS_VGEIN_SHIFT : 0;

    /* Selected guest interrupt file should not be zero */
    if (virt && !vgein) {
        goto done;
    }

    /* Call machine specific IMSIC register emulation */
    ret = env->imsic_rmw_fn[priv](env->imsic_rmw_fn_arg[priv],
                            IMSIC_MAKE_REG(isel, priv, virt, vgein),
                            NULL, new_val, write_mask);

done:
    if (ret) {
        return (riscv_cpu_virt_enabled(env) && virt) ?
               -RISCV_EXCP_VIRT_INSTRUCTION_FAULT : -RISCV_EXCP_ILLEGAL_INST;
    }
    return 0;
}

static int read_xclaimei(CPURISCVState *env, int csrno, target_ulong *val)
{
    bool virt;
    int ret = -EINVAL;
    target_ulong priv, isel, vgein;
    target_ulong topei, write_mask;

    /* Decode register details from CSR number */
    virt = false;
    switch (csrno) {
    case CSR_MCLAIMEI:
        priv = PRV_M;
        break;
    case CSR_SCLAIMEI:
        priv = PRV_S;
        virt = riscv_cpu_virt_enabled(env);
        break;
    default:
        goto done;
    };

    /* IMSIC CSRs only available when machine implements IMSIC. */
    if (!env->imsic_rmw_fn[priv]) {
        goto done;
    }

    /* Find the selected guest interrupt file */
    vgein = (virt) ? (env->hstatus & HSTATUS_VGEIN) >> HSTATUS_VGEIN_SHIFT : 0;

    /* Selected guest interrupt file should not be zero */
    if (virt && !vgein) {
        goto done;
    }

    /* Call machine specific IMSIC register emulation for reading TOPEI */
    ret = env->imsic_rmw_fn[priv](env->imsic_rmw_fn_arg[priv],
                    IMSIC_MAKE_REG(ISELECT_IMSIC_TOPEI, priv, virt, vgein),
                    &topei, 0, 0);
    if (ret) {
        goto done;
    }

    /* If no interrupt pending then we are done */
    if (!topei) {
        goto update_retval_done;
    }

    /* Find target interrupt pending register */
    isel = ((topei >> TOPI_IID_SHIFT) / TARGET_LONG_BITS);
    isel *= (TARGET_LONG_BITS / IMSIC_EIPx_BITS);
    isel += ISELECT_IMSIC_EIP0;

    /* Find the interrupt bit to be cleared */
    write_mask = ((target_ulong)1) <<
                 ((topei >> TOPI_IID_SHIFT) % TARGET_LONG_BITS);

    /* Call machine specific IMSIC register emulation to clear pending bit */
    ret = env->imsic_rmw_fn[priv](env->imsic_rmw_fn_arg[priv],
                            IMSIC_MAKE_REG(isel, priv, virt, vgein),
                            NULL, 0, write_mask);

update_retval_done:
    /* Update return value */
    if (val) {
        *val = topei;
    }

done:
    if (ret) {
        return (riscv_cpu_virt_enabled(env) && virt) ?
               -RISCV_EXCP_VIRT_INSTRUCTION_FAULT : -RISCV_EXCP_ILLEGAL_INST;
    }
    return 0;
}

static int read_midelegh(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = (env->mideleg >> 32);
    return 0;
}

static int write_midelegh(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = delegable_ints & ~TLOWBITS64;
    uint64_t newval = ((uint64_t)val) << 32;

    env->mideleg = (env->mideleg & ~mask) | (newval & mask);
    if (riscv_has_ext(env, RVH)) {
        env->mideleg |= VS_MODE_INTERRUPTS;
    }
    return 0;
}

static int read_mie(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mie;
    return 0;
}

static int write_mie(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = all_ints & TLOWBITS64;

    env->mie = (env->mie & ~mask) | (val & mask);
    return 0;
}

static int read_mieh(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = (env->mie >> 32);
    return 0;
}

static int write_mieh(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = all_ints & ~TLOWBITS64;
    uint64_t newval = ((uint64_t)val) << 32;

    env->mie = (env->mie & ~mask) | (newval & mask);
    return 0;
}

static int read_mtvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtvec;
    return 0;
}

static int write_mtvec(CPURISCVState *env, int csrno, target_ulong val)
{
    /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
    if ((val & 3) < 2) {
        env->mtvec = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "CSR_MTVEC: reserved mode not supported\n");
    }
    return 0;
}

static int read_mcounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mcounteren;
    return 0;
}

static int write_mcounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mcounteren = val;
    return 0;
}

/* This regiser is replaced with CSR_MCOUNTINHIBIT in 1.11.0 */
static int read_mscounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (env->priv_ver < PRIV_VERSION_1_11_0) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    *val = env->mcounteren;
    return 0;
}

/* This regiser is replaced with CSR_MCOUNTINHIBIT in 1.11.0 */
static int write_mscounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    if (env->priv_ver < PRIV_VERSION_1_11_0) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    env->mcounteren = val;
    return 0;
}

/* Machine Trap Handling */
static int read_mscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mscratch;
    return 0;
}

static int write_mscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mscratch = val;
    return 0;
}

static int read_mepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mepc;
    return 0;
}

static int write_mepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mepc = val;
    return 0;
}

static int read_mcause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mcause;
    return 0;
}

static int write_mcause(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mcause = val;
    return 0;
}

static int read_mbadaddr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mbadaddr;
    return 0;
}

static int write_mbadaddr(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mbadaddr = val;
    return 0;
}

static int rmw_mip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    uint64_t mask = ((uint64_t)write_mask) & delegable_ints &
                    ~env->miclaim & TLOWBITS64;
    uint64_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip;
    }

    return 0;
}

static int rmw_miph(CPURISCVState *env, int csrno, target_ulong *ret_value,
                    target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    uint64_t mask = ((uint64_t)write_mask << 32) & delegable_ints &
                    ~env->miclaim & ~TLOWBITS64;
    uint64_t new_value64 = (uint64_t)new_value << 32;
    uint64_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value64 & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip >> 32;
    }

    return 0;
}

/* Supervisor Trap Setup */
static int read_sstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    target_ulong mask = (sstatus_v1_10_mask);
    *val = env->mstatus & mask;
    return 0;
}

static int write_sstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong mask = (sstatus_v1_10_mask);
    target_ulong newval = (env->mstatus & ~mask) | (val & mask);
    return write_mstatus(env, CSR_MSTATUS, newval);
}

static int read_vsie(CPURISCVState *env, int csrno, target_ulong *val)
{
    uint64_t mask = VS_MODE_INTERRUPTS & env->hideleg & TLOWBITS64;

    /* Shift the VS bits to their S bit location in vsie */
    *val = (env->mie & mask) >> 1;
    return 0;
}

static int read_vsieh(CPURISCVState *env, int csrno, target_ulong *val)
{
    uint64_t mask = VS_MODE_INTERRUPTS & env->hideleg & ~TLOWBITS64;

    /* Shift the VS bits to their S bit location in vsieh */
    *val = (env->mie & mask) >> (32 + 1);
    return 0;
}

static int read_sie(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (riscv_cpu_virt_enabled(env)) {
        if (env->hvicontrol & HVICONTROL_VTI) {
            return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        return read_vsie(env, CSR_VSIE, val);
    }

    *val = env->mie & env->mideleg;
    return 0;
}

static int write_vsie(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = VS_MODE_INTERRUPTS & env->hideleg &
                    all_ints & TLOWBITS64;

    /* Shift the S bits to their VS bit location in mie */
    env->mie = (env->mie & ~mask) | ((val << 1) & mask);

    return 0;
}

static int write_vsieh(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = VS_MODE_INTERRUPTS & env->hideleg &
                    all_ints & ~TLOWBITS64;
    uint64_t newval = (uint64_t)val << 32;

    /* Shift the S bits to their VS bit location in mie */
    env->mie = (env->mie & ~mask) | ((newval << 1) & mask);

    return 0;
}

static int write_sie(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask;

    if (riscv_cpu_virt_enabled(env)) {
        if (env->hvicontrol & HVICONTROL_VTI) {
            return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        return write_vsie(env, CSR_VSIE, val);
    }

    mask = S_MODE_INTERRUPTS & env->mideleg & all_ints & TLOWBITS64;
    env->mie = (env->mie & ~mask) | ((uint64_t)val & mask);

    return 0;
}

static int read_sieh(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (riscv_cpu_virt_enabled(env)) {
        if (env->hvicontrol & HVICONTROL_VTI) {
            return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        return read_vsieh(env, CSR_VSIEH, val);
    }

    *val = ((env->mie & env->mideleg) >> 32);
    return 0;
}

static int write_sieh(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask, newval;

    if (riscv_cpu_virt_enabled(env)) {
        if (env->hvicontrol & HVICONTROL_VTI) {
            return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        return write_vsieh(env, CSR_VSIEH, val);
    }

    mask = S_MODE_INTERRUPTS & env->mideleg & all_ints & ~TLOWBITS64;
    newval = (uint64_t)val << 32;

    env->mie = (env->mie & ~mask) | (newval & mask);
    return 0;
}

static int read_stvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->stvec;
    return 0;
}

static int write_stvec(CPURISCVState *env, int csrno, target_ulong val)
{
    /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
    if ((val & 3) < 2) {
        env->stvec = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "CSR_STVEC: reserved mode not supported\n");
    }
    return 0;
}

static int read_scounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->scounteren;
    return 0;
}

static int write_scounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    env->scounteren = val;
    return 0;
}

/* Supervisor Trap Handling */
static int read_sscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sscratch;
    return 0;
}

static int write_sscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sscratch = val;
    return 0;
}

static int read_sepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sepc;
    return 0;
}

static int write_sepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sepc = val;
    return 0;
}

static int read_scause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->scause;
    return 0;
}

static int write_scause(CPURISCVState *env, int csrno, target_ulong val)
{
    env->scause = val;
    return 0;
}

static int read_sbadaddr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sbadaddr;
    return 0;
}

static int write_sbadaddr(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sbadaddr = val;
    return 0;
}

static int rmw_vsip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                    target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    uint64_t mask = ((uint64_t)write_mask << 1) & delegable_ints &
                    vsip_writable_mask & env->hideleg &
                    ~env->miclaim & TLOWBITS64;
    uint64_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip & VS_MODE_INTERRUPTS;
    }

    return 0;
}

static int rmw_vsiph(CPURISCVState *env, int csrno, target_ulong *ret_value,
                     target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    uint64_t mask = ((uint64_t)write_mask << (32 + 1)) & delegable_ints &
                    vsip_writable_mask & env->hideleg &
                    ~env->miclaim & ~TLOWBITS64;
    uint64_t new_value64 = (uint64_t)new_value << 32;
    uint64_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value64 & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = (old_mip & VS_MODE_INTERRUPTS) >> 32;
    }

    return 0;
}

static int rmw_sip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint64_t mask, old_mip;

    if (riscv_cpu_virt_enabled(env)) {
        if (env->hvicontrol & HVICONTROL_VTI) {
            return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        return rmw_vsip(env, CSR_VSIP, ret_value, new_value, write_mask);
    }

    /* Allow software control of delegable interrupts not claimed by hardware */
    mask = ((uint64_t)write_mask) & delegable_ints &
           env->mideleg & sip_writable_mask &
           ~env->miclaim & TLOWBITS64;
    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip;
    }

    return 0;
}

static int rmw_siph(CPURISCVState *env, int csrno, target_ulong *ret_value,
                    target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint64_t mask, new_value64;
    uint64_t old_mip;

    if (riscv_cpu_virt_enabled(env)) {
        if (env->hvicontrol & HVICONTROL_VTI) {
            return -RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
        return rmw_vsiph(env, CSR_VSIPH, ret_value, new_value, write_mask);
    }

    /* Allow software control of delegable interrupts not claimed by hardware */
    mask = ((uint64_t)write_mask << 32) & delegable_ints &
           env->mideleg & sip_writable_mask &
           ~env->miclaim & ~TLOWBITS64;
    new_value64 = (uint64_t)new_value << 32;
    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value64 & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = (old_mip & env->mideleg) >> 32;
    }

    return 0;
}

/* Supervisor Protection and Translation */
static int read_satp(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        *val = 0;
        return 0;
    }

    if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
        return -RISCV_EXCP_ILLEGAL_INST;
    } else {
        *val = env->satp;
    }

    return 0;
}

static int write_satp(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        return 0;
    }
    if (validate_vm(env, get_field(val, SATP_MODE)) &&
        ((val ^ env->satp) & (SATP_MODE | SATP_ASID | SATP_PPN)))
    {
        if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
            return -RISCV_EXCP_ILLEGAL_INST;
        } else {
            if ((val ^ env->satp) & SATP_ASID) {
                tlb_flush(env_cpu(env));
            }
            env->satp = val;
        }
    }
    return 0;
}

static int read_vstopi(CPURISCVState *env, int csrno, target_ulong *val)
{
    int irq, hiid;
    uint8_t hiprio, iprio;

    irq = riscv_cpu_vsirq_pending(env);
    if (irq <= 0 || irq > 63) {
       *val = 0;
    } else {
       *val = (irq & TOPI_IID_MASK) << TOPI_IID_SHIFT;
       iprio = env->hviprio[irq];
       /* TODO: This needs to improve in specification */
       if (!(env->hstatus & HSTATUS_VGEIN)) {
           hiid = (env->hvicontrol & HVICONTROL_IID_MASK) >>
                 HVICONTROL_IID_SHIFT;
           hiprio = env->hvicontrol & HVICONTROL_IPRIO_MASK;
           if (irq == hiid && hiprio) {
               iprio = hiprio;
           }
       }
       *val |= iprio;
    }

    return 0;
}

static int read_stopi(CPURISCVState *env, int csrno, target_ulong *val)
{
    int irq;

    if (riscv_cpu_virt_enabled(env)) {
        return read_vstopi(env, CSR_VSTOPI, val);
    }

    irq = riscv_cpu_sirq_pending(env);
    if (irq <= 0 || irq > 63) {
       *val = 0;
    } else {
       *val = (irq & TOPI_IID_MASK) << TOPI_IID_SHIFT;
       *val |= env->siprio[irq];
    }

    return 0;
}

/* Hypervisor Extensions */
static int read_hstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hstatus;
    if (!riscv_cpu_is_32bit(env)) {
        /* We only support 64-bit VSXL */
        *val = set_field(*val, HSTATUS_VSXL, 2);
    }
    /* We only support little endian */
    *val = set_field(*val, HSTATUS_VSBE, 0);
    return 0;
}

static int write_hstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hstatus = val;
    if (!riscv_cpu_is_32bit(env) && get_field(val, HSTATUS_VSXL) != 2) {
        qemu_log_mask(LOG_UNIMP, "QEMU does not support mixed HSXLEN options.");
    }
    if (get_field(val, HSTATUS_VSBE) != 0) {
        qemu_log_mask(LOG_UNIMP, "QEMU does not support big endian guests.");
    }
    return 0;
}

static int read_hedeleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hedeleg;
    return 0;
}

static int write_hedeleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hedeleg = val;
    return 0;
}

static int read_hideleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hideleg;
    return 0;
}

static int write_hideleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hideleg = val;
    return 0;
}

static int read_hidelegh(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hideleg >> 32;
    return 0;
}

static int write_hidelegh(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = ~TLOWBITS64;
    uint64_t newval = ((uint64_t)val) << 32;

    env->hideleg = (env->hideleg & ~mask) | (newval & mask);

    return 0;
}

static int rmw_hvip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    uint64_t mask = ((uint64_t)write_mask) & delegable_ints &
                    hvip_writable_mask &
                    ~env->miclaim & TLOWBITS64;
    uint64_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip & hvip_writable_mask;
    }

    return 0;
}

static int rmw_hviph(CPURISCVState *env, int csrno, target_ulong *ret_value,
                    target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    uint64_t mask = ((uint64_t)write_mask << 32) & delegable_ints &
                    hvip_writable_mask &
                    ~env->miclaim & ~TLOWBITS64;
    uint64_t new_value64 = (uint64_t)new_value << 32;
    uint64_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value64 & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = (old_mip & hvip_writable_mask) >> 32;
    }

    return 0;
}

static int rmw_hip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    uint64_t mask = ((uint64_t)write_mask) & delegable_ints &
                    hip_writable_mask &
                    ~env->miclaim & TLOWBITS64;
    uint64_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip & hip_writable_mask;
    }

    return 0;
}

static int read_hie(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mie & VS_MODE_INTERRUPTS;
    return 0;
}

static int write_hie(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = VS_MODE_INTERRUPTS & all_ints & TLOWBITS64;
    env->mie = (env->mie & ~mask) | ((uint64_t)val & mask);
    return 0;
}

static int read_hcounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hcounteren;
    return 0;
}

static int write_hcounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hcounteren = val;
    return 0;
}

static int read_hgeie(CPURISCVState *env, int csrno, target_ulong *val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return 0;
}

static int write_hgeie(CPURISCVState *env, int csrno, target_ulong val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return 0;
}

static int read_htval(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->htval;
    return 0;
}

static int write_htval(CPURISCVState *env, int csrno, target_ulong val)
{
    env->htval = val;
    return 0;
}

static int read_htinst(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->htinst;
    return 0;
}

static int write_htinst(CPURISCVState *env, int csrno, target_ulong val)
{
    return 0;
}

static int read_hgeip(CPURISCVState *env, int csrno, target_ulong *val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return 0;
}

static int write_hgeip(CPURISCVState *env, int csrno, target_ulong val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return 0;
}

static int read_hgatp(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hgatp;
    return 0;
}

static int write_hgatp(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hgatp = val;
    return 0;
}

static int read_htimedelta(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->htimedelta;
    return 0;
}

static int write_htimedelta(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    if (riscv_cpu_is_32bit(env)) {
        env->htimedelta = deposit64(env->htimedelta, 0, 32, (uint64_t)val);
    } else {
        env->htimedelta = val;
    }
    return 0;
}

static int read_htimedeltah(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->htimedelta >> 32;
    return 0;
}

static int write_htimedeltah(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!env->rdtime_fn) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    env->htimedelta = deposit64(env->htimedelta, 32, 32, (uint64_t)val);
    return 0;
}

static int read_hvicontrol(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hvicontrol;
    return 0;
}

static int write_hvicontrol(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hvicontrol = val & HVICONTROL_VALID_MASK;
    return 0;
}

static int read_hvipriox(CPURISCVState *env, int first_index,
                         uint8_t *iprio, target_ulong *val)
{
    int i, irq, rdzero, num_irqs = 4 * (TARGET_LONG_BITS / 32);

    /* First index has to be multiple of numbe of irqs per register */
    if (first_index % num_irqs) {
        return (riscv_cpu_virt_enabled(env)) ?
               -RISCV_EXCP_VIRT_INSTRUCTION_FAULT : -RISCV_EXCP_ILLEGAL_INST;
    }

    /* Fill-up return value */
    *val = 0;
    for (i = 0; i < num_irqs; i++) {
        if (riscv_cpu_hviprio_index2irq(first_index + i, &irq, &rdzero)) {
            continue;
        }
        if (rdzero) {
            continue;
        }
        *val |= ((target_ulong)iprio[irq]) << (i * 8);
    }

    return 0;
}

static int write_hvipriox(CPURISCVState *env, int first_index,
                          uint8_t *iprio, target_ulong val)
{
    int i, irq, rdzero, num_irqs = 4 * (TARGET_LONG_BITS / 32);

    /* First index has to be multiple of numbe of irqs per register */
    if (first_index % num_irqs) {
        return (riscv_cpu_virt_enabled(env)) ?
               -RISCV_EXCP_VIRT_INSTRUCTION_FAULT : -RISCV_EXCP_ILLEGAL_INST;
    }

    /* Fill-up priority arrary */
    for (i = 0; i < num_irqs; i++) {
        if (riscv_cpu_hviprio_index2irq(first_index + i, &irq, &rdzero)) {
            continue;
        }
        if (rdzero) {
            iprio[irq] = 0;
        } else {
            iprio[irq] = (val >> (i * 8)) & 0xff;
        }
    }

    return 0;
}

static int read_hviprio1(CPURISCVState *env, int csrno, target_ulong *val)
{
    return read_hvipriox(env, 0, env->hviprio, val);
}

static int write_hviprio1(CPURISCVState *env, int csrno, target_ulong val)
{
    return write_hvipriox(env, 0, env->hviprio, val);
}

static int read_hviprio1h(CPURISCVState *env, int csrno, target_ulong *val)
{
    return read_hvipriox(env, 4, env->hviprio, val);
}

static int write_hviprio1h(CPURISCVState *env, int csrno, target_ulong val)
{
    return write_hvipriox(env, 4, env->hviprio, val);
}

static int read_hviprio2(CPURISCVState *env, int csrno, target_ulong *val)
{
    return read_hvipriox(env, 8, env->hviprio, val);
}

static int write_hviprio2(CPURISCVState *env, int csrno, target_ulong val)
{
    return write_hvipriox(env, 8, env->hviprio, val);
}

static int read_hviprio2h(CPURISCVState *env, int csrno, target_ulong *val)
{
    return read_hvipriox(env, 12, env->hviprio, val);
}

static int write_hviprio2h(CPURISCVState *env, int csrno, target_ulong val)
{
    return write_hvipriox(env, 12, env->hviprio, val);
}

/* Virtual CSR Registers */
static int read_vsstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsstatus;
    return 0;
}

static int write_vsstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    uint64_t mask = (target_ulong)-1;
    env->vsstatus = (env->vsstatus & ~mask) | (uint64_t)val;
    return 0;
}

static int read_vstvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstvec;
    return 0;
}

static int write_vstvec(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vstvec = val;
    return 0;
}

static int read_vsscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsscratch;
    return 0;
}

static int write_vsscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsscratch = val;
    return 0;
}

static int read_vsepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsepc;
    return 0;
}

static int write_vsepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsepc = val;
    return 0;
}

static int read_vscause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vscause;
    return 0;
}

static int write_vscause(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vscause = val;
    return 0;
}

static int read_vstval(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstval;
    return 0;
}

static int write_vstval(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vstval = val;
    return 0;
}

static int read_vsatp(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsatp;
    return 0;
}

static int write_vsatp(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsatp = val;
    return 0;
}

static int read_mtval2(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtval2;
    return 0;
}

static int write_mtval2(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mtval2 = val;
    return 0;
}

static int read_mtinst(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtinst;
    return 0;
}

static int write_mtinst(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mtinst = val;
    return 0;
}

/* Physical Memory Protection */
static int read_pmpcfg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = pmpcfg_csr_read(env, csrno - CSR_PMPCFG0);
    return 0;
}

static int write_pmpcfg(CPURISCVState *env, int csrno, target_ulong val)
{
    pmpcfg_csr_write(env, csrno - CSR_PMPCFG0, val);
    return 0;
}

static int read_pmpaddr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = pmpaddr_csr_read(env, csrno - CSR_PMPADDR0);
    return 0;
}

static int write_pmpaddr(CPURISCVState *env, int csrno, target_ulong val)
{
    pmpaddr_csr_write(env, csrno - CSR_PMPADDR0, val);
    return 0;
}

#endif

/*
 * riscv_csrrw - read and/or update control and status register
 *
 * csrr   <->  riscv_csrrw(env, csrno, ret_value, 0, 0);
 * csrrw  <->  riscv_csrrw(env, csrno, ret_value, value, -1);
 * csrrs  <->  riscv_csrrw(env, csrno, ret_value, -1, value);
 * csrrc  <->  riscv_csrrw(env, csrno, ret_value, 0, value);
 */

int riscv_csrrw(CPURISCVState *env, int csrno, target_ulong *ret_value,
                target_ulong new_value, target_ulong write_mask)
{
    int ret;
    target_ulong old_value;
    RISCVCPU *cpu = env_archcpu(env);

    /* check privileges and return -1 if check fails */
#if !defined(CONFIG_USER_ONLY)
    int effective_priv = env->priv;
    int read_only = get_field(csrno, 0xC00) == 3;

    if (riscv_has_ext(env, RVH) &&
        env->priv == PRV_S &&
        !riscv_cpu_virt_enabled(env)) {
        /*
         * We are in S mode without virtualisation, therefore we are in HS Mode.
         * Add 1 to the effective privledge level to allow us to access the
         * Hypervisor CSRs.
         */
        effective_priv++;
    }

    if ((write_mask && read_only) ||
        (!env->debugger && (effective_priv < get_field(csrno, 0x300)))) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif

    /* ensure the CSR extension is enabled. */
    if (!cpu->cfg.ext_icsr) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    /* check predicate */
    if (!csr_ops[csrno].predicate) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }
    ret = csr_ops[csrno].predicate(env, csrno);
    if (ret < 0) {
        return ret;
    }

    /* execute combined read/write operation if it exists */
    if (csr_ops[csrno].op) {
        return csr_ops[csrno].op(env, csrno, ret_value, new_value, write_mask);
    }

    /* if no accessor exists then return failure */
    if (!csr_ops[csrno].read) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    /* read old value */
    ret = csr_ops[csrno].read(env, csrno, &old_value);
    if (ret < 0) {
        return ret;
    }

    /* write value if writable and write mask set, otherwise drop writes */
    if (write_mask) {
        new_value = (old_value & ~write_mask) | (new_value & write_mask);
        if (csr_ops[csrno].write) {
            ret = csr_ops[csrno].write(env, csrno, new_value);
            if (ret < 0) {
                return ret;
            }
        }
    }

    /* return old value */
    if (ret_value) {
        *ret_value = old_value;
    }

    return 0;
}

/*
 * Debugger support.  If not in user mode, set env->debugger before the
 * riscv_csrrw call and clear it after the call.
 */
int riscv_csrrw_debug(CPURISCVState *env, int csrno, target_ulong *ret_value,
                target_ulong new_value, target_ulong write_mask)
{
    int ret;
#if !defined(CONFIG_USER_ONLY)
    env->debugger = true;
#endif
    ret = riscv_csrrw(env, csrno, ret_value, new_value, write_mask);
#if !defined(CONFIG_USER_ONLY)
    env->debugger = false;
#endif
    return ret;
}

/* Control and Status Register function table */
riscv_csr_operations csr_ops[CSR_TABLE_SIZE] = {
    /* User Floating-Point CSRs */
    [CSR_FFLAGS]   = { "fflags",   fs,     read_fflags,  write_fflags },
    [CSR_FRM]      = { "frm",      fs,     read_frm,     write_frm    },
    [CSR_FCSR]     = { "fcsr",     fs,     read_fcsr,    write_fcsr   },
    /* Vector CSRs */
    [CSR_VSTART]   = { "vstart",   vs,     read_vstart,  write_vstart },
    [CSR_VXSAT]    = { "vxsat",    vs,     read_vxsat,   write_vxsat  },
    [CSR_VXRM]     = { "vxrm",     vs,     read_vxrm,    write_vxrm   },
    [CSR_VL]       = { "vl",       vs,     read_vl                    },
    [CSR_VTYPE]    = { "vtype",    vs,     read_vtype                 },
    /* User Timers and Counters */
    [CSR_CYCLE]    = { "cycle",    ctr,    read_instret  },
    [CSR_INSTRET]  = { "instret",  ctr,    read_instret  },
    [CSR_CYCLEH]   = { "cycleh",   ctr32,  read_instreth },
    [CSR_INSTRETH] = { "instreth", ctr32,  read_instreth },

    /*
     * In privileged mode, the monitor will have to emulate TIME CSRs only if
     * rdtime callback is not provided by machine/platform emulation.
     */
    [CSR_TIME]  = { "time",  ctr,   read_time  },
    [CSR_TIMEH] = { "timeh", ctr32, read_timeh },

#if !defined(CONFIG_USER_ONLY)
    /* Machine Timers and Counters */
    [CSR_MCYCLE]    = { "mcycle",    any,   read_instret  },
    [CSR_MINSTRET]  = { "minstret",  any,   read_instret  },
    [CSR_MCYCLEH]   = { "mcycleh",   any32, read_instreth },
    [CSR_MINSTRETH] = { "minstreth", any32, read_instreth },

    /* Machine Information Registers */
    [CSR_MVENDORID] = { "mvendorid", any,   read_zero    },
    [CSR_MARCHID]   = { "marchid",   any,   read_zero    },
    [CSR_MIMPID]    = { "mimpid",    any,   read_zero    },
    [CSR_MHARTID]   = { "mhartid",   any,   read_mhartid },

    /* Machine Trap Setup */
    [CSR_MSTATUS]     = { "mstatus",    any,   read_mstatus,     write_mstatus     },
    [CSR_MISA]        = { "misa",       any,   read_misa,        write_misa        },
    [CSR_MIDELEG]     = { "mideleg",    any,   read_mideleg,     write_mideleg     },
    [CSR_MEDELEG]     = { "medeleg",    any,   read_medeleg,     write_medeleg     },
    [CSR_MIE]         = { "mie",        any,   read_mie,         write_mie         },
    [CSR_MTVEC]       = { "mtvec",      any,   read_mtvec,       write_mtvec       },
    [CSR_MCOUNTEREN]  = { "mcounteren", any,   read_mcounteren,  write_mcounteren  },

    [CSR_MSTATUSH]    = { "mstatush",   any32, read_mstatush,    write_mstatush    },

    [CSR_MSCOUNTEREN] = { "msounteren", any,   read_mscounteren, write_mscounteren },

    /* Machine Trap Handling */
    [CSR_MSCRATCH] = { "mscratch", any,  read_mscratch, write_mscratch },
    [CSR_MEPC]     = { "mepc",     any,  read_mepc,     write_mepc     },
    [CSR_MCAUSE]   = { "mcause",   any,  read_mcause,   write_mcause   },
    [CSR_MBADADDR] = { "mbadaddr", any,  read_mbadaddr, write_mbadaddr },
    [CSR_MIP]      = { "mip",      any,  NULL,    NULL, rmw_mip        },

    /* Machine-Level Window to Indirectly Accessed Registers (AIA) */
    [CSR_MISELECT] = { "miselect", aia_any,   NULL, NULL,    rmw_xiselect },
    [CSR_MIREG]    = { "mireg",    aia_any,   NULL, NULL,    rmw_xireg },

    /* Machine-Level Interrupts (AIA) */
    [CSR_MTOPI]    = { "mtopi",    aia_any,   read_mtopi },

    /* Machine-Level IMSIC Interface (AIA) */
    [CSR_MSETEIPNUM] = { "mseteipnum", aia_any, read_xsetclreinum, write_xsetclreinum },
    [CSR_MCLREIPNUM] = { "mclreipnum", aia_any, read_xsetclreinum, write_xsetclreinum },
    [CSR_MSETEIENUM] = { "mseteienum", aia_any, read_xsetclreinum, write_xsetclreinum },
    [CSR_MCLREIENUM] = { "mclreienum", aia_any, read_xsetclreinum, write_xsetclreinum },
    [CSR_MCLAIMEI]   = { "mclaimei",   aia_any, read_xclaimei },

    /* Machine-Level High-Half CSRs (AIA) */
    [CSR_MIDELEGH] = { "midelegh", aia_any32, read_midelegh, write_midelegh },
    [CSR_MIEH]     = { "mieh",     aia_any32, read_mieh,     write_mieh     },
    [CSR_MIPH]     = { "miph",     aia_any32, NULL,    NULL, rmw_miph       },

    /* Supervisor Trap Setup */
    [CSR_SSTATUS]    = { "sstatus",    smode, read_sstatus,    write_sstatus    },
    [CSR_SIE]        = { "sie",        smode, read_sie,        write_sie        },
    [CSR_STVEC]      = { "stvec",      smode, read_stvec,      write_stvec      },
    [CSR_SCOUNTEREN] = { "scounteren", smode, read_scounteren, write_scounteren },

    /* Supervisor Trap Handling */
    [CSR_SSCRATCH] = { "sscratch", smode, read_sscratch, write_sscratch },
    [CSR_SEPC]     = { "sepc",     smode, read_sepc,     write_sepc     },
    [CSR_SCAUSE]   = { "scause",   smode, read_scause,   write_scause   },
    [CSR_SBADADDR] = { "sbadaddr", smode, read_sbadaddr, write_sbadaddr },
    [CSR_SIP]      = { "sip",      smode, NULL,    NULL, rmw_sip        },

    /* Supervisor Protection and Translation */
    [CSR_SATP]     = { "satp",     smode, read_satp,    write_satp      },

    /* Supervisor-Level Window to Indirectly Accessed Registers (AIA) */
    [CSR_SISELECT]   = { "siselect",   aia_smode, NULL, NULL, rmw_xiselect },
    [CSR_SIREG]      = { "sireg",      aia_smode, NULL, NULL, rmw_xireg },

    /* Supervisor-Level Interrupts (AIA) */
    [CSR_STOPI]      = { "stopi",      aia_smode, read_stopi },

    /* Supervisor-Level IMSIC Interface (AIA) */
    [CSR_SSETEIPNUM] = { "sseteipnum", aia_smode, read_xsetclreinum, write_xsetclreinum },
    [CSR_SCLREIPNUM] = { "sclreipnum", aia_smode, read_xsetclreinum, write_xsetclreinum },
    [CSR_SSETEIENUM] = { "sseteienum", aia_smode, read_xsetclreinum, write_xsetclreinum },
    [CSR_SCLREIENUM] = { "sclreienum", aia_smode, read_xsetclreinum, write_xsetclreinum },
    [CSR_SCLAIMEI]   = { "sclaimei",   aia_smode, read_xclaimei },

    /* Supervisor-Level High-Half CSRs (AIA) */
    [CSR_SIEH]       = { "sieh",       aia_smode32, read_sieh,  write_sieh },
    [CSR_SIPH]       = { "siph",       aia_smode32, NULL, NULL, rmw_siph   },

    [CSR_HSTATUS]     = { "hstatus",     hmode,   read_hstatus,     write_hstatus     },
    [CSR_HEDELEG]     = { "hedeleg",     hmode,   read_hedeleg,     write_hedeleg     },
    [CSR_HIDELEG]     = { "hideleg",     hmode,   read_hideleg,     write_hideleg     },
    [CSR_HVIP]        = { "hvip",        hmode,   NULL,   NULL,     rmw_hvip          },
    [CSR_HIP]         = { "hip",         hmode,   NULL,   NULL,     rmw_hip           },
    [CSR_HIE]         = { "hie",         hmode,   read_hie,         write_hie         },
    [CSR_HCOUNTEREN]  = { "hcounteren",  hmode,   read_hcounteren,  write_hcounteren  },
    [CSR_HGEIE]       = { "hgeie",       hmode,   read_hgeie,       write_hgeie       },
    [CSR_HTVAL]       = { "htval",       hmode,   read_htval,       write_htval       },
    [CSR_HTINST]      = { "htinst",      hmode,   read_htinst,      write_htinst      },
    [CSR_HGEIP]       = { "hgeip",       hmode,   read_hgeip,       write_hgeip       },
    [CSR_HGATP]       = { "hgatp",       hmode,   read_hgatp,       write_hgatp       },
    [CSR_HTIMEDELTA]  = { "htimedelta",  hmode,   read_htimedelta,  write_htimedelta  },
    [CSR_HTIMEDELTAH] = { "htimedeltah", hmode32, read_htimedeltah, write_htimedeltah },

    [CSR_VSSTATUS]    = { "vsstatus",    hmode,   read_vsstatus,    write_vsstatus    },
    [CSR_VSIP]        = { "vsip",        hmode,   NULL,    NULL,    rmw_vsip          },
    [CSR_VSIE]        = { "vsie",        hmode,   read_vsie,        write_vsie        },
    [CSR_VSTVEC]      = { "vstvec",      hmode,   read_vstvec,      write_vstvec      },
    [CSR_VSSCRATCH]   = { "vsscratch",   hmode,   read_vsscratch,   write_vsscratch   },
    [CSR_VSEPC]       = { "vsepc",       hmode,   read_vsepc,       write_vsepc       },
    [CSR_VSCAUSE]     = { "vscause",     hmode,   read_vscause,     write_vscause     },
    [CSR_VSTVAL]      = { "vstval",      hmode,   read_vstval,      write_vstval      },
    [CSR_VSATP]       = { "vsatp",       hmode,   read_vsatp,       write_vsatp       },

    [CSR_MTVAL2]      = { "mtval2",      hmode,   read_mtval2,      write_mtval2      },
    [CSR_MTINST]      = { "mtinst",      hmode,   read_mtinst,      write_mtinst      },

    /* Virtual Interrupts and Interrupt Priorities (H-extension with AIA) */
    [CSR_HVICONTROL]  = { "hvicontrol",  aia_hmode, read_hvicontrol, write_hvicontrol },
    [CSR_HVIPRIO1]    = { "hviprio1",    aia_hmode, read_hviprio1,   write_hviprio1 },
    [CSR_HVIPRIO2]    = { "hviprio2",    aia_hmode, read_hviprio2,   write_hviprio2 },

    /* VS-Level Window to Indirectly Accessed Registers (H-extension with AIA) */
    [CSR_VSISELECT]   = { "vsiselect",   aia_hmode, NULL, NULL,      rmw_xiselect },
    [CSR_VSIREG]      = { "vsireg",      aia_hmode, NULL, NULL,      rmw_xireg },

    /* VS-Level Interrupts (H-extension with AIA) */
    [CSR_VSTOPI]      = { "vstopi",      aia_hmode, read_vstopi },

    /* VS-Level IMSIC Interface (H-extension with AIA) */
    [CSR_VSSETEIPNUM] = { "vsseteipnum", aia_hmode, read_xsetclreinum, write_xsetclreinum },
    [CSR_VSCLREIPNUM] = { "vsclreipnum", aia_hmode, read_xsetclreinum, write_xsetclreinum },
    [CSR_VSSETEIENUM] = { "vsseteienum", aia_hmode, read_xsetclreinum, write_xsetclreinum },
    [CSR_VSCLREIENUM] = { "vsclreienum", aia_hmode, read_xsetclreinum, write_xsetclreinum },

    /* Hypervisor and VS-Level High-Half CSRs (H-extension with AIA) */
    [CSR_HIDELEGH]    = { "hidelegh",    aia_hmode32, read_hidelegh,  write_hidelegh },
    [CSR_HVIPH]       = { "hviph",       aia_hmode32, NULL, NULL,     rmw_hviph },
    [CSR_HVIPRIO1H]   = { "hviprio1h",   aia_hmode32, read_hviprio1h, write_hviprio1h },
    [CSR_HVIPRIO2H]   = { "hviprio2h",   aia_hmode32, read_hviprio2h, write_hviprio2h },
    [CSR_VSIEH]       = { "vsieh",       aia_hmode32, read_vsieh,     write_vsieh },
    [CSR_VSIPH]       = { "vsiep",       aia_hmode32, NULL, NULL,     rmw_vsiph },

    /* Physical Memory Protection */
    [CSR_PMPCFG0]    = { "pmpcfg0",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG1]    = { "pmpcfg1",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG2]    = { "pmpcfg2",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG3]    = { "pmpcfg3",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPADDR0]   = { "pmpaddr0",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR1]   = { "pmpaddr1",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR2]   = { "pmpaddr2",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR3]   = { "pmpaddr3",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR4]   = { "pmpaddr4",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR5]   = { "pmpaddr5",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR6]   = { "pmpaddr6",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR7]   = { "pmpaddr7",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR8]   = { "pmpaddr8",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR9]   = { "pmpaddr9",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR10]  = { "pmpaddr10", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR11]  = { "pmpaddr11", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR12]  = { "pmpaddr12", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR13]  = { "pmpaddr13", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR14] =  { "pmpaddr14", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR15] =  { "pmpaddr15", pmp, read_pmpaddr, write_pmpaddr },

    /* Performance Counters */
    [CSR_HPMCOUNTER3]    = { "hpmcounter3",    ctr,    read_zero },
    [CSR_HPMCOUNTER4]    = { "hpmcounter4",    ctr,    read_zero },
    [CSR_HPMCOUNTER5]    = { "hpmcounter5",    ctr,    read_zero },
    [CSR_HPMCOUNTER6]    = { "hpmcounter6",    ctr,    read_zero },
    [CSR_HPMCOUNTER7]    = { "hpmcounter7",    ctr,    read_zero },
    [CSR_HPMCOUNTER8]    = { "hpmcounter8",    ctr,    read_zero },
    [CSR_HPMCOUNTER9]    = { "hpmcounter9",    ctr,    read_zero },
    [CSR_HPMCOUNTER10]   = { "hpmcounter10",   ctr,    read_zero },
    [CSR_HPMCOUNTER11]   = { "hpmcounter11",   ctr,    read_zero },
    [CSR_HPMCOUNTER12]   = { "hpmcounter12",   ctr,    read_zero },
    [CSR_HPMCOUNTER13]   = { "hpmcounter13",   ctr,    read_zero },
    [CSR_HPMCOUNTER14]   = { "hpmcounter14",   ctr,    read_zero },
    [CSR_HPMCOUNTER15]   = { "hpmcounter15",   ctr,    read_zero },
    [CSR_HPMCOUNTER16]   = { "hpmcounter16",   ctr,    read_zero },
    [CSR_HPMCOUNTER17]   = { "hpmcounter17",   ctr,    read_zero },
    [CSR_HPMCOUNTER18]   = { "hpmcounter18",   ctr,    read_zero },
    [CSR_HPMCOUNTER19]   = { "hpmcounter19",   ctr,    read_zero },
    [CSR_HPMCOUNTER20]   = { "hpmcounter20",   ctr,    read_zero },
    [CSR_HPMCOUNTER21]   = { "hpmcounter21",   ctr,    read_zero },
    [CSR_HPMCOUNTER22]   = { "hpmcounter22",   ctr,    read_zero },
    [CSR_HPMCOUNTER23]   = { "hpmcounter23",   ctr,    read_zero },
    [CSR_HPMCOUNTER24]   = { "hpmcounter24",   ctr,    read_zero },
    [CSR_HPMCOUNTER25]   = { "hpmcounter25",   ctr,    read_zero },
    [CSR_HPMCOUNTER26]   = { "hpmcounter26",   ctr,    read_zero },
    [CSR_HPMCOUNTER27]   = { "hpmcounter27",   ctr,    read_zero },
    [CSR_HPMCOUNTER28]   = { "hpmcounter28",   ctr,    read_zero },
    [CSR_HPMCOUNTER29]   = { "hpmcounter29",   ctr,    read_zero },
    [CSR_HPMCOUNTER30]   = { "hpmcounter30",   ctr,    read_zero },
    [CSR_HPMCOUNTER31]   = { "hpmcounter31",   ctr,    read_zero },

    [CSR_MHPMCOUNTER3]   = { "mhpmcounter3",   any,    read_zero },
    [CSR_MHPMCOUNTER4]   = { "mhpmcounter4",   any,    read_zero },
    [CSR_MHPMCOUNTER5]   = { "mhpmcounter5",   any,    read_zero },
    [CSR_MHPMCOUNTER6]   = { "mhpmcounter6",   any,    read_zero },
    [CSR_MHPMCOUNTER7]   = { "mhpmcounter7",   any,    read_zero },
    [CSR_MHPMCOUNTER8]   = { "mhpmcounter8",   any,    read_zero },
    [CSR_MHPMCOUNTER9]   = { "mhpmcounter9",   any,    read_zero },
    [CSR_MHPMCOUNTER10]  = { "mhpmcounter10",  any,    read_zero },
    [CSR_MHPMCOUNTER11]  = { "mhpmcounter11",  any,    read_zero },
    [CSR_MHPMCOUNTER12]  = { "mhpmcounter12",  any,    read_zero },
    [CSR_MHPMCOUNTER13]  = { "mhpmcounter13",  any,    read_zero },
    [CSR_MHPMCOUNTER14]  = { "mhpmcounter14",  any,    read_zero },
    [CSR_MHPMCOUNTER15]  = { "mhpmcounter15",  any,    read_zero },
    [CSR_MHPMCOUNTER16]  = { "mhpmcounter16",  any,    read_zero },
    [CSR_MHPMCOUNTER17]  = { "mhpmcounter17",  any,    read_zero },
    [CSR_MHPMCOUNTER18]  = { "mhpmcounter18",  any,    read_zero },
    [CSR_MHPMCOUNTER19]  = { "mhpmcounter19",  any,    read_zero },
    [CSR_MHPMCOUNTER20]  = { "mhpmcounter20",  any,    read_zero },
    [CSR_MHPMCOUNTER21]  = { "mhpmcounter21",  any,    read_zero },
    [CSR_MHPMCOUNTER22]  = { "mhpmcounter22",  any,    read_zero },
    [CSR_MHPMCOUNTER23]  = { "mhpmcounter23",  any,    read_zero },
    [CSR_MHPMCOUNTER24]  = { "mhpmcounter24",  any,    read_zero },
    [CSR_MHPMCOUNTER25]  = { "mhpmcounter25",  any,    read_zero },
    [CSR_MHPMCOUNTER26]  = { "mhpmcounter26",  any,    read_zero },
    [CSR_MHPMCOUNTER27]  = { "mhpmcounter27",  any,    read_zero },
    [CSR_MHPMCOUNTER28]  = { "mhpmcounter28",  any,    read_zero },
    [CSR_MHPMCOUNTER29]  = { "mhpmcounter29",  any,    read_zero },
    [CSR_MHPMCOUNTER30]  = { "mhpmcounter30",  any,    read_zero },
    [CSR_MHPMCOUNTER31]  = { "mhpmcounter31",  any,    read_zero },

    [CSR_MHPMEVENT3]     = { "mhpmevent3",     any,    read_zero },
    [CSR_MHPMEVENT4]     = { "mhpmevent4",     any,    read_zero },
    [CSR_MHPMEVENT5]     = { "mhpmevent5",     any,    read_zero },
    [CSR_MHPMEVENT6]     = { "mhpmevent6",     any,    read_zero },
    [CSR_MHPMEVENT7]     = { "mhpmevent7",     any,    read_zero },
    [CSR_MHPMEVENT8]     = { "mhpmevent8",     any,    read_zero },
    [CSR_MHPMEVENT9]     = { "mhpmevent9",     any,    read_zero },
    [CSR_MHPMEVENT10]    = { "mhpmevent10",    any,    read_zero },
    [CSR_MHPMEVENT11]    = { "mhpmevent11",    any,    read_zero },
    [CSR_MHPMEVENT12]    = { "mhpmevent12",    any,    read_zero },
    [CSR_MHPMEVENT13]    = { "mhpmevent13",    any,    read_zero },
    [CSR_MHPMEVENT14]    = { "mhpmevent14",    any,    read_zero },
    [CSR_MHPMEVENT15]    = { "mhpmevent15",    any,    read_zero },
    [CSR_MHPMEVENT16]    = { "mhpmevent16",    any,    read_zero },
    [CSR_MHPMEVENT17]    = { "mhpmevent17",    any,    read_zero },
    [CSR_MHPMEVENT18]    = { "mhpmevent18",    any,    read_zero },
    [CSR_MHPMEVENT19]    = { "mhpmevent19",    any,    read_zero },
    [CSR_MHPMEVENT20]    = { "mhpmevent20",    any,    read_zero },
    [CSR_MHPMEVENT21]    = { "mhpmevent21",    any,    read_zero },
    [CSR_MHPMEVENT22]    = { "mhpmevent22",    any,    read_zero },
    [CSR_MHPMEVENT23]    = { "mhpmevent23",    any,    read_zero },
    [CSR_MHPMEVENT24]    = { "mhpmevent24",    any,    read_zero },
    [CSR_MHPMEVENT25]    = { "mhpmevent25",    any,    read_zero },
    [CSR_MHPMEVENT26]    = { "mhpmevent26",    any,    read_zero },
    [CSR_MHPMEVENT27]    = { "mhpmevent27",    any,    read_zero },
    [CSR_MHPMEVENT28]    = { "mhpmevent28",    any,    read_zero },
    [CSR_MHPMEVENT29]    = { "mhpmevent29",    any,    read_zero },
    [CSR_MHPMEVENT30]    = { "mhpmevent30",    any,    read_zero },
    [CSR_MHPMEVENT31]    = { "mhpmevent31",    any,    read_zero },

    [CSR_HPMCOUNTER3H]   = { "hpmcounter3h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER4H]   = { "hpmcounter4h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER5H]   = { "hpmcounter5h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER6H]   = { "hpmcounter6h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER7H]   = { "hpmcounter7h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER8H]   = { "hpmcounter8h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER9H]   = { "hpmcounter9h",   ctr32,  read_zero },
    [CSR_HPMCOUNTER10H]  = { "hpmcounter10h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER11H]  = { "hpmcounter11h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER12H]  = { "hpmcounter12h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER13H]  = { "hpmcounter13h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER14H]  = { "hpmcounter14h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER15H]  = { "hpmcounter15h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER16H]  = { "hpmcounter16h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER17H]  = { "hpmcounter17h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER18H]  = { "hpmcounter18h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER19H]  = { "hpmcounter19h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER20H]  = { "hpmcounter20h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER21H]  = { "hpmcounter21h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER22H]  = { "hpmcounter22h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER23H]  = { "hpmcounter23h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER24H]  = { "hpmcounter24h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER25H]  = { "hpmcounter25h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER26H]  = { "hpmcounter26h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER27H]  = { "hpmcounter27h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER28H]  = { "hpmcounter28h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER29H]  = { "hpmcounter29h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER30H]  = { "hpmcounter30h",  ctr32,  read_zero },
    [CSR_HPMCOUNTER31H]  = { "hpmcounter31h",  ctr32,  read_zero },

    [CSR_MHPMCOUNTER3H]  = { "mhpmcounter3h",  any32,  read_zero },
    [CSR_MHPMCOUNTER4H]  = { "mhpmcounter4h",  any32,  read_zero },
    [CSR_MHPMCOUNTER5H]  = { "mhpmcounter5h",  any32,  read_zero },
    [CSR_MHPMCOUNTER6H]  = { "mhpmcounter6h",  any32,  read_zero },
    [CSR_MHPMCOUNTER7H]  = { "mhpmcounter7h",  any32,  read_zero },
    [CSR_MHPMCOUNTER8H]  = { "mhpmcounter8h",  any32,  read_zero },
    [CSR_MHPMCOUNTER9H]  = { "mhpmcounter9h",  any32,  read_zero },
    [CSR_MHPMCOUNTER10H] = { "mhpmcounter10h", any32,  read_zero },
    [CSR_MHPMCOUNTER11H] = { "mhpmcounter11h", any32,  read_zero },
    [CSR_MHPMCOUNTER12H] = { "mhpmcounter12h", any32,  read_zero },
    [CSR_MHPMCOUNTER13H] = { "mhpmcounter13h", any32,  read_zero },
    [CSR_MHPMCOUNTER14H] = { "mhpmcounter14h", any32,  read_zero },
    [CSR_MHPMCOUNTER15H] = { "mhpmcounter15h", any32,  read_zero },
    [CSR_MHPMCOUNTER16H] = { "mhpmcounter16h", any32,  read_zero },
    [CSR_MHPMCOUNTER17H] = { "mhpmcounter17h", any32,  read_zero },
    [CSR_MHPMCOUNTER18H] = { "mhpmcounter18h", any32,  read_zero },
    [CSR_MHPMCOUNTER19H] = { "mhpmcounter19h", any32,  read_zero },
    [CSR_MHPMCOUNTER20H] = { "mhpmcounter20h", any32,  read_zero },
    [CSR_MHPMCOUNTER21H] = { "mhpmcounter21h", any32,  read_zero },
    [CSR_MHPMCOUNTER22H] = { "mhpmcounter22h", any32,  read_zero },
    [CSR_MHPMCOUNTER23H] = { "mhpmcounter23h", any32,  read_zero },
    [CSR_MHPMCOUNTER24H] = { "mhpmcounter24h", any32,  read_zero },
    [CSR_MHPMCOUNTER25H] = { "mhpmcounter25h", any32,  read_zero },
    [CSR_MHPMCOUNTER26H] = { "mhpmcounter26h", any32,  read_zero },
    [CSR_MHPMCOUNTER27H] = { "mhpmcounter27h", any32,  read_zero },
    [CSR_MHPMCOUNTER28H] = { "mhpmcounter28h", any32,  read_zero },
    [CSR_MHPMCOUNTER29H] = { "mhpmcounter29h", any32,  read_zero },
    [CSR_MHPMCOUNTER30H] = { "mhpmcounter30h", any32,  read_zero },
    [CSR_MHPMCOUNTER31H] = { "mhpmcounter31h", any32,  read_zero },
#endif /* !CONFIG_USER_ONLY */
};
