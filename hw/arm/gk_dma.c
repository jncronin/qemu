#include "gk_peripherals.h"

/* STM32MP2 DMA */
struct Stm32MP2DMAClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2DMAState, Stm32MP2DMAClass,
                    STM32MP2_DMA);

static const Property stm32mp2_DMA_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2DMAState, id, 0),
};

#define DMA_REQ_TYPE_BURST      3
#define DMA_REQ_TYPE_BLOCK      0
#define DMA_REQ_TYPE_REPBLOCK   1
#define DMA_REQ_TYPE_LINK       2

static void update_irq(struct Stm32MP2DMAState *s, int chan_id);
static void dma_reset(void *opaque, int n, int level);
static void dma_chan_reset(struct Stm32MP2DMAState *s, int chan_id);
static void dma_req(void *opaque, int n, int level);
static void dma_trig(void *opaque, int n, int level);
static int dma_handle_request(struct Stm32MP2DMAState *s, int chan_id, int req_type);

static void stm32mp2_DMA_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2DMAState *s = opaque;
    (void)update_irq;
    (void)s;

    int chan_id = -1;

    if(addr >= 0x50)
    {
        chan_id = (addr - 0x50) / 0x80;
        addr -= 0x50 + chan_id * 0x80;
    }

    if(chan_id == -1)
    {
        // common register
        switch(addr)
        {
            default:
                fprintf(stderr, "DMA%d: write addr %lx not supported\n", s->id, addr);
                break; 
        }
    }
    else
    {
        switch(addr)
        {
            case 0:
                s->chan[chan_id].lbar = val64;
                break;

            case 0x10:
                s->chan[chan_id].sr = val64;
                break;

            case 0x14:
                s->chan[chan_id].cr = val64;
                if((val64 & 0x1) && (s->chan[chan_id].tr2 & (1u << 9)))
                {
                    // EN + SWREQ
                    dma_handle_request(s, chan_id,
                        (s->chan[chan_id].tr2 >> 14) & 0x3u);
                    s->chan[chan_id].cr &= ~0x1u;
                }
                if(val64 & 0x2)
                {
                    // RESET
                    dma_chan_reset(s, chan_id);
                    s->chan[chan_id].cr &= ~0x2u;
                }
                break;

            case 0x40:
                s->chan[chan_id].tr1 = val64;
                break;

            case 0x44:
                s->chan[chan_id].tr2 = val64;
                break;

            case 0x48:
                s->chan[chan_id].br1 = val64;
                s->chan[chan_id].br1_int = val64;
                break;

            case 0x4c:
                s->chan[chan_id].sar = val64;
                break;

            case 0x50:
                s->chan[chan_id].dar = val64;
                break;

            case 0x54:
                s->chan[chan_id].tr3 = val64;
                break;

            case 0x58:
                s->chan[chan_id].br2 = val64;
                break;

            case 0x7c:
                s->chan[chan_id].llr = val64;
                break;

            default:
                fprintf(stderr, "DMA%d: chan %d, write addr %lx not supported\n", s->id,
                    chan_id, addr);
                break;
        }
    }
}

static uint64_t stm32mp2_DMA_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2DMAState *s = opaque;
    (void)s;

    uint32_t ret = 0;

    int chan_id = -1;

    if(addr >= 0x50)
    {
        chan_id = (addr - 0x50) / 0x80;
        addr -= 0x50 + chan_id * 0x80;
    }

    if(chan_id == -1)
    {
        // common register
        switch(addr)
        {
            default:
                break; 
        }
    }
    else
    {
        switch(addr)
        {
            case 0:
                ret = s->chan[chan_id].lbar;
                break;

            case 0x10:
                ret = s->chan[chan_id].sr;
                if(!(s->chan[chan_id].cr & 0x1))
                    ret |= 0x1; // IDLEF
                break;

            case 0x14:
                ret = s->chan[chan_id].cr;
                break;

            case 0x40:
                ret = s->chan[chan_id].tr1;
                break;

            case 0x44:
                ret = s->chan[chan_id].tr2;
                break;

            case 0x48:
                ret = s->chan[chan_id].br1;
                break;

            case 0x4c:
                ret = s->chan[chan_id].sar;
                break;

            case 0x50:
                ret = s->chan[chan_id].dar;
                break;

            case 0x54:
                ret = s->chan[chan_id].tr3;
                break;

            case 0x58:
                ret = s->chan[chan_id].br2;
                break;

            case 0x7c:
                ret = s->chan[chan_id].llr;
                break;

            default:
                break;
        }
    }
    
    return ret;
}

