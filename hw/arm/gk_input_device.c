/* This represents all the buttons and analog axes on the gk */
#include "gk_peripherals.h"

#include "standard-headers/linux/input-event-codes.h"

OBJECT_DECLARE_SIMPLE_TYPE(GK_Input_Device_State, GK_INPUT_DEVICE);

static void GK_INPUT_DEVICE_init(Object *obj)
{
    GK_Input_Device_State *s = GK_INPUT_DEVICE(obj);
    qdev_init_gpio_out(DEVICE(obj), &s->btns[0], sizeof(s->btns) / sizeof(s->btns[0]));
    s->btn_states = ~0;
    
    for(unsigned int i = 0u; i < sizeof(s->achans) / sizeof(s->achans[0]); i++)
    {
        g_autofree char *str_achan = g_strdup_printf("achan%u", i);

        object_initialize_child(obj, str_achan, &s->achans[i], TYPE_STM32MP2_ADC_INPUT_CHANNEL);
        s->achans[i].chan.minval = 0;
        s->achans[i].chan.maxval = 4096;
        s->achans[i].chan.val = 2048;
        qdev_realize(DEVICE(&s->achans[i]), NULL, &error_fatal);
    }
}

#define GK_IDEVICE_AXIS_DIR_P   2
#define GK_IDEVICE_AXIS_DIR_N   1

static int key_to_axis_id(unsigned int key, int *ax_id,
    int *ax_dir)
{
    switch(key)
    {
        case KEY_A:
            *ax_id = GK_IDEVICE_AXIS_LX;
            *ax_dir = GK_IDEVICE_AXIS_DIR_N;
            return 0;
        case KEY_D:
            *ax_id = GK_IDEVICE_AXIS_LX;
            *ax_dir = GK_IDEVICE_AXIS_DIR_P;
            return 0;
        case KEY_W:
            *ax_id = GK_IDEVICE_AXIS_LY;
            *ax_dir = GK_IDEVICE_AXIS_DIR_P;
            return 0;
        case KEY_S:
            *ax_id = GK_IDEVICE_AXIS_LY;
            *ax_dir = GK_IDEVICE_AXIS_DIR_N;
            return 0;
        case KEY_KP4:
            *ax_id = GK_IDEVICE_AXIS_RY;
            *ax_dir = GK_IDEVICE_AXIS_DIR_N;
            return 0;
        case KEY_KP6:
            *ax_id = GK_IDEVICE_AXIS_RY;
            *ax_dir = GK_IDEVICE_AXIS_DIR_P;
            return 0;
        case KEY_KP2:
            *ax_id = GK_IDEVICE_AXIS_RX;
            *ax_dir = GK_IDEVICE_AXIS_DIR_P;
            return 0;
        case KEY_KP8:
            *ax_id = GK_IDEVICE_AXIS_RX;
            *ax_dir = GK_IDEVICE_AXIS_DIR_N;
            return 0;
        case KEY_KP3:
            *ax_id = GK_IDEVICE_AXIS_THROTTLE;
            *ax_dir = GK_IDEVICE_AXIS_DIR_N;
            return 0;
        case KEY_KP9:
            *ax_id = GK_IDEVICE_AXIS_THROTTLE;
            *ax_dir = GK_IDEVICE_AXIS_DIR_P;
            return 0;
    }
    return -1;
}

static int key_to_btn_id(unsigned int key)
{
    switch(key)
    {
        case KEY_LEFT:
            return 0;
        case KEY_RIGHT:
            return 1;
        case KEY_UP:
            return 2;
        case KEY_DOWN:
            return 3;
        case KEY_SPACE:
            return 4;           // A
        case KEY_LEFTCTRL:
            return 5;           // B
        case KEY_LEFTSHIFT:
            return 6;           // X
        case KEY_LEFTALT:
            return 7;           // Y
        case KEY_MINUS:
            return 8;           // voldown
        case KEY_EQUAL:
            return 9;           // volup
        case KEY_F1:
            return 10;          // menu
        case KEY_RIGHTCTRL:
            return 16;          // joy
        case KEY_1:
            return 21;          // LB
        case KEY_4:
            return 22;          // RB
        case KEY_RIGHTSHIFT:
            return 23;          // joyb
        case KEY_2:
            return 28;          // LT
        case KEY_3:
            return 29;          // RT
        case KEY_ESC:
            return 30;          // select
        case KEY_ENTER:
            return 31;          // start
    }

    return -1;
}

