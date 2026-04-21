#ifndef GRADIENT_TEXT_H
#define GRADIENT_TEXT_H

#include "lvgl.h"

#include <stdint.h>

/*
 * 可复用的渐变文字配置。
 * 组件会先把文字绘制成遮罩，再把背景渐变限制在文字区域内显示。
 */
typedef struct {
    const lv_font_t * font;      /* 文字字体 */
    const lv_color_t * colors;   /* 循环渐变调色板 */
    uint8_t color_count;         /* 调色板颜色数量，至少为 2 */
    uint16_t text_pad;           /* 文字外围留白，避免边缘被裁切 */
    uint16_t phase_max;          /* 动画相位精度，越大越平滑 */
    uint16_t phase_offset;       /* 渐变起止颜色之间的相位差 */
    uint32_t anim_duration_ms;   /* 一轮完整动画时长 */
    lv_grad_dir_t grad_dir;      /* 渐变方向 */
} ui_gradient_text_config_t;

/*
 * 创建一个可复用的渐变文字对象。
 * 返回值仍然是普通的 lv_obj_t*，可以直接参与布局和对齐。
 *
 * 基本使用方式：
 *
 * 1. 先准备一组循环渐变颜色和配置：
 *
 * static const lv_color_t palette[] = {
 *     LV_COLOR_MAKE(0x00, 0x50, 0xff),
 *     LV_COLOR_MAKE(0x3a, 0xc6, 0xff),
 *     LV_COLOR_MAKE(0x8d, 0xff, 0xc1),
 *     LV_COLOR_MAKE(0x00, 0xd8, 0x72),
 * };
 *
 * static const ui_gradient_text_config_t config = {
 *     .font = &lv_font_montserrat_40,
 *     .colors = palette,
 *     .color_count = 4,
 *     .text_pad = 6,
 *     .phase_max = 1024,
 *     .phase_offset = 256,
 *     .anim_duration_ms = 2400,
 *     .grad_dir = LV_GRAD_DIR_HOR,
 * };
 *
 * 2. 创建对象并参与布局：
 *
 * lv_obj_t * title = ui_gradient_text_create(parent, "25:00", &config);
 * lv_obj_set_align(title, LV_ALIGN_CENTER);
 *
 * 3. 运行时更新文字：
 *
 * ui_gradient_text_set_text(title, "24:59");
 *
 * 4. 如果想复用同一个对象但切换风格：
 *
 * ui_gradient_text_apply_config(title, &new_config);
 * ui_gradient_text_set_palette(title, new_palette, new_count);
 *
 * 说明：
 * - `colors` 只需要提供“一个循环”的颜色，不需要把第一个颜色重复写到末尾。
 * - `phase_offset` 越大，左右两端颜色差异越明显，流动感越强。
 * - `phase_max` 越大，颜色插值越细腻，但通常 512~2048 就够用了。
 * - `anim_duration_ms` 越大，动画越慢。
 */
lv_obj_t * ui_gradient_text_create(lv_obj_t * parent, const char * text, const ui_gradient_text_config_t * config);

/* 更新文字内容，组件会自动重建内部遮罩。 */
void ui_gradient_text_set_text(lv_obj_t * obj, const char * text);

/* 应用一整套新配置。 */
void ui_gradient_text_apply_config(lv_obj_t * obj, const ui_gradient_text_config_t * config);

/* 替换调色板并重新启动动画。 */
void ui_gradient_text_set_palette(lv_obj_t * obj, const lv_color_t * colors, uint8_t color_count);

#endif
