cmake_minimum_required(VERSION 4.0)
set(PROJECT mxev)
project(${PROJECT})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wformat=2 -Wformat-security -Wnull-dereference -Wdouble-promotion -Wstrict-aliasing=2 -Werror")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

option(MX_ROUTER "Build with MX_ROUTER support" OFF)
option(MX_SMTPPROXY "Build with MX_SMTPPROXY support" OFF)

if(NOT MX_ROUTER AND NOT MX_SMTPPROXY)
    message(FATAL_ERROR "At least one of MX_ROUTER or MX_SMTPPROXY must be enabled")
endif()

if(MX_ROUTER AND MX_SMTPPROXY)
    message(FATAL_ERROR "MX_ROUTER and MX_SMTPPROXY cannot be enabled at the same time")
endif()

# Set executable name based on enabled flags
if(MX_ROUTER)
    set(EXEC_NAME "mxev")
else()
    set(EXEC_NAME "mxmux")
endif()

if(MX_ROUTER)
    add_compile_definitions(MX_ROUTER)
endif()

if(MX_SMTPPROXY)
    add_compile_definitions(MX_SMTPPROXY)
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Debug build: adding -ggdb flag")
    add_compile_options(-ggdb)
    add_compile_options(-frtti)
    add_compile_options(-DMXEVPORT=3350)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-fmodules-ts)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-fmodules -fmodules-cache-path=${CMAKE_BINARY_DIR}/clang-cache)
endif()

set(INCLUDE_DIRECTORIES src)
set(MODULE_FILES src/arpsocket.cppm src/dnspacket.cppm src/evdev.cppm src/engine.cppm src/event.cppm
                 src/evinputdev.cppm src/kbdinputdev.cppm src/logger.cppm
                 src/mouseinputdev.cppm src/mxarpwatch.cppm src/mxremote.cppm
                 src/mxsrv.cppm src/resolver.cppm src/router.cppm src/smtpproxy.cppm
                 src/semaphore.cppm src/socket.cppm src/tcplistener.cppm
                 src/tcpstream.cppm src/timer.cppm src/udpsocket.cppm src/utils.cppm)
set(HEADER_FILES src/exception.h src/types.h src/ocb.h)
set(SOURCE_FILES src/main.cpp src/ocb.cpp)


add_executable(${EXEC_NAME} ${SOURCE_FILES} ${HEADER_FILES} ${MODULE_FILES})

target_sources(${EXEC_NAME}
  PRIVATE
  FILE_SET
   cxx_modules TYPE CXX_MODULES FILES ${MODULE_FILES}
)
install(TARGETS ${EXEC_NAME})
