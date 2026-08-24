#include "gk_peripherals.h"

/* STM32MP2 ADC */
struct Stm32MP2ADCClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2ADCState, Stm32MP2ADCClass,
                    STM32MP2_ADC)

static const Property stm32mp2_ADC_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2ADCState, id, 0),
};

static void adc_do_conv(struct Stm32MP2ADCState *s, int inst_id);
static void adc_reset(void *opaque, int n, int level);

static void stm32mp2_ADC_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2ADCState *s = opaque;
    (void)s;

    int inst_id = 0;

    if(addr >= 0x100 && addr < 0x200 && s->id == 1)
    {
        inst_id = 1;
        addr -= 0x100;
    }

    switch(addr)
    {
        case 0:
            s->inst[inst_id].isr &= ~val64;
            break;

        case 0x4:
            s->inst[inst_id].ier = val64;
            break;

        case 0x8:
            s->inst[inst_id].cr = val64;
            if(val64 & 0x1)
                s->inst[inst_id].isr |= 0x1;
            else
                s->inst[inst_id].isr &= ~0x1;
            if(val64 & 0x4)
            {
                // adstart
                if(!(s->inst[inst_id].cfgr1 & (1U << 13)))
                {
                    // single conversion
                    adc_do_conv(s, inst_id);
                    s->inst[inst_id].cr &= ~0x4;
                }
                else
                {
                    // TODO
                    fprintf(stderr, "ADC: continuous mode not implemented\n");
                }
            }
            break;
        case 0xc:
            s->inst[inst_id].cfgr1 = val64;
            break;
        case 0x10:
            s->inst[inst_id].cfgr2 = val64;
            break;
        case 0x14:
            s->inst[inst_id].smpr1 = val64;
            break;
        case 0x18:
            s->inst[inst_id].smpr2 = val64;
            break;
        case 0x1c:
            s->inst[inst_id].pcsel = val64;
            break;
        case 0x30:
        case 0x34:
        case 0x38:
        case 0x3c:
            s->inst[inst_id].sqr[(addr - 0x30) / 0x4] = val64;
            break;
        case 0x4c:
            s->inst[inst_id].jsqr = val64;
            break;
        case 0x50:
        case 0x54:
        case 0x58:
        case 0x5c:
            s->inst[inst_id].ofcfgr[(addr - 0x50) / 0x4] = val64;
            break;
        case 0x60:
        case 0x64:
        case 0x68:
        case 0x6c:
            s->inst[inst_id].ofr[(addr - 0x60) / 0x4] = val64;
            break;
        case 0x70:
            s->inst[inst_id].gcomp = val64;
            break;
        case 0x80:
        case 0x84:
        case 0x88:
        case 0x8c:
            s->inst[inst_id].jdr[(addr - 0x80) / 0x4] = val64;
            break;
        case 0xc0:
            s->inst[inst_id].difsel = val64;
            break;
        case 0xc4:
            s->inst[inst_id].calfact = val64;
            break;

        case 0x308:
            s->com.ccr = val64;
            break;

        default:
            fprintf(stderr, "ADC: write %x to %p\n", (unsigned)val64, (void *)addr);
    }
}

static uint64_t stm32mp2_ADC_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2ADCState *s = opaque;
    (void)s;

    uint32_t ret = 0;

    int inst_id = 0;

    if(addr >= 0x100 && addr < 0x200 && s->id == 1)
    {
        inst_id = 1;
        addr -= 0x100;
    }

    switch(addr)
    {
        case 0:
            ret = s->inst[inst_id].isr;
            break;
        case 0x4:
            ret = s->inst[inst_id].ier;
            break;
        case 0x8:
            ret = s->inst[inst_id].cr;
            break;
        case 0xc:
            ret = s->inst[inst_id].cfgr1;
            break;
        case 0x10:
            ret = s->inst[inst_id].cfgr2;
            break;
        case 0x14:
            ret = s->inst[inst_id].smpr1;
            break;
        case 0x18:
            ret = s->inst[inst_id].smpr2;
            break;
        case 0x1c:
            ret = s->inst[inst_id].pcsel;
            break;
        case 0x30:
        case 0x34:
        case 0x38:
        case 0x3c:
            ret = s->inst[inst_id].sqr[(addr - 0x30) / 0x4];
            break;
        case 0x40:
            ret = s->inst[inst_id].dr;
            s->inst[inst_id].isr &= ~(1U << 2);     // clear eoc
            break;
        case 0x4c:
            ret = s->inst[inst_id].jsqr;
            break;
        case 0x50:
        case 0x54:
        case 0x58:
        case 0x5c:
            ret = s->inst[inst_id].ofcfgr[(addr - 0x50) / 0x4];
            break;
        case 0x60:
        case 0x64:
        case 0x68:
        case 0x6c:
            ret = s->inst[inst_id].ofr[(addr - 0x60) / 0x4];
            break;
        case 0x70:
            ret = s->inst[inst_id].gcomp;
            break;
        case 0x80:
        case 0x84:
        case 0x88:
        case 0x8c:
            ret = s->inst[inst_id].jdr[(addr - 0x80) / 0x4];
            break;
        case 0xc0:
            ret = s->inst[inst_id].difsel;
            break;
        case 0xc4:
            ret = s->inst[inst_id].calfact;
            break;

        case 0x304:
            ret = s->com.csr;
            break;

        case 0x308:
            ret = s->com.ccr;
            break;

        case 0x3f0:
            ret = (s->id == 1) ? 0x00000242u : 0x00000241u;
            break;

        case 0x3f4:
            ret = 0x11u;
            break;

        case 0x3f8:
            ret = 0x110008u;
            break;

        case 0x3fc:
            ret = 0xa3c5dd01u;
            break;

        default:
            fprintf(stderr, "ADC: read from %p unimplemented\n",
                (void *)addr);
            break;
    }
    
    return ret;
}

static const MemoryRegionOps stm32mp2_ADC_ops = {
    .read = stm32mp2_ADC_read,
    .write = stm32mp2_ADC_write,
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

static void stm32mp2_ADC_init(Object *obj)
{
    Stm32MP2ADCState *s = STM32MP2_ADC(obj);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_ADC_ops, s,
                          TYPE_STM32MP2_ADC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), adc_reset, "rst", 1);
}

static void stm32mp2_ADC_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_ADC_properties);
}

static const TypeInfo stm32mp2_ADC_types[] = {
    {
        .name           = TYPE_STM32MP2_ADC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2ADCState),
        .instance_init  = stm32mp2_ADC_init,
        .class_size     = sizeof(Stm32MP2ADCClass),
        .class_init     = stm32mp2_ADC_class_init,
    }
};

DEFINE_TYPES(stm32mp2_ADC_types)

static void adc_do_conv(struct Stm32MP2ADCState *s, int inst_id)
{
    // TODO
    if(s->inst[inst_id].isr & (1U << 2))
        s->inst[inst_id].isr |= 1U << 4;    // ovr
    s->inst[inst_id].dr = 0;
    s->inst[inst_id].isr |= 1U << 2;
}

static void adc_reset(void *opaque, int n, int level)
{
    struct Stm32MP2ADCState *s = (struct Stm32MP2ADCState *)opaque;
    (void)s;
    if(level)
    {
        fprintf(stderr, "ADC: RESET: %d\n", level);

        memset(&s->inst[0], 0, sizeof(s->inst));
        memset(&s->com, 0, sizeof(s->com));

        if(s->irq_set)
        {
            s->irq_set = 0;
            qemu_set_irq(s->irq, 0);
        }
    }
}
