set(src_dirs
        I2S
        SPI
        LV_DRIVER
        LCD
        UI)

set(include_dirs
        I2S
        SPI
        LV_DRIVER
        LCD
        UI)

set(requires
        driver
        esp_timer
        lvgl__lvgl)

idf_component_register(SRC_DIRS "${src_dirs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES "${requires}")

component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)
