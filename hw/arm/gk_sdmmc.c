#include "gk_peripherals.h"

/* STM32MP2 SDMMC */
struct Stm32MP2SDMMCClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2SDMMCState, Stm32MP2SDMMCClass,
                    STM32MP2_SDMMC)

static const Property stm32mp2_SDMMC_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2SDMMCState, id, 0),
};

static void stm32mp2_SDMMC_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2SDMMCState *s = opaque;
    (void)s;

    switch(addr)
    {
        default:
            fprintf(stderr, "SDMMC: write %x to %p\n", (unsigned)val64, (void *)addr);
    }
}

static uint64_t stm32mp2_SDMMC_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2SDMMCState *s = opaque;
    (void)s;
    
    fprintf(stderr, "SDMMC: read from %p unimplemented\n",
        (void *)addr);

    return 0;
}

static const MemoryRegionOps stm32mp2_SDMMC_ops = {
    .read = stm32mp2_SDMMC_read,
    .write = stm32mp2_SDMMC_write,
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

static void stm32mp2_SDMMC_init(Object *obj)
{
    Stm32MP2SDMMCState *s = STM32MP2_SDMMC(obj);

    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_SD_BUS, DEVICE(s), "sd-bus");

    memory_region_init_io(&s->mmio, obj, &stm32mp2_SDMMC_ops, s,
                          TYPE_STM32MP2_SDMMC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void stm32mp2_SDMMC_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_SDMMC_properties);
}

static const TypeInfo stm32mp2_SDMMC_types[] = {
    {
        .name           = TYPE_STM32MP2_SDMMC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2SDMMCState),
        .instance_init  = stm32mp2_SDMMC_init,
        .class_size     = sizeof(Stm32MP2SDMMCClass),
        .class_init     = stm32mp2_SDMMC_class_init,
    }
};

DEFINE_TYPES(stm32mp2_SDMMC_types)
