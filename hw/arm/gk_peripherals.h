#ifndef GK_PERIPHERALS_H
#define GK_PERIPHERALS_H

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/raspi_platform.h"
#include "hw/core/registerfields.h"
#include "qemu/error-report.h"
#include "system/device_tree.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/arm/boot.h"
#include "qom/object.h"
#include "target/arm/cpu.h"
#include "hw/misc/unimp.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/block/flash.h"
#include "system/block-backend.h"
#include "hw/core/ptimer.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/intc/arm_gic.h"
#include "hw/arm/bsa.h"
#include "ui/console.h"
#include "ui/sdl2.h"
#include "hw/sd/sd.h"
#include "gk_i2cdevs.h"

#define TYPE_STM32MP2_USART "stm32mp2-usart"
#define TYPE_STM32MP2_RCC "stm32mp2-rcc"
#define TYPE_STM32MP2_TIM "stm32mp2-tim"
#define TYPE_STM32MP2_RTC "stm32mp2-rtc"
#define TYPE_STM32MP2_I2C "stm32mp2-i2c"
#define TYPE_STM32MP2_PWR "stm32mp2-pwr"
#define TYPE_STM32MP2_PLL "stm32mp2-pll"
#define TYPE_STM32MP2_LTDC "stm32mp2-ltdc"
#define TYPE_STM32MP2_SDMMC "stm32mp2-sdmmc"

struct Stm32MP2UsartState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    CharFrontend chr;
};

struct Stm32MP2PLLState {
    DeviceState parent_obj;

    uint64_t input_freq;
    uint64_t output_freq;
    Clock *clk_out;
};

struct Stm32MP2RCCState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    uint32_t regs[65336/4];

    struct Stm32MP2PLLState pll48[5];
};

struct Stm32MP2TIMState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t is_lp;
    int32_t id;
    uint64_t input_freq;

    ptimer_state *pt;

    uint32_t cr1, cr2, dier, sr, psc, arr;

    qemu_irq irq;
};

struct Stm32MP2RTCState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
};

enum i2cstate
{
    i2c_Reset, i2c_M_Addressed, i2c_M_Data
};

struct Stm32MP2I2CState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    qemu_irq irq;

    uint32_t cr1, cr2, isr, timingr, rxdr;

    struct i2c_device *devs[256];

    enum i2cstate state;
};

struct Stm32MP2PWRState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    uint32_t regs[0x400 / 4];
};

struct Stm32MP2LTDCLayerRegs
{
    uint32_t rcr, cr, whpcr, wvpcr, ckcr, pfcr, cacr, dccr, bfcr, blcr, pcr,
        cfbar, cfblr, cfblnr, afba0r, afba1r, afblr, afblnr, sisr, sosr, svsfr, svspr,
        shsfr, shspr, cyr0r, cyr1r, fpf0r, fpf1r;
};

struct Stm32MP2LTDCState;

struct Stm32MP2LTDCLayer
{
    // These, with the exception of clutwr, are shadowed
    struct Stm32MP2LTDCLayerRegs r, sr;
    uint32_t clut[256];

    SDL_Texture *t;
    QEMUBH *resize_bh;
    struct Stm32MP2LTDCState *s;
};

struct Stm32MP2LTDCState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    QemuConsole *con;
    Clock *clk_out;

    struct Stm32MP2LTDCLayer layers[3];

    uint32_t sscr, bpcr, awcr, twcr, gcr, srcr, gccr, bccr, ier, isr, lipcr;
    qemu_irq irq;

    QEMUBH *resize_bh;
    uint32_t new_w, new_h;

    SDL_Window *w;
    SDL_Renderer *r;
    SDL_Texture *t;
};

struct Stm32MP2SDMMCState
{
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    qemu_irq irq;

    uint32_t power, clkcr, argr, cmdr, respcmdr, resp[4], dtimer, dlenr, dctrl,
        dcntr, star, icr, maskr, acktimer, fifothrr, idmactrlr, idmabsizer,
        idmabaser, idmalar, idmabar;
    SDBus sdbus;

    uint32_t rx_fifo_buf[512/4];
    unsigned int rx_fifo_user_ptr;
    unsigned int rx_fifo_data_size;

    uint32_t tx_fifo_buf[512/4];
    unsigned int tx_fifo_user_ptr;
    unsigned int tx_fifo_data_size;
};

#endif
