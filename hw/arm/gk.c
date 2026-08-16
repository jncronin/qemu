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


#define TYPE_GK_MACHINE MACHINE_TYPE_NAME("gk")
OBJECT_DECLARE_SIMPLE_TYPE(GKMachineState, GK_MACHINE)

#define TYPE_STM32MP2_USART "stm32mp2-usart"
#define TYPE_STM32MP2_RCC "stm32mp2-rcc"

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

    object_initialize_child(OBJECT(mc), "cpu[0]", &mc->cpu[0].core,
                            ARM_CPU_TYPE_NAME("cortex-a35"));
    qdev_realize(DEVICE(&mc->cpu[0].core), NULL, &error_fatal);
    object_initialize_child(OBJECT(mc), "cpu[1]", &mc->cpu[1].core,
                            ARM_CPU_TYPE_NAME("cortex-a35"));
    qdev_realize(DEVICE(&mc->cpu[1].core), NULL, &error_fatal);

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

    /* peripherals */
    object_initialize_child(OBJECT(machine), "usart6", &mc->usart6, TYPE_STM32MP2_USART);
    //qdev_prop_set_chr(DEVICE_STATE(&mc->usart6), "chardev", serial_hd(0));
    sysbus_realize(SYS_BUS_DEVICE(&mc->usart6), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mc->usart6), 0, 0x40220000);

    object_initialize_child(OBJECT(machine), "rcc", &mc->rcc, TYPE_STM32MP2_RCC);
    sysbus_realize(SYS_BUS_DEVICE(&mc->rcc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mc->rcc), 0, 0x44200000);

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
