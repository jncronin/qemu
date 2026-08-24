#include "gk_peripherals.h"

/* STM32MP2 EXTI */
struct Stm32MP2EXTIClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2EXTIState, Stm32MP2EXTIClass,
                    STM32MP2_EXTI)

static const Property stm32mp2_EXTI_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2EXTIState, id, 0),
};

static void update_irq(struct Stm32MP2EXTIState *s);

static void cpu2_sysresetq(void *opaque, int n, int level);
static void cpu2_sev(void *opaque, int n, int level);
static void cpu3_sev(void *opaque, int n, int level);

static void stm32mp2_EXTI_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2EXTIState *s = opaque;
    (void)s;

    switch(addr)
    {
        case 0xc:
        case 0x10:
        case 0x2c:
        case 0x30:
        case 0x4c:
        case 0x50:
            s->regs[addr/4] &= ~((uint32_t)val64);
            update_irq(s);
            break;

        default:
            s->regs[addr/4] = (uint32_t)val64;
            break;
    }
}

static uint64_t stm32mp2_EXTI_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2EXTIState *s = opaque;
    (void)s;

    uint32_t ret = 0;

    switch(addr)
    {
        default:
            ret = s->regs[addr/4];
            break;
    }
    
    return ret;
}

static const MemoryRegionOps stm32mp2_EXTI_ops = {
    .read = stm32mp2_EXTI_read,
    .write = stm32mp2_EXTI_write,
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

static void stm32mp2_EXTI_init(Object *obj)
{
    Stm32MP2EXTIState *s = STM32MP2_EXTI(obj);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_EXTI_ops, s,
                          TYPE_STM32MP2_EXTI, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_out(DEVICE(obj), &s->irqs[0][0], STM32MP2_EXTI_NIRQ * 2);
}

static void stm32mp2_EXTI_realize(DeviceState *obj, Error **errp)
{
    /* Here we add all the input pins.  It seems reasonable to get these from the
        Interrupt List table in the RM. */
    struct Stm32MP2EXTIState *s = (struct Stm32MP2EXTIState *)obj;
    switch(s->id)
    {
        case 1:
            qdev_init_gpio_in_named(DEVICE(obj), cpu2_sysresetq, "cpu2_sysresetq", 1);
            qdev_init_gpio_in_named(DEVICE(obj), cpu2_sev, "cpu2_sev", 1);
            break;
        case 2:
            qdev_init_gpio_in_named(DEVICE(obj), cpu3_sev, "cpu3_sev", 1);
            break;
    }
}

static void stm32mp2_EXTI_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_EXTI_properties);
    dc->realize = stm32mp2_EXTI_realize;
}

static const TypeInfo stm32mp2_EXTI_types[] = {
    {
        .name           = TYPE_STM32MP2_EXTI,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2EXTIState),
        .instance_init  = stm32mp2_EXTI_init,
        .class_size     = sizeof(Stm32MP2EXTIClass),
        .class_init     = stm32mp2_EXTI_class_init,
    }
};

DEFINE_TYPES(stm32mp2_EXTI_types)

/* Update output irqs dependent upon value of pending registers and mask registers */
static void update_irq(struct Stm32MP2EXTIState *s)
{
    BQL_LOCK_GUARD();

    uint32_t masked_vals[2][3];
    masked_vals[0][0] = (s->regs[0xc/4] | s->regs[0x10/4]) & s->regs[0x80/4];
    masked_vals[1][0] = (s->regs[0xc/4] | s->regs[0x10/4]) & s->regs[0xc0/4];
    masked_vals[0][1] = (s->regs[0x2c/4] | s->regs[0x30/4]) & s->regs[0x90/4];
    masked_vals[1][1] = (s->regs[0x2c/4] | s->regs[0x30/4]) & s->regs[0xd0/4];
    masked_vals[0][2] = (s->regs[0x4c/4] | s->regs[0x50/4]) & s->regs[0xa0/4];
    masked_vals[1][2] = (s->regs[0x4c/4] | s->regs[0x50/4]) & s->regs[0xe0/4];

    for(unsigned int core = 0; core < 2; core++)
    {
        for(unsigned int i = 0; i < 3; i++)
        {
            uint32_t mv = masked_vals[core][i];

            for(unsigned int bit = 0; bit < 32; bit++)
            {
                uint32_t bittest = 1U << bit;
                if((mv & bittest) && s->irq_set[core][i * 32 + bit] == 0)
                {
                    s->irq_set[core][i * 32 + bit] = 1;
                    qemu_set_irq(s->irqs[core][i * 32 + bit], 1);
                    //fprintf(stderr, "EXTI: set output core %u, irq %u\n", core, i * 32 + bit);
                }
                else if(!(mv & bittest) && s->irq_set[core][i * 32 + bit] == 1)
                {
                    s->irq_set[core][i * 32 + bit] = 0;
                    qemu_set_irq(s->irqs[core][i * 32 + bit], 0);
                    //fprintf(stderr, "EXTI: clear output core %u, irq %u\n", core, i * 32 + bit);
                }
            }
        }
    }
}

/* Handlers for individual input pins */
static void cpu2_sysresetq(void *opaque, int n, int level)
{
    fprintf(stderr, "EXTI: CPU2_SYSRESETQ: %d\n", level);
}

static void cpu2_sev(void *opaque, int n, int level)
{
    //fprintf(stderr, "EXTI: CPU2_SEV: %d\n", level);
    // event 64
    struct Stm32MP2EXTIState *s = (struct Stm32MP2EXTIState *)opaque;
    if(level && (s->regs[0x40/4] & 0x1))
    {
        s->regs[0x4c/4] |= 0x1;
        update_irq(s);
    }
    else if(!level && (s->regs[0x44] & 0x1))
    {
        s->regs[0x50/4] |= 0x1;
        update_irq(s);
    }
}

static void cpu3_sev(void *opaque, int n, int level)
{
    fprintf(stderr, "EXTI: CPU3_SEV: %d\n", level);
}


