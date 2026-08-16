/*
 * gkv4 emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/raspi_platform.h"
#include "hw/core/registerfields.h"
#include "qemu/error-report.h"
#include "system/device_tree.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/arm/boot.h"
#include "qom/object.h"
#include "target/arm/cpu.h"
#include "hw/misc/unimp.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/block/flash.h"
#include "system/block-backend.h"
#include "hw/core/ptimer.h"
#include "hw/core/irq.h"
#include "hw/intc/arm_gic.h"
#include "hw/arm/bsa.h"

#define TYPE_GK_MACHINE MACHINE_TYPE_NAME("gk")
OBJECT_DECLARE_SIMPLE_TYPE(GKMachineState, GK_MACHINE)

#define TYPE_STM32MP2_USART "stm32mp2-usart"
#define TYPE_STM32MP2_RCC "stm32mp2-rcc"
#define TYPE_STM32MP2_TIM "stm32mp2-tim"

#define FLASH_SIZE (4 * MiB)

struct Stm32MP2UsartState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    CharFrontend chr;
};

struct Stm32MP2RCCState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    uint32_t regs[65336/4];
};

struct Stm32MP2TIMState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t is_lp;
    int32_t id;
    uint64_t input_freq;

    ptimer_state *pt;

    uint32_t cr1, cr2, dier, sr, psc, arr;

    qemu_irq irq;
};

struct TIMInit
{
    int id;
    hwaddr base;
    unsigned gic_irq;
};

static const struct TIMInit timinits[] = {
    { -5, 0x46070000, 218 },
    { -4, 0x46060000, 217 },
    { -3, 0x46050000, 216 },
    { 20, 0x40320000, 102 },
    { 17, 0x40270000, 195 },
    { 16, 0x40260000, 194 },
    { 15, 0x40250000, 193 },
    { 8, 0x40210000, 119 },
    { 1, 0x40200000, 98 },
    { 11, 0x401d0000, 225 },
    { 10, 0x401c0000, 205 },
    { -2, 0x400a0000, 215 },
    { -1, 0x40090000, 166 },
    { 14, 0x40080000, 204 },
    { 13, 0x40070000, 203 },
    { 12, 0x40060000, 196 },
    { 7, 0x40050000, 129 },
    { 6, 0x40040000, 128 },
    { 5, 0x40030000, 124 },
    { 4, 0x40020000, 107 },
    { 3, 0x40010000, 106 },
    { 2, 0x40000000, 105 },
};

struct GKMachineState {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    struct arm_boot_info binfo;

    MemoryRegion sysram, sram1, sram2, retram, vderam, lpsram1, lpsram2, lpsram3, ddrram;
    struct
    {
        ARMCPU core;
    } cpu[4];

    struct Stm32MP2UsartState usart6;
    struct Stm32MP2RCCState rcc;
    struct Stm32MP2TIMState tims[sizeof(timinits) / sizeof(timinits[0])];

    DeviceState *gic;
};

static const char *gk_cpu_types[] = { 
    ARM_CPU_TYPE_NAME("cortex-a35"), 
    ARM_CPU_TYPE_NAME("cortex-m33"), 
    ARM_CPU_TYPE_NAME("cortex-m0"), 
    NULL
};

static void gk_machine_init(MachineState *machine)
{
    fprintf(stderr, "gk_machine_init()\n");
    
    GKMachineState *mc = GK_MACHINE(machine);

    const unsigned int ncpus = 2;
    const unsigned int nspis = 384;

    /* GICv2 */
    mc->gic = qdev_new(TYPE_ARM_GIC);
    qdev_prop_set_uint32(mc->gic, "revision", 2);
    qdev_prop_set_uint32(mc->gic, "num-cpu", ncpus);
    qdev_prop_set_uint32(mc->gic, "num-irq", nspis + GIC_INTERNAL);
    qdev_prop_set_bit(mc->gic, "has-security-extensions", true);
    qdev_prop_set_bit(mc->gic, "has-virtualization-extensions", true);
    sysbus_realize(SYS_BUS_DEVICE(mc->gic), &error_fatal);

    sysbus_mmio_map(SYS_BUS_DEVICE(mc->gic), 0, 0x4ac10000);
    sysbus_mmio_map(SYS_BUS_DEVICE(mc->gic), 1, 0x4ac20000);    // per-cpu decoding is done in the gic device
    sysbus_mmio_map(SYS_BUS_DEVICE(mc->gic), 2, 0x4ac40000);
    sysbus_mmio_map(SYS_BUS_DEVICE(mc->gic), 3, 0x4ac60000);

    /* CPUs */
    for(unsigned int i = 0; i < ncpus; i++)
    {
        g_autofree char *str_cpuname = g_strdup_printf("CPU[%u]", i);
        
        // create cpu
        object_initialize_child(OBJECT(mc), str_cpuname, &mc->cpu[i].core,
            ARM_CPU_TYPE_NAME("cortex-a35"));
        qdev_realize(DEVICE(&mc->cpu[i].core), NULL, &error_fatal);

        // Link GIC outputs to CPU IRQ inputs
        sysbus_connect_irq(SYS_BUS_DEVICE(mc->gic), i,
            qdev_get_gpio_in(DEVICE(&mc->cpu[i].core), ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(mc->gic), ncpus + i,
            qdev_get_gpio_in(DEVICE(&mc->cpu[i].core), ARM_CPU_FIQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(mc->gic), 2 * ncpus + i,
            qdev_get_gpio_in(DEVICE(&mc->cpu[i].core), ARM_CPU_VIRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(mc->gic), 3 * ncpus + i,
            qdev_get_gpio_in(DEVICE(&mc->cpu[i].core), ARM_CPU_VFIQ));

        // Link PPI outputs to GIC inputs
        const unsigned int irq_base = nspis + i * GIC_INTERNAL;
        qdev_connect_gpio_out(DEVICE(&mc->cpu[i].core), GTIMER_PHYS,
            qdev_get_gpio_in(DEVICE(mc->gic), irq_base + ARCH_TIMER_NS_EL1_IRQ));
        qdev_connect_gpio_out(DEVICE(&mc->cpu[i].core), GTIMER_VIRT,
            qdev_get_gpio_in(DEVICE(mc->gic), irq_base + ARCH_TIMER_VIRT_IRQ));
    }

#if 0
    object_initialize_child(OBJECT(mc), "cpu[2]", &mc->cpu[2].core,
                            ARM_CPU_TYPE_NAME("cortex-m33"));
    qdev_realize(DEVICE(&mc->cpu[2].core), NULL, &error_fatal);
    object_initialize_child(OBJECT(mc), "cpu[3]", &mc->cpu[3].core,
                            ARM_CPU_TYPE_NAME("cortex-m0"));
    qdev_realize(DEVICE(&mc->cpu[3].core), NULL, &error_fatal);

    // Instead of the above, need to do something like:
    /* 1. Create the ARMV7M container container */
    armv7m = DEVICE(object_initialize_child(OBJECT(machine), "armv7m-subsystem", 
                                            TYPE_ARMV7M));

    /* 2. Set the desired Cortex-M CPU variant */
    object_property_set_str(OBJECT(armv7m), "cpu-type", 
                            ARM_CPU_TYPE_NAME("cortex-m33"), &error_abort);

    /* 3. Configure the number of interrupts your M33 will handle */
    object_property_set_uint(OBJECT(armv7m), "num-irq", 64, &error_abort);

    /* 4. Map the CPU's container memory space (or system_memory) */
    object_property_set_link(OBJECT(armv7m), "memory", 
                            OBJECT(get_system_memory()), &error_abort);

    /* 5. Realize the entire ARMV7M subsystem */
    sysbus_realize(SYS_BUS_DEVICE(armv7m), &error_fatal);
#endif


    memory_region_init_ram(&mc->sysram, NULL, "SYSRAM", 256 * KiB, &error_fatal);
    memory_region_init_ram(&mc->sram1, NULL, "SRAM1", 128 * KiB, &error_fatal);
    memory_region_init_ram(&mc->sram2, NULL, "SRAM2", 128 * KiB, &error_fatal);
    memory_region_init_ram(&mc->retram, NULL, "RETRAM", 128 * KiB, &error_fatal);
    memory_region_init_ram(&mc->vderam, NULL, "VDERAM", 128 * KiB, &error_fatal);
    memory_region_init_ram(&mc->lpsram1, NULL, "LPSRAM1", 8 * KiB, &error_fatal);
    memory_region_init_ram(&mc->lpsram2, NULL, "LPSRAM2", 8 * KiB, &error_fatal);
    memory_region_init_ram(&mc->lpsram3, NULL, "LPSRAM3", 16 * KiB, &error_fatal);
    memory_region_init_ram(&mc->ddrram, NULL, "DDRRAM", 1 * GiB, &error_fatal);

    const hwaddr sram_offsets[] = { 0x0a000000, 0x0e000000, 0x20000000, 0x30000000 };

    for(unsigned i = 0; i < sizeof(sram_offsets) / sizeof(sram_offsets[0]); i++)
    {
        MemoryRegion *a_sysram = g_new(MemoryRegion, 1);
        MemoryRegion *a_sram1 = g_new(MemoryRegion, 1);
        MemoryRegion *a_sram2 = g_new(MemoryRegion, 1);
        MemoryRegion *a_retram = g_new(MemoryRegion, 1);
        MemoryRegion *a_vderam = g_new(MemoryRegion, 1);

        g_autofree char *str_sysram = g_strdup_printf("SYSRAM@%p", (void *)(sram_offsets[i] + 0));
        g_autofree char *str_sram1 = g_strdup_printf("SRAM1@%p", (void *)(sram_offsets[i] + 0x40000));
        g_autofree char *str_sram2 = g_strdup_printf("SRAM2@%p", (void *)(sram_offsets[i] + 0x60000));
        g_autofree char *str_retram = g_strdup_printf("RETRAM@%p", (void *)(sram_offsets[i] + 0x80000));
        g_autofree char *str_vderam = g_strdup_printf("VDERAM@%p", (void *)(sram_offsets[i] + 0xa0000));        

        memory_region_init_alias(a_sysram, NULL, str_sysram, &mc->sysram, 0, 256 * KiB);
        memory_region_init_alias(a_sram1, NULL, str_sram1, &mc->sram1, 0, 128 * KiB);
        memory_region_init_alias(a_sram2, NULL, str_sram2, &mc->sram2, 0, 128 * KiB);
        memory_region_init_alias(a_retram, NULL, str_retram, &mc->retram, 0, 128 * KiB);
        memory_region_init_alias(a_vderam, NULL, str_vderam, &mc->vderam, 0, 128 * KiB);

        memory_region_add_subregion(get_system_memory(), sram_offsets[i] + 0, a_sysram);
        memory_region_add_subregion(get_system_memory(), sram_offsets[i] + 0x40000, a_sram1);
        memory_region_add_subregion(get_system_memory(), sram_offsets[i] + 0x60000, a_sram2);
        memory_region_add_subregion(get_system_memory(), sram_offsets[i] + 0x80000, a_retram);
        memory_region_add_subregion(get_system_memory(), sram_offsets[i] + 0xa0000, a_vderam);
    }

    const hwaddr lpsram_offsets[] = { 0x200c0000, 0x300c0000 };

    for(unsigned i = 0; i < sizeof(lpsram_offsets) / sizeof(lpsram_offsets[0]); i++)
    {
        MemoryRegion *a_lpsram1 = g_new(MemoryRegion, 1);
        MemoryRegion *a_lpsram2 = g_new(MemoryRegion, 1);
        MemoryRegion *a_lpsram3 = g_new(MemoryRegion, 1);

        g_autofree char *str_lpsram1 = g_strdup_printf("LPSRAM1@%p", (void *)(lpsram_offsets[i] + 0));
        g_autofree char *str_lpsram2 = g_strdup_printf("LPSRAM2@%p", (void *)(lpsram_offsets[i] + 0x2000));
        g_autofree char *str_lpsram3 = g_strdup_printf("LPSRAM3@%p", (void *)(lpsram_offsets[i] + 0x4000));

        memory_region_init_alias(a_lpsram1, NULL, str_lpsram1, &mc->lpsram1, 0, 8 * KiB);
        memory_region_init_alias(a_lpsram2, NULL, str_lpsram2, &mc->lpsram2, 0, 8 * KiB);
        memory_region_init_alias(a_lpsram3, NULL, str_lpsram3, &mc->lpsram3, 0, 16 * KiB);

        memory_region_add_subregion(get_system_memory(), lpsram_offsets[i] + 0, a_lpsram1);
        memory_region_add_subregion(get_system_memory(), lpsram_offsets[i] + 0x2000, a_lpsram2);
        memory_region_add_subregion(get_system_memory(), lpsram_offsets[i] + 0x4000, a_lpsram3);
    }

    memory_region_add_subregion(get_system_memory(), 0x80000000, &mc->ddrram);

    create_unimplemented_device("stm32mp2_peripherals_ns", 0x40000000, 0x10000000);
    create_unimplemented_device("stm32mp2_peripherals_s", 0x50000000, 0x10000000);

    /* Flash */
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);
    if(!dinfo)
    {
        error_report("Please provide a flash image with the -pflash option");
        exit(1);
    }
    BlockBackend *blk_flash = blk_by_legacy_dinfo(dinfo);
    int64_t blk_flash_len = blk_getlength(blk_flash);
    if(blk_flash_len < FLASH_SIZE)
    {
        // pad up to flash size.  Need to wrap in get/release resize permissions
        uint64_t perm, shared_perm;
        blk_get_perm(blk_flash, &perm, &shared_perm);
        blk_set_perm(blk_flash, perm | BLK_PERM_RESIZE, shared_perm, &error_fatal);
        blk_truncate(blk_flash, FLASH_SIZE, 0, PREALLOC_MODE_OFF, 0, &error_fatal);
        blk_set_perm(blk_flash, perm, shared_perm, &error_fatal);
    }
    pflash_cfi01_register(0x60000000, "ospi.flash", FLASH_SIZE,
        blk_by_legacy_dinfo(dinfo), 4 * KiB, 4, 0, 0, 0, 0, 0);

    /* peripherals */
    object_initialize_child(OBJECT(machine), "usart6", &mc->usart6, TYPE_STM32MP2_USART);
    //qdev_prop_set_chr(DEVICE_STATE(&mc->usart6), "chardev", serial_hd(0));
    sysbus_realize(SYS_BUS_DEVICE(&mc->usart6), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mc->usart6), 0, 0x40220000);

    object_initialize_child(OBJECT(machine), "rcc", &mc->rcc, TYPE_STM32MP2_RCC);
    sysbus_realize(SYS_BUS_DEVICE(&mc->rcc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mc->rcc), 0, 0x44200000);

    for(unsigned i = 0; i < sizeof(timinits) / sizeof(timinits[0]); i++)
    {
        int is_lp = timinits[i].id < 0 ? 1 : 0;
        int id = is_lp ? -timinits[i].id : timinits[i].id;
        g_autofree char *str_timname = g_strdup_printf("%sTIM%d",
            is_lp ? "LP" : "", id);
        
        object_initialize_child(OBJECT(machine), str_timname, &mc->tims[i], TYPE_STM32MP2_TIM);
        qdev_prop_set_int32(DEVICE(&mc->tims[i]), "is_lp", is_lp);
        qdev_prop_set_int32(DEVICE(&mc->tims[i]), "id", id);
        sysbus_realize(SYS_BUS_DEVICE(&mc->tims[i]), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(&mc->tims[i]), 0, timinits[i].base);

        sysbus_connect_irq(SYS_BUS_DEVICE(&mc->tims[i]), 0,
            qdev_get_gpio_in(DEVICE(mc->gic), timinits[i].gic_irq));
    }

    mc->binfo.ram_size = 1 * GiB;
    arm_load_kernel(&mc->cpu[0].core, machine, &mc->binfo);
}

