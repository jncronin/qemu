#include "gk_peripherals.h"

/* STM32MP2 I2C */
struct Stm32MP2I2CClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2I2CState, Stm32MP2I2CClass,
                    STM32MP2_I2C)

static const Property stm32mp2_I2C_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2I2CState, id, 0),
};

static unsigned int i2c_cr2_to_addr(uint32_t addr)
{
    return (unsigned int)((addr >> 1) & 0x7f);
}

static void i2c_send_stop(Stm32MP2I2CState *s)
{
    // send stop
    unsigned int i2caddr = i2c_cr2_to_addr(s->cr2);
    if(s->devs[i2caddr] && s->devs[i2caddr]->stop)
        s->devs[i2caddr]->stop(s->devs[i2caddr]);
    s->isr |= 1U << 5;      // set STOPF
    s->isr &= ~(1U << 6);       // clear TC
}

static uint32_t i2c_read_data(Stm32MP2I2CState *s)
{
    uint32_t n_left = (s->cr2 >> 16) & 0xffU;
    //fprintf(stderr, "I2C: read with n_left: %u\n", n_left);
    if(n_left && (s->cr2 & (0x1 << 10)))        // ensure wrn set
    {
        // get data from slave
        //fprintf(stderr, "I2C: read data\n");

        // get data from slave
        unsigned int i2caddr = i2c_cr2_to_addr(s->cr2);
        s->rxdr = (s->devs[i2caddr] && s->devs[i2caddr]->read) ? s->devs[i2caddr]->read(s->devs[i2caddr]) : 0;
        s->isr |= 1U << 2;

        n_left--;
        //fprintf(stderr, "I2C: n_left->: %u\n", n_left);

        // program new nbytes
        s->cr2 = (s->cr2 & ~(0xffU << 16)) | (n_left << 16);

        if(!n_left)
        {
            if(s->cr2 & (1U << 25))
            {
                // autoend - send stop
                i2c_send_stop(s);
                //fprintf(stderr, "I2C: autoend, send stop\n");
            }
            else
            {
                // no autoend, set tc
                s->isr |= 1U << 6;
                //fprintf(stderr, "I2C: set TC\n");
            }

            if(s->cr2 & (1U << 24))
            {
                // reload, set tcr
                s->isr |= 1U << 7;
                //fprintf(stderr, "I2C: set TCR\n");
            }
        }
    }
    else
    {
        // nothing to read - don't update rxdr
        s->isr &= ~(1U << 2);
    }
    return s->rxdr;
}

static void i2c_write_data(Stm32MP2I2CState *s, uint32_t d)
{
    if(!(s->isr & 0x1))
    {
        // txe not set - abort
        return;
    }

    uint32_t n_left = (s->cr2 >> 16) & 0xffU;
    if(n_left)
    {
        // send data to slave
        unsigned int i2caddr = i2c_cr2_to_addr(s->cr2);
        if(s->devs[i2caddr] && s->devs[i2caddr]->write)
            s->devs[i2caddr]->write(s->devs[i2caddr], d);

        n_left--;

        // program new nbytes
        s->cr2 = (s->cr2 & ~(0xffU << 16)) | (n_left << 16);

        if(!n_left)
        {
            if(s->cr2 & (1U << 25))
            {
                // autoend - send stop
                i2c_send_stop(s);
            }
            else
            {
                // no autoend, set tc
                s->isr |= 1U << 6;
            }

            if(s->cr2 & (1U << 24))
            {
                // reload, set tcr
                s->isr |= 1U << 7;
            }

            s->isr &= ~0x3U;        // clear txe, txis
        }
    }
    else
    {
        // nothing to write
        s->isr &= ~0x3U;        // clear txe, txis
    }
}

static void stm32mp2_I2C_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2I2CState *s = opaque;
    (void)s;

    //fprintf(stderr, "I2C: write %x to %p\n", (unsigned)val64, (void *)addr);

    switch(addr)
    {
        case 0:
            s->cr1 = val64;
            if(!(val64 & 0x1))
            {
                s->state = i2c_Reset;
                s->cr2 &= ~(1U << 13);      // clear START
            }
            return;

        case 4:
            s->cr2 = val64;

            if(val64 & 0xff0000U)
            {
                // nbtyes non zero
                s->isr &= ~(1U << 7);       // clear TCR
            }

            if(val64 & (1U << 13))
            {
                s->isr &= ~(1U << 6);       // clear TC

                if(!(s->cr1 & 0x1))
                {
                    // PE not enabled - just clear
                    s->cr2 &= ~(1U << 13);
                }
                else
                {
                    // send start
                    unsigned int i2caddr = i2c_cr2_to_addr(val64);
                    unsigned int is_read = val64 & (0x1 << 10);

                    //fprintf(stderr, "I2C: START to %x\n", i2caddr);

                    // for now, assume ack
                    int ack = (s->devs[i2caddr] && s->devs[i2caddr]->start &&
                        s->devs[i2caddr]->start(s->devs[i2caddr]) == 0) ? 1 : 0;
                    if(ack)
                    {
                        s->cr2 &= ~(1U << 13);  // clear start

                        if(is_read)
                        {
                            s->rxdr = i2c_read_data(s);
                        }
                        else
                        {
                            s->isr |= 0x3u;  // set txis + txe
                        }
                    }
                    else
                    {
                        s->cr2 &= ~(1U << 13);  // clear start
                        s->isr |= (1U << 4);    // NACKF
                    }
                }
            }
            if(val64 & (1U << 14))
            {
                i2c_send_stop(s);
                s->cr2 &= ~(1U << 14);
            }
            return;

        case 0x10:
            s->timingr = val64;
            return;

        case 0x1c:
            s->isr &= (0xffffc0c7 | ~val64);
            return;

        case 0x28:
            i2c_write_data(s, val64);
            return;
    }
}

static uint64_t stm32mp2_I2C_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2I2CState *s = opaque;
    (void)s;

    uint32_t ret = 0;
    
    switch(addr)
    {
        case 0:
            ret = s->cr1;
            break;

        case 4:
            ret = s->cr2;
            break;

        case 0x10:
            ret = s->timingr;
            break;

        case 0x18:
            ret = s->isr;
            break;

        case 0x24:
            {
                uint32_t retval = s->rxdr;
                i2c_read_data(s);
                ret = retval;
            }
            break;
            
        default:
            fprintf(stderr, "I2C: read from %p unimplemented\n",
                (void *)addr);
            break;
    }

    //fprintf(stderr, "I2C: read from 0x%lx : %x\n",
    //    addr, ret);
    return ret;
}

static const MemoryRegionOps stm32mp2_I2C_ops = {
    .read = stm32mp2_I2C_read,
    .write = stm32mp2_I2C_write,
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

static void stm32mp2_I2C_init(Object *obj)
{
    Stm32MP2I2CState *s = STM32MP2_I2C(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_I2C_ops, s,
                          TYPE_STM32MP2_I2C, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32mp2_I2C_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    device_class_set_props(dc, stm32mp2_I2C_properties);
}

static const TypeInfo stm32mp2_I2C_types[] = {
    {
        .name           = TYPE_STM32MP2_I2C,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2I2CState),
        .instance_init  = stm32mp2_I2C_init,
        .class_size     = sizeof(Stm32MP2I2CClass),
        .class_init     = stm32mp2_I2C_class_init,
    }
};

DEFINE_TYPES(stm32mp2_I2C_types)
