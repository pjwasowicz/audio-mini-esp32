cmake_minimum_required(VERSION 3.16)
# cspot i jego biblioteki deklarują stare wersje CMake (<3.5), których CMake 4 już nie przyjmuje
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ustawienia: sdkconfig.defaults + sdkconfig.local (Wi-Fi, nie trafia do repozytorium)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.local")
endif()

# numer wersji programu (wyświetlacz po starcie, /api/system): przy każdej zmianie +0.1
set(PROJECT_VER "1.4")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(audioesp)
