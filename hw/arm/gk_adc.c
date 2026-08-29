#include "gk_peripherals.h"

/* STM32MP2 ADC */
struct Stm32MP2ADCClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2ADCState, Stm32MP2ADCClass,
                    STM32MP2_ADC);

OBJECT_DECLARE_SIMPLE_TYPE(Stm32MP2ADCChannelInputState, STM32MP2_ADC_INPUT_CHANNEL);

static const Property stm32mp2_ADC_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2ADCState, id, 0),
    DEFINE_PROP_LINK("inp0", Stm32MP2ADCState, inp[0], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp1", Stm32MP2ADCState, inp[1], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp2", Stm32MP2ADCState, inp[2], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp3", Stm32MP2ADCState, inp[3], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp4", Stm32MP2ADCState, inp[4], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp5", Stm32MP2ADCState, inp[5], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp6", Stm32MP2ADCState, inp[6], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp7", Stm32MP2ADCState, inp[7], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp8", Stm32MP2ADCState, inp[8], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp9", Stm32MP2ADCState, inp[9], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp10", Stm32MP2ADCState, inp[10], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp11", Stm32MP2ADCState, inp[11], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp12", Stm32MP2ADCState, inp[12], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp13", Stm32MP2ADCState, inp[13], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp14", Stm32MP2ADCState, inp[14], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp15", Stm32MP2ADCState, inp[15], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp16", Stm32MP2ADCState, inp[16], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp17", Stm32MP2ADCState, inp[17], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
    DEFINE_PROP_LINK("inp18", Stm32MP2ADCState, inp[18], TYPE_STM32MP2_ADC_INPUT_CHANNEL, 
        struct Stm32MP2ADCChannelInputState *),
};

