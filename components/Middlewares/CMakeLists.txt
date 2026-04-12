set(src_dirs
        TTS
        ASR
        WIFI
        LLM)

set(include_dirs
        .
        TTS
        ASR
        WIFI
        LLM)

set(requires
        driver
        esp_wifi
        esp_event
        esp_netif
        esp_http_client
        json
        BSP
        nvs_flash
        mbedtls
        esp_timer)

idf_component_register(SRC_DIRS "${src_dirs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES "${requires}")

component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)