static void GK_INPUT_DEVICE_ihandler(DeviceState *dev, QemuConsole *src, QemuInputEvent *evt)
{
    if(!evt || !evt->type == INPUT_EVENT_KIND_KEY)
        return;

    GK_Input_Device_State *s = GK_INPUT_DEVICE(dev);

    int btn_id = key_to_btn_id(evt->key.key);
    if(btn_id >= 0)
    {
        if(evt->key.down)
        {
            s->btn_states &= ~(1U << btn_id);
            qemu_set_irq(s->btns[btn_id], 0);
        }
        else
        {
            s->btn_states |= 1U << btn_id;
            qemu_set_irq(s->btns[btn_id], 1);
        }
    }

    /* First update axis state.  This is because user can press positive axis button,
        negative axis button, both or neither, and the output adc value depends on
        the combination of both the positive and negative buttons */
    int ax_id, ax_dir;
    if(key_to_axis_id(evt->key.key, &ax_id, &ax_dir) == 0)
    {
        if(evt->key.down)
        {
            s->achans[ax_id].pn_state |= ax_dir;
        }
        else
        {
            s->achans[ax_id].pn_state &= ~ax_dir;
        }

        switch(s->achans[ax_id].pn_state)
        {
            case 0:
            case GK_IDEVICE_AXIS_DIR_N | GK_IDEVICE_AXIS_DIR_P:
                s->achans[ax_id].chan.val = (s->achans[ax_id].chan.maxval +
                    s->achans[ax_id].chan.minval) / 2;
                break;

            case GK_IDEVICE_AXIS_DIR_N:
                s->achans[ax_id].chan.val = s->achans[ax_id].chan.minval;
                break;

            case GK_IDEVICE_AXIS_DIR_P:
                s->achans[ax_id].chan.val = s->achans[ax_id].chan.maxval;
                break;
        }
    }
}

static const QemuInputHandler GK_INPUT_DEVICE_ihandler_info =
{
    .name = "GK_INPUT_DEVICE_input",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = GK_INPUT_DEVICE_ihandler
};


static void GK_INPUT_DEVICE_realize(DeviceState *dev, Error **errp)
{
    GK_Input_Device_State *s = GK_INPUT_DEVICE(dev);
    s->ihandler = qemu_input_handler_register(dev, &GK_INPUT_DEVICE_ihandler_info);
    qemu_input_handler_activate(s->ihandler);

    // update output irq levels
    for(unsigned int i = 0; i < 32u; i++)
    {
        if(s->btn_states & (1u << i))
        {
            qemu_set_irq(s->btns[i], 1);
        }
        else
        {
            qemu_set_irq(s->btns[i], 0);
        }
    }
}

static void GK_INPUT_DEVICE_unrealize(DeviceState *dev)
{
    GK_Input_Device_State *s = GK_INPUT_DEVICE(dev);
    if(s->ihandler)
    {
        qemu_input_handler_unregister(s->ihandler);
        s->ihandler = NULL;
    }
}

static void GK_INPUT_DEVICE_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = GK_INPUT_DEVICE_realize;
    dc->unrealize = GK_INPUT_DEVICE_unrealize;
}

static const TypeInfo GK_INPUT_DEVICE_types[] = {
    {
        .name           = TYPE_GK_INPUT_DEVICE,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(GK_Input_Device_State),
        .instance_init  = GK_INPUT_DEVICE_init,
        .class_init     = GK_INPUT_DEVICE_class_init,
    }
};

DEFINE_TYPES(GK_INPUT_DEVICE_types)