static void adc_do_conv(struct Stm32MP2ADCState *s, int inst_id);
static void adc_reset(void *opaque, int n, int level);
static void tim_cb(void *opaque);
static void update_irq(struct Stm32MP2ADCState *s, int inst_id);

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
            s->inst[inst_id].r.isr &= ~val64;
            break;

        case 0x4:
            s->inst[inst_id].r.ier = val64;
            break;

        case 0x8:
            s->inst[inst_id].r.cr = val64;
            if(val64 & 0x1)
                s->inst[inst_id].r.isr |= 0x1;
            else
                s->inst[inst_id].r.isr &= ~0x1;
            if(val64 & 0x4)
            {
                // adstart
                ptimer_transaction_begin(s->inst[inst_id].pt);
                adc_do_conv(s, inst_id);
                ptimer_transaction_commit(s->inst[inst_id].pt);
            }
            break;
        case 0xc:
            s->inst[inst_id].r.cfgr1 = val64;
            break;
        case 0x10:
            s->inst[inst_id].r.cfgr2 = val64;
            break;
        case 0x14:
            s->inst[inst_id].r.smpr[0] = val64;
            break;
        case 0x18:
            s->inst[inst_id].r.smpr[1] = val64;
            break;
        case 0x1c:
            s->inst[inst_id].r.pcsel = val64;
            break;
        case 0x30:
        case 0x34:
        case 0x38:
        case 0x3c:
            s->inst[inst_id].r.sqr[(addr - 0x30) / 0x4] = val64;
            break;
        case 0x4c:
            s->inst[inst_id].r.jsqr = val64;
            break;
        case 0x50:
        case 0x54:
        case 0x58:
        case 0x5c:
            s->inst[inst_id].r.ofcfgr[(addr - 0x50) / 0x4] = val64;
            break;
        case 0x60:
        case 0x64:
        case 0x68:
        case 0x6c:
            s->inst[inst_id].r.ofr[(addr - 0x60) / 0x4] = val64;
            break;
        case 0x70:
            s->inst[inst_id].r.gcomp = val64;
            break;
        case 0x80:
        case 0x84:
        case 0x88:
        case 0x8c:
            s->inst[inst_id].r.jdr[(addr - 0x80) / 0x4] = val64;
            break;
        case 0xc0:
            s->inst[inst_id].r.difsel = val64;
            break;
        case 0xc4:
            s->inst[inst_id].r.calfact = val64;
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
            ret = s->inst[inst_id].r.isr;
            break;
        case 0x4:
            ret = s->inst[inst_id].r.ier;
            break;
        case 0x8:
            ret = s->inst[inst_id].r.cr;
            break;
        case 0xc:
            ret = s->inst[inst_id].r.cfgr1;
            break;
        case 0x10:
            ret = s->inst[inst_id].r.cfgr2;
            break;
        case 0x14:
            ret = s->inst[inst_id].r.smpr[0];
            break;
        case 0x18:
            ret = s->inst[inst_id].r.smpr[1];
            break;
        case 0x1c:
            ret = s->inst[inst_id].r.pcsel;
            break;
        case 0x30:
        case 0x34:
        case 0x38:
        case 0x3c:
            ret = s->inst[inst_id].r.sqr[(addr - 0x30) / 0x4];
            break;
        case 0x40:
            ret = s->inst[inst_id].r.dr;
            s->inst[inst_id].r.isr &= ~(1U << 2);     // clear eoc
            qemu_set_irq(s->dma[inst_id], 0);
            update_irq(s, inst_id);
            break;
        case 0x4c:
            ret = s->inst[inst_id].r.jsqr;
            break;
        case 0x50:
        case 0x54:
        case 0x58:
        case 0x5c:
            ret = s->inst[inst_id].r.ofcfgr[(addr - 0x50) / 0x4];
            break;
        case 0x60:
        case 0x64:
        case 0x68:
        case 0x6c:
            ret = s->inst[inst_id].r.ofr[(addr - 0x60) / 0x4];
            break;
        case 0x70:
            ret = s->inst[inst_id].r.gcomp;
            break;
        case 0x80:
        case 0x84:
        case 0x88:
        case 0x8c:
            ret = s->inst[inst_id].r.jdr[(addr - 0x80) / 0x4];
            break;
        case 0xc0:
            ret = s->inst[inst_id].r.difsel;
            break;
        case 0xc4:
            ret = s->inst[inst_id].r.calfact;
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
    qdev_init_gpio_out(DEVICE(obj), s->irq, 2);
    qdev_init_gpio_out_named(DEVICE(obj), s->dma, "dma", 2);
    qdev_init_gpio_in_named(DEVICE(obj), adc_reset, "rst", 1);

    s->inst[0].inst_id = 0;
    s->inst[0].adc = s;
    s->input_freq[0] = 64000000;
    s->inst[0].pt = ptimer_init(tim_cb, &s->inst[0], PTIMER_POLICY_CONTINUOUS_TRIGGER);
    s->inst[1].inst_id = 1;
    s->inst[1].adc = s;
    s->input_freq[1] = 64000000;
    s->inst[1].pt = ptimer_init(tim_cb, &s->inst[1], PTIMER_POLICY_CONTINUOUS_TRIGGER);
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
    },
    {
        .name           = TYPE_STM32MP2_ADC_INPUT_CHANNEL,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(Stm32MP2ADCChannelInputState)
    }
};

DEFINE_TYPES(stm32mp2_ADC_types)

