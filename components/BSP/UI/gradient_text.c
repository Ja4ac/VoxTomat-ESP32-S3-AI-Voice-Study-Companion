#include "gradient_text.h"

#include <stdbool.h>

/*
 * 组件的内部状态。
 *
 * 设计思路：
 * - 对外仍然暴露普通 lv_obj_t *，便于参与现有 LVGL 布局体系。
 * - 对内把运行时需要维护的数据全部收拢到这个状态结构里，
 *   再挂到 obj->user_data 上，避免业务文件里散落一堆静态变量。
 */
typedef struct {
    lv_draw_buf_t * mask;        /* 文字遮罩缓冲区，负责把渐变裁成“文字形状” */
    lv_color_t * palette;        /* 当前使用的循环调色板，组件内部持有一份拷贝 */
    uint8_t color_count;         /* 调色板颜色数 */
    uint16_t text_pad;           /* 文字外围留白 */
    uint16_t phase_max;          /* 动画总相位数 */
    uint16_t phase_offset;       /* 渐变首尾颜色相位差 */
    uint32_t anim_duration_ms;   /* 一轮动画时长 */
    const lv_font_t * font;      /* 当前字体 */
    lv_grad_dir_t grad_dir;      /* 渐变方向 */
} ui_gradient_text_state_t;

/* 读取对象内部状态。所有对外接口都会先通过它拿到上下文。 */
static ui_gradient_text_state_t * ui_gradient_text_get_state(lv_obj_t * obj);

/* 写入默认配置，保证“零配置也能工作”。 */
static void ui_gradient_text_apply_defaults(ui_gradient_text_state_t * state);

/* 拷贝调色板到组件内部，避免外部数组生命周期影响组件。 */
static bool ui_gradient_text_copy_palette(ui_gradient_text_state_t * state, const lv_color_t * colors, uint8_t color_count);

/* 根据新文字重新计算尺寸并刷新遮罩。 */
static void ui_gradient_text_refresh_mask(lv_obj_t * obj, const char * text);

/* 真正把文字绘制到 L8 缓冲区里，结果会被当作 bitmap mask 使用。 */
static void ui_gradient_text_generate_mask(lv_obj_t * parent, lv_draw_buf_t * mask, const char * text,
                                           const lv_font_t * font);

/* 配置变化后重新启动动画，避免旧动画参数残留。 */
static void ui_gradient_text_restart_anim(lv_obj_t * obj);

/* 动画每一帧的执行函数：根据相位计算首尾颜色并更新对象样式。 */
static void ui_gradient_text_anim_cb(void * var, int32_t value);

/* 按相位从循环调色板中取样当前颜色。 */
static lv_color_t ui_gradient_text_color_at(const ui_gradient_text_state_t * state, uint16_t phase);

/* 在线性插值两个颜色，得到平滑过渡色。 */
static lv_color_t ui_gradient_text_color_lerp(lv_color_t from, lv_color_t to, uint8_t mix);

/* 对象销毁时释放动画、遮罩和调色板，避免内存泄漏。 */
static void ui_gradient_text_delete_cb(lv_event_t * e);

lv_obj_t * ui_gradient_text_create(lv_obj_t * parent, const char * text, const ui_gradient_text_config_t * config)
{
    lv_obj_t * obj = lv_obj_create(parent);
    ui_gradient_text_state_t * state = lv_malloc_zeroed(sizeof(ui_gradient_text_state_t));

    if(state == NULL) {
        lv_obj_delete(obj);
        return NULL;
    }

    ui_gradient_text_apply_defaults(state);

    /*
     * 这个对象本身只是“渐变承载层”：
     * - 不需要默认边框、圆角、内边距
     * - 不参与滚动
     * - 只需要一个可见的背景渐变
     */
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);

    lv_obj_set_user_data(obj, state);
    lv_obj_add_event_cb(obj, ui_gradient_text_delete_cb, LV_EVENT_DELETE, NULL);

    ui_gradient_text_apply_config(obj, config);
    ui_gradient_text_set_text(obj, text != NULL ? text : "");

    return obj;
}

void ui_gradient_text_set_text(lv_obj_t * obj, const char * text)
{
    ui_gradient_text_state_t * state = ui_gradient_text_get_state(obj);
    if(state == NULL) {
        return;
    }

    ui_gradient_text_refresh_mask(obj, text != NULL ? text : "");
}

