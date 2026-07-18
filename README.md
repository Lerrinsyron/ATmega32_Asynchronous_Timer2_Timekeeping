ATmega32 Asynchronous Timer2 Timekeeping

CT 321 — Group 13 

Low-power real-time clock using ATmega32 Timer2 in asynchronous mode, driven by a 32.768 kHz watch crystal on TOSC1/TOSC2. The CPU sleeps between timer events; Timer2 keeps running and wakes the CPU exactly once per second.


Build
```bash
cd firmware

# Real hardware
avr-gcc -mmcu=atmega32 -DF_CPU=8000000UL -Os -I/usr/avr/include \
  -o hex/firmware.elf src/main.c
avr-objcopy -O ihex -R .eeprom hex/firmware.elf hex/firmware.hex

# SimulIDE
avr-gcc -mmcu=atmega32 -DF_CPU=8000000UL -DSIMULIDE_MODE -Os -I/usr/avr/include \
  -o hex/firmware_simulide.elf src/main.c
avr-objcopy -O ihex -R .eeprom hex/firmware_simulide.elf hex/firmware_simulide.hex
```

If `/usr/avr/include` doesn't exist on your machine, run `find /usr -name "io.h" | grep avr` to find your avr-libc path.




SimulIDE

Load hex/firmware_simulide.hex into the ATmega32. Set Freq to `8000000` and Serial Monitor baud to `9600`.

> ⚠️ Known limitation: SimulIDE cannot model the external 32.768 kHz crystal on TOSC1/TOSC2. The SIMULIDE_MODE   build uses an internal-clock CTC fallback to produce the same 1-second tick. The KiCad design remains correct for real hardware.


Common Issues

Garbage output on serial monitor baud rate mismatch. Confirm SimulIDE Serial Monitor is set to 9600, not 4800.

"SLEEP instruction not fully implemented" warning expected. SimulIDE does not model power-save sleep. Ignore it; everything else works correctly.
