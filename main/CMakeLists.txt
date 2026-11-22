file(GLOB SOURCES "*.cpp")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event esp_timer nvs_flash esp_driver_i2c esp_driver_pcnt esp_driver_gpio esp_driver_rmt esp_http_server app_update esp_app_format
             esp_driver_i2s
    EMBED_TXTFILES index.html
)

# biblioteki dołączone do cspot (opus itd.) dają ostrzeżenia, które nowszy GCC traktuje jako błędy
idf_build_set_property(COMPILE_OPTIONS "-Wno-error;-Wno-error=array-bounds;-Wno-array-bounds;-Wno-error=stringop-overflow;-Wno-stringop-overflow;-Wno-sizeof-array-div;-Wno-overloaded-virtual" APPEND)

# cspot jako biblioteka statyczna, bez jego starych wyjść audio (legacy I2S)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(BELL_DISABLE_SINKS ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../cspot/cspot" ${CMAKE_CURRENT_BINARY_DIR}/cspot)

target_link_libraries(${COMPONENT_LIB} PUBLIC cspot)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++20)
