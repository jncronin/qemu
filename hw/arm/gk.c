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
#include "hw/core/qdev-clock.h"
#include "hw/intc/arm_gic.h"
#include "hw/arm/bsa.h"
#include <time.h>

#define TYPE_GK_MACHINE MACHINE_TYPE_NAME("gk")
OBJECT_DECLARE_SIMPLE_TYPE(GKMachineState, GK_MACHINE)

#define TYPE_STM32MP2_USART "stm32mp2-usart"
#define TYPE_STM32MP2_RCC "stm32mp2-rcc"
#define TYPE_STM32MP2_TIM "stm32mp2-tim"
#define TYPE_STM32MP2_RTC "stm32mp2-rtc"
#define TYPE_STM32MP2_I2C "stm32mp2-i2c"
#define TYPE_STM32MP2_PWR "stm32mp2-pwr"
#define TYPE_STM32MP2_PLL "stm32mp2-pll"

#define TYPE_I2C_INA236A "ina236a"

#define FLASH_SIZE (4 * MiB)

struct i2c_device
{
    SysBusDevice parent_obj;

    int (*start)(struct i2c_device *);
    uint8_t (*read)(struct i2c_device *);
    int (*write)(struct i2c_device *, uint8_t);
    void (*stop)(struct i2c_device *);
};

struct ina236_state
{
    struct i2c_device base;
    int bytes_since_start;
    int reg_id;
};

struct Stm32MP2UsartState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    CharFrontend chr;
};

struct Stm32MP2PLLState {
    SysBusDevice parent_obj;

    uint64_t input_freq;
    uint64_t output_freq;
    Clock *clk_out;
};

struct Stm32MP2RCCState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    uint32_t regs[65336/4];

    struct Stm32MP2PLLState pll48[5];
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

struct Stm32MP2RTCState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
};

enum i2cstate
{
    i2c_Reset, i2c_M_Addressed, i2c_M_Data
};

struct Stm32MP2I2CState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    qemu_irq irq;

    uint32_t cr1, cr2, isr, timingr, rxdr;

    struct i2c_device *devs[256];

    enum i2cstate state;
};

struct Stm32MP2PWRState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    uint32_t regs[0x400 / 4];
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

static const struct TIMInit i2cinits[] = {
    { 1, 0x40120000, 108 },
    { 2, 0x40130000, 110 },
    { 3, 0x40140000, 137 },
    { 4, 0x40150000, 168 },
    { 5, 0x40160000, 181 },
    { 6, 0x40170000, 208 },
    { 7, 0x40180000, 210 },
    { 8, 0x46040000, 212 }
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
    struct Stm32MP2RTCState rtc;
    struct Stm32MP2I2CState i2cs[sizeof(i2cinits) / sizeof(i2cinits[0])];
    struct Stm32MP2PWRState pwr;

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

        // Generic timer input frequency
        object_property_set_int(OBJECT(&mc->cpu[i].core), "cntfrq", 64000000, &error_fatal);

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

    object_initialize_child(OBJECT(machine), "rtc", &mc->rtc, TYPE_STM32MP2_RTC);
    sysbus_realize(SYS_BUS_DEVICE(&mc->rtc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mc->rtc), 0, 0x46000000);

    for(unsigned i = 0; i < sizeof(i2cinits) / sizeof(i2cinits[0]); i++)
    {
        int id = i2cinits[i].id;
        g_autofree char *str_i2cname = g_strdup_printf("I2C%d", id);

        object_initialize_child(OBJECT(machine), str_i2cname, &mc->i2cs[i], TYPE_STM32MP2_I2C);
        qdev_prop_set_int32(DEVICE(&mc->i2cs[i]), "id", id);

        sysbus_realize(SYS_BUS_DEVICE(&mc->i2cs[i]), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(&mc->i2cs[i]), 0, i2cinits[i].base);

        sysbus_connect_irq(SYS_BUS_DEVICE(&mc->i2cs[i]), 0,
            qdev_get_gpio_in(DEVICE(mc->gic), i2cinits[i].gic_irq));
    }

    object_initialize_child(OBJECT(machine), "pwr", &mc->pwr, TYPE_STM32MP2_PWR);
    sysbus_realize(SYS_BUS_DEVICE(&mc->pwr), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mc->pwr), 0, 0x44210000);

    // i2c devices - need to assign to pointer of concrete type first to avoid sizeof() issue in object_initialize_child
    mc->i2cs[1].devs[0x40] = (struct i2c_device *)qdev_new(TYPE_I2C_INA236A);
    //struct ina236_state *ina236 = NULL;
    //object_initialize_child(OBJECT(&mc->i2cs[1]), "ina236a", ina236, TYPE_I2C_INA236A);
    //mc->i2cs[1].devs[0x40] = &ina236->base;
    qdev_realize(DEVICE(mc->i2cs[1].devs[0x40]), NULL, &error_fatal);
    //init_ina236a(&mc->i2cs[1].devs[0x40]);
    //init_max17048(&mc->i2cs[1].devs[0x36]);

    // kernel
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

