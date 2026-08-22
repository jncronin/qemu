#include "gk_peripherals.h"

/* STM32MP2 SDMMC */
struct Stm32MP2SDMMCClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2SDMMCState, Stm32MP2SDMMCClass,
                    STM32MP2_SDMMC)

static const Property stm32mp2_SDMMC_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2SDMMCState, id, 0),
};

static void sdmmc_send_command(struct Stm32MP2SDMMCState *s);
static int sdmmc_read_block(struct Stm32MP2SDMMCState *s);
static void sdmmc_patch_star(struct Stm32MP2SDMMCState *s);
static void sdmmc_update_irq(struct Stm32MP2SDMMCState *s);
static void sdmmc_reset(void *opaque, int n, int level);

static void stm32mp2_SDMMC_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2SDMMCState *s = opaque;
    (void)s;

    qemu_mutex_lock(&s->m);

    switch(addr)
    {
        case 0:
            s->power = val64;
            break;
        case 0x4:
            s->clkcr = val64;
            break;
        case 0x8:
            s->argr = val64;
            break;
        case 0xc:
            s->cmdr = val64;
            // handle commands here
            if(val64 & (1U << 12))
            {
                // cpsmen
                sdmmc_send_command(s);
            }

            break;
        case 0x24:
            s->dtimer = val64;
            break;
        case 0x28:
            s->dlenr = val64;
            break;
        case 0x2c:
            s->dctrl = val64;

            // handle data here
            break;
        case 0x30:
            s->dcntr = val64;
            break;
        case 0x38:
            s->star = s->star & ~(uint32_t)(val64 & 0x1fe00fffu);
            //fprintf(stderr, "SDMMC: ICR: %x -> STAR: %x\n", (uint32_t)val64, s->star);
            sdmmc_update_irq(s);
            break;
        case 0x3c:
            s->maskr = val64;
            break;
        case 0x40:
            s->acktimer = val64;
            break;
        case 0x44:
            s->fifothrr = val64;
            break;
        case 0x50:
            s->idmactrlr = val64;
            break;
        case 0x54:
            s->idmabsizer = val64;
            break;
        case 0x58:
            s->idmabaser = val64;
            break;
        case 0x64:
            s->idmalar = val64;
            break;
        case 0x68:
            s->idmabar = val64;
            break;
        case 0x80:
        case 0x84:
        case 0x88:
        case 0x8c:
        case 0x90:
        case 0x94:
        case 0x98:
        case 0x9c:
        case 0xa0:
        case 0xa4:
        case 0xa8:
        case 0xac:
        case 0xb0:
        case 0xb4:
        case 0xb8:
        case 0xbc:
            //s->fifo = val64;
            // TODO send these data
            break;            

        default:
            fprintf(stderr, "SDMMC: write %x to %p\n", (unsigned)val64, (void *)addr);
            break;
    }

    qemu_mutex_unlock(&s->m);
}

