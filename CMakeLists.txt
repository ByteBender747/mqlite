cmake_minimum_required(VERSION 3.15)

# Project name, version and language 
project(mqlite VERSION 0.0.1 LANGUAGES C CXX)

# Set compiler standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_BOARD STREQUAL "pico_w")
    set(USE_TYPE INTERFACE)
    set(USE_LWIP True)
else()
    set(USE_TYPE PRIVATE)
    set(USE_LWIP False)
endif()

if (PICO_BOARD STREQUAL "pico_w")
    add_library(${PROJECT_NAME} INTERFACE)
else()
    add_library(${PROJECT_NAME})
endif()

target_sources(${PROJECT_NAME} ${USE_TYPE}
    ${CMAKE_CURRENT_LIST_DIR}/src/mqtt_client.c
    ${CMAKE_CURRENT_LIST_DIR}/src/utf8.c
    ${CMAKE_CURRENT_LIST_DIR}/src/ident.c
)
if (${USE_LWIP})
    target_sources(${PROJECT_NAME} ${USE_TYPE}
        ${CMAKE_CURRENT_LIST_DIR}/src/mqtt_lwip.c
    )
else()
    target_sources(${PROJECT_NAME} ${USE_TYPE}
        ${CMAKE_CURRENT_LIST_DIR}/src/mqtt_socket.c
    )
endif()

target_include_directories(${PROJECT_NAME} ${USE_TYPE}
    ${CMAKE_CURRENT_LIST_DIR}/include
    )

if (${USE_LWIP})
    target_link_libraries(${PROJECT_NAME} ${USE_TYPE}
        pico_cyw43_arch_lwip_poll
        )
endif()