#include "gk_peripherals.h"
#include "exec/tb-flush.h"

/* STM32MP2 RCC */
struct Stm32MP2RCCClass
{
    SysBusDeviceClass parent_class;
};

struct Stm32MP2PLLClass
{
    SysBusDeviceClass parent_class;
};

static void rcc_reset_cm33(struct Stm32MP2RCCState *s);
static void rcc_start_cm33(struct Stm32MP2RCCState *s);

OBJECT_DECLARE_TYPE(Stm32MP2RCCState, Stm32MP2RCCClass,
                    STM32MP2_RCC)
OBJECT_DECLARE_TYPE(Stm32MP2PLLState, Stm32MP2PLLClass,
                    STM32MP2_PLL)

static const Property stm32mp2_PLL_properties[] = {
    DEFINE_PROP_UINT64("input_freq", Stm32MP2PLLState, input_freq, 64000000),
    DEFINE_PROP_UINT64("output_freq", Stm32MP2PLLState, output_freq, 64000000)
};

static void stm32mp2_PLL_write(Stm32MP2PLLState *s,
    unsigned int id, hwaddr addr,
    uint32_t val, uint32_t *regs)
{
    switch(addr)
    {
        case 0:
            // rdy/en
            regs[0] = (regs[0] & ~0x101u) |
                (val & 0x101u);
            
            if(regs[0] & 0x100u)
            {
                regs[0] |= 0x1000000u;
                // calc clock output freq

                // p. 977 for figure, p.1279 for regs
                uint64_t fref = s->input_freq;
                uint64_t vco_out = (fref * (uint64_t)((regs[1] >> 16) & 0xfffu));
                if(regs[2] & 0xffffffu)
                {
                    vco_out += fref * (uint64_t)(regs[2] & 0xffffffu) / 0x1000000u;
                }
                vco_out = (regs[1] & 0x1fu) ? (vco_out / (regs[1] & 0x1fu)) : 0u;

                uint64_t postdiv1 = (regs[6] & 0x7u) ? (vco_out / (regs[6] & 0x7u)) : 0u;
                uint64_t postdiv2 = (regs[7] & 0x7u) ? (postdiv1 / (regs[7] & 0x7u)) : 0u;

                uint64_t foutpostdiv = (regs[3] & 0x400u) ? fref : postdiv2;
                s->output_freq = (regs[3] & 0x200u) ? foutpostdiv : 0u;
            }
            else
            {
                regs[0] &= ~0x1000000u;
                s->output_freq = 0;
            }

            fprintf(stderr, "PLL%u set to %lu Hz\n", id, s->output_freq);
            clock_set_hz(s->clk_out, s->output_freq);
            break;

        default:
            regs[addr/4] = val;
            break;
    }
}

static uint32_t stm32mp2_PLL_read(Stm32MP2PLLState *s,
    unsigned int id, hwaddr addr,
    uint32_t *regs)
{
    return regs[addr/4];
}

static void stm32mp2_PLL_setinput(Stm32MP2PLLState *s,
    uint32_t iclk)
{
    switch(iclk)
    {
        case 0:
            s->input_freq = 64000000;
            break;
        case 1:
            s->input_freq = 40000000;
            break;
        case 2:
            s->input_freq = 4000000;
            break;
        case 3:
            s->input_freq = 0;
            break;
    }
}