static uint64_t stm32mp2_SDMMC_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2SDMMCState *s = opaque;
    (void)s;
    uint32_t ret = 0;

    qemu_mutex_lock(&s->m);

    switch(addr)
    {
        case 0:
            ret = s->power;
            break;
        case 0x4:
            ret = s->clkcr;
            break;
        case 0x8:
            ret = s->argr;
            break;
        case 0xc:
            ret = s->cmdr;
            break;
        case 0x10:
            ret = s->respcmdr;
            break;
        case 0x14:
            ret = s->resp[0];
            break;
        case 0x18:
            ret = s->resp[1];
            break;
        case 0x1c:
            ret = s->resp[2];
            break;
        case 0x20:
            ret = s->resp[3];
            break;
        case 0x24:
            ret = s->dtimer;
            break;
        case 0x28:
            ret = s->dlenr;
            break;
        case 0x2c:
            ret = s->dctrl;
            break;
        case 0x30:
            ret = s->dcntr;
            break;
        case 0x34:
            sdmmc_patch_star(s);
            sdmmc_update_irq(s);
            ret = s->star;
            break;
        case 0x38:
            ret = s->icr;
            break;
        case 0x3c:
            ret = s->maskr;
            break;
        case 0x40:
            ret = s->acktimer;
            break;
        case 0x44:
            ret = s->fifothrr;
            break;
        case 0x50:
            ret = s->idmactrlr;
            break;
        case 0x54:
            ret = s->idmabsizer;
            break;
        case 0x58:
            ret = s->idmabaser;
            break;
        case 0x64:
            ret = s->idmalar;
            break;
        case 0x68:
            ret = s->idmabar;
            break;
        case 0x80:
        case 0x84:
        case 0x88:
        case 0x8c:
        case 0x90:
        case 0x94:
        case 0x98:
        case 0x9c:
        case 0xa0:
        case 0xa4:
        case 0xa8:
        case 0xac:
        case 0xb0:
        case 0xb4:
        case 0xb8:
        case 0xbc:
            {
                if(s->rx_fifo_user_ptr < s->rx_fifo_data_size)
                {
                    ret = s->rx_fifo_buf[s->rx_fifo_user_ptr / 4];
                    s->rx_fifo_user_ptr += 4;
                }
                if(s->rx_fifo_user_ptr >= s->rx_fifo_data_size)
                {
                    if(s->dctrl & 0x1)
                    {
                        if(s->dcntr)
                            sdmmc_read_block(s);
                        else
                        {
                            // end of read
                            s->dctrl &= ~0x1u;      // clear dten
                            s->star |= 1U << 8;     // dataend
                            sdmmc_patch_star(s);
                            sdmmc_update_irq(s);
                        }
                    }
                    else
                    {
                        // spurious read
                        fprintf(stderr, "SDMMC: FIFO read but not in transfer state\n");
                        ret = 0;
                    }
                }
            }
            break;
        case 0x3f0:
            ret = 0x4;
            break;
        case 0x3f4:
            ret = 0x30;
            break;
        case 0x3f8:
            ret = 0x140022;
            break;
        case 0x3fc:
            ret = 0xa3c5dd01;
            break;
        default:
            fprintf(stderr, "SDMMC: read from %p unimplemented\n",
                (void *)addr);
    }
    
    qemu_mutex_unlock(&s->m);

    return ret;
}

static const MemoryRegionOps stm32mp2_SDMMC_ops = {
    .read = stm32mp2_SDMMC_read,
    .write = stm32mp2_SDMMC_write,
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

static void stm32mp2_SDMMC_init(Object *obj)
{
    Stm32MP2SDMMCState *s = STM32MP2_SDMMC(obj);

    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_SD_BUS, DEVICE(s), "sd-bus");

    memory_region_init_io(&s->mmio, obj, &stm32mp2_SDMMC_ops, s,
                          TYPE_STM32MP2_SDMMC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), sdmmc_reset, "rst", 1);

    qemu_mutex_init(&s->m);
}

static void stm32mp2_SDMMC_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_SDMMC_properties);
}

static const TypeInfo stm32mp2_SDMMC_types[] = {
    {
        .name           = TYPE_STM32MP2_SDMMC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2SDMMCState),
        .instance_init  = stm32mp2_SDMMC_init,
        .class_size     = sizeof(Stm32MP2SDMMCClass),
        .class_init     = stm32mp2_SDMMC_class_init,
    }
};

DEFINE_TYPES(stm32mp2_SDMMC_types)

