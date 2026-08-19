#include "gk_peripherals.h"

/* STM32MP2 RTC */
struct Stm32MP2RTCClass
{
    SysBusDeviceClass parent_class;
};

OBJECT_DECLARE_TYPE(Stm32MP2RTCState, Stm32MP2RTCClass,
                    STM32MP2_RTC)

static void stm32mp2_RTC_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32MP2RTCState *s = opaque;
    (void)s;

    fprintf(stderr, "RTC: write %x to %p\n", (unsigned)val64, (void *)addr);
}

static uint32_t to_bcd(uint32_t val)
{
    uint32_t ret = 0;
    for(unsigned i = 0; i < 8; i++)
    {
        uint32_t cv = val % 10;
        val /= 10;

        ret |= cv << (i * 4);
    }
    return ret;
}

static uint64_t stm32mp2_RTC_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32MP2RTCState *s = opaque;
    (void)s;
    
    switch(addr)
    {
        case 0:
            {
                time_t ct = time(NULL);
                struct tm *gtime = gmtime(&ct);
                return (to_bcd(gtime->tm_hour) << 16) |
                    (to_bcd(gtime->tm_min) << 8 ) |
                    (to_bcd(gtime->tm_sec) << 0);
            }

        case 4:
            {
                time_t ct = time(NULL);
                struct tm *gtime = gmtime(&ct);
                return ((to_bcd(gtime->tm_year) & 0xffU) << 16) |
                    (gtime->tm_wday << 13) |
                    (to_bcd(gtime->tm_mon) << 8) |
                    (to_bcd(gtime->tm_mday) << 0);
            }

        case 0xc:
            return 1U << 4;

        default:
            break;
    }

    fprintf(stderr, "RTC: read from %p unimplemented\n",
        (void *)addr);
    return 0;
}

static const MemoryRegionOps stm32mp2_RTC_ops = {
    .read = stm32mp2_RTC_read,
    .write = stm32mp2_RTC_write,
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

static void stm32mp2_RTC_init(Object *obj)
{
    Stm32MP2RTCState *s = STM32MP2_RTC(obj);

    memory_region_init_io(&s->mmio, obj, &stm32mp2_RTC_ops, s,
                          TYPE_STM32MP2_RTC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32mp2_RTC_class_init(ObjectClass *class,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    (void)dc;
}

static const TypeInfo stm32mp2_RTC_types[] = {
    {
        .name           = TYPE_STM32MP2_RTC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32MP2RTCState),
        .instance_init  = stm32mp2_RTC_init,
        .class_size     = sizeof(Stm32MP2RTCClass),
        .class_init     = stm32mp2_RTC_class_init,
    }
};

DEFINE_TYPES(stm32mp2_RTC_types)