static void stm32mp2_RCC_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2RCCState *s = opaque;

    if(addr < 65536)
    {
        s->regs[addr / 4] = (uint32_t)val64;
    }

    if(addr >= 0x1360u && addr < 0x1360u + 0x28u * 5u)
    {
        unsigned int id = (addr - 0x1360u) / 0x28u;
        hwaddr baddr = 0x1360u + id * 0x28u;
        stm32mp2_PLL_write(&s->pll48[id], id + 4, addr - baddr, (uint32_t)val64, &s->regs[baddr / 4]);
        return;
    }

    switch(addr)
    {
        case 0x40c:
            // C2RST
            if(val64 & 0x1)
            {
                rcc_reset_cm33(s);
                if(s->regs[0x434/4] & 0x1)
                {
                    rcc_start_cm33(s);
                }
                s->regs[0x40c/4] &= ~0x1;
            }
            break;

        case 0x434:
            // cpubootcr
            if(val64 & 0x1)
            {
                rcc_start_cm33(s);
            }
            break;

        case 0x49c:
            if(val64 & 0x100)
                s->regs[0x4a4 / 4] |= 0x100;
            if(val64 & 0x1)
                s->regs[0x4a4 / 4] |= 0x1;
            break;

        case 0x4a0:
            if(val64 & 0x100)
                s->regs[0x4a4 / 4] &= ~0x100;
            if(val64 & 0x1)
                s->regs[0x4a4 / 4] &= ~0x1;
            break;

        case 0x440: // BDCR
            if(val64 & 0x200)
                s->regs[0x440 / 4] |= 0x400;
            else
                s->regs[0x440 / 4] &= ~-0x400;
            if(val64 & 0x1)
                s->regs[0x440 / 4] |= 0x4;
            else
                s->regs[0x440 / 4] &= ~0x4;
            break;

        case 0x7e8:
            qemu_set_irq(s->adc_rst[0], val64 & 0x1);
            break;

        case 0x7ec:
            qemu_set_irq(s->adc_rst[1], val64 & 0x1);
            break;

        case 0x830:
            qemu_set_irq(s->sdmmc1_rst, val64 & 0x1);
            break;

        case 0x1000:    // MUXSELCFGR
            {
                uint64_t pll8sel = (val64 >> 16) & 0x3u;
                uint64_t pll7sel = (val64 >> 12) & 0x3u;
                uint64_t pll6sel = (val64 >> 8) & 0x3u;
                uint64_t pll5sel = (val64 >> 4) & 0x3u;
                uint64_t pll4sel = (val64 >> 0) & 0x3u;

                stm32mp2_PLL_setinput(&s->pll48[0], pll4sel);
                stm32mp2_PLL_setinput(&s->pll48[1], pll5sel);
                stm32mp2_PLL_setinput(&s->pll48[2], pll6sel);
                stm32mp2_PLL_setinput(&s->pll48[3], pll7sel);
                stm32mp2_PLL_setinput(&s->pll48[4], pll8sel);
            }
            break;

        default:
            break;
    }
}

static uint64_t stm32mp2_RCC_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2RCCState *s = opaque;

    if(addr >= 0x1360u && addr < 0x1360u + 0x28u * 5u)
    {
        unsigned int id = (addr - 0x1360u) / 0x28u;
        hwaddr baddr = 0x1360u + id * 0x28u;
        return stm32mp2_PLL_read(&s->pll48[id], id + 4, addr - baddr, &s->regs[baddr / 4]);
    }

    // handle read-only id registers
    switch(addr)
    {
        case 0xfff8:
            return 0x80000003u;     // special qemu-detection code (normally 0x3u on hardware)
    }

    if(addr < 65536)
    {
        return (uint64_t)s->regs[addr / 4];
    }
    return 0;
}

static const MemoryRegionOps stm32mp2_RCC_ops = {
    .read = stm32mp2_RCC_read,
    .write = stm32mp2_RCC_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
    .impl = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
};

static void stm32mp2_RCC_init(Object *obj)
{
    Stm32MP2RCCState *s = STM32MP2_RCC(obj);

    //sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_RCC_ops, s,
                          TYPE_STM32MP2_RCC, 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    s->regs[0x4d0/4] = 0x80000000u; // LSMCUDIVR

    for(unsigned int id = 0; id < 5; id++)
    {
        g_autofree char *str_pll = g_strdup_printf("PLL%u", id + 4);

        object_initialize_child(obj, str_pll, &s->pll48[id], TYPE_STM32MP2_PLL);
        qdev_realize(DEVICE(&s->pll48[id]), NULL, &error_fatal);
    }

    qdev_init_gpio_out_named(DEVICE(obj), &s->sdmmc1_rst, "sdmmc1_rst", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->adc_rst[0], "adc_rst", sizeof(s->adc_rst) / sizeof(s->adc_rst[0]));

    s->ck_icn_hs_mcu = qdev_init_clock_out(DEVICE(s), "ck_icn_hs_mcu");
    clock_set_hz(s->ck_icn_hs_mcu, 400000000);
    s->ck_cm33_systick = qdev_init_clock_out(DEVICE(s), "ck_cm33_systick");
    clock_set_source(s->ck_cm33_systick, s->ck_icn_hs_mcu);
    clock_set_mul_div(s->ck_cm33_systick, 8, 1);
    clock_propagate(s->ck_icn_hs_mcu);
}