struct Stm32MP2PLLClass
{
    SysBusDeviceClass parent_class;
};

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

/* STM32MP2 RTC */
struct Stm32MP2RTCClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2RTCState, Stm32MP2RTCClass,
                    STM32MP2_RTC)

static void stm32mp2_RTC_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2RTCState *s = opaque;
    (void)s;

    fprintf(stderr, "RTC: write %x to %p\n", (unsigned)val64, (void *)addr);
}

static uint32_t to_bcd(uint32_t val)
{
    uint32_t ret = 0;
    for(unsigned i = 0; i < 8; i++)
    {
        uint32_t cv = val % 10;
        val /= 10;

        ret |= cv << (i * 4);
    }
    return ret;
}

static uint64_t stm32mp2_RTC_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2RTCState *s = opaque;
    (void)s;
    
    switch(addr)
    {
        case 0:
            {
                time_t ct = time(NULL);
                struct tm *gtime = gmtime(&ct);
                return (to_bcd(gtime->tm_hour) << 16) |
                    (to_bcd(gtime->tm_min) << 8 ) |
                    (to_bcd(gtime->tm_sec) << 0);
            }

        case 4:
            {
                time_t ct = time(NULL);
                struct tm *gtime = gmtime(&ct);
                return ((to_bcd(gtime->tm_year) & 0xffU) << 16) |
                    (gtime->tm_wday << 13) |
                    (to_bcd(gtime->tm_mon) << 8) |
                    (to_bcd(gtime->tm_mday) << 0);
            }

        case 0xc:
            return 1U << 4;

        default:
            break;
    }

    fprintf(stderr, "RTC: read from %p unimplemented\n",
        (void *)addr);
    return 0;
}

static const MemoryRegionOps stm32mp2_RTC_ops = {
    .read = stm32mp2_RTC_read,
    .write = stm32mp2_RTC_write,
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

static void stm32mp2_RTC_init(Object *obj)
{
    Stm32MP2RTCState *s = STM32MP2_RTC(obj);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_RTC_ops, s,
                          TYPE_STM32MP2_RTC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32mp2_RTC_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
}

static const TypeInfo stm32mp2_RTC_types[] = {
    {
        .name           = TYPE_STM32MP2_RTC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2RTCState),
        .instance_init  = stm32mp2_RTC_init,
        .class_size     = sizeof(Stm32MP2RTCClass),
        .class_init     = stm32mp2_RTC_class_init,
    }
};

DEFINE_TYPES(stm32mp2_RTC_types)

/* STM32MP2 I2C */
struct Stm32MP2I2CClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2I2CState, Stm32MP2I2CClass,
                    STM32MP2_I2C)

static const Property stm32mp2_I2C_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2I2CState, id, 0),
};

static unsigned int i2c_cr2_to_addr(uint32_t addr)
{
    return (unsigned int)((addr >> 1) & 0x7f);
}

static void i2c_send_stop(Stm32MP2I2CState *s)
{
    // send stop
    unsigned int i2caddr = i2c_cr2_to_addr(s->cr2);
    if(s->devs[i2caddr] && s->devs[i2caddr]->stop)
        s->devs[i2caddr]->stop(s->devs[i2caddr]);
    s->isr |= 1U << 5;      // set STOPF
    s->isr &= ~(1U << 6);       // clear TC
}

static uint32_t i2c_read_data(Stm32MP2I2CState *s)
{
    uint32_t n_left = (s->cr2 >> 16) & 0xffU;
    //fprintf(stderr, "I2C: read with n_left: %u\n", n_left);
    if(n_left && (s->cr2 & (0x1 << 10)))        // ensure wrn set
    {
        // get data from slave
        //fprintf(stderr, "I2C: read data\n");

        // get data from slave
        unsigned int i2caddr = i2c_cr2_to_addr(s->cr2);
        s->rxdr = (s->devs[i2caddr] && s->devs[i2caddr]->read) ? s->devs[i2caddr]->read(s->devs[i2caddr]) : 0;
        s->isr |= 1U << 2;

        n_left--;
        //fprintf(stderr, "I2C: n_left->: %u\n", n_left);

        // program new nbytes
        s->cr2 = (s->cr2 & ~(0xffU << 16)) | (n_left << 16);

        if(!n_left)
        {
            if(s->cr2 & (1U << 25))
            {
                // autoend - send stop
                i2c_send_stop(s);
                //fprintf(stderr, "I2C: autoend, send stop\n");
            }
            else
            {
                // no autoend, set tc
                s->isr |= 1U << 6;
                //fprintf(stderr, "I2C: set TC\n");
            }

            if(s->cr2 & (1U << 24))
            {
                // reload, set tcr
                s->isr |= 1U << 7;
                //fprintf(stderr, "I2C: set TCR\n");
            }
        }
    }
    else
    {
        // nothing to read - don't update rxdr
        s->isr &= ~(1U << 2);
    }
    return s->rxdr;
}

