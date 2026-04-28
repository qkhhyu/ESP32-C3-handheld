#include "weather_app.h"

#include <time.h>

#include "extra/libs/gif/lv_gif.h"
#include "main.h"
#include "weather_service.h"

LV_IMG_DECLARE(image_taikong);
LV_FONT_DECLARE(font_alipuhui);
LV_FONT_DECLARE(font_qweather);
LV_FONT_DECLARE(font_led);
LV_FONT_DECLARE(font_myawesome);

static lv_timer_t *s_weather_timer;
static lv_obj_t *s_status_label;
static lv_obj_t *s_qweather_icon_label;
static lv_obj_t *s_qweather_temp_label;
static lv_obj_t *s_qweather_text_label;
static lv_obj_t *s_qair_level_obj;
static lv_obj_t *s_qair_level_label;
static lv_obj_t *s_led_time_label;
static lv_obj_t *s_week_label;
static lv_obj_t *s_sun_label;
static lv_obj_t *s_outdoor_temp_label;
static lv_obj_t *s_outdoor_humi_label;
static lv_obj_t *s_indoor_temp_label;
static lv_obj_t *s_indoor_humi_label;
static lv_obj_t *s_date_label;

static void weather_app_set_icon_and_text(int icon)
{
    const char *symbol = "\xEF\x85\x86";
    const char *text = "未知";

    switch (icon) {
    case 100: symbol = "\xEF\x84\x81"; text = "晴"; break;
    case 101: symbol = "\xEF\x84\x82"; text = "多云"; break;
    case 102: symbol = "\xEF\x84\x83"; text = "少云"; break;
    case 103: symbol = "\xEF\x84\x84"; text = "晴间多云"; break;
    case 104: symbol = "\xEF\x84\x85"; text = "阴"; break;
    case 150: symbol = "\xEF\x84\x86"; text = "晴"; break;
    case 151: symbol = "\xEF\x84\x87"; text = "多云"; break;
    case 152: symbol = "\xEF\x84\x88"; text = "少云"; break;
    case 153: symbol = "\xEF\x84\x89"; text = "晴间多云"; break;
    case 300: symbol = "\xEF\x84\x8A"; text = "阵雨"; break;
    case 301: symbol = "\xEF\x84\x8B"; text = "强阵雨"; break;
    case 302: symbol = "\xEF\x84\x8C"; text = "雷阵雨"; break;
    case 303: symbol = "\xEF\x84\x8D"; text = "强雷阵雨"; break;
    case 304: symbol = "\xEF\x84\x8E"; text = "雷阵雨伴有冰雹"; break;
    case 305: symbol = "\xEF\x84\x8F"; text = "小雨"; break;
    case 306: symbol = "\xEF\x84\x90"; text = "中雨"; break;
    case 307: symbol = "\xEF\x84\x91"; text = "大雨"; break;
    case 308: symbol = "\xEF\x84\x92"; text = "极端降雨"; break;
    case 309: symbol = "\xEF\x84\x93"; text = "毛毛雨"; break;
    case 310: symbol = "\xEF\x84\x94"; text = "暴雨"; break;
    case 311: symbol = "\xEF\x84\x95"; text = "大暴雨"; break;
    case 312: symbol = "\xEF\x84\x96"; text = "特大暴雨"; break;
    case 313: symbol = "\xEF\x84\x97"; text = "冻雨"; break;
    case 314: symbol = "\xEF\x84\x98"; text = "小到中雨"; break;
    case 315: symbol = "\xEF\x84\x99"; text = "中到大雨"; break;
    case 316: symbol = "\xEF\x84\x9A"; text = "大到暴雨"; break;
    case 317: symbol = "\xEF\x84\x9B"; text = "暴雨到大暴雨"; break;
    case 318: symbol = "\xEF\x84\x9C"; text = "大暴雨到特大暴雨"; break;
    case 350: symbol = "\xEF\x84\x9D"; text = "阵雨"; break;
    case 351: symbol = "\xEF\x84\x9E"; text = "强阵雨"; break;
    case 399: symbol = "\xEF\x84\x9F"; text = "雨"; break;
    case 400: symbol = "\xEF\x84\xA0"; text = "小雪"; break;
    case 401: symbol = "\xEF\x84\xA1"; text = "中雪"; break;
    case 402: symbol = "\xEF\x84\xA2"; text = "大雪"; break;
    case 403: symbol = "\xEF\x84\xA3"; text = "暴雪"; break;
    case 404: symbol = "\xEF\x84\xA4"; text = "雨夹雪"; break;
    case 405: symbol = "\xEF\x84\xA5"; text = "雨雪天气"; break;
    case 406: symbol = "\xEF\x84\xA6"; text = "阵雨夹雪"; break;
    case 407: symbol = "\xEF\x84\xA7"; text = "阵雪"; break;
    case 408: symbol = "\xEF\x84\xA8"; text = "小到中雪"; break;
    case 409: symbol = "\xEF\x84\xA9"; text = "中到大雪"; break;
    case 410: symbol = "\xEF\x84\xAA"; text = "大到暴雪"; break;
    case 456: symbol = "\xEF\x84\xAB"; text = "阵雨夹雪"; break;
    case 457: symbol = "\xEF\x84\xAC"; text = "阵雪"; break;
    case 499: symbol = "\xEF\x84\xAD"; text = "雪"; break;
    case 500: symbol = "\xEF\x84\xAE"; text = "薄雾"; break;
    case 501: symbol = "\xEF\x84\xAF"; text = "雾"; break;
    case 502: symbol = "\xEF\x84\xB0"; text = "霾"; break;
    case 503: symbol = "\xEF\x84\xB1"; text = "扬沙"; break;
    case 504: symbol = "\xEF\x84\xB2"; text = "浮尘"; break;
    case 507: symbol = "\xEF\x84\xB3"; text = "沙尘暴"; break;
    case 508: symbol = "\xEF\x84\xB4"; text = "强沙尘暴"; break;
    case 509: symbol = "\xEF\x84\xB5"; text = "浓雾"; break;
    case 510: symbol = "\xEF\x84\xB6"; text = "强浓雾"; break;
    case 511: symbol = "\xEF\x84\xB7"; text = "中度霾"; break;
    case 512: symbol = "\xEF\x84\xB8"; text = "重度霾"; break;
    case 513: symbol = "\xEF\x84\xB9"; text = "严重霾"; break;
    case 514: symbol = "\xEF\x84\xBA"; text = "大雾"; break;
    case 515: symbol = "\xEF\x84\xBB"; text = "特强浓雾"; break;
    case 900: symbol = "\xEF\x85\x84"; text = "热"; break;
    case 901: symbol = "\xEF\x85\x85"; text = "冷"; break;
    default: break;
    }

    if (s_qweather_icon_label != NULL) {
        lv_label_set_text(s_qweather_icon_label, symbol);
    }
    if (s_qweather_text_label != NULL) {
        lv_label_set_text(s_qweather_text_label, text);
    }
}

