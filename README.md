# Donkey Kong Jr.

Nintendo's 1982 arcade game on the ColecoVision. The cart runs the arcade's own program and sound from your MAME ROM set.

## Controls

- Stick or d-pad: move and climb
- Either fire button, or A, B, X or Y on a pad: jump
- Keypad \* or #, or - on a pad: coin
- Keypad 1, or + on a pad: one player
- Keypad 2: two players
- RESET, or both fire buttons and \* held for 2 seconds (Select and Start on a USB pad): back to the cart's menu

## What it needs

- MAME's `dkongjr` set, as `dkongjr.zip` or its loose files, in `/DKONGJR` on the card. The plugin finds the files by their CRC, so their names don't matter. If any are missing, it lists them on the screen.
- A USB pad is optional. It needs a powered hub between it and the cart.

## The picture

The play area is three quarters of the arcade's size, in the middle of the screen. The scores, bonus, level and lives stay at the arcade's size and sit in the black margins either side: 1UP and HIGH SCORE on the left, the bonus, level and lives on the right.

## Credits

- The board, its sound circuits, the 8035 and the Z80 follow MAME 0.288: `dkong.cpp`, `dkong_a.cpp` and `dkong_v.cpp` by Couriersud, the discrete sound modules by K. Wilkins, Couriersud, Derrick Renaud and Frank Palazzolo, `mcs48.cpp` by Dan Boris, Mirko Buffoni, Aaron Giles and Couriersud, and `z80.cpp` by Juergen Buchmueller. BSD 3-Clause: <https://github.com/mamedev/mame/tree/mame0288>
- Parrot Cart plugin template and SDK, by the Parrot Cart contributors, MIT: <https://github.com/plasticbugs/parrot-plugin-template>
- TinyUSB, by Ha Thach and contributors, MIT: <https://github.com/hathach/tinyusb>
- Raspberry Pi Pico SDK, BSD 3-Clause: <https://github.com/raspberrypi/pico-sdk>

Donkey Kong Jr. is Nintendo's. The plugin contains none of the game. It reads your ROM set from the card each time it starts.

Source: <https://github.com/plasticbugs/parrot-plugin-dkongjr>

## License

BSD-3-Clause. See LICENSE. THIRD-PARTY.md lists the parts that came from other projects.

## Building

```sh
cmake -B build -G Ninja \
  -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

That makes `build/DKONGJR.CVP`. Copy it to `/PLUGINS` on the card.

- `src/jr.c` is the arcade board: the Z80, the 8035 and their memory. `src/jrsnd.c` is the sound circuits after the 8035.
- `src/jrplay.c` makes each of the console's lists: 16.77 ms of the game, 100 samples of its sound, and as much of its picture as fits. `src/jrcv.c` and `src/jrdraw.c` draw and convert only what changed.
- The lists are a little longer than a TV frame and run back to back, so the console's clock is the game's sound clock.

## Testing

- `test/run.sh donkey-kong-jr donkey-kong-jr-usb` checks the screens it shows without its ROMs, on the kit's model of the console.
- `test/jr/check.sh DIR`, with `DIR/dkongjr.zip` your own set, checks the game against MAME. The board's memory has to match MAME's at every frame for 6000 frames, and the plugin's lists, played to the SDK's kernel on MAME's ColecoVision, have to show the same picture as the model. It also measures how loud it is next to the arcade, from 20 Hz to 15 kHz over the seconds the arcade has sound (`test/jr/level.py`), and what the soft knee takes from the music and from the effects (`test/jr/knee.py`).
- `test/jr/armcount.py` estimates the time each list takes on the cart's processor (`test/jr/build-arm.sh` builds what it runs).
