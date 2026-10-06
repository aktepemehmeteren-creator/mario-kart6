cmake_minimum_required(VERSION 3.10)
project(MarioKartPSP C)

add_executable(MarioKartPSP main.c)

target_link_libraries(MarioKartPSP PRIVATE
    pspgum
    pspgu
    pspge
    pspdisplay
    pspctrl
    psppower
    m
)

create_pbp_file(
    TARGET MarioKartPSP
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Mario Kart PSP"
)
