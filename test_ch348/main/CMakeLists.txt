idf_component_register(SRCS "test_ch348.c" "../../firmware/lib/ch348/src/ch348.c"
                    INCLUDE_DIRS "../../firmware/lib/ch348/src"
                    PRIV_REQUIRES usb esp_timer
                    )
