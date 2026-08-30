#include "gk_i2cdevs.h"
#include "ui/input.h"
#include "gk_peripherals.h"

// INA236
OBJECT_DECLARE_SIMPLE_TYPE(ina236_state, I2C_INA236A)

static int ina236_start(struct i2c_device *_d)
{
    struct ina236_state *d = (struct ina236_state *)_d;
    d->bytes_since_start = 0;
    return 0;
}

static void ina236_stop(struct i2c_device *)
{ }

static uint8_t ina236_read(struct i2c_device *_d)
{
    struct ina236_state *d = (struct ina236_state *)_d;
    uint8_t ret = 0;

    switch(d->reg_id)
    {
        case 0x3e*2:
            ret = 0x54;
            break;
        case 0x3e*2+1:
            ret = 0x49;
            break;
        case 0x3f*2:
            ret = 0xa0;
            break;
        case 0x3f*2+1:
            ret = 0x80;
            break;
        case 0x1*2:
            ret = 0x7;
            break;
        case 0x1*2+1:
            ret = 0xd0;
            break;
        case 0x2*2:
            ret = 0x9;
            break;
        case 0x2*2+1:
            ret = 0xc4;
            break;
    }

    //fprintf(stderr, "INA236: reg %x%s: %x\n", d->reg_id / 2, (d->reg_id & 0x1) ? "L" : "H", ret);
    d->reg_id++;

    return ret;
}

static int ina236_write(struct i2c_device *_d, uint8_t v)
{
    struct ina236_state *d = (struct ina236_state *)_d;

    if(d->bytes_since_start == 0)
    {
        d->reg_id = v * 2;
    }
    d->bytes_since_start++;

    return 0;
}

static void ina236_init(Object *obj)
{
    ina236_state *s = I2C_INA236A(obj);
    s->base.start = ina236_start;
    s->base.stop = ina236_stop;
    s->base.read = ina236_read;
    s->base.write = ina236_write;
}

static const TypeInfo ina236_types[] = {
    {
        .name           = TYPE_I2C_INA236A,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(ina236_state),
        .instance_init  = ina236_init,
    }
};

DEFINE_TYPES(ina236_types)

// MAX17048
OBJECT_DECLARE_SIMPLE_TYPE(MAX17048_state, I2C_MAX17048)

static int MAX17048_start(struct i2c_device *_d)
{
    struct MAX17048_state *d = (struct MAX17048_state *)_d;
    d->bytes_since_start = 0;
    return 0;
}

static void MAX17048_stop(struct i2c_device *)
{ }

static uint8_t MAX17048_read(struct i2c_device *_d)
{
    struct MAX17048_state *d = (struct MAX17048_state *)_d;
    uint8_t ret = 0;

    switch(d->reg_id)
    {
        case 0x02:
            ret = 0xc8;
            break;
        case 0x02+1:
            ret = 0;
            break;
        case 0x04:
            ret = 0x64;
            break;
        case 0x04+1:
            ret = 0;
            break;
        case 0x16:
            ret = 0;
            break;
        case 0x16+1:
            ret = 0;
            break;
    }

    //fprintf(stderr, "MAX17048: reg %x%s: %x\n", d->reg_id / 2, (d->reg_id & 0x1) ? "L" : "H", ret);
    d->reg_id++;

    return ret;
}

static int MAX17048_write(struct i2c_device *_d, uint8_t v)
{
    struct MAX17048_state *d = (struct MAX17048_state *)_d;

    if(d->bytes_since_start == 0)
    {
        d->reg_id = v;
    }
    d->bytes_since_start++;

    return 0;
}

static void MAX17048_init(Object *obj)
{
    MAX17048_state *s = I2C_MAX17048(obj);
    s->base.start = MAX17048_start;
    s->base.stop = MAX17048_stop;
    s->base.read = MAX17048_read;
    s->base.write = MAX17048_write;
}

static const TypeInfo MAX17048_types[] = {
    {
        .name           = TYPE_I2C_MAX17048,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(MAX17048_state),
        .instance_init  = MAX17048_init,
    }
};

DEFINE_TYPES(MAX17048_types)

// BQ25601
OBJECT_DECLARE_SIMPLE_TYPE(BQ25601_state, I2C_BQ25601)

static int BQ25601_start(struct i2c_device *_d)
{
    struct BQ25601_state *d = (struct BQ25601_state *)_d;
    d->bytes_since_start = 0;
    return 0;
}

static void BQ25601_stop(struct i2c_device *)
{ }

static uint8_t BQ25601_read(struct i2c_device *_d)
{
    struct BQ25601_state *d = (struct BQ25601_state *)_d;
    uint8_t ret = 0;

    switch(d->reg_id)
    {
        case 0x08:
            ret = 0x60;
            break;
        case 0x09:
            ret = 0;
            break;
        case 0x0a:
            ret = 0x80;
            break;
        case 0x0b:
            ret = 0;
            break;
    }

    //fprintf(stderr, "BQ25601: reg %x%s: %x\n", d->reg_id / 2, (d->reg_id & 0x1) ? "L" : "H", ret);
    d->reg_id++;

    return ret;
}

static int BQ25601_write(struct i2c_device *_d, uint8_t v)
{
    struct BQ25601_state *d = (struct BQ25601_state *)_d;

    if(d->bytes_since_start == 0)
    {
        d->reg_id = v;
    }
    d->bytes_since_start++;

    return 0;
}

