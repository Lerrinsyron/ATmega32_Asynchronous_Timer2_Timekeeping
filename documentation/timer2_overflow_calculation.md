# Timer2 Prescaler and Overflow Period Calculation

## Overview

Timer2 in asynchronous mode is clocked directly from the 32.768 kHz watch
crystal via TOSC1. The goal is to configure Timer2 so that its overflow
interrupt fires exactly once per second, giving a 1 Hz timekeeping base.

---

## Timer2 Characteristics

| Property        | Value                          |
|----------------|--------------------------------|
| Timer width     | 8-bit (counts 0 to 255)        |
| Clock source    | External 32.768 kHz crystal (AS2=1) |
| Overflow point  | After 256 counts (0xFF → 0x00) |
| Available prescalers | /1, /8, /32, /64, /128, /256, /1024 |

---

## Derivation

The timer overflow period is:

    T_overflow = (Prescaler × 256) / f_TOSC

We need T_overflow = 1 second and f_TOSC = 32768 Hz.

Rearranging for the prescaler:

    Prescaler = (T_overflow × f_TOSC) / 256
    Prescaler = (1 × 32768) / 256
    Prescaler = 128

**Prescaler = 128** is an available option (CS2[2:0] = 0b101). ✓

---

## Verification

    f_TOSC      = 32768 Hz
    Prescaler   = 128
    Timer ticks per second = 32768 / 128 = 256 ticks/second

    Timer2 is 8-bit → overflows after 256 counts

    T_overflow  = 256 / 256 = 1.000 second ✓

Timer2 generates exactly one overflow interrupt per second with zero error.
This is the key advantage of the 32.768 kHz frequency — it divides cleanly
by 128 × 256 = 32768 with no remainder.

---

## Register Configuration

```c
/* Enable asynchronous mode — Timer2 clocked from TOSC1 crystal */
ASSR |= (1 << AS2);

/* Set prescaler /128: CS22=1, CS21=0, CS20=1 */
TCCR2 = (1 << CS22) | (1 << CS20);

/* Wait for async registers to synchronise (mandatory per datasheet) */
while (ASSR & ((1 << TCN2UB) | (1 << OCR2UB) | (1 << TCR2UB)));

/* Enable Timer2 overflow interrupt */
TIMSK |= (1 << TOIE2);
```

### CS2 Prescaler Bit Table (ATmega32 Datasheet)

| CS22 | CS21 | CS20 | Prescaler |
|------|------|------|-----------|
| 0    | 0    | 0    | Timer stopped |
| 0    | 0    | 1    | /1 |
| 0    | 1    | 0    | /8 |
| 0    | 1    | 1    | /32 |
| 1    | 0    | 0    | /64 |
| 1    | 0    | 1    | /128 ← selected |
| 1    | 1    | 0    | /256 |
| 1    | 1    | 1    | /1024 |

---

## SimulIDE Fallback Calculation

SimulIDE cannot simulate the external 32.768 kHz crystal. In simulation,
Timer2 is reconfigured in CTC (Clear Timer on Compare) mode using the
internal 8 MHz CPU clock to approximate 1-second ticks in software.

    f_CPU       = 8,000,000 Hz
    Prescaler   = 1024
    OCR2        = 77  (compare match value)

    f_compare   = f_CPU / (Prescaler × (OCR2 + 1))
    f_compare   = 8,000,000 / (1024 × 78)
    f_compare   = 8,000,000 / 79,872
    f_compare   ≈ 100.16 Hz

The ISR accumulates 100 compare-match events in a software counter
(g_tick_count) before incrementing the clock — giving a period of:

    T_second    = 100 / 100.16 ≈ 0.9984 seconds

Error ≈ 0.16% — acceptable for simulation demonstration purposes.
This fallback is activated by compiling with -DSIMULIDE_MODE and does
not affect the real hardware configuration.

---

## Summary

| Parameter          | Real hardware     | SimulIDE fallback     |
|-------------------|-------------------|-----------------------|
| Clock source       | 32.768 kHz crystal | 8 MHz internal RC    |
| Timer mode         | Normal (overflow)  | CTC (compare match)  |
| Prescaler          | /128               | /1024                |
| Overflow/compare   | 256 counts         | OCR2 = 77            |
| ISR rate           | 1 Hz exactly       | ~100 Hz              |
| Software accumulator | not needed       | 100 ticks = 1 second |
| Timing error       | 0%                 | ~0.16%               |