static void adc_do_conv(struct Stm32MP2ADCState *s, int inst_id)
{
    if(s->inst[inst_id].r.isr & (1U << 2))
        s->inst[inst_id].r.isr |= 1U << 4;    // ovr

    int64_t val = 0;
    double sample_time = 1.5;

    if(!(s->inst[inst_id].r.cr & (1u << 31)))         // ensure not in calibration mode
    {
        // Get current channel id
        int seq_id = s->inst[inst_id].r.seq_idx + 1;      // index 0 in sqr0 is sequence length
        int seq_reg = seq_id / 5;
        int seq_offset = (seq_id % 5) * 6;
        uint32_t chan_id = (s->inst[inst_id].r.sqr[seq_reg] >> seq_offset) & 0x1fu;

        // Get current sample time
        int smp_id = s->inst[inst_id].r.seq_idx;
        int smp_reg = smp_id / 10;
        int smp_offset = (smp_id % 10) * 3;
        uint32_t smp_val = (s->inst[inst_id].r.smpr[smp_reg] >> smp_offset) & 0x7u;
        switch(smp_val)
        {
            case 0:
                sample_time = 1.5;
                break;
            case 1:
                sample_time = 2.5;
                break;
            case 2:
                sample_time = 6.5;
                break;
            case 3:
                sample_time = 11.5;
                break;
            case 4:
                sample_time = 23.5;
                break;
            case 5:
                sample_time = 46.5;
                break;
            case 6:
                sample_time = 246.5;
                break;
            case 7:
                sample_time = 1499.5;
                break;
        }

        // Get value of this channel, interpolate to output size
        struct Stm32MP2ADCChannelInputState *chanp = s->inp[chan_id];

        if(!chanp)
        {
            fprintf(stderr, "ADC%d: chan: %d, no connection\n", s->id + inst_id, chan_id);
        }
        else
        {
            uint64_t maxval = 0;
            switch((s->inst[inst_id].r.cfgr1 >> 2) & 0x3u)
            {
                case 0:
                    maxval = 1ull << 12;
                    break;
                case 1:
                    maxval = 1ull << 10;
                    break;
                case 2:
                    maxval = 1ull << 8;
                    break;
                case 3:
                    maxval = 1ull << 6;
                    break;
            }

            maxval *= (uint64_t)(((s->inst[inst_id].r.cfgr2 >> 16) & 0x3ffu) + 1);  // ovsr
            uint32_t ovss_shift = (s->inst[inst_id].r.cfgr2 >> 5) & 0xfu;
            uint32_t lshift = (s->inst[inst_id].r.cfgr2 >> 28) & 0xfu;

            // ovss handles rounding, so if both ovss and lshift are enabled then
            //  assume they happen together
            if(lshift > ovss_shift)
            {
                maxval <<= lshift - ovss_shift;
            }
            else if(lshift < ovss_shift)
            {
                maxval >>= ovss_shift - lshift;
            }

            val = (int64_t)(chanp->val - chanp->minval) * (int64_t)maxval /
                (int64_t)(chanp->maxval - chanp->minval);

            maxval -= 1;
            if(val >= maxval)
                val = maxval;

            //fprintf(stderr, "ADC%d: chan: %d, lshift: %u, ovss_shift: %u, %d [%d - %d] -> %ld [0 - %lu]\n",
            //    s->id + inst_id, chan_id,
            //    lshift, ovss_shift,
            //    chanp->val, chanp->minval, chanp->maxval, val, maxval);
        }
    }    

    s->inst[inst_id].r.dr = (uint32_t)val;
    s->inst[inst_id].r.isr |= 1U << 2;        // eoc

    int is_eos = 0;
    int retrigger = 0;
    s->inst[inst_id].r.seq_idx++;
    //fprintf(stderr, "ADC%d: set seq_idx to %d\n", s->id + inst_id, s->inst[inst_id].r.seq_idx);

    //fprintf(stderr, "ADC%d: CR: %08x, CFGR1: %08x, CFGR2: %08x\n", s->id + inst_id,
    //    s->inst[inst_id].r.cr, s->inst[inst_id].r.cfgr1, s->inst[inst_id].r.cfgr2);

    if(s->inst[inst_id].r.cfgr1 & (1u << 16))
    {
        // discontinuous mode
        if(s->inst[inst_id].r.n_disc)
            s->inst[inst_id].r.n_disc--;

        if(s->inst[inst_id].r.n_disc == 0)
        {
            is_eos = 1;
        }
        else
        {
            retrigger = 1;
        }
    }
    else
    {
        if(s->inst[inst_id].r.seq_idx >= ((s->inst[inst_id].r.sqr[0] & 0xfu) + 1))
        {
            is_eos = 1;
            s->inst[inst_id].r.seq_idx = 0;
            //fprintf(stderr, "ADC%d: sequence of %u complete\n", s->id + inst_id, (s->inst[inst_id].r.sqr[0] & 0xfu) + 1);
            //fprintf(stderr, "ADC%d: set seq_idx to %d\n", s->id + inst_id, s->inst[inst_id].r.seq_idx);
        }
        if(s->inst[inst_id].r.cfgr1 & (1u << 13))
        {
            // continuous mode
            retrigger = 1;
        }
        else
        {
            // single conversion mode - clear adstart
            s->inst[inst_id].r.cr &= ~0x4;
        }
    }

    if(is_eos)
    {
        s->inst[inst_id].r.isr |= 1U << 3;        // eoc
    }
    update_irq(s, inst_id);
    switch(s->inst[inst_id].r.cfgr1 & 0x3)
    {
        case 0x1:
        case 0x3:
            qemu_set_irq(s->dma[inst_id], 1);
            break;
    }

    // reschedule another conversion
    if(retrigger)
    {
        // calculate retrigger time
        double retrig_period = 1.0 / (double)s->input_freq[inst_id];

        // adjust by prescaler
        uint32_t presc_val = (s->com.ccr >> 18) & 0xfu;
        retrig_period *= (double)(1u << presc_val);

        // adjust by sample time + sar time
        double sar_time = 13.5;
        switch((s->inst[inst_id].r.cfgr1 >> 2) & 0x3u)
        {
            case 0:
                sar_time = 13.5;
                break;
            case 1:
                sar_time = 11.5;
                break;
            case 2:
                sar_time = 8.5;
                break;
            case 3:
                sar_time = 6.5;
                break;
        }
        retrig_period *= sample_time * sar_time;

        // adjust by number of samples per conversion
        retrig_period *= (double)(1u << ((s->inst[inst_id].r.cfgr1 >> 16) & 0x3ffu));

        //fprintf(stderr, "ADC: retrigger %f s = %f Hz\n", retrig_period, 1.0 / retrig_period);

        //ptimer_transaction_begin called outside this function
        ptimer_set_limit(s->inst[inst_id].pt, 1, 1);
        ptimer_set_period(s->inst[inst_id].pt, (int64_t)(retrig_period * 1000000000.0));
        ptimer_run(s->inst[inst_id].pt, 1);
        //ptimer_transaction_commit called outside this function
    }
}