void sdmmc_send_command(struct Stm32MP2SDMMCState *s)
{
    uint32_t cmd = s->cmdr & 0x3fu;
    uint32_t waitresp = (s->cmdr >> 8) & 0x3u;
    int with_data = (s->cmdr & (1U << 6)) ? 1 : 0;

    unsigned int nresp = 0;
    switch(waitresp)
    {
        case 0:
            nresp = 0;
            break;
        case 1:
        case 2:
            nresp = 1;
            break;
        case 3:
            nresp = 4;
            break;
    }

    SDRequest req;
    req.cmd = cmd;
    req.arg = s->argr;

    uint32_t resp[4];

    size_t cmdret = sdbus_do_command(&s->sdbus, &req, (uint8_t *)resp, 4 * nresp);
    //fprintf(stderr, "SDMMC: cmd %u, cmdret: %zu\n", cmd, cmdret);

    if(nresp == 0)
    {
        s->respcmdr = 0x3fu;
        memset(s->resp, 0, sizeof(s->resp));
        s->star |= 1U << 7;     // CMDSENT
        sdmmc_update_irq(s);
    }
    else
    {
        if(cmdret == nresp * 4)
        {
            s->respcmdr = cmd;

            if(nresp == 1)
            {
                s->resp[0] = ldl_be_p(&resp[0]);
                s->resp[1] = 0;
                s->resp[2] = 0;
                s->resp[3] = 0;
            }
            else
            {
                s->resp[0] = ldl_be_p(&resp[0]);
                s->resp[1] = ldl_be_p(&resp[1]);
                s->resp[2] = ldl_be_p(&resp[2]);
                s->resp[3] = ldl_be_p(&resp[3]);
            }
            /*for(unsigned int i = 0; i < nresp; i++)
            {
                fprintf(stderr, "SDMMC:   resp%u: %x\n", i, s->resp[i]);
            }*/

            s->star |= 1U << 6;     // CMDREND
            sdmmc_update_irq(s);
        }
        else
        {
            // timeout
            s->respcmdr = 0x3fu;
            memset(s->resp, 0, sizeof(s->resp));
            s->star |= 1U << 2;     // CTIMEOUT
            sdmmc_update_irq(s);
            return;
        }
    }

    // if with data, start the transfer (if read)
    if(with_data)
    {
        s->dctrl |= 0x1;        // set DTEN
        s->dcntr = s->dlenr;
        if(s->dctrl & 0x2)
        {
            // its a read
            //fprintf(stderr, "SDMMC: read data, dcntr: %x, idmactrlr: %x, idmabsize: %x\n",
            //    s->dcntr, s->idmactrlr, s->idmabsizer);

            // if idma active, use it to send the appropriate number of blocks
            if(s->idmactrlr & 0x1)
            {
                if(s->idmactrlr & 0x2)
                {
                    fprintf(stderr, "SDMMC: IDMA linked list not supported\n");
                }
                else
                {
                    hwaddr hlen = s->dcntr;
                    void *host_addr = address_space_map(&address_space_memory, (hwaddr)s->idmabaser, 
                        &hlen, true, MEMTXATTRS_UNSPECIFIED);
                    if(!host_addr)
                    {
                        fprintf(stderr, "SDMMC: couldn't get host address for idma: %x\n", s->idmabaser);
                        s->star |= 1U << 27;        // idmate
                        sdmmc_update_irq(s);
                        return;
                    }

                    sdbus_read_data(&s->sdbus, host_addr, s->dcntr);

                    address_space_unmap(&address_space_memory, host_addr, hlen, false, hlen);

                    s->dcntr = 0;
                    s->star |= 1U << 10;        // dbckend
                    s->star |= 1U << 8;     // dataend
                    s->dctrl &= ~0x1u;
                    sdmmc_update_irq(s);
                }
            }
            else
            {
                // handle blocks via PIO
                sdmmc_read_block(s);
            }
        }
        else
        {
            // its a write

            // if with idma - do the transfer direct from user memory
            if(s->idmactrlr & 0x1)
            {
                uint32_t blk_size = 1U << ((s->dctrl >> 4) & 0xfu);
                if(s->idmactrlr & 0x2)
                {
                    fprintf(stderr, "SDMMC: IDMA linked list not supported\n");
                }
                else
                {
                    if(s->dcntr % blk_size)
                    {
                        fprintf(stderr, "SDMMC: DLENR (%x) is not a multiple of blk_size (%x)\n",
                            s->dcntr, blk_size);
                        s->dcntr = 0;
                        s->dctrl &= ~0x1u;
                        s->star |= 1U << 27;    // idmate
                        sdmmc_update_irq(s);
                    }
                    while(s->dcntr)
                    {
                        hwaddr hlen = blk_size;
                        void *host_addr = address_space_map(&address_space_memory, (hwaddr)s->idmabaser, 
                            &hlen, false, MEMTXATTRS_UNSPECIFIED);
                        if(!host_addr)
                        {
                            fprintf(stderr, "SDMMC: couldn't get host address for idma: %x\n", s->idmabaser);
                            s->star |= 1U << 27;        // idmate
                            sdmmc_update_irq(s);
                            break;
                        }

                        sdbus_write_data(&s->sdbus, host_addr, blk_size);

                        address_space_unmap(&address_space_memory, host_addr, hlen, false, hlen);

                        s->idmabaser += blk_size;
                        s->dcntr -= blk_size;

                        if(s->dcntr)
                        {
                            s->star |= 1U << 10;        // dbckend
                            sdmmc_update_irq(s);
                        }
                    }
                    s->star |= 1U << 8;     // dataend
                    s->dctrl &= ~0x1u;
                    sdmmc_update_irq(s);
                }
            }
            // if not, handle via writes to FIFOR
        }
    }
}

