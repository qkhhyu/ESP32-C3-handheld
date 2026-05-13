/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

// This demo UI is adapted from LVGL official example: https://docs.lvgl.io/master/widgets/extra/meter.html#simple-meter

#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main.h"
#include "weather_app.h"
#include "power_mgmt.h"
#include "chip_power.h"

LV_FONT_DECLARE(font_alipuhui20);
LV_FONT_DECLARE(font_myawesome);

lv_obj_t * main_obj;


/*********************** 开机界面 ****************************/
lv_obj_t * lckfb_logo;
// 设置图片旋转角度的回调函数
static void set_angle(void * img, int32_t v)
{
    lv_img_set_angle(img, v); // 设置图片的旋转角度
}
// 开机界面
void lv_gui_start(void)
{
    // 显示logo
    LV_IMG_DECLARE(image_lckfb_logo);  // 声明图片
    lckfb_logo = lv_img_create(lv_scr_act()); // 创建图片对象
    lv_img_set_src(lckfb_logo, &image_lckfb_logo); // 设置图片对象的图片源
    lv_obj_align(lckfb_logo, LV_ALIGN_CENTER, 0, 0); // 设置图片位置为屏幕正中心
    lv_img_set_pivot(lckfb_logo, 60, 60); // 设置图片围绕自身中心旋转

    // 设置旋转动画
    lv_anim_t a; // 创建动画变量
    lv_anim_init(&a); // 初始化动画变量
    lv_anim_set_var(&a, lckfb_logo); // 动画变量赋值为logo图片
    lv_anim_set_exec_cb(&a, set_angle); // 设置旋转执行回调
    lv_anim_set_values(&a, 0, 3600); // 设置动画旋转角度范围
    lv_anim_set_time(&a, 200); // 设置单圈动画周期为200毫秒
    lv_anim_set_repeat_count(&a, 5); // 设置旋转5次
    lv_anim_start(&a); // 启动动画
}

/******************************** 触摸屏手势处理 ******************************/
lv_obj_t * icon_in_obj;
lv_timer_t * my_lv_timer;
int icon_flag; 
int temp_value, humi_value;

// 手势处理函数
static void my_gesture_event_cb(lv_event_t * e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if(dir == LV_DIR_TOP)
    {
        if(icon_flag == 1)
        {
            chip_power_exit_low_power();
            weather_app_close();
        }
        if((icon_flag == 2)||(icon_flag == 4)||(icon_flag == 5))
        {
            lv_timer_del(my_lv_timer);
            my_lv_timer = NULL;
        }
        lv_obj_del(icon_in_obj); 
        icon_flag = 0;
    }
}

/******************************** 第一个图标 天气桌面应用程序 ******************************/

