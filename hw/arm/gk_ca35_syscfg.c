#include "gk_peripherals.h"

/* STM32MP2 CA35_SYSCFG */
struct Stm32MP2CA35_SYSCFGClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2CA35_SYSCFGState, Stm32MP2CA35_SYSCFGClass,
                    STM32MP2_CA35_SYSCFG)

static const Property stm32mp2_CA35_SYSCFG_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2CA35_SYSCFGState, id, 0),
};

static void stm32mp2_CA35_SYSCFG_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2CA35_SYSCFGState *s = opaque;
    (void)s;

    switch(addr)
    {
        case 0x2088:
            s->m33_access_cr = val64;
            break;

        case 0x20a0:
            s->m33_tzen_cr = val64;
            break;

        case 0x20a4:
            s->m33_initsvtor_cr = val64;
            break;

        case 0x20a8:
            s->m33_initnsvtor_cr = val64;
            break;
            
        default:
            fprintf(stderr, "CA35_SYSCFG: write %x to %p\n", (unsigned)val64, (void *)addr);
    }
}

static uint64_t stm32mp2_CA35_SYSCFG_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2CA35_SYSCFGState *s = opaque;
    (void)s;

    uint32_t ret = 0;

    switch(addr)
    {
        case 0x2088:
            ret = s->m33_access_cr;
            break;

        case 0x20a0:
            ret = s->m33_tzen_cr;
            break;

        case 0x20a4:
            ret = s->m33_initsvtor_cr;
            break;

        case 0x20a8:
            ret = s->m33_initnsvtor_cr;
            break;

        default:
            fprintf(stderr, "CA35_SYSCFG: read from %p unimplemented\n",
                (void *)addr);
            break;
    }
    
    return ret;
}

static const MemoryRegionOps stm32mp2_CA35_SYSCFG_ops = {
    .read = stm32mp2_CA35_SYSCFG_read,
    .write = stm32mp2_CA35_SYSCFG_write,
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

static void stm32mp2_CA35_SYSCFG_init(Object *obj)
{
    Stm32MP2CA35_SYSCFGState *s = STM32MP2_CA35_SYSCFG(obj);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_CA35_SYSCFG_ops, s,
                          TYPE_STM32MP2_CA35_SYSCFG, 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    s->m33_access_cr = 0x3;
    s->m33_tzen_cr = 0x1;
    s->m33_initsvtor_cr = 0x0e080000;
    s->m33_initnsvtor_cr = 0x0a080000;
}

static void stm32mp2_CA35_SYSCFG_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_CA35_SYSCFG_properties);
}

static const TypeInfo stm32mp2_CA35_SYSCFG_types[] = {
    {
        .name           = TYPE_STM32MP2_CA35_SYSCFG,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2CA35_SYSCFGState),
        .instance_init  = stm32mp2_CA35_SYSCFG_init,
        .class_size     = sizeof(Stm32MP2CA35_SYSCFGClass),
        .class_init     = stm32mp2_CA35_SYSCFG_class_init,
    }
};

DEFINE_TYPES(stm32mp2_CA35_SYSCFG_types)
