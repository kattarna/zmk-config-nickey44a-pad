#define DT_DRV_COMPAT zmk_behavior_f13_hold

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <stdlib.h>
#include <drivers/behavior.h>
#include <drivers/input_processor.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/keys.h>

static bool f13_is_pressed = false;

static void trigger_layer_up(void) {
    uint8_t current = zmk_keymap_highest_layer_active();
    uint8_t next = (current >= 9) ? 0 : (current + 1);
    zmk_keymap_layer_to(next);
}

/* =========================================================
 * 1. &f13_hold : F13キー押下状態の管理とF13送信
 * ========================================================= */
static int f13_hold_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    f13_is_pressed = true;
    zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_F13);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int f13_hold_released(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    f13_is_pressed = false;
    zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_F13);
    zmk_endpoints_send_report(HID_USAGE_PAGE_KEYBOARD);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api f13_hold_driver_api = {
    .binding_pressed = f13_hold_pressed,
    .binding_released = f13_hold_released,
};

#define F13_HOLD_INST(n) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &f13_hold_driver_api);

DT_INST_FOREACH_STATUS_OKAY(F13_HOLD_INST)

/* =========================================================
 * 2. &f13_layer_up : 現在のレイヤーから1つ上へ移動（0〜9循環）
 * ========================================================= */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_behavior_layer_up

static int layer_up_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    trigger_layer_up();
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api layer_up_driver_api = {
    .binding_pressed = layer_up_pressed,
};

#define LAYER_UP_INST(n) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &layer_up_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LAYER_UP_INST)

/* =========================================================
 * 3. &f13_gate_normal / &f13_gate_active : トラックパッド分岐ゲート
 * ========================================================= */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_input_processor_f13_gate

struct f13_gate_config {
    uint8_t mode; /* 0: F13非押下時のみ通過, 1: F13押下時のみ通過 */
};

static int f13_gate_handle_event(const struct device *dev, struct input_event *event,
                                 uint32_t param1, uint32_t param2,
                                 struct zmk_input_processor_state *state) {
    const struct f13_gate_config *cfg = dev->config;
    if (cfg->mode == 0) {
        return f13_is_pressed ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
    } else {
        return f13_is_pressed ? ZMK_INPUT_PROC_CONTINUE : ZMK_INPUT_PROC_STOP;
    }
}

static const struct zmk_input_processor_driver_api f13_gate_driver_api = {
    .handle_event = f13_gate_handle_event,
};

#define F13_GATE_INST(n) \
    static const struct f13_gate_config f13_gate_config_##n = { \
        .mode = DT_INST_PROP(n, mode), \
    }; \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &f13_gate_config_##n, POST_KERNEL, \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &f13_gate_driver_api);

DT_INST_FOREACH_STATUS_OKAY(F13_GATE_INST)

/* =========================================================
 * 4. &f13_layer_gesture : F13押下中の4方向スワイプ＆タップ判定
 * ========================================================= */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_input_processor_f13_layer_gesture

struct f13_gesture_config {
    uint16_t hwheel_code;
    uint16_t wheel_code;
    uint16_t touch_code;
    int32_t threshold;
    int32_t axis_ratio;
};

struct f13_gesture_data {
    int32_t acc_x;
    int32_t acc_y;
    bool touching;
    bool triggered;
};

static int f13_gesture_handle_event(const struct device *dev, struct input_event *event,
                                    uint32_t param1, uint32_t param2,
                                    struct zmk_input_processor_state *state) {
    const struct f13_gesture_config *cfg = dev->config;
    struct f13_gesture_data *data = dev->data;

    /* タップ判定（押下時に1回だけ実行） */
    if (event->type == INPUT_EV_KEY) {
        if (event->code == cfg->touch_code) {
            if (event->value) {
                data->touching = true;
                data->triggered = false;
                data->acc_x = 0;
                data->acc_y = 0;
            } else {
                data->touching = false;
                data->acc_x = 0;
                data->acc_y = 0;
            }
            return ZMK_INPUT_PROC_STOP;
        }
        if (event->code == INPUT_BTN_0 && event->value == 1) {
            trigger_layer_up();
            return ZMK_INPUT_PROC_STOP;
        }
        if (event->code == INPUT_BTN_2 && event->value == 1) {
            zmk_keymap_layer_to(0);
            return ZMK_INPUT_PROC_STOP;
        }
        return ZMK_INPUT_PROC_STOP;
    }

    /* 4方向スワイプ判定（1回のタッチにつき1回だけ判定） */
    if (event->type == INPUT_EV_REL && !data->triggered) {
        if (event->code == cfg->hwheel_code) {
            data->acc_x += event->value;
        } else if (event->code == cfg->wheel_code) {
            data->acc_y += event->value;
        } else {
            return ZMK_INPUT_PROC_STOP;
        }

        int32_t abs_x = abs(data->acc_x);
        int32_t abs_y = abs(data->acc_y);

        if (abs_x >= cfg->threshold && abs_x >= abs_y * cfg->axis_ratio) {
            data->triggered = true;
            if (data->acc_x < 0) {
                /* 物理右スワイプ（負のDIAL） -> レイヤー4 */
                zmk_keymap_layer_to(4);
            } else {
                /* 物理左スワイプ（正のDIAL） -> レイヤー6 */
                zmk_keymap_layer_to(6);
            }
        } else if (abs_y >= cfg->threshold && abs_y >= abs_x * cfg->axis_ratio) {
            data->triggered = true;
            if (data->acc_y > 0) {
                /* 上スワイプ -> レイヤー3 */
                zmk_keymap_layer_to(3);
            } else {
                /* 下スワイプ -> レイヤー5 */
                zmk_keymap_layer_to(5);
            }
        }
    }

    /* F13押下中はホストへ余計なスクロール・ポインタ移動を送らない */
    return ZMK_INPUT_PROC_STOP;
}

static const struct zmk_input_processor_driver_api f13_gesture_driver_api = {
    .handle_event = f13_gesture_handle_event,
};

#define F13_GESTURE_INST(n) \
    static const struct f13_gesture_config f13_gesture_config_##n = { \
        .hwheel_code = DT_INST_PROP(n, hwheel_code), \
        .wheel_code = DT_INST_PROP(n, wheel_code), \
        .touch_code = DT_INST_PROP(n, touch_code), \
        .threshold = DT_INST_PROP(n, threshold), \
        .axis_ratio = DT_INST_PROP(n, axis_ratio), \
    }; \
    static struct f13_gesture_data f13_gesture_data_##n; \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &f13_gesture_data_##n, &f13_gesture_config_##n, POST_KERNEL, \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &f13_gesture_driver_api);

DT_INST_FOREACH_STATUS_OKAY(F13_GESTURE_INST)
