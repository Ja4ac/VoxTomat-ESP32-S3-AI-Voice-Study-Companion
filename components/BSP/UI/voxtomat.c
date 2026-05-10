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

static lv_style_t s_style_status_bar;
static lv_style_t s_style_function_area;
static lv_style_t s_style_schedule_area;
static lv_style_t s_style_dialogue_area;

// 状态栏 时间
static lv_obj_t *s_label_time = NULL;

// 状态栏 电量
static lv_obj_t *s_label_battery = NULL;
void voxtomat_bat_set(uint8_t bat_percent);

// 功能区 当前活动
enum active_e{
    ACTIVE_NONE = 0,
    ACTIVE_PAUSE,
    ACTIVE_REST,
    ACTIVE_STUDY,
    ACTIVE_WORK,
    ACTIVE_EXERCISE,
    ACTIVE_MEETING,
};
static lv_obj_t *s_label_active = NULL;

// 功能区 倒计时
static lv_obj_t *s_countdown = NULL;
static lv_timer_t *s_countdown_timer = NULL;
static uint32_t s_remaining_seconds;
void voxtomat_countdown_start(void);
void voxtomat_countdown_pause(void);
void voxtomat_countdown_reset(uint16_t minutes);
static void countdown_timer_cb(lv_timer_t *timer)
{
    if(s_remaining_seconds > 0) 
    {
        s_remaining_seconds--;
    }
    if(s_remaining_seconds <= 0)
    {
        voxtomat_countdown_pause();
    }

    char buf2[8];
    lv_snprintf(buf2, sizeof(buf2), "%02lu:%02lu",
                s_remaining_seconds / 60,
                s_remaining_seconds % 60);

    if(s_countdown != NULL)
    {
        ui_gradient_text_set_text(s_countdown, buf2);
    }
}

// 日程区 修改
static lv_obj_t *s_schedule_label1 = NULL;
static lv_obj_t *s_schedule_label2 = NULL;
void voxtomat_schedule_refresh(const voxtomat_schedule_item_t *items, uint8_t count);

// 对话区 对话内容
static lv_obj_t *s_dialogue_label1 = NULL;
static lv_obj_t *s_dialogue_label2 = NULL;
void voxtomat_dialogue_set_text(const char *text);

// 样式配置函数
static void voxtomat_style_init(void);
static void voxtomat_status_bar_components_create(lv_obj_t *parent);
static void voxtomat_function_area_components_create(lv_obj_t *parent);
static void voxtomat_schedule_area_components_create(lv_obj_t *parent);
static void voxtomat_dialogue_area_components_create(lv_obj_t *parent);

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

    lv_obj_add_style(status_bar, &s_style_status_bar, LV_PART_MAIN);
    lv_obj_add_style(function_area, &s_style_function_area, LV_PART_MAIN);
    lv_obj_add_style(schedule_area, &s_style_schedule_area, LV_PART_MAIN);
    lv_obj_add_style(dialogue_area, &s_style_dialogue_area, LV_PART_MAIN);

    voxtomat_status_bar_components_create(status_bar);
    voxtomat_function_area_components_create(function_area);
    voxtomat_schedule_area_components_create(schedule_area);
    voxtomat_dialogue_area_components_create(dialogue_area);

    lv_scr_load(scr_main);
}

static void voxtomat_style_init()
{
    lv_style_init(&s_style_status_bar);
    lv_style_init(&s_style_function_area);
    lv_style_init(&s_style_schedule_area);
    lv_style_init(&s_style_dialogue_area);

    // 状态栏
    lv_style_set_width(&s_style_status_bar, LV_HOR_RES);
    lv_style_set_height(&s_style_status_bar, STATUS_BAR_HEIGHT);
    lv_style_set_border_width(&s_style_status_bar, 0);
    lv_style_set_radius(&s_style_status_bar, 0);
    lv_style_set_opa(&s_style_status_bar, LV_OPA_COVER);

    // 功能区
    lv_style_set_width(&s_style_function_area, LV_HOR_RES);
    lv_style_set_height(&s_style_function_area, FUNCTION_AREA_HEIGHT);
    lv_style_set_border_width(&s_style_function_area, 3);
    lv_style_set_border_color(&s_style_function_area, lv_color_make(50, 64, 85));
    lv_style_set_radius(&s_style_function_area, 12);
    lv_style_set_opa(&s_style_function_area, LV_OPA_COVER);

    // 日程区
    lv_style_set_width(&s_style_schedule_area, LV_HOR_RES);
    lv_style_set_height(&s_style_schedule_area, SCHEDULE_AREA_HEIGHT);
    lv_style_set_border_width(&s_style_schedule_area, 0);
    lv_style_set_radius(&s_style_schedule_area, 0);
    lv_style_set_opa(&s_style_schedule_area, LV_OPA_COVER);

    // 对话区
    lv_style_set_width(&s_style_dialogue_area, LV_HOR_RES);
    lv_style_set_height(&s_style_dialogue_area, DIALOGUE_AREA_HEIGHT);
    lv_style_set_border_width(&s_style_dialogue_area, 4);
    lv_style_set_radius(&s_style_dialogue_area, 0);
    lv_style_set_opa(&s_style_dialogue_area, LV_OPA_COVER);
}

// 状态栏组件配置
static void voxtomat_status_bar_components_create(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_label_time = lv_label_create(parent);
    lv_obj_set_style_text_color(s_label_time, lv_color_make(238, 243, 250), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_label_time, &JetBrainsMono_BoldItalic_14px, LV_PART_MAIN);

    s_label_battery = lv_label_create(parent);
    voxtomat_bat_set(100);
    lv_obj_set_style_text_font(s_label_battery, &JetBrainsMono_BoldItalic_14px, LV_PART_MAIN);

}