static const MemoryRegionOps stm32mp2_DMA_ops = {
    .read = stm32mp2_DMA_read,
    .write = stm32mp2_DMA_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .max_access_size = 4,
        .min_access_size = 1,
        .unaligned = false
    },
    .impl = {
        .max_access_size = 4,
        .min_access_size = 1,
        .unaligned = false
    },
};

static void stm32mp2_DMA_init(Object *obj)
{
    Stm32MP2DMAState *s = STM32MP2_DMA(obj);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_DMA_ops, s,
                          TYPE_STM32MP2_DMA, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    qdev_init_gpio_out_named(DEVICE(obj), s->irq, "sysbus-irq", 16);
    qdev_init_gpio_out_named(DEVICE(obj), s->tc, "tc", 16);
    qdev_init_gpio_in_named(DEVICE(obj), dma_reset, "rst", 1);
    qdev_init_gpio_in_named(DEVICE(obj), dma_req, "req", 255);
    qdev_init_gpio_in_named(DEVICE(obj), dma_trig, "trig", 128);

    for(unsigned int i = 0u; i < 16u; i++)
    {
        s->chan[i].chan_id = i;
        s->chan[i].dma = s;
    }
}

static void stm32mp2_DMA_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_DMA_properties);
}

static const TypeInfo stm32mp2_DMA_types[] = {
    {
        .name           = TYPE_STM32MP2_DMA,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2DMAState),
        .instance_init  = stm32mp2_DMA_init,
        .class_size     = sizeof(Stm32MP2DMAClass),
        .class_init     = stm32mp2_DMA_class_init,
    }
};

DEFINE_TYPES(stm32mp2_DMA_types)

static void update_irq(struct Stm32MP2DMAState *s, int chan_id)
{
    //fprintf(stderr, "DMA: update_irq: TODO\n");
}

static void dma_chan_reset(struct Stm32MP2DMAState *s, int chan_id)
{
    fprintf(stderr, "DMA: chan_reset: TODO\n");
}


static void dma_reset(void *opaque, int n, int level)
{
    fprintf(stderr, "DMA: dma_reset: TODO\n");
}

static void dma_req(void *opaque, int n, int level)
{
    struct Stm32MP2DMAState *s = STM32MP2_DMA(opaque);

    //fprintf(stderr, "DMA%d: REQ %d: %d\n", s->id, n, level);

    if(level)
    {
        // dma requests are all rising edge
        for(unsigned int chan_id = 0u; chan_id < 16u; chan_id++)
        {
            //fprintf(stderr, "DMA%d: chan %d: cr: %08x, tr1: %08x, tr2: %08x\n",
            //    s->id, chan_id, s->chan[chan_id].cr, s->chan[chan_id].tr1, s->chan[chan_id].tr2);
            if((s->chan[chan_id].cr & 0x1) &&                       // EN
                !(s->chan[chan_id].tr2 & (1u << 9)) &&              // !SWREQ
                (s->chan[chan_id].tr2 & 0xffu) == (uint32_t)n)      // REQSEL
            {
                dma_handle_request(s, chan_id,
                    (s->chan[chan_id].tr2 & (1u << 11)) ? DMA_REQ_TYPE_BLOCK : DMA_REQ_TYPE_BURST);
            }
        }
    }
}

static void dma_trig(void *opaque, int n, int level)
{
    fprintf(stderr, "DMA: dma_trig: TODO\n");
}

