#include "gk_peripherals.h"
#include "ui/console.h"
#include "ui/sdl2.h"

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
static void ltdc_layer_resize_main_thread(void *);

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
                if(s->layers[0].r.rcr & 0x4)
                    ltdc_update_shadow_regs(s, &s->layers[0]);
                if(s->layers[1].r.rcr & 0x4)
                    ltdc_update_shadow_regs(s, &s->layers[1]);
                if(s->layers[2].r.rcr & 0x4)
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
            if(addr >= 0x100 && addr < 0x400)
            {
                hwaddr layer = (addr - 0x100) / 0x100;
                hwaddr layer_addr = addr & 0xffu;

                struct Stm32MP2LTDCLayer *l = &s->layers[layer];

                switch(layer_addr)
                {
                    case 0x8:
                        l->sr.rcr = val64;
                        if(val64 & 0x1)
                        {
                            l->sr.rcr &= ~0x1u;
                            ltdc_update_shadow_regs(s, l);
                        }
                        return;

                    case 0xc:
                        l->sr.cr = val64;
                        return;
                    case 0x10:
                        l->sr.whpcr = val64;
                        return;
                    case 0x14:
                        l->sr.wvpcr = val64;
                        return;
                    case 0x18:
                        l->sr.ckcr = val64;
                        return;
                    case 0x1c:
                        l->sr.pfcr = val64;
                        return;
                    case 0x20:
                        l->sr.cacr = val64;
                        return;
                    case 0x24:
                        l->sr.dccr = val64;
                        return;
                    case 0x28:
                        l->sr.bfcr = val64;
                        return;
                    case 0x2c:
                        l->sr.blcr = val64;
                        return;
                    case 0x30:
                        l->sr.pcr = val64;
                        return;
                    case 0x34:
                        l->sr.cfbar = val64;
                        return;
                    case 0x38:
                        l->sr.cfblr = val64;
                        return;
                    case 0x3c:
                        l->sr.cfblnr = val64;
                        return;
                    case 0x40:
                        l->sr.afba0r = val64;
                        return;
                    case 0x44:
                        l->sr.afba1r = val64;
                        return;
                    case 0x48:
                        l->sr.afblr = val64;
                        return;
                    case 0x4c:
                        l->sr.afblnr = val64;
                        return;
                    case 0x50:
                        l->clut[(val64 >> 24) & 0xffu] = (uint32_t)(val64 & 0xffffffu);
                        return;
                    case 0x54:
                        l->sr.sisr = val64;
                        return;
                    case 0x58:
                        l->sr.sosr = val64;
                        return;
                    case 0x5c:
                        l->sr.svsfr = val64;
                        return;
                    case 0x60:
                        l->sr.svspr = val64;
                        return;
                    case 0x64:
                        l->sr.shsfr = val64;
                        return;
                    case 0x68:
                        l->sr.shspr = val64;
                        return;
                    case 0x6c:
                        l->sr.cyr0r = val64;
                        return;
                    case 0x70:
                        l->sr.cyr1r = val64;
                        return;
                    case 0x74:
                        l->sr.fpf0r = val64;
                        return;
                    case 0x78:
                        l->sr.fpf1r = val64;
                        return;
                }
            }

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
            if(addr >= 0x100 && addr < 0x400)
            {
                hwaddr layer = (addr - 0x100) / 0x100;
                hwaddr layer_addr = addr & 0xffu;

                struct Stm32MP2LTDCLayer *l = &s->layers[layer];

                switch(layer_addr)
                {
                    case 0:
                        return 0xff50a075;
                    case 4:
                        return (layer_addr == 2) ? 0 : 0x80000007;
                    case 8:
                        return l->sr.rcr;
                    case 0xc:
                        return l->sr.cr;
                    case 0x10:
                        return l->sr.whpcr;
                    case 0x14:
                        return l->sr.wvpcr;
                    case 0x18:
                        return l->sr.ckcr;
                    case 0x1c:
                        return l->sr.pfcr;
                    case 0x20:
                        return l->sr.cacr;
                    case 0x24:
                        return l->sr.dccr;
                    case 0x28:
                        return l->sr.bfcr;
                    case 0x2c:
                        return l->sr.blcr;
                    case 0x30:
                        return l->sr.pcr;
                    case 0x34:
                        return l->sr.cfbar;
                    case 0x38:
                        return l->sr.cfblr;
                    case 0x3c:
                        return l->sr.cfblnr;
                    case 0x40:
                        return l->sr.afba0r;
                    case 0x44:
                        return l->sr.afba1r;
                    case 0x48:
                        return l->sr.afblr;
                    case 0x4c:
                        return l->sr.afblnr;
                    case 0x54:
                        return l->sr.sisr;
                    case 0x58:
                        return l->sr.sosr;
                    case 0x5c:
                        return l->sr.svsfr;
                    case 0x60:
                        return l->sr.svspr;
                    case 0x64:
                        return l->sr.shsfr;
                    case 0x68:
                        return l->sr.shspr;
                    case 0x6c:
                        return l->sr.cyr0r;
                    case 0x70:
                        return l->sr.cyr1r;
                    case 0x74:
                        return l->sr.fpf0r;
                    case 0x78:
                        return l->sr.fpf1r;
                }
            }
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
    for(unsigned int i = 0u; i < 3; i++)
    {
        s->layers[i].resize_bh = qemu_bh_new(ltdc_layer_resize_main_thread, &s->layers[i]);
        s->layers[i].s = s;
        s->layers[i].r.rcr = 0x4;
        s->layers[i].sr.rcr = 0x4;
    }

    // create an offscreen renderer associated to a hidden window
    s->w = SDL_CreateWindow("", 0, 0, 800, 480, SDL_WINDOW_HIDDEN);
    s->r = SDL_CreateRenderer(s->w, -1, SDL_RENDERER_TARGETTEXTURE);
    s->t = NULL;
    if(!s->w)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    }
    if(!s->r)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    }
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
    if(!s->t)
        return true;

    // Render the background and the various layers to the texture
    SDL_SetRenderDrawColor(s->r,
        (Uint8)((s->bccr >> 16) & 0xffU), 
        (Uint8)((s->bccr >> 8) & 0xffU), 
        (Uint8)((s->bccr >> 0) & 0xffU),
        0xffu);

    SDL_RenderClear(s->r);

    // Blend layers with scaling
    for(unsigned int i = 0; i < 3; i++)
    {
        struct Stm32MP2LTDCLayer *l = &s->layers[i];

        if(!l->t)
            continue;

        if(!(l->r.cr & 0x1))
        {
            // layer disabled.  TODO: draw background anyway if not 0
            continue;
        }

        // get output size and target location
        uint32_t left = l->r.whpcr & 0xffffU;
        uint32_t right = (l->r.whpcr >> 16) & 0xffffu;
        uint32_t top = l->r.wvpcr & 0xffffU;
        uint32_t bottom = (l->r.wvpcr >> 16) & 0xffffu;

        if(left > right || top > bottom)
        {
            // zero/negative size - skip
            continue;
        }
        uint32_t lw = right - left + 1;
        uint32_t lh = bottom - top + 1;

        uint32_t stride = (l->r.cfblr >> 16) & 0xffffu;

        left -= ((s->bpcr >> 16) & 0xffffU) + 1;
        top -= (s->bpcr & 0xffffU) + 1;

        // get memory address and update texture with it
        hwaddr hlen = lh * stride;
        void *host_addr = address_space_map(&address_space_memory, (hwaddr)l->r.cfbar, 
            &hlen, false, MEMTXATTRS_UNSPECIFIED);
        //void *host_addr = qemu_map_ram_ptr(NULL, (hwaddr)l->r.cfbar);
        if(!host_addr)
        {
            fprintf(stderr, "LTDC: couldn't get host address for layer addr %x\n", l->r.cfbar);
            continue;
        }

        SDL_UpdateTexture(l->t, NULL, host_addr, stride);
        address_space_unmap(&address_space_memory, host_addr, hlen, false, hlen);

        SDL_Rect dest;
        dest.x = left;
        dest.y = top;
        dest.w = lw;
        dest.h = lh;

        SDL_RenderCopy(s->r, l->t, NULL, &dest);
    }

    // Copy to offscreen buffer (annoyingly this is again converted to texture in qemu sdl2 code)
    
    uint8_t *data = surface_data(surface);
    size_t stride = surface_stride(surface);
    size_t nlines = surface_height(surface);
    size_t width = surface_width(surface);

    SDL_RenderReadPixels(s->r, NULL, 0, data, stride);

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
        if(s->layers[0].r.rcr & 0x4)
            ltdc_update_shadow_regs(s, &s->layers[0]);
        if(s->layers[1].r.rcr & 0x4)
            ltdc_update_shadow_regs(s, &s->layers[1]);
        if(s->layers[2].r.rcr & 0x4)
            ltdc_update_shadow_regs(s, &s->layers[2]);
        s->srcr &= ~0x2u;
    }

    for(unsigned int i = 0; i < 3u; i++)
    {
        if(s->layers[i].r.rcr & 0x2)
            ltdc_update_shadow_regs(s, &s->layers[i]);
        s->layers[i].r.rcr &= ~0x2u;
        s->layers[i].sr.rcr &= ~0x2u;
    }

    s->isr |= 0x1;
    if(s->ier & 0x1)
    {
        qemu_set_irq(s->irq, 1);
    }
}

