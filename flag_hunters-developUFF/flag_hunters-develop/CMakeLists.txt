cmake_minimum_required(VERSION 3.15)
project(flag_hunters C)

set(CMAKE_C_STANDARD 11) # c89

add_compile_options(-pedantic) # pedantic

include_directories(lib/headers)

add_executable(flag_hunters
        lib/headers/board.h
        lib/headers/debug.h
        lib/headers/game.h
        lib/headers/models.h
        lib/headers/pawn.h
        lib/headers/player.h
        lib/headers/queue.h
        lib/board.c
        lib/debug.c
        lib/game.c
        lib/main.c
        lib/pawn.c
        lib/player.c
        lib/queue.c)

target_link_libraries(flag_hunters m)