static int dma_handle_request(struct Stm32MP2DMAState *s, int chan_id, int req_type)
{
    //fprintf(stderr, "DMA%d: chan: %d, request received\n", s->id, chan_id);

    struct Stm32MP2DMAChannelState *cs = &s->chan[chan_id];

    int ret = -1;

    switch(req_type)
    {
        case DMA_REQ_TYPE_BURST:
            if((cs->br1 & 0xffffu) == (cs->br1_int & 0xffffu))
            {
                //fprintf(stderr, "DMA%d: chan: %d, begin block, sar: %08x, dar: %08x\n", s->id, chan_id,
                //    s->chan[chan_id].sar, s->chan[chan_id].dar);

                cs->sar_int = cs->sar;
                cs->dar_int = cs->dar;
            }

            {
                // copy 1 burst.  If beat sizes are the same then copy direct, else transfer via
                //  a internal pointer
                size_t src_beat_size = 1u << ((cs->tr1 >> 0) & 0x3);
                size_t dest_beat_size = 1u << ((cs->tr1 >> 16) & 0x3);
                size_t slen = ((cs->tr1 >> 4) & 0x3fu) + 1;
                size_t dlen = ((cs->tr1 >> 20) & 0x3fu) + 1;
                size_t slen_bytes = slen * src_beat_size;
                size_t dlen_bytes = dlen * dest_beat_size;

                // map src and addr each of block len
                cs->src_hlen = slen_bytes;
                cs->dst_hlen = dlen_bytes;
                cs->src = address_space_map(&address_space_memory, (hwaddr)cs->sar, 
                    &cs->src_hlen, false, MEMTXATTRS_UNSPECIFIED);
                cs->dst = address_space_map(&address_space_memory, (hwaddr)cs->dar, 
                    &cs->dst_hlen, true, MEMTXATTRS_UNSPECIFIED);

                if(!cs->src || !cs->dst)
                {
                    if(!cs->src)
                    {
                        fprintf(stderr, "DMA: failed to map src addr %p\n", (void *)(uintptr_t)cs->sar);
                    }
                    if(!cs->dst)
                    {
                        fprintf(stderr, "DMA: failed to map dst addr %p\n", (void *)(uintptr_t)cs->dar);
                    }
                    cs->sr |= 1u << 10;
                    cs->cr &= ~0x1u;
                    update_irq(s, chan_id);
                    return -1;
                }
                //fprintf(stderr, "DMA%d: chan: %d, src_beat_size: %zu, slen: %zu, slen_bytes: %zu, dest_beat_size: %zu, dlen: %zu, dlen_bytes: %zu\n",
                //    s->id, chan_id, src_beat_size, slen, slen_bytes, dest_beat_size, dlen, dlen_bytes);
                //fprintf(stderr, "DMA%d: chan: %d, sar: %08x, dar: %08x\n", s->id, chan_id, cs->sar, cs->dar);

                if(src_beat_size == dest_beat_size)
                {
                    size_t tfer_bytes = slen_bytes;
                    if(dlen_bytes < tfer_bytes)
                        tfer_bytes = dlen_bytes;
                    memcpy(cs->dst, cs->src, tfer_bytes);
                }
                else
                {
                    // copy via internal pointer
                    uint64_t v = 0;
                    if(slen > dlen) slen = dlen;

                    for(size_t i = 0u; i < slen; i++)
                    {
                        //fprintf(stderr, "DMA%d: chan: %d, src_addr: %08zx, dst_addr: %08zx\n",
                        //    s->id, chan_id, s->chan[chan_id].sar + i * src_beat_size,
                        //    s->chan[chan_id].dar + i * dest_beat_size);
                        switch(src_beat_size)
                        {
                            case 1:
                                v = *(uint8_t *)((uintptr_t)cs->src + i * src_beat_size);
                                break;
                            case 2:
                                v = *(uint16_t *)((uintptr_t)cs->src + i * src_beat_size);
                                break;
                            case 4:
                                v = *(uint32_t *)((uintptr_t)cs->src + i * src_beat_size);
                                break;
                            case 8:
                                v = *(uint64_t *)((uintptr_t)cs->src + i * src_beat_size);
                                break;
                        }

                        switch(dest_beat_size)
                        {
                            case 1:
                                *(uint8_t *)((uintptr_t)cs->dst + i * dest_beat_size) = (uint8_t)v;
                                break;
                            case 2:
                                *(uint16_t *)((uintptr_t)cs->dst + i * dest_beat_size) = (uint16_t)v;
                                break;
                            case 4:
                                *(uint32_t *)((uintptr_t)cs->dst + i * dest_beat_size) = (uint32_t)v;
                                break;
                            case 8:
                                *(uint64_t *)((uintptr_t)cs->dst + i * dest_beat_size) = (uint64_t)v;
                                break;
                        }
                    }
                }

                // unmap address
                address_space_unmap(&address_space_memory, cs->src, cs->src_hlen, false, cs->src_hlen);
                address_space_unmap(&address_space_memory, cs->dst, cs->dst_hlen, true, cs->dst_hlen);

                cs->src = NULL;
                cs->dst = NULL;

                // burst complete, now update addresses if necessary
                if(cs->tr1 & (1u << 19))
                    cs->dar += dlen_bytes;
                if(cs->tr1 & (1u << 3))
                    cs->sar += slen_bytes;
                if(cs->tr3 & (0x1fffu << 16))
                {
                    size_t dao = (cs->tr3 >> 16) & 0x1fffu;
                    switch((cs->tr3 >> 14) & 0x3)
                    {
                        case 0:
                            break;
                        case 1:
                            dao *= 16;
                            break;
                        case 2:
                        case 3:
                            dao *= 256;
                            break;
                    }
                    if(cs->br1 & (1u << 29))
                        cs->dar -= dao;
                    else
                        cs->dar += dao;
                }
                if(cs->tr3 & 0x1fffu)
                {
                    size_t sao = cs->tr3 & 0x1fffu;
                    switch((cs->tr3 >> 14) & 0x3)
                    {
                        case 0:
                            break;
                        case 1:
                            sao *= 16;
                            break;
                        case 2:
                        case 3:
                            sao *= 256;
                            break;
                    }
                    if(cs->br1 & (1u << 28))
                        cs->sar -= sao;
                    else
                        cs->sar += sao;
                }

                // update block number of data
                if((cs->br1 & 0xffffu) >= ((cs->br1_int & 0xffffu) / 2) && 
                    (cs->br1 & 0xffffu) < ((cs->br1_int & 0xffffu) / 2 + slen_bytes))
                {
                    // half block complete
                    if(((cs->tr2 >> 30) & 0x3u) == 0)
                        cs->sr |= 1u << 9;
                }

                if((cs->br1 & 0xffffu) <= slen_bytes)
                {
                    // end of block
                    if(((cs->tr2 >> 30) & 0x3u) == 0)
                        cs->sr |= 1u << 8;
                    //fprintf(stderr, "DMA%d: chan: %d, end block\n", s->id, chan_id);

                    ret = DMA_REQ_TYPE_BLOCK;

                    // BRDAO/BRSAO?
                    if(cs->br2)
                    {
                        uint32_t mult = 0;
                        switch((cs->tr3 >> 30) & 0x3)
                        {
                            case 0:
                                mult = 1;
                                break;
                            case 1:
                                mult = 16;
                                break;
                            case 2:
                            case 3:
                                mult = 256;
                                break;
                        }
                        if(cs->br1 & (1u << 31))
                            cs->dar -= ((cs->br2 >> 16) & 0xffffu) * mult;
                        else
                            cs->dar += ((cs->br2 >> 16) & 0xffffu) * mult;
                        if(cs->br1 & (1u << 30))
                            cs->sar -= ((cs->br2 >> 0) & 0xffffu) * mult;
                        else
                            cs->sar += ((cs->br2 >> 0) & 0xffffu) * mult;
                    }

                    // reload?
                    if(cs->br1 & (0x7ffu << 16))
                    {
                        cs->br1 = (cs->br1 & 0xf8000000u) | ((cs->br1 & 0x7ff0000u) - (1u << 16)) |
                            (cs->br1_int & 0xffffu);
                    }
                    else if(cs->llr & (1u << 29))
                    {
                        fprintf(stderr, "DMA: LLR UB1 not implemented\n");
                        cs->br1 = (cs->br1 & 0xffff0000u) | (cs->br1_int & 0xffffu);
                        ret = DMA_REQ_TYPE_REPBLOCK;
                    }
                    else if(cs->llr)
                    {
                        // any other linked list - just reload
                        cs->br1 = (cs->br1 & 0xffff0000u) | (cs->br1_int & 0xffffu);
                        ret = DMA_REQ_TYPE_REPBLOCK;
                        if(((cs->tr2 >> 30) & 0x3u) == 1)
                            cs->sr |= 1u << 8;
                        if(((cs->tr2 >> 30) & 0x3u) == 2)
                            cs->sr |= 1u << 8;
                        //fprintf(stderr, "DMA%d: chan: %d, end 2D block\n", s->id, chan_id);
                    }
                    else
                    {
                        cs->br1 = cs->br1 & 0xffff0000u;
                        ret = DMA_REQ_TYPE_LINK;
                        if(((cs->tr2 >> 30) & 0x3u) == 3)
                            cs->sr |= 1u << 8;
                        //fprintf(stderr, "DMA%d: chan: %d, end channel\n", s->id, chan_id);
                    }
                }
                else
                {
                    cs->br1 = (cs->br1 & 0xffff0000u) | ((cs->br1 & 0xffffu) - slen_bytes);
                    ret = DMA_REQ_TYPE_BURST;
                }
            }

            update_irq(s, chan_id);
            return ret;

        case DMA_REQ_TYPE_BLOCK:
            while(true)
            {
                ret = dma_handle_request(s, chan_id, DMA_REQ_TYPE_BURST);
                if(ret != DMA_REQ_TYPE_BURST)
                    return ret;
            }
            break;

        case DMA_REQ_TYPE_REPBLOCK:
            while(true)
            {
                ret = dma_handle_request(s, chan_id, DMA_REQ_TYPE_BLOCK);
                if(ret != DMA_REQ_TYPE_BLOCK)
                    return ret;
            }
            break;

        case DMA_REQ_TYPE_LINK:
            while(true)
            {
                ret = dma_handle_request(s, chan_id, DMA_REQ_TYPE_REPBLOCK);
                if(ret != DMA_REQ_TYPE_REPBLOCK)
                    return ret;
            }
            break;
    }

    return ret;
}