static void adc_reset(void *opaque, int n, int level)
{
    struct Stm32MP2ADCState *s = (struct Stm32MP2ADCState *)opaque;
    (void)s;
    if(level)
    {
        fprintf(stderr, "ADC: RESET: %d\n", level);

        memset(&s->inst[0].r, 0, sizeof(s->inst[0].r));
        memset(&s->com, 0, sizeof(s->com));

        for(unsigned int i = 0u; i < 2u; i++)
        {
            if(s->irq_set[i])
            {
                s->irq_set[i] = 0;
                qemu_set_irq(s->irq[i], 0);
            }
        }
    }
}

static void tim_cb(void *opaque)
{
    struct adc_inst *inst = (struct adc_inst *)opaque;

    adc_do_conv(inst->adc, inst->inst_id);
}

static void update_irq(struct Stm32MP2ADCState *s, int inst_id)
{
    uint32_t irq_val = s->inst[inst_id].r.isr & s->inst[inst_id].r.ier;
    if(irq_val && !s->irq_set[inst_id])
    {
        s->irq_set[inst_id] = 1;
        qemu_set_irq(s->irq[inst_id], 1);
    }
    else if(!irq_val && s->irq_set[inst_id])
    {
        s->irq_set[inst_id] = 0;
        qemu_set_irq(s->irq[inst_id], 0);
    }
}