static void gk_machine_class_init(ObjectClass *oc, const void *data)
{
    fprintf(stderr, "gk_machine_class_init()\n");

    MachineClass *mc = MACHINE_CLASS(oc);
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a35");
    mc->default_cpus = 4;
    mc->min_cpus = 1;
    mc->max_cpus = 4;
    mc->valid_cpu_types = gk_cpu_types;
    mc->init = gk_machine_init;
    mc->desc = g_strdup_printf("gkv4 STM32MP2-powered handheld gaming device (Cortex-A35)");
}

static const TypeInfo gk_machine_types[] = {
    {
        .name = MACHINE_TYPE_NAME("gk"),
        .parent = TYPE_MACHINE,
        .class_init = gk_machine_class_init,
        .interfaces = arm_aarch64_machine_interfaces,
        .instance_size = sizeof(GKMachineState)
    }
};

DEFINE_TYPES(gk_machine_types)


/* STM32MP2 USART */
struct Stm32MP2UsartClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2UsartState, Stm32MP2UsartClass,
                    STM32MP2_USART)

static void stm32mp2_usart_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    switch(addr)
    {
        case 0x28:
            fprintf(stderr, "%c", (char)val64);
            break;
    }
}

static uint64_t stm32mp2_usart_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    switch(addr)
    {
        case 0x1c:
            return 0x800080;
        default:
            return 0;
    }
}

