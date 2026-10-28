cmake_minimum_required(VERSION 3.16)
project(tgmuos C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Locate SDL2/SDL2_ttf without pkg-config (muOS toolchain has no pkg-config).
# Allow overrides via SDL2_ROOT / SDL2_TTF_ROOT for cross builds.
set(SDL2_ROOT "" CACHE PATH "SDL2 root directory")
set(SDL2_TTF_ROOT "" CACHE PATH "SDL2_ttf root directory")

macro(tgmuos_find_lib VAR NAME)
    find_library(${VAR} ${NAME} PATHS ${SDL2_ROOT}/lib ${SDL2_TTF_ROOT}/lib
                 PATH_SUFFIXES lib lib64)
endmacro()

macro(tgmuos_find_include VAR NAME)
    find_path(${VAR} ${NAME} PATHS ${SDL2_ROOT} ${SDL2_TTF_ROOT}
              PATH_SUFFIXES include)
endmacro()

tgmuos_find_lib(SDL2_LIBRARY SDL2)
tgmuos_find_lib(SDL2_TTF_LIBRARY SDL2_ttf)
tgmuos_find_lib(SDL2_MIXER_LIBRARY SDL2_mixer)

tgmuos_find_include(SDL2_INCLUDE_DIR SDL2/SDL.h)
tgmuos_find_include(SDL2_TTF_INCLUDE_DIR SDL2/SDL_ttf.h)

if(NOT SDL2_LIBRARY OR NOT SDL2_TTF_LIBRARY OR NOT SDL2_INCLUDE_DIR)
    message(FATAL_ERROR
        "SDL2/SDL2_ttf not found. Set SDL2_ROOT and SDL2_TTF_ROOT to the "
        "sysroot containing libSDL2.so, libSDL2_ttf.so and headers.")
endif()

set(SDL2_LIBRARIES ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY})
set(SDL2_TTF_LIBRARIES ${SDL2_TTF_LIBRARY})
set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
set(SDL2_TTF_INCLUDE_DIRS ${SDL2_TTF_INCLUDE_DIR})

add_executable(tgmuos
    src/main.c
    src/app.c
    src/screen_chats.c
    src/screen_chat.c
    src/screen_auth.c
    src/screen_settings.c
    src/screen_profile.c
    src/input.c
    src/keyboard.c
    src/ui.c
    src/renderer.c
    src/telegram.c
    src/qr.c
    src/preferences.c
    src/statusbar.c
    src/images.c
    src/log.c
)
target_include_directories(tgmuos PRIVATE include
    ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
target_link_libraries(tgmuos PRIVATE
    ${SDL2_TTF_LIBRARIES} ${SDL2_LIBRARIES}
    ${CMAKE_DL_LIBS} m)

add_executable(muvpn
    muvpn/src/main.c
    src/input.c
    src/ui.c
    src/renderer.c
)
target_include_directories(muvpn PRIVATE include
    ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
target_link_libraries(muvpn PRIVATE
    ${SDL2_TTF_LIBRARIES} ${SDL2_LIBRARIES} m)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME telegram-json COMMAND tgmuos --telegram-json-test)
    add_test(NAME qr-encoder COMMAND tgmuos --qr-test)
    add_test(NAME external-keyboard COMMAND tgmuos --keyboard-test)
    add_test(NAME settings-screen COMMAND tgmuos --settings-test)
    set_tests_properties(settings-screen PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME muvpn-smoke COMMAND muvpn --smoke-test)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME vless-subscription
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_vless_subscription.py")
        add_test(NAME muvpn-service
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/muvpn/tests/test_service.py")
    endif()
endif()

set(TGMUOS_ATOMIC_LIBRARY "" CACHE FILEPATH "Optional target atomic support library")
if(TGMUOS_ATOMIC_LIBRARY)
    target_link_libraries(tgmuos PRIVATE "${TGMUOS_ATOMIC_LIBRARY}")
endif()