static void i2c_write_data(Stm32MP2I2CState *s, uint32_t d)
{
    if(!(s->isr & 0x1))
    {
        // txe not set - abort
        return;
    }

    uint32_t n_left = (s->cr2 >> 16) & 0xffU;
    if(n_left)
    {
        // send data to slave
        unsigned int i2caddr = i2c_cr2_to_addr(s->cr2);
        if(s->devs[i2caddr] && s->devs[i2caddr]->write)
            s->devs[i2caddr]->write(s->devs[i2caddr], d);

        n_left--;

        // program new nbytes
        s->cr2 = (s->cr2 & ~(0xffU << 16)) | (n_left << 16);

        if(!n_left)
        {
            if(s->cr2 & (1U << 25))
            {
                // autoend - send stop
                i2c_send_stop(s);
            }
            else
            {
                // no autoend, set tc
                s->isr |= 1U << 6;
            }

            if(s->cr2 & (1U << 24))
            {
                // reload, set tcr
                s->isr |= 1U << 7;
            }

            s->isr &= ~0x3U;        // clear txe, txis
        }
    }
    else
    {
        // nothing to write
        s->isr &= ~0x3U;        // clear txe, txis
    }
}

static void stm32mp2_I2C_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2I2CState *s = opaque;
    (void)s;

    //fprintf(stderr, "I2C: write %x to %p\n", (unsigned)val64, (void *)addr);

    switch(addr)
    {
        case 0:
            s->cr1 = val64;
            if(!(val64 & 0x1))
            {
                s->state = i2c_Reset;
                s->cr2 &= ~(1U << 13);      // clear START
            }
            return;

        case 4:
            s->cr2 = val64;

            if(val64 & 0xff0000U)
            {
                // nbtyes non zero
                s->isr &= ~(1U << 7);       // clear TCR
            }

            if(val64 & (1U << 13))
            {
                s->isr &= ~(1U << 6);       // clear TC

                if(!(s->cr1 & 0x1))
                {
                    // PE not enabled - just clear
                    s->cr2 &= ~(1U << 13);
                }
                else
                {
                    // send start
                    unsigned int i2caddr = i2c_cr2_to_addr(val64);
                    unsigned int is_read = val64 & (0x1 << 10);

                    //fprintf(stderr, "I2C: START to %x\n", i2caddr);

                    // for now, assume ack
                    int ack = (s->devs[i2caddr] && s->devs[i2caddr]->start &&
                        s->devs[i2caddr]->start(s->devs[i2caddr]) == 0) ? 1 : 0;
                    if(ack)
                    {
                        s->cr2 &= ~(1U << 13);  // clear start

                        if(is_read)
                        {
                            s->rxdr = i2c_read_data(s);
                        }
                        else
                        {
                            s->isr |= 0x3u;  // set txis + txe
                        }
                    }
                    else
                    {
                        s->cr2 &= ~(1U << 13);  // clear start
                        s->isr |= (1U << 4);    // NACKF
                    }
                }
            }
            if(val64 & (1U << 14))
            {
                i2c_send_stop(s);
                s->cr2 &= ~(1U << 14);
            }
            return;

        case 0x10:
            s->timingr = val64;
            return;

        case 0x1c:
            s->isr &= (0xffffc0c7 | ~val64);
            return;

        case 0x28:
            i2c_write_data(s, val64);
            return;
    }
}

static uint64_t stm32mp2_I2C_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2I2CState *s = opaque;
    (void)s;

    uint32_t ret = 0;
    
    switch(addr)
    {
        case 0:
            ret = s->cr1;
            break;

        case 4:
            ret = s->cr2;
            break;

        case 0x10:
            ret = s->timingr;
            break;

        case 0x18:
            ret = s->isr;
            break;

        case 0x24:
            {
                uint32_t retval = s->rxdr;
                i2c_read_data(s);
                ret = retval;
            }
            break;
            
        default:
            fprintf(stderr, "I2C: read from %p unimplemented\n",
                (void *)addr);
            break;
    }

    //fprintf(stderr, "I2C: read from 0x%lx : %x\n",
    //    addr, ret);
    return ret;
}

