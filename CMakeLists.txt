# Donkey Kong Jr.: Nintendo's 1982 Donkey Kong Jr. arcade game on the ColecoVision, from your own MAME ROMs.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                     -> build/DKONGJR.CVP
#
# and copy the .CVP into the card's /PLUGINS folder (docs/getting-started.md).
# Made by tools/new-plugin.py.
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project(donkey_kong_jr C CXX ASM)
parrotcart_init()

add_executable(donkey_kong_jr src/main.c src/jr.c src/jrroms.c src/jrsnd.c src/jrcv.c src/jrdraw.c src/jrplay.c
               src/jrzip.c)
parrotcart_frames(donkey_kong_jr)           # a whole picture every TV frame: the frame kernel
# the board, its sound and the picture run every list: -O2 (test/jr/armcount.py counts them)
set_source_files_properties(src/jr.c src/jrsnd.c src/jrcv.c src/jrdraw.c src/jrplay.c
                            PROPERTIES COMPILE_OPTIONS "-O2")

# The features (docs/features.md). Each one links its code, sets up the
# USB port with only what it needs, and puts its flag in the .CVP's USES:
#   PADS KEYBOARD MOUSE MIDI_IN   the USB port as a host, through a powered hub
#   MIDI_OUT                      the USB port as a MIDI device, on a computer
#   PSRAM                         the 8 MB PSRAM
#   CARD                          the card: files and saves (a plugin with
#                                 OPENS needs it to read its file)
# To add one later, add its word here and its code to src/main.c.
cvcart_features(donkey_kong_jr PADS PSRAM CARD)

# The plugin (docs/plugin-rules.md): the ID is yours for good; the version
# is the release's tag without its v.
cvcart_plugin(donkey_kong_jr ID parrotcart.dkongjr NAME "Donkey Kong Jr." VERSION 0.1.2 BY "Parrot Cart"
              ICON src/icon.txt README README.md FILE DKONGJR.CVP
              HOME /DKONGJR)
