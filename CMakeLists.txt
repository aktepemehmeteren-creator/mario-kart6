cmake_minimum_required(VERSION 3.5)
project(MarioKartPSP C)

add_executable(MarioKartPSP main.c)

target_link_libraries(MarioKartPSP PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspgum
    pspgu
    pspctrl
    psppower
    pspaudio
    m
)

create_pbp_file(
    TARGET MarioKartPSP
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Mario Kart PSP"
)
