idf_component_register(SRCS "power_mgmt.c" "qmc5883l.c" "qmi8658c.c" "myi2c.c" "gxhtc3.c" "spi_lcd_touch_example_main.c" "lvgl_demo_ui.c" "audio.c" "weather_app.c" "weather_service.c"
                       SRCS "font_alipuhui.c" "font_qweather.c" "font_led.c" "image_taikong.c"
                       SRCS "assets/image_lckfb_logo.c" "assets/image_th_icon.c" "assets/image_spr_icon.c" "assets/image_mic_icon.c"
                       SRCS "assets/image_att_icon.c" "assets/image_comp_icon.c" "assets/image_set_icon.c" "assets/font_alipuhui20.c" "assets/font_myawesome.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES "sword.pcm")