static void weather_app_set_week(const struct tm *timeinfo)
{
    static const char *kWeek[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
    };

    int wday = timeinfo->tm_wday;
    if (wday < 0 || wday > 6) {
        wday = 0;
    }
    lv_label_set_text(s_week_label, kWeek[wday]);
}

static void weather_app_set_air_level(int level)
{
    lv_color_t bg = lv_palette_main(LV_PALETTE_GREY);
    lv_color_t fg = lv_color_hex(0xFFFFFF);
    const char *text = "未";

    switch (level) {
    case 1: text = "优"; bg = lv_palette_main(LV_PALETTE_GREEN); break;
    case 2: text = "良"; bg = lv_palette_main(LV_PALETTE_YELLOW); fg = lv_color_hex(0x000000); break;
    case 3: text = "轻"; bg = lv_palette_main(LV_PALETTE_ORANGE); break;
    case 4: text = "中"; bg = lv_palette_main(LV_PALETTE_RED); break;
    case 5: text = "重"; bg = lv_palette_main(LV_PALETTE_PURPLE); break;
    case 6: text = "严"; bg = lv_palette_main(LV_PALETTE_BROWN); break;
    default: break;
    }

    lv_obj_set_style_bg_color(s_qair_level_obj, bg, 0);
    lv_obj_set_style_text_color(s_qair_level_label, fg, 0);
    lv_label_set_text(s_qair_level_label, text);
}