static const MemoryRegionOps stm32mp2_usart_ops = {
    .read = stm32mp2_usart_read,
    .write = stm32mp2_usart_write,
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

static const Property stm32mp2_usart_properties[] = {
    DEFINE_PROP_CHR("chardev", Stm32MP2UsartState, chr),
};

static void stm32mp2_usart_init(Object *obj)
{
    Stm32MP2UsartState *s = STM32MP2_USART(obj);

    //sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_usart_ops, s,
                          TYPE_STM32MP2_USART, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32mp2_usart_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, stm32mp2_usart_properties);
}

static const TypeInfo stm32mp2_usart_types[] = {
    {
        .name           = TYPE_STM32MP2_USART,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2UsartState),
        .instance_init  = stm32mp2_usart_init,
        .class_size     = sizeof(Stm32MP2UsartClass),
        .class_init     = stm32mp2_usart_class_init,
    }
};

DEFINE_TYPES(stm32mp2_usart_types)

/* STM32MP2 RCC */
struct Stm32MP2RCCClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2RCCState, Stm32MP2RCCClass,
                    STM32MP2_RCC)

static void stm32mp2_RCC_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2RCCState *s = opaque;

    if(addr < 65536)
    {
        s->regs[addr / 4] = (uint32_t)val64;
    }

    switch(addr)
    {
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

        default:
            break;
    }
}

