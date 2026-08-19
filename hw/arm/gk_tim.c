#include "gk_peripherals.h"

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
