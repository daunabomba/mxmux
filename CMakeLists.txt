cmake_minimum_required(VERSION 4.0)
set(PROJECT mxmux)
project(${PROJECT})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if(CMAKE_BUILD_TYPE MATCHES DEBUG)
    message(STATUS "Debug build: adding -ggdb flag")
    add_compile_options(-ggdb)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-fmodules-ts)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-fmodules)
endif()

set(INCLUDE_DIRECTORIES src)
set(MODULE_FILES src/evdev.cppm src/engine.cppm src/event.cppm src/evinputdev.cppm src/kbdinputdev.cppm src/logger.cppm src/mouseinputdev.cppm src/mxsrv.cppm src/router.cppm src/smtpproxy.cppm src/semaphore.cppm src/socket.cppm src/tcplistener.cppm src/tcpstream.cppm src/timer.cppm src/udpsocket.cppm src/utils.cppm)
set(HEADER_FILES src/endians.h src/exception.h src/types.h)
set(SOURCE_FILES src/main.cpp)


add_executable(${PROJECT} ${SOURCE_FILES} ${HEADER_FILES} ${MODULE_FILES})

target_sources(${PROJECT} 
  PRIVATE
  FILE_SET
    cxx_modules TYPE CXX_MODULES FILES ${MODULE_FILES}
)
install(TARGETS ${PROJECT})