void ltdc_update_shadow_regs(struct Stm32MP2LTDCState *s, struct Stm32MP2LTDCLayer *l)
{
    // If we've made any changes to either the size or pixel format of the layer, we
    //  need to recreate its backing texture
    int update_tex = 0;
    if(l->r.cfblr != l->sr.cfblr)
        update_tex = 1;
    if(l->r.cfblnr != l->sr.cfblnr)
        update_tex = 1;
    if(l->r.pfcr != l->sr.pfcr)
        update_tex = 1;
    if(l->r.fpf0r != l->sr.fpf0r)
        update_tex = 1;
    if(l->r.fpf1r != l->sr.fpf1r)
        update_tex = 1;
    
    memcpy(&l->r, &l->sr, sizeof(struct Stm32MP2LTDCLayerRegs));

    if(update_tex)
    {
        // schedule the update to happen on the main thread
        qemu_bh_schedule(l->resize_bh);
    }
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

    // Now create a new texture for us to render to in an off-screen buffer
    if(s->t)
    {
        SDL_DestroyTexture(s->t);
        s->t = NULL;
    }
    DisplaySurface *surface = qemu_console_surface(s->con);
    pixman_format_code_t fmt = surface_format(surface);
    uint32_t sdl_fmt = 0;
    switch(fmt)
    {
        case PIXMAN_a8r8g8b8:
            sdl_fmt = SDL_PIXELFORMAT_ARGB8888;
            break;
        case PIXMAN_x8r8g8b8:
            sdl_fmt = SDL_PIXELFORMAT_XRGB8888;
            break;
        case PIXMAN_a8b8g8r8:
            sdl_fmt = SDL_PIXELFORMAT_ABGR8888;
            break;
        case PIXMAN_x8b8g8r8:
            sdl_fmt = SDL_PIXELFORMAT_XBGR8888;
            break;
        case PIXMAN_b8g8r8a8:
            sdl_fmt = SDL_PIXELFORMAT_BGRA8888;
            break;
        case PIXMAN_b8g8r8x8:
            sdl_fmt = SDL_PIXELFORMAT_BGRX8888;
            break;
        case PIXMAN_r8g8b8a8:
            sdl_fmt = SDL_PIXELFORMAT_RGBA8888;
            break;
        case PIXMAN_r8g8b8x8:
            sdl_fmt = SDL_PIXELFORMAT_RGBX8888;
            break;
        case PIXMAN_r8g8b8:
            sdl_fmt = SDL_PIXELFORMAT_RGB888;
            break;
        case PIXMAN_b8g8r8:
            sdl_fmt = SDL_PIXELFORMAT_BGR888;
            break;
        case PIXMAN_r5g6b5:
            sdl_fmt = SDL_PIXELFORMAT_RGB565;
            break;
        case PIXMAN_a1r5g5b5:
            sdl_fmt = SDL_PIXELFORMAT_ARGB1555;
            break;
        case PIXMAN_x1r5g5b5:
            sdl_fmt = SDL_PIXELFORMAT_XRGB1555;
            break;
    }
    
    s->t = SDL_CreateTexture(s->r, sdl_fmt, SDL_TEXTUREACCESS_TARGET, s->new_w, s->new_h);
    if(!s->t)
    {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    }
    else
    {
        SDL_SetRenderTarget(s->r, s->t);
    }
}

