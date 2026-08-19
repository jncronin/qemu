#include "gk_peripherals.h"

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

        case 0x18:
            {
                // CR7
                uint32_t settable = 0x3000101;
                s->regs[0x18/4] = (s->regs[0x18/4] & ~settable) |
                    (settable & (uint32_t)val64);

                // duplicate bit 0 to bit 16
                s->regs[0x18/4] = (s->regs[0x18/4] & ~(0x1U << 16)) |
                    ((s->regs[0x18/4] & 0x1U) << 16);
            }
            break;

        case 0x1c:
            {
                // CR8
                uint32_t settable = 0x3000101;
                s->regs[0x1c/4] = (s->regs[0x1c/4] & ~settable) |
                    (settable & (uint32_t)val64);

                // duplicate bit 0 to bit 16
                s->regs[0x1c/4] = (s->regs[0x1c/4] & ~(0x1U << 16)) |
                    ((s->regs[0x1c/4] & 0x1U) << 16);
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
