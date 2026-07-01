# SimulIDE Simulator Limitation — Asynchronous Timer2 Oscillator

## Limitation Description

SimulIDE 1.1.0 does not fully model the external crystal oscillator circuit
connected to the TOSC1 (PC6) and TOSC2 (PC7) pins of the ATmega32. When the
`AS2` bit in the `ASSR` register is set to 1 and an external 32.768 kHz
crystal is wired to TOSC1/TOSC2, SimulIDE does not drive Timer2 from that
crystal. Timer2 either does not run or behaves incorrectly in simulation.

This is a known limitation of SimulIDE's AVR peripheral model — the
asynchronous clock domain for Timer2 is not implemented.

Additionally, SimulIDE prints the following warning when the firmware executes
a sleep instruction:

    Warning: AVR SLEEP instruction not Fully implemented
    eMcu::sleep: Sleeping

This confirms that power-save sleep mode (SLEEP_MODE_PWR_SAVE), which keeps
Timer2 running while the CPU halts, is also not fully modelled. In simulation
the CPU continues executing rather than entering a true sleep state.

---

## Impact on Simulation

| Feature | Real hardware | SimulIDE behaviour |
|---------|--------------|-------------------|
| Timer2 async crystal clock | Runs from 32.768 kHz TOSC | Not modelled |
| 1-second overflow interrupt | Exact, zero error | Not generated |
| Power-save sleep mode | CPU halts, Timer2 runs | CPU does not halt |
| Wake on Timer2 overflow | Works correctly | Not triggered |

---

## Workaround Implemented

As permitted by the project specification, an equivalent Timer2 configuration
using the internal CPU clock was implemented for simulation. This is activated
by compiling the firmware with the `-DSIMULIDE_MODE` preprocessor flag:

```bash
avr-gcc -mmcu=atmega32 -DF_CPU=8000000UL -DSIMULIDE_MODE -Os \
  -I/usr/avr/include \
  -o hex/firmware_simulide.elf src/main.c

avr-objcopy -O ihex -R .eeprom hex/firmware_simulide.elf firmware_simulide.hex
```

### Fallback Timer2 Configuration

In SimulIDE mode, Timer2 is configured in CTC (Clear Timer on Compare) mode
using the internal 8 MHz CPU clock:

```c
TCCR2 = (1 << WGM21)                           /* CTC mode            */
      | (1 << CS22) | (1 << CS21) | (1 << CS20); /* prescaler /1024   */
OCR2  = 77;
TIMSK |= (1 << OCIE2);   /* compare-match interrupt ~100 times/second */
```

The ISR accumulates 100 compare-match events before incrementing the clock,
producing a software-defined 1-second period with ~0.16% timing error.

### What Remains Unchanged in Simulation

All timekeeping logic, alarm comparison, LED toggling, buzzer activation,
button handling, and USART output behave identically in both the real hardware
and SimulIDE builds. Only the clock source differs.

---

## KiCad Design Correctness

Despite the simulator limitation, the KiCad schematic and PCB layout reflect
the correct real-hardware design:

- 32.768 kHz watch crystal connected between TOSC1 (PC6) and TOSC2 (PC7)
- Two 22 pF NP0/C0G load capacitors from each crystal pin to GND
- Crystal placed close to the MCU with short traces
- TOSC traces routed away from switching outputs

The SimulIDE workaround is purely a firmware compile-time switch and does not
affect the hardware design in any way.

---

## Sleep Mode Documentation

Since SimulIDE does not implement power-save sleep mode, the low-power
behaviour is documented here rather than demonstrated in simulation.

In real hardware operation:

1. After each Timer2 overflow ISR completes, `main()` calls `enter_sleep()`
2. The CPU enters SLEEP_MODE_PWR_SAVE — halting the CPU clock and most peripherals
3. Timer2 and its external crystal oscillator continue running unaffected
4. After exactly 1 second the next Timer2 overflow fires, waking the CPU
5. The ISR runs, updates the clock, and `main()` resumes briefly before sleeping again

In this mode the ATmega32 current consumption drops from ~5 mA active to
roughly 1–2 µA, allowing a coin cell battery to power the timekeeping system
for months.