void ltdc_layer_resize_main_thread(void *opaque)
{
    struct Stm32MP2LTDCLayer *l = (struct Stm32MP2LTDCLayer *)opaque;

    uint32_t lw_bytes = (l->r.cfblr & 0xffffU) - 7;
    uint32_t sdl_pf = 0;
    uint32_t bpp = 0;

    switch(l->r.pfcr)
    {
        case 0:
            sdl_pf = SDL_PIXELFORMAT_ARGB8888;
            bpp = 4;
            break;
        case 1:
            sdl_pf = SDL_PIXELFORMAT_ABGR8888;
            bpp = 4;
            break;
        case 2:
            sdl_pf = SDL_PIXELFORMAT_RGBA8888;
            bpp = 4;
            break;
        case 3:
            sdl_pf = SDL_PIXELFORMAT_BGRA8888;
            bpp = 4;
            break;
        case 4:
            sdl_pf = SDL_PIXELFORMAT_RGB565;
            bpp = 2;
            break;
        case 5:
            sdl_pf = SDL_PIXELFORMAT_BGR565;
            bpp = 2;
            break;
        case 6:
            sdl_pf = SDL_PIXELFORMAT_RGB888;
            bpp = 3;
            break;
        case 7:
            // flexible format
            {
                uint64_t pf = (uint64_t)l->r.fpf0r | (((uint64_t)l->r.fpf1r) << 32);
                switch(pf)
                {
                    case 0x0006010000020000:
                        sdl_pf = SDL_PIXELFORMAT_RGB332;    // not really this, but renderer doesn't support palletes
                        bpp = 1;
                        break;

                    default:
                        fprintf(stderr, "LTDC: unknown pixel format: %lx\n", pf);
                        break;
                }
            }
    }

    if(bpp == 0)
        return;

    uint32_t lw = lw_bytes / bpp;
    uint32_t lh = l->r.cfblnr;

    l->t = SDL_CreateTexture(l->s->r, sdl_pf, SDL_TEXTUREACCESS_STREAMING, lw, lh);
    if(!l->t)
    {
        fprintf(stderr, "LTDC: SDL_CreateTexture failed: %s\n", SDL_GetError());
    }
}
