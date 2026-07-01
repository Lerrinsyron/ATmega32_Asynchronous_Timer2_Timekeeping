# Pin Mapping Table

## TOSC1 / TOSC2 — Asynchronous Timer2 Oscillator Pins

| Pin Name | ATmega32 Pin No. | Port / GPIO | Direction | Connected To | Description |
|----------|-----------------|-------------|-----------|--------------|-------------|
| TOSC1 | 29 | PC6 | Input | 32.768 kHz crystal pin 1 | Timer2 external oscillator input. Receives the crystal clock signal. |
| TOSC2 | 28 | PC7 | Output | 32.768 kHz crystal pin 2 | Timer2 external oscillator output. Completes the crystal feedback loop. |

## Notes

- When `AS2` bit in the `ASSR` register is set to 1, PC6 and PC7 are **disconnected from Port C** and dedicated exclusively to the Timer2 oscillator circuit. They cannot be used as general-purpose GPIO while asynchronous mode is active.
- The 32.768 kHz watch crystal must be connected directly between TOSC1 and TOSC2 with load capacitors from each pin to GND.
- TOSC1/TOSC2 traces must be kept short on the PCB and routed away from switching signals to avoid noise coupling into the oscillator.

## Full Project Pin Mapping

| ATmega32 Pin | Pin No. | Function | Direction | Connected To | Notes |
|-------------|---------|----------|-----------|--------------|-------|
| PC6 / TOSC1 | 29 | Timer2 oscillator input | Input | 32.768 kHz crystal | Do not use as GPIO when AS2=1 |
| PC7 / TOSC2 | 28 | Timer2 oscillator output | Output | 32.768 kHz crystal | Do not use as GPIO when AS2=1 |
| PB0 | 1 | Wake-up LED | Output | LED + 330 Ω resistor | Toggles every 10 seconds |
| PB1 | 2 | Alarm buzzer | Output | Buzzer | Activates when alarm time is reached |
| PA0 | 40 | Reset button | Input | Push button to GND | Active LOW, internal pull-up enabled |
| PA1 | 39 | Set alarm button | Input | Push button to GND | Active LOW, internal pull-up enabled |
| PA2 | 38 | Increment button | Input | Push button to GND | Active LOW, internal pull-up enabled |
| PD1 / TXD | 15 | USART transmit | Output | Serial terminal RX | 4800 baud, 8N1 |
| VCC | 10 | Power supply | Power | 5 V rail | Main digital power |
| AVCC | 11 | Analog power supply | Power | 5 V rail | Required even without ADC use |
| GND | 11, 31 | Ground | Power | Ground | Both GND pins must be connected |
| RESET | 9 | Master reset | Input | 5 V rail via pull-up | Active LOW — held HIGH for normal operation |
