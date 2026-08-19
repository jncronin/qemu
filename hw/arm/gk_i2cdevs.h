#ifndef GK_I2CDEVS_H
#define GK_I2CDEVS_H

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

#define TYPE_I2C_INA236A "ina236a"
#define TYPE_I2C_MAX17048 "max17048"
#define TYPE_I2C_BQ25601 "bq25601"

struct i2c_device
{
    DeviceState parent_obj;

    int (*start)(struct i2c_device *);
    uint8_t (*read)(struct i2c_device *);
    int (*write)(struct i2c_device *, uint8_t);
    void (*stop)(struct i2c_device *);
};

struct ina236_state
{
    struct i2c_device base;
    int bytes_since_start;
    int reg_id;
};

struct MAX17048_state
{
    struct i2c_device base;
    int bytes_since_start;
    int reg_id;
};

struct BQ25601_state
{
    struct i2c_device base;
    int bytes_since_start;
    int reg_id;
};

#endif