static uint64_t stm32mp2_RCC_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2RCCState *s = opaque;

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
}

static void stm32mp2_RCC_class_init(ObjectClass *class,
                                            const void *data)
{
}

static const TypeInfo stm32mp2_RCC_types[] = {
    {
        .name           = TYPE_STM32MP2_RCC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2RCCState),
        .instance_init  = stm32mp2_RCC_init,
        .class_size     = sizeof(Stm32MP2RCCClass),
        .class_init     = stm32mp2_RCC_class_init,
    }
};

DEFINE_TYPES(stm32mp2_RCC_types)

/* STM32MP2 TIM */
struct Stm32MP2TIMClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2TIMState, Stm32MP2TIMClass,
                    STM32MP2_TIM)

static const Property stm32mp2_tim_properties[] = {
    DEFINE_PROP_INT32("is_lp", Stm32MP2TIMState, is_lp, 0),
    DEFINE_PROP_INT32("id", Stm32MP2TIMState, id, 0),
    DEFINE_PROP_UINT64("input_freq", Stm32MP2TIMState, input_freq, 200000000)
};

static void stm32mp2_TIM_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2TIMState *s = opaque;
    (void)s;

    //fprintf(stderr, "%sTIM%d: write %x to %p\n",
    //    s->is_lp ? "LP" : "", s->id, (unsigned)val64, (void *)addr);

    ptimer_transaction_begin(s->pt);

    if(!s->is_lp)
    {
        switch(addr)
        {
            case 0:
                {
                    uint32_t changed_vals = s->cr1 ^ (uint32_t)val64;
                    s->cr1 = (uint32_t)val64 & 0x188fu;
                    if(changed_vals & 0x1)
                    {
                        if(val64 & 0x1)
                            ptimer_run(s->pt, (s->cr1 & 0x8) == 0 ? 0 : 1);
                        else
                            ptimer_stop(s->pt);
                    }
                }
                break;

            case 0xc:
                s->dier = (uint32_t)val64 & 0xf05f5f;
                break;

            case 0x10:
                s->sr = (uint32_t)val64 & 0x1;
                if((val64 & 0x1) == 0)
                {
                    //fprintf(stderr, "%sTIM%d: SR clear\n",
                    //    s->is_lp ? "LP" : "", s->id);
                    qemu_set_irq(s->irq, 0);
                }
                break;

            case 0x24:
                ptimer_set_count(s->pt, ptimer_get_limit(s->pt) - (uint32_t)val64);;
                break;

            case 0x28:
                s->psc = (uint32_t)val64 & 0xffffU;
                ptimer_set_freq(s->pt, (uint32_t)(s->input_freq / (uint64_t)(s->psc + 1)));
                break;

            case 0x2c:
                s->arr = (uint32_t)val64;
                ptimer_set_limit(s->pt, s->arr, 0);
                break;
        }
    }
    switch(addr)
    {
        default:
            break;
    }

    ptimer_transaction_commit(s->pt);
}

