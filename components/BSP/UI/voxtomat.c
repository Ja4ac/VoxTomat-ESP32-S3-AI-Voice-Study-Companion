#include "voxtomat.h"

#include "../UI/gradient_text.h"

extern const lv_font_t JetBrainsMono_BoldItalic_14px;
extern const lv_font_t JetBrainsMono_BoldItalic_94px;
extern const lv_font_t SourceHanSerifCN_Regular_1_3500chi_14px;
extern const lv_font_t SourceHanSerifCN_Regular_1_3500chi_16px;

#define STATUS_BAR_HEIGHT      23
#define FUNCTION_AREA_HEIGHT   137
#define SCHEDULE_AREA_HEIGHT   50
#define DIALOGUE_AREA_HEIGHT   30

#define FUNCTION_COUNTDOWN_TEXT "25:00"

// static lv_style_t style_scr_main;
static lv_style_t style_status_bar;
static lv_style_t style_function_area;
static lv_style_t style_schedule_area;
static lv_style_t style_dialogue_area;

static void voxtomat_style_init(void);
static void voxtomat_status_bar_components_create(lv_obj_t *parent);
static void voxtomat_function_area_components_create(lv_obj_t *parent);
static void voxtomat_schedule_area_componetns_create(lv_obj_t *parent);
static void voxtomat_dialogue_area_componetns_create(lv_obj_t *parent);


void voxtomat_create(void)
{
    lv_obj_t *scr_main = lv_obj_create(NULL);
    lv_obj_set_flex_flow(scr_main, LV_FLEX_FLOW_COLUMN_WRAP);
    lv_obj_set_style_pad_all(scr_main, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr_main, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scr_main, lv_color_make(41, 40, 49), LV_PART_MAIN);

    lv_obj_set_style_text_font(scr_main, &SourceHanSerifCN_Regular_1_3500chi_16px, LV_PART_MAIN);

    lv_obj_t *status_bar = lv_obj_create(scr_main);
    lv_obj_t *function_area = lv_obj_create(scr_main);
    lv_obj_t *schedule_area = lv_obj_create(scr_main);
    lv_obj_t *dialogue_area = lv_obj_create(scr_main);

    voxtomat_style_init();

    // lv_obj_add_style(scr_main, &style_scr_main, LV_PART_MAIN);
    lv_obj_add_style(status_bar, &style_status_bar, LV_PART_MAIN);
    lv_obj_add_style(function_area, &style_function_area, LV_PART_MAIN);
    lv_obj_add_style(schedule_area, &style_schedule_area, LV_PART_MAIN);
    lv_obj_add_style(dialogue_area, &style_dialogue_area, LV_PART_MAIN);

    voxtomat_status_bar_components_create(status_bar);
    voxtomat_function_area_components_create(function_area);
    voxtomat_schedule_area_componetns_create(schedule_area);
    voxtomat_dialogue_area_componetns_create(dialogue_area);

    lv_scr_load(scr_main);
}

static void voxtomat_style_init()
{
    // lv_style_init(&style_scr_main);
    lv_style_init(&style_status_bar);
    lv_style_init(&style_function_area);
    lv_style_init(&style_schedule_area);
    lv_style_init(&style_dialogue_area);

    // 状态栏
    lv_style_set_width(&style_status_bar, LV_HOR_RES);
    lv_style_set_height(&style_status_bar, STATUS_BAR_HEIGHT);
    lv_style_set_border_width(&style_status_bar, 0);
    lv_style_set_radius(&style_status_bar, 0);
    lv_style_set_opa(&style_status_bar, LV_OPA_COVER);

    // 功能区
    lv_style_set_width(&style_function_area, LV_HOR_RES);
    lv_style_set_height(&style_function_area, FUNCTION_AREA_HEIGHT);
    lv_style_set_border_width(&style_function_area, 3);
    lv_style_set_border_color(&style_function_area, lv_color_make(50, 64, 85));
    lv_style_set_radius(&style_function_area, 12);
    lv_style_set_opa(&style_function_area, LV_OPA_COVER);

    // 日程区
    lv_style_set_width(&style_schedule_area, LV_HOR_RES);
    lv_style_set_height(&style_schedule_area, SCHEDULE_AREA_HEIGHT);
    lv_style_set_border_width(&style_schedule_area, 0);
    lv_style_set_radius(&style_schedule_area, 0);
    lv_style_set_opa(&style_schedule_area, LV_OPA_COVER);

    // 对话区
    lv_style_set_width(&style_dialogue_area, LV_HOR_RES);
    lv_style_set_height(&style_dialogue_area, DIALOGUE_AREA_HEIGHT);
    lv_style_set_border_width(&style_dialogue_area, 4);
    lv_style_set_radius(&style_dialogue_area, 0);
    lv_style_set_opa(&style_dialogue_area, LV_OPA_COVER);
}