static const MemoryRegionOps stm32mp2_I2C_ops = {
    .read = stm32mp2_I2C_read,
    .write = stm32mp2_I2C_write,
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

static void stm32mp2_I2C_init(Object *obj)
{
    Stm32MP2I2CState *s = STM32MP2_I2C(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_I2C_ops, s,
                          TYPE_STM32MP2_I2C, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32mp2_I2C_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, stm32mp2_I2C_properties);
}

static const TypeInfo stm32mp2_I2C_types[] = {
    {
        .name           = TYPE_STM32MP2_I2C,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2I2CState),
        .instance_init  = stm32mp2_I2C_init,
        .class_size     = sizeof(Stm32MP2I2CClass),
        .class_init     = stm32mp2_I2C_class_init,
    }
};

DEFINE_TYPES(stm32mp2_I2C_types)

/* STM32MP2 PWR */
struct Stm32MP2PWRClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2PWRState, Stm32MP2PWRClass,
                    STM32MP2_PWR)

static const Property stm32mp2_PWR_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2PWRState, id, 0),
};

static void stm32mp2_PWR_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2PWRState *s = opaque;
    (void)s;

    switch(addr)
    {
        case 0:
            {
                // CR1
                uint32_t settable = 0x7001b1f;
                s->regs[0] = (s->regs[0] & ~settable) |
                    (settable & (uint32_t)val64);

                // duplicate lower 5 bits (vmem enable) to bits 16+ (vmem ready)
                s->regs[0] = (s->regs[0] & ~(0x1fU << 16)) |
                    ((s->regs[0] & 0x1fU) << 16);
            }
            break;

        default:
            fprintf(stderr, "PWR: write %x to %p\n", (unsigned)val64, (void *)addr);
    }
}

static uint64_t stm32mp2_PWR_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2PWRState *s = opaque;
    (void)s;
    
    if(addr >= sizeof(s->regs))
    {
        fprintf(stderr, "PWR: read from %p unimplemented\n",
            (void *)addr);
    }

    return s->regs[addr / 4];
}

static const MemoryRegionOps stm32mp2_PWR_ops = {
    .read = stm32mp2_PWR_read,
    .write = stm32mp2_PWR_write,
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

static void stm32mp2_PWR_init(Object *obj)
{
    Stm32MP2PWRState *s = STM32MP2_PWR(obj);

    memset(s->regs, 0, sizeof(s->regs));

    memory_region_init_io(&s->mmio, obj, &stm32mp2_PWR_ops, s,
                          TYPE_STM32MP2_PWR, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32mp2_PWR_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_PWR_properties);
}

static const TypeInfo stm32mp2_PWR_types[] = {
    {
        .name           = TYPE_STM32MP2_PWR,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2PWRState),
        .instance_init  = stm32mp2_PWR_init,
        .class_size     = sizeof(Stm32MP2PWRClass),
        .class_init     = stm32mp2_PWR_class_init,
    }
};

DEFINE_TYPES(stm32mp2_PWR_types)

// INA236
OBJECT_DECLARE_SIMPLE_TYPE(ina236_state, I2C_INA236A)

static int ina236_start(struct i2c_device *_d)
{
    struct ina236_state *d = (struct ina236_state *)_d;
    d->bytes_since_start = 0;
    return 0;
}

static void ina236_stop(struct i2c_device *)
{ }

static uint8_t ina236_read(struct i2c_device *_d)
{
    struct ina236_state *d = (struct ina236_state *)_d;
    uint8_t ret = 0;

    switch(d->reg_id)
    {
        case 0x3e*2:
            ret = 0x54;
            break;
        case 0x3e*2+1:
            ret = 0x49;
            break;
        case 0x3f*2:
            ret = 0xa0;
            break;
        case 0x3f*2+1:
            ret = 0x80;
            break;
        case 0x1*2:
            ret = 0x7;
            break;
        case 0x1*2+1:
            ret = 0xd0;
            break;
        case 0x2*2:
            ret = 0x9;
            break;
        case 0x2*2+1:
            ret = 0xc4;
            break;
    }

    //fprintf(stderr, "INA236: reg %x%s: %x\n", d->reg_id / 2, (d->reg_id & 0x1) ? "L" : "H", ret);
    d->reg_id++;

    return ret;
}

static int ina236_write(struct i2c_device *_d, uint8_t v)
{
    struct ina236_state *d = (struct ina236_state *)_d;

    if(d->bytes_since_start == 0)
    {
        d->reg_id = v * 2;
    }
    d->bytes_since_start++;

    return 0;
}

static void ina236_init(Object *obj)
{
    ina236_state *s = I2C_INA236A(obj);
    s->base.start = ina236_start;
    s->base.stop = ina236_stop;
    s->base.read = ina236_read;
    s->base.write = ina236_write;
}

static const TypeInfo ina236_types[] = {
    {
        .name           = TYPE_I2C_INA236A,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(ina236_state),
        .instance_init  = ina236_init,
    }
};

DEFINE_TYPES(ina236_types)