static uint64_t stm32mp2_TIM_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2TIMState *s = opaque;
    
    if(!s->is_lp)
    {
        switch(addr)
        {
            case 0:
                return s->cr1;
            case 4:
                return s->cr2;
            case 0xc:
                return s->dier;
            case 0x10:
                return s->sr;
            case 0x24:
                return ptimer_get_limit(s->pt) - ptimer_get_count(s->pt);
            case 0x28:
                return s->psc;
            case 0x2c:
                return s->arr;
        }
    }

    fprintf(stderr, "%sTIM%d: read from %p unimplemented\n",
        s->is_lp ? "LP" : "", s->id, (void *)addr);

    return 0;
}

static const MemoryRegionOps stm32mp2_TIM_ops = {
    .read = stm32mp2_TIM_read,
    .write = stm32mp2_TIM_write,
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

static void tim_cb(void *opaque)
{
    Stm32MP2TIMState *s = STM32MP2_TIM(opaque);

    s->sr |= 0x1;
    if(s->dier & 0x1)
    {
        qemu_set_irq(s->irq, 1);
    }

    //fprintf(stderr, "%sTIM%d: tick\n",
    //    s->is_lp ? "LP" : "", s->id);
}

static void stm32mp2_TIM_init(Object *obj)
{
    Stm32MP2TIMState *s = STM32MP2_TIM(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    s->pt = ptimer_init(tim_cb, s, PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD);
    s->cr1 = 0;
    s->cr2 = 0;
    s->dier = 0;
    s->sr = 0;
    s->psc = 0;
    s->arr = 0;

    memory_region_init_io(&s->mmio, obj, &stm32mp2_TIM_ops, s,
                          TYPE_STM32MP2_TIM, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32mp2_TIM_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, stm32mp2_tim_properties);
}

static const TypeInfo stm32mp2_TIM_types[] = {
    {
        .name           = TYPE_STM32MP2_TIM,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2TIMState),
        .instance_init  = stm32mp2_TIM_init,
        .class_size     = sizeof(Stm32MP2TIMClass),
        .class_init     = stm32mp2_TIM_class_init,
    }
};

DEFINE_TYPES(stm32mp2_TIM_types)
