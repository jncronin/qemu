#include "gk_peripherals.h"
#include "ui/console.h"

/* STM32MP2 LTDC */
struct Stm32MP2LTDCClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2LTDCState, Stm32MP2LTDCClass,
                    STM32MP2_LTDC)

static const Property stm32mp2_LTDC_properties[] = {
    DEFINE_PROP_INT32("id", Stm32MP2LTDCState, id, 0),
};

static void ltdc_invalidate_display(void * opaque);
static bool ltdc_update_display(void *opaque);
static void tim_cb(void *, enum ClockEvent);
static void ltdc_update_shadow_regs(struct Stm32MP2LTDCState *ltdc, struct Stm32MP2LTDCLayer *l);
static void ltdc_update_size(struct Stm32MP2LTDCState *ltdc);
static void ltdc_update_size_main_thread(void *);

static const GraphicHwOps ltdc_gfx_ops = {
    .invalidate  = ltdc_invalidate_display,
    .gfx_update  = ltdc_update_display,
};

static void stm32mp2_LTDC_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2LTDCState *s = opaque;
    (void)s;

    switch(addr)
    {
        case 0x8:
            s->sscr = val64;
            break;

        case 0xc:
            s->bpcr = val64;
            ltdc_update_size(s);
            break;

        case 0x10:
            s->awcr = val64;
            ltdc_update_size(s);
            break;

        case 0x14:
            s->twcr = val64;
            break;

        case 0x18:
            s->gcr = val64;
            if(s->gcr & 0x1)
            {
                clock_set_hz(s->clk_out, 60);
            }
            else
            {
                clock_set(s->clk_out, 0);
            }
            break;

        case 0x24:
            s->srcr = val64;
            if(val64 & 0x1)
            {
                // IMR - update shadow registers
                ltdc_update_shadow_regs(s, &s->layers[0]);
                ltdc_update_shadow_regs(s, &s->layers[1]);
                ltdc_update_shadow_regs(s, &s->layers[2]);
                s->srcr &= ~0x1u;
            }
            break;

        case 0x28:
            s->gccr = val64;
            break;

        case 0x2c:
            s->bccr = val64;
            break;

        case 0x34:
            s->ier = val64;
            break;

        case 0x3c:
            {
                uint32_t old_isr = s->isr;
                s->isr = s->isr & (0xfffffe30 | ~(uint32_t)val64);
                if(!s->isr && old_isr)
                {
                    qemu_set_irq(s->irq, 0);
                }
            }
            break;

        case 0x40:
            s->lipcr = val64;
            break;

        default:
            fprintf(stderr, "LTDC: write %x to %p\n", (unsigned)val64, (void *)addr);
            break;
    }
}

static uint64_t stm32mp2_LTDC_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2LTDCState *s = opaque;
    (void)s;
    
    switch(addr)
    {
        case 0x8:
            return s->sscr;

        case 0xc:
            return s->bpcr;

        case 0x10:
            return s->awcr;

        case 0x14:
            return s->twcr;

        case 0x18:
            return s->gcr;

        case 0x1c:
            return 0x6be4d888;

        case 0x20:
            return 0xbf30;

        case 0x24:
            return s->srcr;

        case 0x28:
            return s->gccr;

        case 0x2c:
            return s->bccr;

        case 0x34:
            return s->ier;

        case 0x38:
            return s->isr;

        case 0x3c:
            return 0;

        case 0x40:
            return s->lipcr;

        default:
            fprintf(stderr, "LTDC: read from %p unimplemented\n",
                (void *)addr);
            break;
    }

    return 0;
}

static const MemoryRegionOps stm32mp2_LTDC_ops = {
    .read = stm32mp2_LTDC_read,
    .write = stm32mp2_LTDC_write,
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

static void stm32mp2_LTDC_init(Object *obj)
{
    Stm32MP2LTDCState *s = STM32MP2_LTDC(obj);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_LTDC_ops, s,
                          TYPE_STM32MP2_LTDC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    s->clk_out = qdev_init_clock_out(DEVICE(s), "clk_out");
    clock_set_callback(s->clk_out, tim_cb, s, ClockUpdate);
    s->con = qemu_graphic_console_create(DEVICE(obj), 0, &ltdc_gfx_ops, s);
    s->resize_bh = qemu_bh_new(ltdc_update_size_main_thread, s);
}

static void stm32mp2_LTDC_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
    device_class_set_props(dc, stm32mp2_LTDC_properties);
}

static const TypeInfo stm32mp2_LTDC_types[] = {
    {
        .name           = TYPE_STM32MP2_LTDC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2LTDCState),
        .instance_init  = stm32mp2_LTDC_init,
        .class_size     = sizeof(Stm32MP2LTDCClass),
        .class_init     = stm32mp2_LTDC_class_init,
    }
};

DEFINE_TYPES(stm32mp2_LTDC_types)



static void ltdc_invalidate_display(void * opaque)
{
    fprintf(stderr, "LTDC: INVALIDATE\n");
}

static uint32_t col = 0xff0000ff;

static bool ltdc_update_display(void *opaque)
{
    Stm32MP2LTDCState *s = (Stm32MP2LTDCState *)opaque;

    DisplaySurface *surface = qemu_console_surface(s->con);
    (void)surface;

    if(!(s->gcr & 0x1))
        return true;
    
    uint8_t *data = surface_data(surface);
    size_t stride = surface_stride(surface);
    size_t nlines = surface_height(surface);
    size_t width = surface_width(surface);

    for(size_t y = 0; y < nlines; y++)
    {
        uint32_t *line = (uint32_t *)(data + y * stride);

        for(size_t x = 0; x < width; x++)
        {
            line[x] = col;
        }
    }

    col += 0x100;

    qemu_console_update(s->con, 0, 0, width, nlines);

    return true;        // update done synchronously
}

static void tim_cb(void *opaque, enum ClockEvent)
{
    Stm32MP2LTDCState *s = STM32MP2_LTDC(opaque);

    if(s->srcr & 0x2)
    {
        // VBR - update shadow registers
        ltdc_update_shadow_regs(s, &s->layers[0]);
        ltdc_update_shadow_regs(s, &s->layers[1]);
        ltdc_update_shadow_regs(s, &s->layers[2]);
        s->srcr &= ~0x2u;
    }

    s->isr |= 0x1;
    if(s->ier & 0x1)
    {
        qemu_set_irq(s->irq, 1);
    }
}

void ltdc_update_shadow_regs(struct Stm32MP2LTDCState *s, struct Stm32MP2LTDCLayer *l)
{
    // TODO
}

void ltdc_update_size(struct Stm32MP2LTDCState *s)
{
    uint32_t a_w = (s->awcr >> 16) & 0xfffu;
    uint32_t a_h = s->awcr & 0xfffu;
    uint32_t b_w = (s->bpcr >> 16) & 0xfffu;
    uint32_t b_h = s->bpcr & 0xfffu;
    if(a_w && a_h && b_w && b_h && a_w > b_w && a_h > b_h)
    {
        uint32_t new_w = a_w - b_w;
        uint32_t new_h = a_h - b_h;
        fprintf(stderr, "LTDC: resize to %u x %u\n", new_w, new_h);
        s->new_w = new_w;
        s->new_h = new_h;

        // schedule the resize to happen on the main thread
        qemu_bh_schedule(s->resize_bh);
    }
}

void ltdc_update_size_main_thread(void *opaque)
{
    Stm32MP2LTDCState *s = (Stm32MP2LTDCState *)opaque;
    qemu_console_resize(s->con, s->new_w, s->new_h);
    qemu_console_update(s->con, 0, 0, s->new_w, s->new_h);
}