void ui_gradient_text_apply_config(lv_obj_t * obj, const ui_gradient_text_config_t * config)
{
    ui_gradient_text_state_t * state = ui_gradient_text_get_state(obj);
    if(state == NULL) {
        return;
    }

    if(config != NULL) {
        state->font = config->font != NULL ? config->font : state->font;
        state->text_pad = config->text_pad > 0 ? config->text_pad : state->text_pad;
        state->phase_max = config->phase_max > 1 ? config->phase_max : state->phase_max;
        state->phase_offset = config->phase_offset % state->phase_max;
        state->anim_duration_ms = config->anim_duration_ms > 0 ? config->anim_duration_ms : state->anim_duration_ms;
        state->grad_dir = config->grad_dir != LV_GRAD_DIR_NONE ? config->grad_dir : LV_GRAD_DIR_HOR;

        if(config->colors != NULL && config->color_count >= 2) {
            if(!ui_gradient_text_copy_palette(state, config->colors, config->color_count)) {
                return;
            }
        }
    }

    /*
     * 颜色动画本质上还是通过 obj 的普通背景渐变来驱动，
     * 所以这里只需要设置方向，然后立即刷新一帧并重启动画。
     */
    lv_obj_set_style_bg_grad_dir(obj, state->grad_dir, LV_PART_MAIN);
    ui_gradient_text_anim_cb(obj, 0);
    ui_gradient_text_restart_anim(obj);
}

void ui_gradient_text_set_palette(lv_obj_t * obj, const lv_color_t * colors, uint8_t color_count)
{
    ui_gradient_text_state_t * state = ui_gradient_text_get_state(obj);
    if(state == NULL || colors == NULL || color_count < 2) {
        return;
    }

    if(!ui_gradient_text_copy_palette(state, colors, color_count)) {
        return;
    }

    ui_gradient_text_anim_cb(obj, 0);
    ui_gradient_text_restart_anim(obj);
}

static ui_gradient_text_state_t * ui_gradient_text_get_state(lv_obj_t * obj)
{
    if(obj == NULL) {
        return NULL;
    }

    return lv_obj_get_user_data(obj);
}

static void ui_gradient_text_apply_defaults(ui_gradient_text_state_t * state)
{
    static const lv_color_t default_palette[] = {
        LV_COLOR_MAKE(0x00, 0x50, 0xff),
        LV_COLOR_MAKE(0x3a, 0xc6, 0xff),
        LV_COLOR_MAKE(0x8d, 0xff, 0xc1),
        LV_COLOR_MAKE(0x00, 0xd8, 0x72),
    };

    state->font = LV_FONT_DEFAULT;
    state->text_pad = 4;
    state->phase_max = 1024;
    state->phase_offset = state->phase_max / 4;
    state->anim_duration_ms = 2400;
    state->grad_dir = LV_GRAD_DIR_HOR;
    ui_gradient_text_copy_palette(state, default_palette, (uint8_t)(sizeof(default_palette) / sizeof(default_palette[0])));
}

static bool ui_gradient_text_copy_palette(ui_gradient_text_state_t * state, const lv_color_t * colors, uint8_t color_count)
{
    lv_color_t * new_palette = lv_malloc(sizeof(lv_color_t) * color_count);
    if(new_palette == NULL) {
        return false;
    }

    lv_memcpy(new_palette, colors, sizeof(lv_color_t) * color_count);

    if(state->palette != NULL) {
        lv_free(state->palette);
    }

    state->palette = new_palette;
    state->color_count = color_count;
    return true;
}

