#include "gk_i2cdevs.h"

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

