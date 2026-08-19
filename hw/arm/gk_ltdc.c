#include "gk_peripherals.h"

/* STM32MP2 LTDC */
struct Stm32MP2LTDCClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2LTDCState, Stm32MP2LTDCClass,
                    STM32MP2_LTDC)

static const Property stm32mp2_LTDC_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2LTDCState, id, 0),
};

static void stm32mp2_LTDC_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2LTDCState *s = opaque;
    (void)s;

    switch(addr)
    {
        default:
            fprintf(stderr, "LTDC: write %x to %p\n", (unsigned)val64, (void *)addr);
    }
}

static uint64_t stm32mp2_LTDC_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2LTDCState *s = opaque;
    (void)s;
    
    if(addr >= sizeof(s->regs))
    {
        fprintf(stderr, "LTDC: read from %p unimplemented\n",
            (void *)addr);
    }

    return s->regs[addr / 4];
}

static const MemoryRegionOps stm32mp2_LTDC_ops = {
    .read = stm32mp2_LTDC_read,
    .write = stm32mp2_LTDC_write,
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

static void stm32mp2_LTDC_init(Object *obj)
{
    Stm32MP2LTDCState *s = STM32MP2_LTDC(obj);

    memset(s->regs, 0, sizeof(s->regs));

    memory_region_init_io(&s->mmio, obj, &stm32mp2_LTDC_ops, s,
                          TYPE_STM32MP2_LTDC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32mp2_LTDC_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_LTDC_properties);
}

static const TypeInfo stm32mp2_LTDC_types[] = {
    {
        .name           = TYPE_STM32MP2_LTDC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2LTDCState),
        .instance_init  = stm32mp2_LTDC_init,
        .class_size     = sizeof(Stm32MP2LTDCClass),
        .class_init     = stm32mp2_LTDC_class_init,
    }
};

DEFINE_TYPES(stm32mp2_LTDC_types)