static void ui_gradient_text_refresh_mask(lv_obj_t * obj, const char * text)
{
    ui_gradient_text_state_t * state = ui_gradient_text_get_state(obj);
    lv_point_t text_size;

    /*
     * 先用 LVGL 的文本测量接口算出真实文字尺寸，
     * 然后在四周补一点 padding，避免大字号边缘被裁掉。
     */
    lv_text_get_size(&text_size, text, state->font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    uint32_t mask_w = (uint32_t)text_size.x + state->text_pad * 2U;
    uint32_t mask_h = (uint32_t)text_size.y + state->text_pad * 2U;

    if(state->mask != NULL) {
        /* 先解绑旧遮罩，再销毁缓冲区，避免留下悬空指针。 */
        lv_obj_set_style_bitmap_mask_src(obj, NULL, LV_PART_MAIN);
        lv_draw_buf_destroy(state->mask);
        state->mask = NULL;
    }

    state->mask = lv_draw_buf_create(mask_w, mask_h, LV_COLOR_FORMAT_L8, LV_STRIDE_AUTO);
    if(state->mask == NULL) {
        return;
    }

    ui_gradient_text_generate_mask(obj, state->mask, text, state->font);
    lv_obj_set_size(obj, (int32_t)mask_w, (int32_t)mask_h);
    lv_obj_set_style_bitmap_mask_src(obj, state->mask, LV_PART_MAIN);
}

static void ui_gradient_text_generate_mask(lv_obj_t * parent, lv_draw_buf_t * mask, const char * text,
                                           const lv_font_t * font)
{
    /*
     * 这里借助一个临时 canvas 把文字画进 L8 缓冲区。
     * 最终得到的不是彩色文字，而是一张“透明度遮罩图”。
     */
    lv_obj_t * canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(canvas, mask);
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    label_dsc.text = text;
    label_dsc.font = font;

    lv_area_t text_area = {
        .x1 = 0,
        .y1 = 0,
        .x2 = (int32_t)mask->header.w - 1,
        .y2 = (int32_t)mask->header.h - 1,
    };
    lv_draw_label(&layer, &label_dsc, &text_area);
    lv_canvas_finish_layer(canvas, &layer);
    lv_obj_delete(canvas);
}

static void ui_gradient_text_restart_anim(lv_obj_t * obj)
{
    ui_gradient_text_state_t * state = ui_gradient_text_get_state(obj);
    if(state == NULL) {
        return;
    }

    /* 配置变动后先删除旧动画，避免重复注册。 */
    lv_anim_del(obj, ui_gradient_text_anim_cb);

    if(state->anim_duration_ms == 0 || state->phase_max < 2 || state->color_count < 2) {
        return;
    }

    /* 用线性动画推进“相位”，真正的颜色计算放在 exec_cb 里完成。 */
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, ui_gradient_text_anim_cb);
    lv_anim_set_values(&anim, 0, state->phase_max - 1);
    lv_anim_set_time(&anim, state->anim_duration_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
}

static void ui_gradient_text_anim_cb(void * var, int32_t value)
{
    lv_obj_t * obj = var;
    ui_gradient_text_state_t * state = ui_gradient_text_get_state(obj);
    if(state == NULL || state->color_count < 2 || state->phase_max < 2) {
        return;
    }

    /*
     * 当前帧会取两种颜色：
     * - start_color：渐变起始色
     * - end_color：渐变结束色
     *
     * 两者相位错开一定距离，就能形成“流动中的色带”效果。
     */
    uint16_t phase = (uint16_t)value % state->phase_max;
    lv_color_t start_color = ui_gradient_text_color_at(state, phase);
    lv_color_t end_color = ui_gradient_text_color_at(state, (uint16_t)((phase + state->phase_offset) % state->phase_max));

    lv_obj_set_style_bg_color(obj, start_color, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(obj, end_color, LV_PART_MAIN);
}

/*
 * 按相位从循环调色板中采样当前颜色。
 * 这里会自动做首尾衔接，因此调用方不需要手动把第一个颜色重复到数组末尾。
 */
static lv_color_t ui_gradient_text_color_at(const ui_gradient_text_state_t * state, uint16_t phase)
{
    uint32_t scaled = (uint32_t)phase * state->color_count;
    uint16_t section = (uint16_t)(scaled / state->phase_max);
    uint16_t local = (uint16_t)(scaled % state->phase_max);
    uint8_t mix = (uint8_t)(local * 255U / (state->phase_max - 1U));

    lv_color_t from = state->palette[section % state->color_count];
    lv_color_t to = state->palette[(section + 1U) % state->color_count];

    return ui_gradient_text_color_lerp(from, to, mix);
}

static lv_color_t ui_gradient_text_color_lerp(lv_color_t from, lv_color_t to, uint8_t mix)
{
    /*
     * lv_color_t 在不同色深下底层表示可能不同，
     * 这里先统一转成 32 位颜色再做插值，最后再转回当前色深。
     */
    lv_color32_t from32 = lv_color_to_32(from, LV_OPA_COVER);
    lv_color32_t to32 = lv_color_to_32(to, LV_OPA_COVER);

    uint16_t inv = 255U - mix;
    uint8_t red = (uint8_t)((from32.red * inv + to32.red * mix) / 255U);
    uint8_t green = (uint8_t)((from32.green * inv + to32.green * mix) / 255U);
    uint8_t blue = (uint8_t)((from32.blue * inv + to32.blue * mix) / 255U);

    return lv_color_make(red, green, blue);
}

static void ui_gradient_text_delete_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    ui_gradient_text_state_t * state = ui_gradient_text_get_state(obj);
    if(state == NULL) {
        return;
    }

    /* 对象销毁时同步清理组件持有的运行时资源。 */
    lv_anim_del(obj, ui_gradient_text_anim_cb);

    if(state->mask != NULL) {
        lv_draw_buf_destroy(state->mask);
    }

    if(state->palette != NULL) {
        lv_free(state->palette);
    }

    lv_free(state);
    lv_obj_set_user_data(obj, NULL);
}
