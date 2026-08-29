#include "gk_peripherals.h"

/* STM32MP2 GPIO */
struct Stm32MP2GPIOClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2GPIOState, Stm32MP2GPIOClass,
                    STM32MP2_GPIO)

static const Property stm32mp2_GPIO_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2GPIOState, id, 0),
};

static void gpio_reset(void *opaque, int n, int level);
static void gpio_input_handler(void *opaque, int n, int level);
static void gpio_update_outputs(struct Stm32MP2GPIOState *s);
static char gpio_letter(int id);
static unsigned int gpio_num_pins(int id);

static void stm32mp2_GPIO_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2GPIOState *s = opaque;
    (void)s;

    switch(addr)
    {
        case 0:
            s->moder = val64;
            gpio_update_outputs(s);
            break;

        case 4:
            s->otyper = val64;
            break;

        case 8:
            s->ospeedr = val64;
            break;
        
        case 0xc:
            s->pupdr = val64;
            break;

        case 0x14:
            s->odr = val64;
            gpio_update_outputs(s);
            break;

        case 0x18:
            s->odr |= val64 & 0xffffu;
            s->odr &= ~((val64 >> 16) & 0xffffu);
            gpio_update_outputs(s);
            break;

        case 0x20:
            s->afrl = val64;
            break;

        case 0x24:
            s->afrh = val64;
            break;

        case 0x28:
            s->odr &= ~(val64 & 0xffffu);
            gpio_update_outputs(s);
            break;

        default:
            fprintf(stderr, "GPIO: write %x to %p\n", (unsigned)val64, (void *)addr);
            break;
    }
}

static uint64_t stm32mp2_GPIO_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2GPIOState *s = opaque;
    (void)s;
    
    uint32_t ret = 0;

    switch(addr)
    {
        case 0:
            ret = s->moder;
            break;

        case 4:
            ret = s->otyper;
            break;

        case 8:
            ret = s->ospeedr;
            break;

        case 0xc:
            ret = s->pupdr;
            break;

        case 0x10:
            ret = s->idr;
            break;

        case 0x14:
            ret = s->odr;
            break;

        case 0x20:
            ret = s->afrl;
            break;

        case 0x24:
            ret = s->afrh;
            break;
    }

    return ret;
}

static const MemoryRegionOps stm32mp2_GPIO_ops = {
    .read = stm32mp2_GPIO_read,
    .write = stm32mp2_GPIO_write,
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

static void stm32mp2_GPIO_realize(DeviceState *obj, Error **errp)
{
    Stm32MP2GPIOState *s = STM32MP2_GPIO(obj);
    gpio_reset(s, 0, 1);
}

static void stm32mp2_GPIO_init(Object *obj)
{
    Stm32MP2GPIOState *s = STM32MP2_GPIO(obj);
    memory_region_init_io(&s->mmio, obj, &stm32mp2_GPIO_ops, s,
                          TYPE_STM32MP2_GPIO, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    qdev_init_gpio_in(DEVICE(obj), gpio_input_handler, 16);
    qdev_init_gpio_in_named(DEVICE(obj), gpio_reset, "rst", 1);
    qdev_init_gpio_out(DEVICE(obj), s->outputs, 16);
}

static void stm32mp2_GPIO_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_GPIO_properties);
    dc->realize = stm32mp2_GPIO_realize;
}

static const TypeInfo stm32mp2_GPIO_types[] = {
    {
        .name           = TYPE_STM32MP2_GPIO,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2GPIOState),
        .instance_init  = stm32mp2_GPIO_init,
        .class_size     = sizeof(Stm32MP2GPIOClass),
        .class_init     = stm32mp2_GPIO_class_init,
    }
};

DEFINE_TYPES(stm32mp2_GPIO_types)

static char gpio_letter(int id)
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    if(id < 0 || id >= (sizeof(alphabet) - 1))
        return '!';
    return alphabet[id];
}

static void gpio_input_handler(void *opaque, int n, int level)
{
    struct Stm32MP2GPIOState *s = (struct Stm32MP2GPIOState *)opaque;
    if(n < 0 || n >= (int)gpio_num_pins(s->id))
        return;
    if(level)
    {
        s->idr |= 1u << n;
    }
    else
    {
        s->idr &= ~(1u << n);
    }

    fprintf(stderr, "GPIO%c: IDR: %08x\n", gpio_letter(s->id), s->idr);
}

static void gpio_reset(void *opaque, int n, int level)
{
    struct Stm32MP2GPIOState *s = (struct Stm32MP2GPIOState *)opaque;
    (void)s;
    if(level)
    {
        fprintf(stderr, "GPIO%c: RESET: %d\n", gpio_letter(s->id), level);

        switch(s->id)
        {
            case GK_GPIOH:
                s->moder = 0x0fffffffu;
                break;
            case GK_GPIOK:
                s->moder = 0x0000ffffu;
                break;
            case GK_GPIOZ:
                s->moder = 0x000fffffu;
                break;
            default:
                s->moder = 0xffffffffu;
                break;
        }

        s->otyper = 0;
        s->ospeedr = 0;
        s->pupdr = 0;
        s->idr &= 0xffffu;
        s->odr = 0;
        s->afrl = 0;
        s->afrh = 0;

        gpio_update_outputs(s);
    }
}

static unsigned int gpio_num_pins(int id)
{
    switch(id)
    {
        case GK_GPIOA:
        case GK_GPIOB:
        case GK_GPIOC:
        case GK_GPIOD:
        case GK_GPIOE:
        case GK_GPIOF:
        case GK_GPIOG:
        case GK_GPIOI:
        case GK_GPIOJ:
            return 16u;
       
        case GK_GPIOH:
            return 14u;

        case GK_GPIOK:
            return 8u;

        case GK_GPIOZ:
            return 10u;

        default:
            return 0u;
    }
}

static void gpio_update_outputs(struct Stm32MP2GPIOState *s)
{
    uint32_t new_irq_vals = 0;
    
    for(unsigned int i = 0u; i < gpio_num_pins(s->id); i++)
    {
        if(((s->odr >> (i * 2)) & 0x3u) != 0x1u)
            continue;
        new_irq_vals |= s->odr & (1u << i);
    }

    uint32_t set_vals = (s->irq_set ^ new_irq_vals) & new_irq_vals;
    uint32_t unset_vals = (s->irq_set ^ new_irq_vals) & ~new_irq_vals;

    s->irq_set = new_irq_vals;      // set first in case anything hooks the update

    for(unsigned int i = 0u; i < gpio_num_pins(s->id); i++)
    {
        if(set_vals & (1u << i))
            qemu_set_irq(s->outputs[i], 1);
        else if(unset_vals & (1u << i))
            qemu_set_irq(s->outputs[i], 0);
    }
}