static void stm32mp2_RCC_class_init(ObjectClass *class,
                                            const void *data)
{
}

static void stm32mp2_PLL_init(Object *obj)
{
    Stm32MP2PLLState *s = STM32MP2_PLL(obj);

    s->clk_out = qdev_init_clock_out(DEVICE(s), "clk_out");
}

static void stm32mp2_PLL_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, stm32mp2_PLL_properties);
}

static const TypeInfo stm32mp2_RCC_types[] = {
    {
        .name           = TYPE_STM32MP2_RCC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2RCCState),
        .instance_init  = stm32mp2_RCC_init,
        .class_size     = sizeof(Stm32MP2RCCClass),
        .class_init     = stm32mp2_RCC_class_init,
    },
    {
        .name           = TYPE_STM32MP2_PLL,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(Stm32MP2PLLState),
        .instance_init  = stm32mp2_PLL_init,
        .class_size     = sizeof(Stm32MP2PLLClass),
        .class_init     = stm32mp2_PLL_class_init,
    }
};

DEFINE_TYPES(stm32mp2_RCC_types)

static void rcc_reset_cm33(struct Stm32MP2RCCState *s)
{
    fprintf(stderr, "RCC: CM33 reset: TZEN: %x, INITSVTOR: %x, INITNSVTOR: %x\n",
        s->ca35_syscfg->m33_tzen_cr,
        s->ca35_syscfg->m33_initsvtor_cr,
        s->ca35_syscfg->m33_initnsvtor_cr);

    CPUState *cpu = CPU(s->cm33->cpu);
    cpu_interrupt(cpu, CPU_INTERRUPT_HALT);
    cpu_exit(cpu);
    queue_tb_flush(cpu);

    device_cold_reset(DEVICE(s->cm33));
    device_cold_reset(DEVICE(s->cm33->cpu));
    device_cold_reset(DEVICE(&s->cm33->nvic));
    device_cold_reset(DEVICE(&s->cm33->systick[0]));
    device_cold_reset(DEVICE(&s->cm33->systick[1]));

    CPUARMState *env = cpu_env(cpu);
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    env->v7m.vecbase[M_REG_S] = s->ca35_syscfg->m33_initsvtor_cr;
    env->v7m.vecbase[M_REG_NS] = s->ca35_syscfg->m33_initnsvtor_cr;
    env->v7m.secure = s->ca35_syscfg->m33_tzen_cr & 0x1;
    env->thumb = 1;

    uint32_t sp, pc;
    uint32_t vtor = (s->ca35_syscfg->m33_tzen_cr & 0x1) ? s->ca35_syscfg->m33_initsvtor_cr :
        s->ca35_syscfg->m33_initnsvtor_cr;
    address_space_read(&address_space_memory, vtor, MEMTXATTRS_UNSPECIFIED, (uint8_t *)&sp, 4);
    address_space_read(&address_space_memory, vtor + 4, MEMTXATTRS_UNSPECIFIED, (uint8_t *)&pc, 4);
    le32_to_cpus(&sp);
    le32_to_cpus(&pc);
    env->regs[13] = sp;
    env->regs[15] = pc & ~0x1u;

    arm_cpu->power_state = PSCI_ON;
    env->halt_reason = 0;
    cpu->halted = 1;
}

static void rcc_start_cm33(struct Stm32MP2RCCState *s)
{
    CPUState *cpu = CPU(s->cm33->cpu);
    CPUARMState *env = cpu_env(cpu);
    rcc_reset_cm33(s);

    fprintf(stderr, "CM33: reset with exception: %d, cfsr[S]: %x, cfsr[NS]: %x, ccr[S]: %x, ccr[NS]: %x\n",
        env->v7m.exception,
        env->v7m.cfsr[M_REG_S], env->v7m.cfsr[M_REG_NS],
        env->v7m.ccr[M_REG_S], env->v7m.ccr[M_REG_NS]);
    if(cpu->halted)
    {
        cpu->halted = 0;
        cpu_reset_interrupt(cpu, CPU_INTERRUPT_HALT);
        qemu_cpu_kick(cpu);
    }
}