static void BQ25601_init(Object *obj)
{
    BQ25601_state *s = I2C_BQ25601(obj);
    s->base.start = BQ25601_start;
    s->base.stop = BQ25601_stop;
    s->base.read = BQ25601_read;
    s->base.write = BQ25601_write;
}

static const TypeInfo BQ25601_types[] = {
    {
        .name           = TYPE_I2C_BQ25601,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(BQ25601_state),
        .instance_init  = BQ25601_init,
    }
};

DEFINE_TYPES(BQ25601_types)

// PCA6416
OBJECT_DECLARE_SIMPLE_TYPE(PCA6416_state, I2C_PCA6416)

static int PCA6416_start(struct i2c_device *_d)
{
    struct PCA6416_state *d = I2C_PCA6416(_d);
    d->bytes_since_start = 0;
    return 0;
}

static void PCA6416_stop(struct i2c_device *)
{ }

static uint8_t PCA6416_read(struct i2c_device *_d)
{
    struct PCA6416_state *d = I2C_PCA6416(_d);
    uint8_t ret = 0;

    switch(d->reg_id)
    {
        case 0:
            if(d->idevice)
            {
                if(d->idevice->btn_states & (1U << 4))
                    ret |= 1U << 0;
                if(d->idevice->btn_states & (1U << 5))
                    ret |= 1U << 1;
                if(d->idevice->btn_states & (1U << 6))
                    ret |= 1U << 2;
                if(d->idevice->btn_states & (1U << 7))
                    ret |= 1U << 3;
                if(d->idevice->btn_states & (1U << 2))
                    ret |= 1U << 4;
                if(d->idevice->btn_states & (1U << 3))
                    ret |= 1U << 5;
                if(d->idevice->btn_states & (1U << 0))
                    ret |= 1U << 6;
                if(d->idevice->btn_states & (1U << 1))
                    ret |= 1U << 7;
            }
            else
            {
                ret = 0xff;
            }
            break;
        case 1:
            if(d->idevice)
            {
                if(d->idevice->btn_states & (1U << 31))
                    ret |= 1U << 0;
                if(d->idevice->btn_states & (1U << 30))
                    ret |= 1U << 1;
                if(d->idevice->btn_states & (1U << 28))
                    ret |= 1U << 2;
                if(d->idevice->btn_states & (1U << 29))
                    ret |= 1U << 3;
                if(d->idevice->btn_states & (1U << 16))
                    ret |= 1U << 4;
                if(d->idevice->btn_states & (1U << 23))
                    ret |= 1U << 5;
                if(d->idevice->btn_states & (1U << 10))
                    ret |= 1U << 6;
            }
            else
            {
                ret = 0xff;
            }
            break;
    }

    //fprintf(stderr, "PCA6416: read reg %u: %x\n", d->reg_id, ret);
    d->reg_id++;

    return ret;
}

static int PCA6416_write(struct i2c_device *_d, uint8_t v)
{
    struct PCA6416_state *d = I2C_PCA6416(_d);

    if(d->bytes_since_start == 0)
    {
        d->reg_id = v;
    }
    d->bytes_since_start++;

    return 0;
}

static void PCA6416_init(Object *obj)
{
    PCA6416_state *s = I2C_PCA6416(obj);
    s->base.start = PCA6416_start;
    s->base.stop = PCA6416_stop;
    s->base.read = PCA6416_read;
    s->base.write = PCA6416_write;

    object_property_add_link(obj, "idevice", TYPE_GK_INPUT_DEVICE, (Object **)&s->idevice,
        object_property_allow_set_link, 0);
}

static const TypeInfo PCA6416_types[] = {
    {
        .name           = TYPE_I2C_PCA6416,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(PCA6416_state),
        .instance_init  = PCA6416_init,
    }
};

DEFINE_TYPES(PCA6416_types)

// GSLX680
OBJECT_DECLARE_SIMPLE_TYPE(GSLX680_state, I2C_GSLX680)

static int GSLX680_start(struct i2c_device *_d)
{
    struct GSLX680_state *d = (struct GSLX680_state *)_d;
    d->bytes_since_start = 0;
    return 0;
}

static void GSLX680_stop(struct i2c_device *)
{ }

static uint8_t GSLX680_read(struct i2c_device *_d)
{
    struct GSLX680_state *d = (struct GSLX680_state *)_d;
    uint8_t ret = 0;

    switch(d->reg_id)
    {
        case 0xb0:
            ret = 0x5a;
            break;
        case 0xb1:
            ret = 0x5a;
            break;
        case 0xb2:
            ret = 0x5a;
            break;
        case 0xb3:
            ret = 0x5a;
            break;
    }

    //fprintf(stderr, "GSLX680: read reg %u: %x\n", d->reg_id, ret);
    d->reg_id++;

    return ret;
}

static int GSLX680_write(struct i2c_device *_d, uint8_t v)
{
    struct GSLX680_state *d = (struct GSLX680_state *)_d;

    if(d->bytes_since_start == 0)
    {
        d->reg_id = v;
    }
    d->bytes_since_start++;

    return 0;
}

static void GSLX680_init(Object *obj)
{
    GSLX680_state *s = I2C_GSLX680(obj);
    s->base.start = GSLX680_start;
    s->base.stop = GSLX680_stop;
    s->base.read = GSLX680_read;
    s->base.write = GSLX680_write;
}

static const TypeInfo GSLX680_types[] = {
    {
        .name           = TYPE_I2C_GSLX680,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(GSLX680_state),
        .instance_init  = GSLX680_init,
    }
};

DEFINE_TYPES(GSLX680_types)
