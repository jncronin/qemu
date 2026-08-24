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
#include "hw/arm/armv7m.h"
#include "gk_i2cdevs.h"
#include "gk_peripherals.h"
#include <time.h>

#define TYPE_GK_MACHINE MACHINE_TYPE_NAME("gk")
OBJECT_DECLARE_SIMPLE_TYPE(GKMachineState, GK_MACHINE)

#define FLASH_SIZE (4 * MiB)

static const unsigned int ncpus = 2;

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

static const struct TIMInit sdmmcinits[] = {
    { 1, 0x48220000, 123 },
    { 2, 0x48230000, 197 },
    { 3, 0x48240000, 214 }
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
    } cpu[2];
    struct ARMv7MState cm33;

    struct Stm32MP2UsartState usart6;
    struct Stm32MP2RCCState rcc;
    struct Stm32MP2TIMState tims[sizeof(timinits) / sizeof(timinits[0])];
    struct Stm32MP2RTCState rtc;
    struct Stm32MP2I2CState i2cs[sizeof(i2cinits) / sizeof(i2cinits[0])];
    struct Stm32MP2PWRState pwr;
    struct Stm32MP2LTDCState ltdc;
    struct Stm32MP2SDMMCState sdmmc[sizeof(sdmmcinits) / sizeof(sdmmcinits[0])];
    struct Stm32MP2CA35_SYSCFGState ca35_syscfg;

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

    /* RCC */
    object_initialize_child(OBJECT(machine), "rcc", &mc->rcc, TYPE_STM32MP2_RCC);
    sysbus_realize(SYS_BUS_DEVICE(&mc->rcc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mc->rcc), 0, 0x44200000);

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

    // CM33 needs creating within its own subsystem
    object_initialize_child(OBJECT(machine), "armv7m-subsystem", &mc->cm33, TYPE_ARMV7M);
    object_property_set_str(OBJECT(&mc->cm33), "cpu-type", 
                            ARM_CPU_TYPE_NAME("cortex-m33"), &error_fatal);
    object_property_set_uint(OBJECT(&mc->cm33), "num-irq", 64, &error_fatal);
    object_property_set_link(OBJECT(&mc->cm33), "memory", 
                            OBJECT(get_system_memory()), &error_fatal);
    object_property_set_bool(OBJECT(&mc->cm33), "start-powered-off", true, &error_fatal);
    object_property_set_bool(OBJECT(&mc->cm33), "enable-sev-out", true, &error_fatal);

    // cpuclk
    qdev_connect_clock_in(DEVICE(&mc->cm33), "cpuclk",
        qdev_get_clock_out(DEVICE(&mc->rcc), "ck_icn_hs_mcu"));
    qdev_connect_clock_in(DEVICE(&mc->cm33), "refclk",
        qdev_get_clock_out(DEVICE(&mc->rcc), "ck_cm33_systick"));

    sysbus_realize(SYS_BUS_DEVICE(&mc->cm33), &error_fatal);
    resettable_assert_reset(OBJECT(&mc->cm33), RESET_TYPE_COLD);

    // Let the RCC control the CM33
    object_property_add_link(OBJECT(&mc->rcc), "cm33", TYPE_ARMV7M,
        (Object **)&mc->rcc.cm33,
        object_property_allow_set_link, 0);
    object_property_set_link(OBJECT(&mc->rcc), "cm33", OBJECT(&mc->cm33), &error_fatal);

    /* Memories */
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

    object_initialize_child(OBJECT(machine), "ltdc", &mc->ltdc, TYPE_STM32MP2_LTDC);
    sysbus_realize(SYS_BUS_DEVICE(&mc->ltdc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mc->ltdc), 0, 0x48010000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&mc->ltdc), 0,
        qdev_get_gpio_in(DEVICE(mc->gic), 158));

    for(unsigned i = 0; i < sizeof(sdmmcinits) / sizeof(sdmmcinits[0]); i++)
    {
        int id = sdmmcinits[i].id;
        g_autofree char *str_sdmmcname = g_strdup_printf("SDMMC%d", id);

        object_initialize_child(OBJECT(machine), str_sdmmcname, &mc->sdmmc[i], TYPE_STM32MP2_SDMMC);
        qdev_prop_set_int32(DEVICE(&mc->sdmmc[i]), "id", id);

        sysbus_realize(SYS_BUS_DEVICE(&mc->sdmmc[i]), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(&mc->sdmmc[i]), 0, sdmmcinits[i].base);

        sysbus_connect_irq(SYS_BUS_DEVICE(&mc->sdmmc[i]), 0,
            qdev_get_gpio_in(DEVICE(mc->gic), sdmmcinits[i].gic_irq));
    }
    qdev_connect_gpio_out_named(DEVICE(&mc->rcc), "sdmmc1_rst", 0,
        qdev_get_gpio_in_named(DEVICE(&mc->sdmmc[0]), "rst", 0));

    object_initialize_child(OBJECT(machine), "ca35-syscfg", &mc->ca35_syscfg, TYPE_STM32MP2_CA35_SYSCFG);
    object_property_add_link(OBJECT(&mc->ca35_syscfg), "cm33", TYPE_ARMV7M,
        (Object **)&mc->ca35_syscfg.cm33,
        object_property_allow_set_link, 0);
    object_property_set_link(OBJECT(&mc->ca35_syscfg), "cm33", OBJECT(&mc->cm33), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&mc->ca35_syscfg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mc->ca35_syscfg), 0, 0x48800000);

    object_property_add_link(OBJECT(&mc->rcc), "ca35_syscfg", TYPE_STM32MP2_CA35_SYSCFG,
        (Object **)&mc->rcc.ca35_syscfg,
        object_property_allow_set_link, 0);
    object_property_set_link(OBJECT(&mc->rcc), "ca35_syscfg", OBJECT(&mc->ca35_syscfg), &error_fatal);

    // i2c devices
    mc->i2cs[1].devs[0x40] = (struct i2c_device *)qdev_new(TYPE_I2C_INA236A);
    object_property_add_child(OBJECT(&mc->i2cs[1]), "INA236@0x40", OBJECT(mc->i2cs[1].devs[0x40]));
    qdev_realize(DEVICE(mc->i2cs[1].devs[0x40]), NULL, &error_fatal);

    mc->i2cs[1].devs[0x36] = (struct i2c_device *)qdev_new(TYPE_I2C_MAX17048);
    object_property_add_child(OBJECT(&mc->i2cs[1]), "MAX17048@0x36", OBJECT(mc->i2cs[1].devs[0x36]));
    qdev_realize(DEVICE(mc->i2cs[1].devs[0x36]), NULL, &error_fatal);

    mc->i2cs[1].devs[0x6b] = (struct i2c_device *)qdev_new(TYPE_I2C_BQ25601);
    object_property_add_child(OBJECT(&mc->i2cs[1]), "BQ25601@0x6B", OBJECT(mc->i2cs[1].devs[0x6b]));
    qdev_realize(DEVICE(mc->i2cs[1].devs[0x6b]), NULL, &error_fatal);

    // SD card
    dinfo = drive_get(IF_SD, 0, 0);
    BlockBackend *blk_sd = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    BusState *sd_bus = qdev_get_child_bus(DEVICE(&mc->sdmmc[0]), "sd-bus");
    if (sd_bus == NULL) {
        error_report("No SD bus found in SOC object");
        exit(1);
    }
    DeviceState *carddev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(carddev, "drive", blk_sd, &error_fatal);
    qdev_realize_and_unref(carddev, sd_bus, &error_fatal);

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
