#include "gk_peripherals.h"

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