int sdmmc_read_block(struct Stm32MP2SDMMCState *s)
{
    uint32_t bread = 0;

    if(!(s->dctrl & 0x1) || !(s->dctrl & 0x2))
    {
        fprintf(stderr, "SDMMC: read_block called when not in read state\n");
        return -1;
    }

    uint32_t blk_size = 1U << ((s->dctrl >> 4) & 0xfu);
    if(blk_size > sizeof(s->rx_fifo_buf))
    {
        fprintf(stderr, "SDMMC: blk_size too big: %u\n", blk_size);
        return -1;
    }
    if(s->dcntr >= blk_size)
    {
        // read a block
        sdbus_read_data(&s->sdbus, s->rx_fifo_buf, blk_size);
        bread = blk_size;
        s->dcntr -= blk_size;
        s->rx_fifo_user_ptr = 0;
        s->rx_fifo_data_size = blk_size;

        s->star |= 1U << 10;    // dbckend
        sdmmc_update_irq(s);
    }
    else if(s->dcntr != 0)
    {
        fprintf(stderr, "SDMMC: dcntr (%u) not multiple of blk_size (%u)\n", s->dcntr, blk_size);
        return -1;
    }

    return (int)bread;
}

static void sdmmc_patch_star(struct Stm32MP2SDMMCState *s)
{
    // set fifo members of star appropriately
    if(s->rx_fifo_data_size == 0)
    {
        s->star |= 1U << 19;        // rxfifoe
        s->star &= ~(1U << 17);     // !rxfifof
        s->star &= ~(1U << 15);     // !rxfifohf
    }
    else
    {
        s->star &= ~(1U << 19);     // !rxfifoe

        if(s->rx_fifo_user_ptr == 0)
        {
            s->star |= 1U << 17;    // rxfifof
            s->star |= 1U << 15;    // rxfifohf
        }
        else if(s->rx_fifo_user_ptr < s->rx_fifo_data_size / 2)
        {
            s->star &= ~(1U << 17); // !rxfifof
            s->star |= 1U << 15;    // rxfifohf
        }
        else
        {
            s->star &= ~(1U << 17);     // !rxfifof
            s->star &= ~(1U << 15);     // !rxfifohf
        }
    }

    // TODO: same for txfifo


    // DPSMACT
    if(s->dctrl & 0x1)
    {
        s->star |= 1U << 12;
    }
    else
    {
        s->star &= ~(1U << 12);
    }
}

static void sdmmc_update_irq(struct Stm32MP2SDMMCState *s)
{
    uint32_t need_irq = s->star & s->maskr;
    if(need_irq && !s->irq_set)
    {
        //fprintf(stderr, "SDMMC: update_irq: %x (%x) -> 1\n", s->star, need_irq);
        s->irq_set = 1;
        qemu_set_irq(s->irq, 1);
    }
    else if(!need_irq && s->irq_set)
    {
        //fprintf(stderr, "SDMMC: update_irq: %x (%x) -> 0\n", s->star, need_irq);
        s->irq_set = 0;
        qemu_set_irq(s->irq, 0);
    }
}

static void sdmmc_reset(void *opaque, int n, int level)
{
    struct Stm32MP2SDMMCState *s = (struct Stm32MP2SDMMCState *)opaque;
    (void)s;
    if(level)
    {
        fprintf(stderr, "SDMMC: RESET: %d\n", level);
        
        s->power = 0;
        s->clkcr = 0;
        s->argr = 0;
        s->cmdr = 0;
        s->respcmdr = 0;
        s->resp[0] = 0;
        s->resp[1] = 0;
        s->resp[2] = 0;
        s->resp[3] = 0;
        s->dtimer = 0;
        s->dlenr = 0;
        s->dctrl = 0;
        s->dcntr = 0;
        s->star = 0;
        s->icr = 0;
        s->maskr = 0;
        s->acktimer = 0;
        s->fifothrr = 0;
        s->idmactrlr = 0;
        s->idmabsizer = 0;
        s->idmabaser = 0;
        s->idmalar = 0;
        s->idmabar = 0;
        memset(s->rx_fifo_buf, 0, sizeof(s->rx_fifo_buf));
        memset(s->tx_fifo_buf, 0, sizeof(s->tx_fifo_buf));
        s->rx_fifo_data_size = 0;
        s->rx_fifo_user_ptr = 0;
        s->tx_fifo_data_size = 0;
        s->tx_fifo_user_ptr = 0;
        if(s->irq_set)
        {
            s->irq_set = 0;
            qemu_set_irq(s->irq, 0);
        }
    }
}