static void weather_app_refresh(lv_timer_t *timer)
{
    weather_snapshot_t snapshot;
    weather_service_get_snapshot(&snapshot);

    if (snapshot.time_synced) {
        time_t now;
        struct tm timeinfo;

        time(&now);
        localtime_r(&now, &timeinfo);

        lv_label_set_text_fmt(s_led_time_label, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        lv_label_set_text_fmt(s_date_label, "%d年%02d月%02d日", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        weather_app_set_week(&timeinfo);

        if (snapshot.daily_ready) {
            if ((timeinfo.tm_sec / 5) % 2 == 0) {
                lv_label_set_text_fmt(s_sun_label, "日落 %s", snapshot.sunset);
            } else {
                lv_label_set_text_fmt(s_sun_label, "日出 %s", snapshot.sunrise);
            }
        }
    } else {
        lv_label_set_text(s_led_time_label, "--:--:--");
        lv_label_set_text(s_date_label, "----年--月--日");
        lv_label_set_text(s_week_label, "星期日");
    }

    if (snapshot.weather_ready) {
        weather_app_set_icon_and_text(snapshot.now_icon);
        lv_label_set_text_fmt(s_outdoor_temp_label, "%d℃", snapshot.now_temp);
        lv_label_set_text_fmt(s_outdoor_humi_label, "%d%%", snapshot.now_humi);
    } else {
        lv_label_set_text(s_qweather_icon_label, "\xEF\x85\x86");
        lv_label_set_text(s_qweather_text_label, "未知");
        lv_label_set_text(s_outdoor_temp_label, "--℃");
        lv_label_set_text(s_outdoor_humi_label, "--%");
    }

    if (snapshot.daily_ready) {
        lv_label_set_text_fmt(s_qweather_temp_label, "%d~%d℃", snapshot.daily_temp_min, snapshot.daily_temp_max);
    } else {
        lv_label_set_text(s_qweather_temp_label, "--~--℃");
    }

    if (snapshot.air_ready) {
        weather_app_set_air_level(snapshot.air_level);
    } else {
        weather_app_set_air_level(0);
    }

    lv_label_set_text_fmt(s_indoor_temp_label, "%d℃", temp_value);
    lv_label_set_text_fmt(s_indoor_humi_label, "%d%%", humi_value);

    if (snapshot.weather_ready) {
        lv_label_set_text(s_status_label, "");
    } else {
        lv_label_set_text(s_status_label, snapshot.status[0] ? snapshot.status : "正在获取天气信息");
    }

    LV_UNUSED(timer);
}

static lv_obj_t *weather_app_create_card(lv_obj_t *parent, lv_color_t color, lv_align_t align, lv_coord_t x_ofs)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 100, 80);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_bg_color(card, color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 5, 0);
    lv_obj_align(card, align, x_ofs, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

void weather_app_open(lv_obj_t *root)
{
    weather_app_close();

    weather_snapshot_t snapshot;
    weather_service_get_snapshot(&snapshot);

    bool has_time = false;
    time_t now = 0;
    struct tm timeinfo = {0};
    if (snapshot.time_synced) {
        time(&now);
        localtime_r(&now, &timeinfo);
        has_time = true;
    }

    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 320, 240);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x00BFFF), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, 10, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 10, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *addr_label = lv_label_create(root);
    lv_obj_set_style_text_font(addr_label, &font_alipuhui, 0);
    lv_label_set_text(addr_label, "深圳市|宝安区");
    lv_obj_align(addr_label, LV_ALIGN_TOP_LEFT, 0, 0);

    s_date_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_date_label, &font_alipuhui, 0);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *above_bar = lv_bar_create(root);
    lv_obj_set_size(above_bar, 300, 3);
    lv_obj_set_pos(above_bar, 0, 30);
    lv_bar_set_value(above_bar, 100, LV_ANIM_OFF);

    s_qweather_icon_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_qweather_icon_label, &font_qweather, 0);
    lv_obj_set_pos(s_qweather_icon_label, 0, 40);
    weather_app_set_icon_and_text(snapshot.weather_ready ? snapshot.now_icon : -1);

    s_qair_level_obj = lv_obj_create(root);
    lv_obj_set_size(s_qair_level_obj, 50, 26);
    lv_obj_set_style_radius(s_qair_level_obj, 10, 0);
    lv_obj_set_style_border_width(s_qair_level_obj, 0, 0);
    lv_obj_set_style_pad_all(s_qair_level_obj, 0, 0);
    lv_obj_align_to(s_qair_level_obj, s_qweather_icon_label, LV_ALIGN_OUT_RIGHT_TOP, 5, 0);
    lv_obj_clear_flag(s_qair_level_obj, LV_OBJ_FLAG_SCROLLABLE);

    s_qair_level_label = lv_label_create(s_qair_level_obj);
    lv_obj_set_style_text_font(s_qair_level_label, &font_alipuhui, 0);
    lv_obj_align(s_qair_level_label, LV_ALIGN_CENTER, 0, 0);

    s_qweather_temp_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_qweather_temp_label, &font_alipuhui, 0);
    if (snapshot.daily_ready) {
        lv_label_set_text_fmt(s_qweather_temp_label, "%d~%dC", snapshot.daily_temp_min, snapshot.daily_temp_max);
    } else {
        lv_label_set_text(s_qweather_temp_label, "--~--C");
    }
    lv_obj_align_to(s_qweather_temp_label, s_qweather_icon_label, LV_ALIGN_OUT_RIGHT_MID, 5, 5);

    s_qweather_text_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_qweather_text_label, &font_alipuhui, 0);
    lv_label_set_long_mode(s_qweather_text_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_qweather_text_label, 80);
    weather_app_set_icon_and_text(snapshot.weather_ready ? snapshot.now_icon : -1);
    lv_obj_align_to(s_qweather_text_label, s_qweather_icon_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, 0);

    s_led_time_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_led_time_label, &font_led, 0);
    if (has_time) {
        lv_label_set_text_fmt(s_led_time_label, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        lv_label_set_text(s_led_time_label, "--:--:--");
    }
    lv_obj_set_pos(s_led_time_label, 142, 42);

    s_week_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_week_label, &font_alipuhui, 0);
    if (has_time) {
        weather_app_set_week(&timeinfo);
    } else {
        lv_label_set_text(s_week_label, "Week");
    }
    lv_obj_align_to(s_week_label, s_led_time_label, LV_ALIGN_OUT_BOTTOM_RIGHT, -10, 6);

    s_sun_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_sun_label, &font_alipuhui, 0);
    if (snapshot.daily_ready) {
        if (has_time && ((timeinfo.tm_sec / 5) % 2 != 0)) {
            lv_label_set_text_fmt(s_sun_label, "Sunrise %s", snapshot.sunrise);
        } else {
            lv_label_set_text_fmt(s_sun_label, "Sunset %s", snapshot.sunset);
        }
    } else {
        lv_label_set_text(s_sun_label, "");
    }
    lv_obj_set_pos(s_sun_label, 190, 103);

    lv_obj_t *below_bar = lv_bar_create(root);
    lv_obj_set_size(below_bar, 300, 3);
    lv_obj_set_pos(below_bar, 0, 130);
    lv_bar_set_value(below_bar, 100, LV_ANIM_OFF);

    lv_obj_t *outdoor_obj = weather_app_create_card(root, lv_color_hex(0xD8B010), LV_ALIGN_BOTTOM_LEFT, 0);
    lv_obj_t *outdoor_title = lv_label_create(outdoor_obj);
    lv_obj_set_style_text_font(outdoor_title, &font_alipuhui, 0);
    lv_label_set_text(outdoor_title, "室外");
    lv_obj_align(outdoor_title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *outdoor_temp_icon = lv_label_create(outdoor_obj);
    lv_obj_set_style_text_font(outdoor_temp_icon, &font_myawesome, 0);
    lv_label_set_text(outdoor_temp_icon, "\xEF\x8B\x88");
    lv_obj_align(outdoor_temp_icon, LV_ALIGN_LEFT_MID, 10, 0);

    s_outdoor_temp_label = lv_label_create(outdoor_obj);
    lv_obj_set_style_text_font(s_outdoor_temp_label, &font_alipuhui, 0);
    lv_obj_align_to(s_outdoor_temp_label, outdoor_temp_icon, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *outdoor_humi_icon = lv_label_create(outdoor_obj);
    lv_obj_set_style_text_font(outdoor_humi_icon, &font_myawesome, 0);
    lv_label_set_text(outdoor_humi_icon, "\xEF\x81\x83");
    lv_obj_align(outdoor_humi_icon, LV_ALIGN_BOTTOM_LEFT, 10, 0);

    s_outdoor_humi_label = lv_label_create(outdoor_obj);
    lv_obj_set_style_text_font(s_outdoor_humi_label, &font_alipuhui, 0);
    lv_obj_align_to(s_outdoor_humi_label, outdoor_humi_icon, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *indoor_obj = weather_app_create_card(root, lv_color_hex(0xFE6464), LV_ALIGN_BOTTOM_MID, 10);
    lv_obj_t *indoor_title = lv_label_create(indoor_obj);
    lv_obj_set_style_text_font(indoor_title, &font_alipuhui, 0);
    lv_label_set_text(indoor_title, "室内");
    lv_obj_align(indoor_title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *indoor_temp_icon = lv_label_create(indoor_obj);
    lv_obj_set_style_text_font(indoor_temp_icon, &font_myawesome, 0);
    lv_label_set_text(indoor_temp_icon, "\xEF\x8B\x88");
    lv_obj_align(indoor_temp_icon, LV_ALIGN_LEFT_MID, 10, 0);

    s_indoor_temp_label = lv_label_create(indoor_obj);
    lv_obj_set_style_text_font(s_indoor_temp_label, &font_alipuhui, 0);
    lv_obj_align_to(s_indoor_temp_label, indoor_temp_icon, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *indoor_humi_icon = lv_label_create(indoor_obj);
    lv_obj_set_style_text_font(indoor_humi_icon, &font_myawesome, 0);
    lv_label_set_text(indoor_humi_icon, "\xEF\x81\x83");
    lv_obj_align(indoor_humi_icon, LV_ALIGN_BOTTOM_LEFT, 10, 0);

    s_indoor_humi_label = lv_label_create(indoor_obj);
    lv_obj_set_style_text_font(s_indoor_humi_label, &font_alipuhui, 0);
    lv_obj_align_to(s_indoor_humi_label, indoor_humi_icon, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    lv_obj_t *tk_gif = lv_gif_create(root);
    lv_gif_set_src(tk_gif, &image_taikong);
    lv_obj_align(tk_gif, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_move_background(tk_gif);

    s_status_label = lv_label_create(root);
    lv_obj_set_style_text_font(s_status_label, &font_alipuhui, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x404040), 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_LEFT, 6, -8);

    weather_app_refresh(NULL);
    s_weather_timer = lv_timer_create(weather_app_refresh, 1000, NULL);
}

void weather_app_close(void)
{
    if (s_weather_timer != NULL) {
        lv_timer_del(s_weather_timer);
        s_weather_timer = NULL;
    }

    s_status_label = NULL;
    s_qweather_icon_label = NULL;
    s_qweather_temp_label = NULL;
    s_qweather_text_label = NULL;
    s_qair_level_obj = NULL;
    s_qair_level_label = NULL;
    s_led_time_label = NULL;
    s_week_label = NULL;
    s_sun_label = NULL;
    s_outdoor_temp_label = NULL;
    s_outdoor_humi_label = NULL;
    s_indoor_temp_label = NULL;
    s_indoor_humi_label = NULL;
    s_date_label = NULL;
}