// 功能区组件配置
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

    // s_label_active = lv_label_create(parent);
    // lv_label_set_text(s_label_active, "当前:学习中");
    // lv_obj_set_style_text_color(s_label_active, lv_color_make(238, 243, 250), LV_PART_MAIN);
    // lv_obj_set_style_text_font(s_label_active, &SourceHanSerifCN_Regular_1_3500chi_14px, LV_PART_MAIN);
    // lv_obj_align(s_label_active, LV_ALIGN_TOP_LEFT, -8, -8);

    s_countdown = ui_gradient_text_create(parent, FUNCTION_COUNTDOWN_TEXT, &countdown_config);

    if(s_countdown != NULL) 
    {
        lv_obj_align(s_countdown, LV_ALIGN_CENTER, 0, 5);
    }

    s_countdown_timer = lv_timer_create(countdown_timer_cb, 1000, NULL);
    if(s_countdown_timer != NULL)
    {
        voxtomat_countdown_pause();
        voxtomat_countdown_reset(25);
    }
    
}

// 日程区组件配置
static void voxtomat_schedule_area_components_create(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(parent, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "接下来:");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_add_flag(label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_color(label, lv_color_make(238, 243, 250), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &SourceHanSerifCN_Regular_1_3500chi_14px, LV_PART_MAIN);

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

    s_schedule_label1 = lv_label_create(container);

    lv_obj_t *divider = lv_obj_create(container);
    lv_obj_set_size(divider, lv_pct(80), 1);
    lv_obj_set_style_bg_color(divider, lv_color_make(0x00, 0x50, 0xff), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(divider, lv_color_make(0x00, 0xd8, 0x72), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(divider, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);

    s_schedule_label2 = lv_label_create(container);

    lv_label_set_text(s_schedule_label1, "暂无日程");
    lv_label_set_text(s_schedule_label2, "");
}

// 对话区组件配置
static void voxtomat_dialogue_area_components_create(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 0, 0);

    s_dialogue_label1 = lv_label_create(parent);
    lv_obj_set_width(s_dialogue_label1, 58);
    lv_obj_set_style_text_font(s_dialogue_label1, &JetBrainsMono_BoldItalic_14px, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_dialogue_label1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_text(s_dialogue_label1, "AI");

    lv_obj_t *content_wrap = lv_obj_create(parent);
    lv_obj_set_height(content_wrap, LV_PCT(100));
    lv_obj_set_style_border_width(content_wrap, 0, 0);
    lv_obj_set_style_pad_all(content_wrap, 0, 0);
    lv_obj_set_flex_grow(content_wrap, 1);

    s_dialogue_label2 = lv_label_create(content_wrap);
    lv_obj_set_align(s_dialogue_label2, LV_ALIGN_LEFT_MID);
    lv_obj_set_width(s_dialogue_label2, LV_PCT(100));
    lv_label_set_text(s_dialogue_label2, "暂无内容");
    lv_label_set_long_mode(s_dialogue_label2, LV_LABEL_LONG_MODE_SCROLL);
}

// 状态栏 时间更新
void voxtomat_time_update(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute)
{
    if(s_label_time == NULL)
    {
        return;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    lv_label_set_text(s_label_time, buf);
}

// 状态栏 电量函数
void voxtomat_bat_set(uint8_t bat_percent)
{
    lv_label_set_text_fmt(s_label_battery, "%d%%", bat_percent);
    if(bat_percent >= 60)
    {
        lv_obj_set_style_text_color(s_label_battery, lv_color_make(40, 250, 70), LV_PART_MAIN);
        return;
    }
    if(bat_percent >= 20)
    {
        lv_obj_set_style_text_color(s_label_battery, lv_color_make(237, 161, 30), LV_PART_MAIN);
        return;
    }
    lv_obj_set_style_text_color(s_label_battery, lv_color_make(235, 46, 23), LV_PART_MAIN);
    return;
}

// 功能区 倒计时函数
void voxtomat_countdown_start(void)
{
    if(s_countdown_timer != NULL) 
    {
        lv_timer_resume(s_countdown_timer);
    }
}
void voxtomat_countdown_pause(void)
{
    if(s_countdown_timer != NULL) 
    {
        lv_timer_pause(s_countdown_timer);
    }
}
void voxtomat_countdown_reset(uint16_t minutes)
{
    s_remaining_seconds = minutes * 60;
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%02lu:%02lu",
                s_remaining_seconds / 60,
                s_remaining_seconds % 60);

    if(s_countdown != NULL) {
        ui_gradient_text_set_text(s_countdown, buf);
    }
}

// 日程区 刷新函数
void voxtomat_schedule_refresh(const voxtomat_schedule_item_t *items, uint8_t count)
{
    char buf[128];

    if(s_schedule_label1 == NULL || s_schedule_label2 == NULL)
    {
        return;
    }

    if(count == 0 || items == NULL)
    {
        lv_label_set_text(s_schedule_label1, "暂无日程");
        lv_label_set_text(s_schedule_label2, "");
        return;
    }

    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d %s",
             items[0].year, items[0].month, items[0].day,
             items[0].hour, items[0].minute, items[0].text);
    lv_label_set_text(s_schedule_label1, buf);

    if(count >= 2)
    {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d %s",
                 items[1].year, items[1].month, items[1].day,
                 items[1].hour, items[1].minute, items[1].text);
        lv_label_set_text(s_schedule_label2, buf);
    }
    else
    {
        lv_label_set_text(s_schedule_label2, "");
    }
}

// 对话区 显示对话内容
void voxtomat_dialogue_set_text(const char *text)
{
    if(s_dialogue_label2 == NULL)
    {
        return;
    }
    lv_label_set_text(s_dialogue_label2, text != NULL ? text : "");
}