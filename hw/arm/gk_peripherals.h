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
#include "hw/arm/armv7m.h"
#include "ui/input.h"
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
#define TYPE_STM32MP2_CA35_SYSCFG "stm32mp2-ca35-syscfg"
#define TYPE_STM32MP2_ADC "stm32mp2-adc"
#define TYPE_STM32MP2_EXTI "stm32mp2-exti"
#define TYPE_STM32MP2_GPIO "stm32mp2-gpio"

#define TYPE_GK_INPUT_DEVICE "gk-input-device"

struct Stm32MP2CA35_SYSCFGState;
struct GK_Input_Device_State;

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

    qemu_irq sdmmc1_rst;
    qemu_irq adc_rst[2];
    qemu_irq ltdc_rst;

    Clock *ck_icn_hs_mcu;
    Clock *ck_cm33_systick;

    struct ARMv7MState *cm33;
    struct Stm32MP2CA35_SYSCFGState *ca35_syscfg;
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

    uint32_t sdl_input_pf;
    uint32_t bpp, lw, lh;
    int needs_software_conv;
    SDL_PixelFormat *sdl_input_pf_struct;
};

struct Stm32MP2LTDCState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    QemuConsole *con;
    //Clock *clk_out;
    ptimer_state *clk_out;

    struct Stm32MP2LTDCLayer layers[3];

    uint32_t sscr, bpcr, awcr, twcr, gcr, srcr, gccr, bccr, ier, isr, lipcr;
    qemu_irq irq;

    QEMUBH *resize_bh;
    uint32_t new_w, new_h;

    SDL_Window *w;
    SDL_Renderer *r;
    SDL_Texture *t;


    int irq_set;
};

struct Stm32MP2SDMMCState
{
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    QemuMutex m;

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

    int irq_set;
};

struct Stm32MP2CA35_SYSCFGState
{
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    uint32_t m33_access_cr;
    uint32_t m33_tzen_cr;
    uint32_t m33_initsvtor_cr;
    uint32_t m33_initnsvtor_cr;

    struct ARMv7MState *cm33;
};

struct adc_inst
{
    uint32_t isr, ier, cr, cfgr1, cfgr2, smpr1, smpr2, pcsel, sqr[4], dr, jsqr,
        ofcfgr[4], ofr[4], gcomp, jdr[4], difsel, calfact;
};

struct adcc
{
    uint32_t csr, ccr;
};

struct Stm32MP2ADCState
{
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;
    qemu_irq irq;

    struct adc_inst inst[2];
    struct adcc com;

    int irq_set;
};

#define STM32MP2_EXTI_NIRQ  96

struct Stm32MP2EXTIState
{
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    qemu_irq irqs[2][STM32MP2_EXTI_NIRQ];

    int irq_set[2][STM32MP2_EXTI_NIRQ];

    uint32_t regs[0x400/4];
};

struct GK_Input_Device_State
{
    DeviceState parent_obj;
    QemuInputHandlerState *ihandler;

    uint32_t btn_states;
    int16_t axes[8];

    qemu_irq btns[32];
};

#define GK_GPIOA    0
#define GK_GPIOB    1
#define GK_GPIOC    2
#define GK_GPIOD    3
#define GK_GPIOE    4
#define GK_GPIOF    5
#define GK_GPIOG    6
#define GK_GPIOH    7
#define GK_GPIOI    8
#define GK_GPIOJ    9
#define GK_GPIOK    10
#define GK_GPIOZ    25

struct Stm32MP2GPIOState
{
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    int32_t id;

    qemu_irq outputs[16];

    uint32_t moder, otyper, ospeedr, pupdr, idr, odr, afrl, afrh;
    uint32_t irq_set;
};

#endif
