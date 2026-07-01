# Crystal and Load Capacitor Selection

## 32.768 kHz Watch Crystal

The ATmega32 Timer2 asynchronous oscillator requires an external low-frequency
crystal connected to TOSC1 (PC6) and TOSC2 (PC7). The standard choice for
timekeeping applications is a 32.768 kHz watch crystal.

### Why 32.768 kHz?

32.768 kHz = 2^15 Hz

This frequency is a power of two. Dividing it by 2 fifteen times gives exactly
1 Hz — one tick per second. This makes it the industry standard for all
real-time clock (RTC) applications including wristwatches, alarm clocks, and
embedded timekeeping systems.

In this project the division is handled by Timer2's prescaler and 8-bit
overflow rather than a binary divider chain, but the principle is the same.

### Selected Crystal

| Parameter        | Value                        |
|-----------------|------------------------------|
| Frequency       | 32.768 kHz                   |
| Package         | Cylindrical through-hole (2-pin), e.g. MC-306 or equivalent |
| Frequency tolerance | ±20 ppm typical          |
| Load capacitance (CL) | 6 pF or 12.5 pF (see datasheet) |
| ESR (max)       | 70 kΩ typical                |
| Operating temperature | -10°C to +60°C          |
| Application     | TOSC1/TOSC2 on ATmega32      |

Common equivalent parts: Epson FC-135, Abracon AB38T, or any standard
32.768 kHz watch crystal with CL of 6–12.5 pF.

---

## Load Capacitors

Every crystal oscillator requires two load capacitors — one from each crystal
pin to ground. These capacitors are part of the oscillator feedback network.
Without them the crystal will not oscillate reliably or at all.

### How to Calculate Load Capacitor Value

The crystal datasheet specifies a load capacitance CL. The two external
capacitors (C1 and C2) appear in series from the oscillator's perspective,
so the formula is:

    CL = (C1 × C2) / (C1 + C2) + Cstray

Where Cstray is the parasitic capacitance of the PCB traces and MCU pins,
typically 2–5 pF.

For a symmetric design where C1 = C2 = C:

    CL = C/2 + Cstray
    C  = 2 × (CL - Cstray)

### Calculation for This Project

Assuming CL = 12.5 pF and Cstray = 2 pF:

    C = 2 × (12.5 - 2)
    C = 2 × 10.5
    C = 21 pF

The nearest standard capacitor value is **22 pF**.

For a crystal with CL = 6 pF:

    C = 2 × (6 - 2) = 8 pF → use 8.2 pF standard value

### Selected Load Capacitors

| Parameter   | Value                              |
|------------|-------------------------------------|
| Value       | 22 pF (or 8.2 pF for CL=6 pF crystal) |
| Type        | NP0 / C0G ceramic (low drift)      |
| Voltage rating | 10 V or higher                  |
| Quantity    | 2 (one per crystal pin)            |
| Placement   | As close as possible to TOSC1/TOSC2 pins on PCB |

NP0/C0G type capacitors are mandatory for oscillator circuits — they have
near-zero temperature coefficient, meaning their value does not drift with
temperature. X7R or Y5V types drift significantly and will cause the clock
to run fast or slow as temperature changes.

---

## PCB Placement Requirements

| Requirement | Reason |
|------------|---------|
| Place crystal within 5 mm of PC6/PC7 pins | Long traces add parasitic inductance and capacitance |
| Keep TOSC traces short and direct | Reduces noise pickup |
| Route TOSC traces away from switching signals (PB0, PB1, TXD) | Switching edges can couple into the high-impedance oscillator circuit |
| Do not place vias under crystal | Vias add stray capacitance |
| Connect load capacitor grounds to a local ground pour | Reduces ground impedance at oscillator frequency |
