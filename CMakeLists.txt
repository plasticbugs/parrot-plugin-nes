# NES: NES games on the ColecoVision. Pick a .nes in the menu and press A:
# QuickNES runs it on the cart, and nescv sends each picture and the sound
# to the console as the Game Boy kernel's lists. RESET goes back to the menu.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                      -> build/NES.CVP
#
# Copy NES.CVP into the card's /PLUGINS folder.
cmake_minimum_required(VERSION 3.13)

# the cart's SDK: the copy in sdk/ (sdk/README.md)
set(PARROTCART_SDK ${CMAKE_CURRENT_LIST_DIR}/sdk/firmware/revc)
set(PICO_BOARD_HEADER_DIRS ${PARROTCART_SDK}/cvcart_revc/boards)
set(PICO_BOARD cvcart_revc)
set(PICO_PLATFORM rp2350-arm-s)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# the USB host: sdk/tinyusb (TinyUSB 0.21 and its fixes since), not the pico-sdk's 0.18
include(${PARROTCART_SDK}/sdk/cvpad_tinyusb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/pico_sdk_import.cmake)

project(nes C CXX ASM)
pico_sdk_init()
include(${PARROTCART_SDK}/sdk/cvplugin.cmake)

# The same source makes the same file, whenever it's built: the compiler's
# __DATE__ and __TIME__ (the build date in the binary info) come from
# SOURCE_DATE_EPOCH, fixed here at the tested build's, 2026-11-15 19:28:57 UTC.
set(BUILD_EPOCH 1794770937)
set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})

set(Q ${CMAKE_CURRENT_LIST_DIR}/vendor/quicknes)
file(GLOB QSRC ${Q}/core_quicknes/*.cpp ${Q}/emu2413/*.cpp)
# vendored: its own warnings aren't ours to fix
set_source_files_properties(${QSRC} PROPERTIES COMPILE_OPTIONS "-w")
# ours: nes_data.h's four-character tags aside
set_source_files_properties(src/nes.cpp src/nescv.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-multichar")

add_executable(nes src/nes.cpp src/nescv.c ${PARROTCART_SDK}/midihello/vdp.c ${QSRC})
target_include_directories(nes PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src ${PARROTCART_SDK}/midihello)
target_include_directories(nes SYSTEM PRIVATE ${Q}/core_quicknes ${Q}/emu2413)
# NO_UNALIGNED_ACCESS: else the PPU's unaligned 32-bit accesses may become
# LDRD/STRD, which fault (vendor/quicknes/VENDORED.md). QNES_EXT_TILE_CACHE:
# a CHR ROM's tile cache where nes.cpp says.
target_compile_definitions(nes PRIVATE NO_UNALIGNED_ACCESS QNES_EXT_TILE_CACHE)
# -DNES_NESCV_020=ON: nescv's slot choice as 0.2.0 and 0.4.0 had it (nescv.c:
# no refresh, a full third overwrites a slot on screen), to compare on the
# console
option(NES_NESCV_020 "nescv as NES 0.2.0 had it" OFF)
if (NES_NESCV_020)
  target_compile_definitions(nes PRIVATE NESCV_020)
endif()
# RAM for QuickNES's heap (the plugin runs from SRAM: code is heap lost).
# QuickNES's one fprintf (stderr: an unsupported mapper, which nes.cpp
# shows) brings in newlib's whole printf and stdio, 19K that write nowhere:
# nes.cpp's __wrap_fprintf instead. And the SDK's printf without floats
# (nothing here prints one).
target_link_options(nes PRIVATE -Wl,--wrap=fprintf)
target_compile_definitions(nes PRIVATE PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0)
# -DNES_CACHE_SPARE=40000: a CHR RAM's tile cache in the PSRAM whatever the
# heap has (nes.cpp's CACHE_SPARE), to try that way on the console
if (NES_CACHE_SPARE)
  target_compile_definitions(nes PRIVATE CACHE_SPARE=${NES_CACHE_SPARE})
endif()
cvframes_use(nes)
cvsd_use(nes)
cvpsram_use(nes)
cvpad_use(nes)                              # USB game pads: the port is a host
pico_enable_stdio_usb(nes 0)
pico_enable_stdio_uart(nes 0)
cvcart_plugin(nes ID parrotcart.nes NAME "NES" VERSION 0.4.7 BY "Parrot Cart"
              HOME /NES OPENS .nes USES usb-host psram ICON src/nes_icon.txt FILE_ICON src/nes_file_icon.txt HEAP
              README CART-README.md)