static void voxtomat_status_bar_components_create(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lable_time = lv_label_create(parent);
    lv_label_set_text(lable_time, "12:00");
    lv_obj_set_style_text_color(lable_time, lv_color_make(238, 243, 250), LV_PART_MAIN);
    lv_obj_set_style_text_font(lable_time, &JetBrainsMono_BoldItalic_14px, LV_PART_MAIN);

    lv_obj_t *lable_battery = lv_label_create(parent);
    lv_label_set_text(lable_battery, "100%");
    lv_obj_set_style_text_color(lable_battery, lv_color_make(40, 250, 70), LV_PART_MAIN);
    lv_obj_set_style_text_font(lable_battery, &JetBrainsMono_BoldItalic_14px, LV_PART_MAIN);

}

static void voxtomat_function_area_components_create(lv_obj_t *parent)
{
    /*
     * 页面层只描述自己的需求：
     * 1. 使用哪套调色板
     * 2. 使用什么字体
     * 3. 动画速度
     * 4. 渐变方向
     *
     * 遮罩生成、动画推进、颜色插值和资源释放
     * 都封装在可复用的 gradient_text 组件内部。
     */
    static const lv_color_t countdown_palette[] = {
        LV_COLOR_MAKE(0x3B, 0x5B, 0xFF),  // blue
        LV_COLOR_MAKE(0x00, 0x8F, 0xFF),  // sky
        LV_COLOR_MAKE(0x00, 0xD0, 0xFF),  // cyan
        LV_COLOR_MAKE(0x00, 0xE8, 0x9B),  // green-cyan
        LV_COLOR_MAKE(0x8E, 0xF3, 0x3D),  // lime
        LV_COLOR_MAKE(0xFF, 0xD3, 0x3D),  // yellow
        LV_COLOR_MAKE(0xFF, 0x8A, 0x47),  // orange
    };

    static const ui_gradient_text_config_t countdown_config = {
        .font = &JetBrainsMono_BoldItalic_94px,
        .colors = countdown_palette,
        .color_count = (uint8_t)(sizeof(countdown_palette) / sizeof(countdown_palette[0])),
        .text_pad = 6,
        .phase_max = 1024,
        .phase_offset = 192,
        .anim_duration_ms = 5000,
        .grad_dir = LV_GRAD_DIR_HOR,
    };

    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lable_active = lv_label_create(parent);
    lv_label_set_text(lable_active, "当前:学习中");
    lv_obj_set_style_text_color(lable_active, lv_color_make(238, 243, 250), LV_PART_MAIN);
    lv_obj_set_style_text_font(lable_active, &SourceHanSerifCN_Regular_1_3500chi_14px, LV_PART_MAIN);
    lv_obj_align(lable_active, LV_ALIGN_TOP_LEFT, -8, -8);


    lv_obj_t *countdown = ui_gradient_text_create(parent, FUNCTION_COUNTDOWN_TEXT, &countdown_config);
    if(countdown != NULL) {
        lv_obj_align(countdown, LV_ALIGN_CENTER, 0, 10);
    }
}

static void voxtomat_schedule_area_componetns_create(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(parent, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);

    lv_obj_t *lable = lv_label_create(parent);
    lv_label_set_text(lable, "接下来:");
    lv_obj_align(lable, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_add_flag(lable, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_color(lable, lv_color_make(238, 243, 250), LV_PART_MAIN);
    lv_obj_set_style_text_font(lable, &SourceHanSerifCN_Regular_1_3500chi_14px, LV_PART_MAIN);

    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(container, 1);
    lv_obj_set_pos(container, 0, 0);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(container, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lable1 = lv_label_create(container);
    lv_label_set_text(lable1, "4-20 11:30 工作");

    lv_obj_t *divider = lv_obj_create(container);
    lv_obj_set_size(divider, lv_pct(80), 1);
    lv_obj_set_style_bg_color(divider, lv_color_make(0x00, 0x50, 0xff), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(divider, lv_color_make(0x00, 0xd8, 0x72), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(divider, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);

    lv_obj_t *lable2 = lv_label_create(container);
    lv_label_set_text(lable2, "4-21 18:00 开会");
}

static void voxtomat_dialogue_area_componetns_create(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 0, 0);

    lv_obj_t *lable1 = lv_label_create(parent);
    lv_obj_set_width(lable1, 58);
    lv_label_set_text(lable1, " 回答中:");
    lv_obj_set_style_text_align(lable1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    lv_obj_t *content_wrap = lv_obj_create(parent);
    lv_obj_set_height(content_wrap, LV_PCT(100));
    lv_obj_set_style_border_width(content_wrap, 0, 0);
    lv_obj_set_style_pad_all(content_wrap, 0, 0);
    lv_obj_set_flex_grow(content_wrap, 1);

    lv_obj_t *content = lv_label_create(content_wrap);
    lv_obj_set_align(content, LV_ALIGN_LEFT_MID);
    lv_obj_set_width(content, LV_PCT(100));
    lv_label_set_text(content, "暂无内容");
    lv_label_set_long_mode(content, LV_LABEL_LONG_MODE_SCROLL);
}