static void th_event_handler(lv_event_t * e)
{
    chip_power_enter_low_power();

    icon_in_obj = lv_obj_create(lv_scr_act());
    icon_flag = 1;
    weather_app_open(icon_in_obj);

    lv_obj_t * label = lv_label_create(icon_in_obj);
    lv_label_set_text(label, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

    lv_obj_add_event_cb(icon_in_obj, my_gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(icon_in_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(icon_in_obj, LV_OBJ_FLAG_CLICKABLE);

    LV_UNUSED(e);
}

/******************************** 第二个图标 弹力球应用程序 ******************************/
lv_obj_t * mat;   // 弹力球的垫子
lv_obj_t * ball;  // 创建一个弹力球
int mat_flag;     // 弹力球标志位
int ball_height = 0; // 弹力球的高度
int ball_dir = 0; // 弹力球的方向
int mat_height = 0; // 垫子的高度

// 弹力球动画更新函数
void game_update_cb(lv_timer_t * timer)
{
    if (strength != 0)  // 发现手指按下屏幕
    {
        if(strength < 31) // 限制手指按下时间最大为30
        {
            mat_height = 60 - strength;  // 计算正方体的高度
            lv_obj_set_size(mat, 80, mat_height); // 调整正方体的高度
            lv_obj_align_to(ball, mat, LV_ALIGN_OUT_TOP_MID, 0, 0); // 让弹力球跟随垫子一起移动
            mat_flag = 1; // 表示垫子已经缩小
        }
    }
    else if(mat_flag == 1) // 如果垫子已经缩小
    {
        lv_obj_set_size(mat, 80, 60); // 垫子回弹到原始大小
        lv_obj_align_to(ball, mat, LV_ALIGN_OUT_TOP_MID, 0, 0); // 弹力球继续跟随垫子
        mat_flag = 2; // 标记垫子已经回弹
    }
    else if(mat_flag == 2) // 垫子已经回弹 小球应该向上走了
    {
        if (ball_dir == 0) // 向上运动
        {
            if(ball_height < 150) // 限制弹力球上弹高度为150像素
            {
                ball_height = ball_height + 10; // 每次上升10个像素
                lv_obj_align_to(ball, mat, LV_ALIGN_OUT_TOP_MID, 0, -ball_height); // 更新弹力球位置
                if (ball_height >= (0 + (60-mat_height)*5)) // 根据力度计算小球最大高度
                {
                    ball_dir = 1;  // 到达最高点后改为向下运动
                }
            }
        }
        else // 向下运动
        {
            if(ball_height > 0) // 限制小球最低高度
            {
                ball_height = ball_height - 10; // 每次降低10个像素高度
                lv_obj_align_to(ball, mat, LV_ALIGN_OUT_TOP_MID, 0, -ball_height); // 更新弹力球高度
                if (ball_height == 0) // 如果弹力球落回垫子
                {
                    mat_flag = 0; // 垫子状态恢复
                    ball_dir = 0; // 小球方向恢复
                    mat_height = 0; // 垫子高度恢复
                }
            }
        }
    }
}

// 第二个图标 弹力球应用程序
static void game_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xcccccc));
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建一个垫子
    static lv_style_t mat_style;
    lv_style_init(&mat_style);
    lv_style_set_radius(&mat_style, 0);  
    lv_style_set_border_width(&mat_style, 0);
    lv_style_set_pad_all(&mat_style, 0);
    lv_style_set_shadow_width(&mat_style, 10);
    lv_style_set_shadow_color(&mat_style, lv_color_black());
    lv_style_set_shadow_ofs_x(&mat_style, 10);
    lv_style_set_shadow_ofs_y(&mat_style, 10);

    mat = lv_obj_create(icon_in_obj);
    lv_obj_add_style(mat, &mat_style, 0);
    lv_obj_set_style_bg_color(mat, lv_color_hex(0x6B8E23), 0);
    lv_obj_align(mat, LV_ALIGN_BOTTOM_LEFT, 30, -30);
    lv_obj_set_size(mat, 80, 60);

    // 创建一个圆球
    ball  = lv_led_create(icon_in_obj);
    lv_led_set_brightness(ball, 150);
    lv_led_set_color(ball, lv_palette_main(LV_PALETTE_DEEP_ORANGE));
    lv_obj_align_to(ball, mat, LV_ALIGN_OUT_TOP_MID, 0, 0);

    // 创建一个 lv_timer 用于更新圆球坐标
    icon_flag = 2; // 标记已经进入第二个图标应用
    my_lv_timer = lv_timer_create(game_update_cb, 50, NULL);  // 
    
    // 绘制退出提示符
    lv_obj_t * label = lv_label_create(icon_in_obj);
    lv_label_set_text(label, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

    // 添加向上滑动退出功能
    lv_obj_add_event_cb(icon_in_obj, my_gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(icon_in_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(icon_in_obj, LV_OBJ_FLAG_CLICKABLE); 
}

/******************************** 第三个图标 回声应用程序 ******************************/
static void echo_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0x00BFFF));
    lv_style_set_bg_grad_color( &style, lv_color_hex( 0x00BF00 ) );
    lv_style_set_bg_grad_dir( &style, LV_GRAD_DIR_VER );
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 创建一个文字label
    lv_obj_t * text_label = lv_label_create(icon_in_obj);
    lv_obj_set_style_text_color(text_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(text_label, &font_alipuhui20, 0);
    lv_obj_align(text_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(text_label, "请对准USB旁边的麦克风说话");

    // 创建一个音乐回声任务
    icon_flag = 3; // 标记已经进入第三个图标应用
    xTaskCreate(echo_task, "music_echo_task", 8192, NULL, 5, NULL);
    
    // 绘制退出提示符
    lv_obj_t * label = lv_label_create(icon_in_obj);
    lv_label_set_text(label, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

    // 添加向上滑动退出功能
    lv_obj_add_event_cb(icon_in_obj, my_gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(icon_in_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(icon_in_obj, LV_OBJ_FLAG_CLICKABLE);
    
}

/******************************** 第四个图标 水平仪应用程序 ******************************/
lv_obj_t * att_x_label;
lv_obj_t * att_y_label;
lv_obj_t * att_led;

// 定时更新水平仪坐标值
void att_update_cb(lv_timer_t * timer)
{
    t_sQMI8658C QMI8658C;
    int att_led_x, att_led_y;

    qmi8658c_fetch_angleFromAcc(&QMI8658C);
    att_led_x = round(QMI8658C.AngleX);
    att_led_y = round(QMI8658C.AngleY);
    lv_obj_align(att_led, LV_ALIGN_CENTER, -att_led_x, att_led_y);
    lv_label_set_text_fmt(att_x_label, "X=%d°", -att_led_x);
    lv_label_set_text_fmt(att_y_label, "Y=%d°", att_led_y);
}

// 水平仪应用程序
static void att_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0x00BFFF));
    lv_style_set_bg_grad_color( &style, lv_color_hex( 0x00BF00 ) );
    lv_style_set_bg_grad_dir( &style, LV_GRAD_DIR_VER );
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 画一个外圆
    lv_obj_t * arc = lv_arc_create(icon_in_obj);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);   
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE); 
    lv_obj_center(arc);
    lv_arc_set_value(arc, 360);
    lv_obj_set_size(arc, 200, 200);

    // 画一个实心圆点
    att_led  = lv_led_create(icon_in_obj);
    lv_obj_align(att_led, LV_ALIGN_CENTER, 0, 0);
    lv_led_set_brightness(att_led, 255);
    lv_led_set_color(att_led, lv_palette_main(LV_PALETTE_RED));

    // 显示 X 和 Y 的角度
    att_x_label = lv_label_create(icon_in_obj);
    lv_obj_set_style_text_font(att_x_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(att_x_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(att_x_label, LV_ALIGN_TOP_LEFT, 20, 20);

    att_y_label = lv_label_create(icon_in_obj);
    lv_obj_set_style_text_font(att_y_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(att_y_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(att_y_label, LV_ALIGN_TOP_RIGHT, -20, 20);

    // 创建一个 lv_timer 用于更新圆点坐标
    icon_flag = 4; // 标记已经进入第四个图标应用
    my_lv_timer = lv_timer_create(att_update_cb, 100, NULL); 
    
    // 绘制退出提示符
    lv_obj_t * label = lv_label_create(icon_in_obj);
    lv_label_set_text(label, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

    // 添加向上滑动退出功能
    lv_obj_add_event_cb(icon_in_obj, my_gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(icon_in_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(icon_in_obj, LV_OBJ_FLAG_CLICKABLE);
}

/******************************** 第五个图标 指南针应用程序 ******************************/
lv_obj_t * comp_label;
lv_obj_t * compass_meter;
lv_meter_scale_t * compass_scale;

// 定时更新方位角数值
void comp_update_cb(lv_timer_t * timer)
{
    t_sQMC5883L QMC5883L;
    int comp_angle;

    qmc5883l_fetch_azimuth(&QMC5883L);
    comp_angle = round(QMC5883L.azimuth);
    comp_angle = (comp_angle + 120)%360;  // 校准角度，修正正北方向
    lv_label_set_text_fmt(comp_label, "%d°", comp_angle);
    comp_angle = 360 - (comp_angle+90)%360;  // 计算旋转角度
    lv_meter_set_scale_range(compass_meter, compass_scale, 0, 360, 360, comp_angle); 
}
// 指南针应用程序
static void comp_event_handler(lv_event_t * e)
{
    // 创建一个界面对象
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10); 
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0x00BFFF));
    lv_style_set_bg_grad_color( &style, lv_color_hex(0x00BF00));
    lv_style_set_bg_grad_dir( &style, LV_GRAD_DIR_VER );
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 绘制指南针仪表
    compass_meter = lv_meter_create(icon_in_obj);
    lv_obj_center(compass_meter);
    lv_obj_set_size(compass_meter, 220, 220);
    lv_obj_set_style_bg_opa(compass_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(compass_meter, lv_color_hex(0xffffff), 0);
    lv_obj_remove_style(compass_meter, NULL, LV_PART_INDICATOR);

    compass_scale = lv_meter_add_scale(compass_meter);
    lv_meter_set_scale_ticks(compass_meter, compass_scale, 61, 1, 10, lv_color_hex(0xffffff)); 
    lv_meter_set_scale_major_ticks(compass_meter, compass_scale, 10, 2, 16, lv_color_hex(0xffffff), 10); 
    lv_meter_set_scale_range(compass_meter, compass_scale, 0, 360, 360, 270); 
    
    // 添加指针
    lv_obj_t * arrow_label = lv_label_create(icon_in_obj);
    lv_label_set_text(arrow_label, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(arrow_label, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(arrow_label, LV_ALIGN_CENTER, 0, -100);

    // 显示方位角度
    comp_label = lv_label_create(icon_in_obj);
    lv_obj_set_style_text_font(comp_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(comp_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(comp_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(comp_label, "0");

    // 创建一个 lv_timer 用于更新方位角
    icon_flag = 5; // 标记已经进入第五个图标应用
    my_lv_timer = lv_timer_create(comp_update_cb, 100, NULL);  
    
    // 绘制退出提示符
    lv_obj_t * label = lv_label_create(icon_in_obj);
    lv_label_set_text(label, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

    // 添加向上滑动退出功能
    lv_obj_add_event_cb(icon_in_obj, my_gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(icon_in_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(icon_in_obj, LV_OBJ_FLAG_CLICKABLE);
}

/******************************** 第六个图标 设置应用程序 ******************************/
static void power_save_switch_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    power_mgmt_set_enabled(enabled);
}

static void slider_event_cb(lv_event_t * e)
{
    int x;

    lv_obj_t * slider = lv_event_get_target(e);
    lv_slider_set_range(slider, 10, 80);
    x = lv_slider_get_value(slider);
    bg_duty = (float)x/100; // 根据滑动条的值计算占空比
    // 设置占空比
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 8191*(1-bg_duty));
    // 更新背光
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    power_mgmt_set_brightness(bg_duty);
}

// 进入设置界面
static void set_event_handler(lv_event_t * e)
{
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0xE680FF));
    lv_style_set_bg_grad_color( &style, lv_color_hex( 0xE68000 ) );
    lv_style_set_bg_grad_dir( &style, LV_GRAD_DIR_VER );
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);
    lv_style_set_height(&style, 240);

    icon_in_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(icon_in_obj, &style, 0);

    // 节能模式开关
    lv_obj_t * power_label = lv_label_create(icon_in_obj);
    lv_obj_set_style_text_color(power_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(power_label, &font_alipuhui20, 0);
    lv_obj_align(power_label, LV_ALIGN_CENTER, 0, -80);
    lv_label_set_text(power_label, "节能模式");

    lv_obj_t * power_hint = lv_label_create(icon_in_obj);
    lv_obj_set_style_text_color(power_hint, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(power_hint, &font_alipuhui20, 0);
    lv_obj_align(power_hint, LV_ALIGN_CENTER, 0, -58);
    lv_label_set_text(power_hint, "18:30熄屏，触屏唤醒亮1分钟");

    lv_obj_t * power_switch = lv_switch_create(icon_in_obj);
    lv_obj_align(power_switch, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(power_switch, power_save_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (power_mgmt_is_enabled()) {
        lv_obj_add_state(power_switch, LV_STATE_CHECKED);
    }

    // 亮度调节
    lv_obj_t * text_label = lv_label_create(icon_in_obj);
    lv_obj_set_style_text_color(text_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(text_label, &font_alipuhui20, 0);
    lv_obj_align(text_label, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_text(text_label, "滑动调节屏幕亮度");

    lv_obj_t * slider = lv_slider_create(icon_in_obj);
    lv_slider_set_value(slider, bg_duty*100 , 0);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 55);
    lv_obj_set_height(slider, 50);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    icon_flag = 6; // 标记已经进入第六个图标应用

    // 绘制退出提示符
    lv_obj_t * label = lv_label_create(icon_in_obj);
    lv_label_set_text(label, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

    // 添加向上滑动退出功能
    lv_obj_add_event_cb(icon_in_obj, my_gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_clear_flag(icon_in_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(icon_in_obj, LV_OBJ_FLAG_CLICKABLE);

}


/******************************** 主界面 ******************************/

// 主界面
void lv_main_page(void)
{
    lv_obj_del(lckfb_logo); // 删除开机logo

    // 创建主界面基础对象
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0); // 修改背景为黑色

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_radius(&style, 10);  
    lv_style_set_bg_opa( &style, LV_OPA_COVER );
    lv_style_set_bg_color(&style, lv_color_hex(0x00BFFF));
    lv_style_set_bg_grad_color( &style, lv_color_hex( 0x00BF00 ) );
    lv_style_set_bg_grad_dir( &style, LV_GRAD_DIR_VER );
    lv_style_set_border_width(&style, 0);
    lv_style_set_pad_all(&style, 0);
    lv_style_set_width(&style, 320);  
    lv_style_set_height(&style, 240); 

    main_obj = lv_obj_create(lv_scr_act());
    lv_obj_add_style(main_obj, &style, 0);

    // 显示右上角状态图标
    lv_obj_t * sylbom_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(sylbom_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sylbom_label, lv_color_hex(0xffffff), 0);
    lv_label_set_text(sylbom_label, LV_SYMBOL_CALL" "LV_SYMBOL_USB" "LV_SYMBOL_GPS" "LV_SYMBOL_BLUETOOTH" "LV_SYMBOL_WIFI" "LV_SYMBOL_BATTERY_FULL );
    lv_obj_align_to(sylbom_label, main_obj, LV_ALIGN_TOP_RIGHT, -8, 8);

    // 显示左上角欢迎语
    lv_obj_t * text_label = lv_label_create(main_obj);
    lv_obj_set_style_text_font(text_label, &font_alipuhui20, 0);
    lv_label_set_long_mode(text_label, LV_LABEL_LONG_SCROLL_CIRCULAR);     /*Circular scroll*/
    lv_obj_set_width(text_label, 120);
    lv_label_set_text(text_label, "广告位招租，联系钟羽邮箱qkhhyu@163.com");
    lv_obj_align_to(text_label, main_obj, LV_ALIGN_TOP_LEFT, 8, 8);

    // 设置应用图标style
    static lv_style_t btn_style;
    lv_style_init(&btn_style);
    lv_style_set_radius(&btn_style, 16);  
    lv_style_set_bg_opa( &btn_style, LV_OPA_COVER );
    lv_style_set_text_color(&btn_style, lv_color_hex(0xffffff)); 
    lv_style_set_border_width(&btn_style, 0);
    lv_style_set_pad_all(&btn_style, 5);
    lv_style_set_width(&btn_style, 80);  
    lv_style_set_height(&btn_style, 80); 

    // 创建第一个应用图标
    lv_obj_t * icon1 = lv_btn_create(main_obj);
    lv_obj_add_style(icon1, &btn_style, 0);
    lv_obj_set_style_bg_color(icon1, lv_color_hex(0x30a830), 0);
    lv_obj_set_pos(icon1, 15, 50);
    lv_obj_add_event_cb(icon1, th_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img1 = lv_img_create(icon1);
    LV_IMG_DECLARE(image_th_icon);
    lv_img_set_src(img1, &image_th_icon);
    lv_obj_align(img1, LV_ALIGN_CENTER, 0, 0);

    // 创建第二个应用图标
    lv_obj_t * icon2 = lv_btn_create(main_obj);
    lv_obj_add_style(icon2, &btn_style, 0);
    lv_obj_set_style_bg_color(icon2, lv_color_hex(0xf87c30), 0);
    lv_obj_set_pos(icon2, 120, 50);
    lv_obj_add_event_cb(icon2, game_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img2 = lv_img_create(icon2);
    LV_IMG_DECLARE(image_spr_icon);
    lv_img_set_src(img2, &image_spr_icon);
    lv_obj_align(img2, LV_ALIGN_CENTER, 0, 0);

    // 创建第三个应用图标
    lv_obj_t * icon3 = lv_btn_create(main_obj);
    lv_obj_add_style(icon3, &btn_style, 0);
    lv_obj_set_style_bg_color(icon3, lv_color_hex(0x008b8b), 0);
    lv_obj_set_pos(icon3, 225, 50);
    lv_obj_add_event_cb(icon3, echo_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img3 = lv_img_create(icon3);
    LV_IMG_DECLARE(image_mic_icon);
    lv_img_set_src(img3, &image_mic_icon);
    lv_obj_align(img3, LV_ALIGN_CENTER, 0, 0);

    // 创建第四个应用图标
    lv_obj_t * icon4 = lv_btn_create(main_obj);
    lv_obj_add_style(icon4, &btn_style, 0);
    lv_obj_set_style_bg_color(icon4, lv_color_hex(0xd8b010), 0);
    lv_obj_set_pos(icon4, 15, 147);
    lv_obj_add_event_cb(icon4, att_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img4 = lv_img_create(icon4);
    LV_IMG_DECLARE(image_att_icon);
    lv_img_set_src(img4, &image_att_icon);
    lv_obj_align(img4, LV_ALIGN_CENTER, 0, 0);

    // 创建第五个应用图标
    lv_obj_t * icon5 = lv_btn_create(main_obj);
    lv_obj_add_style(icon5, &btn_style, 0);
    lv_obj_set_style_bg_color(icon5, lv_color_hex(0xcd5c5c), 0);
    lv_obj_set_pos(icon5, 120, 147);
    lv_obj_add_event_cb(icon5, comp_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img5 = lv_img_create(icon5);
    LV_IMG_DECLARE(image_comp_icon);
    lv_img_set_src(img5, &image_comp_icon);
    lv_obj_align(img5, LV_ALIGN_CENTER, 0, 0);

    // 创建第六个应用图标
    lv_obj_t * icon6 = lv_btn_create(main_obj);
    lv_obj_add_style(icon6, &btn_style, 0);
    lv_obj_set_style_bg_color(icon6, lv_color_hex(0xb87fa8), 0);
    lv_obj_set_pos(icon6, 225, 147);
    lv_obj_add_event_cb(icon6, set_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t * img6 = lv_img_create(icon6);
    LV_IMG_DECLARE(image_set_icon);
    lv_img_set_src(img6, &image_set_icon);
    lv_obj_align(img6, LV_ALIGN_CENTER, 0, 0);

}


