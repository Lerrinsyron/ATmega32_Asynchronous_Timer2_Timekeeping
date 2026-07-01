/*
 * Project 13: Asynchronous Timer2 Low-Power Timekeeping System
 * Target MCU : ATmega32
 * CPU Clock  : 8 MHz internal RC  (F_CPU must match your fuse bits)
 * Timer2     : Driven by external 32.768 kHz watch crystal on TOSC1/TOSC2
 *              Prescaler /128 → overflow every exactly 1 second
 *
 * Pin Map
 * -------
 * PC6 / TOSC1  ← 32.768 kHz crystal (do NOT use as GPIO)
 * PC7 / TOSC2  ← 32.768 kHz crystal (do NOT use as GPIO)
 * PB0          → Wake-up LED  (active HIGH, through 330 Ω resistor)
 * PB1          → Buzzer       (active HIGH, through driver if needed)
 * PA0          ← Reset-time button  (active LOW, internal pull-up)
 * PA1          ← Set-alarm button   (active LOW, internal pull-up)
 * PA2          ← Increment button   (active LOW, internal pull-up)
 * PD1 / TXD   → Serial terminal    (USART TX, 9600 baud @ 8 MHz)
 *
 * SimulIDE note
 * -------------
 * SimulIDE cannot simulate the external 32.768 kHz crystal on TOSC1/TOSC2.
 * The firmware automatically falls back to SIMULIDE_MODE when that macro is
 * defined at compile time (see Makefile). In that mode Timer2 uses the
 * internal CPU clock with prescaler /1024 and a compare-match at OCR2=77
 * to approximate a 1-second tick at 8 MHz  (8000000 / 1024 / 78 ≈ 100 Hz
 * ticks, accumulated in software to 1 second). This keeps all logic
 * identical while working around the simulator limitation.
 *
 * Compile (real hardware):
 *   make
 * Compile (SimulIDE):
 *   make simulide
 */

#define F_CPU 8000000UL

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <util/delay.h>

/* =========================================================
 * Configuration
 * ========================================================= */

#define WAKE_LED_PIN PB0
#define BUZZER_PIN PB1
#define BTN_RESET PA0
#define BTN_SET_ALARM PA1
#define BTN_INCREMENT PA2

/* How long the buzzer sounds when alarm fires (in seconds) */
#define ALARM_BUZZ_DURATION 3

/* How often the wake-up LED blinks (in seconds) */
#define WAKEUP_INTERVAL 10

/* USART baud rate */
#define BAUD 9600
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1) /* = 51 at 8 MHz */

/* =========================================================
 * Global time state  (all volatile — modified inside ISR)
 * ========================================================= */

volatile uint8_t g_seconds = 0;
volatile uint8_t g_minutes = 0;
volatile uint8_t g_hours = 0;

volatile uint8_t g_alarm_seconds = 0;
volatile uint8_t g_alarm_minutes = 0;
volatile uint8_t g_alarm_hours = 0;

volatile bool g_time_updated = false; /* set by ISR, cleared by main */
volatile bool g_alarm_ringing = false;
volatile uint8_t g_buzz_countdown = 0; /* seconds remaining for buzzer */

/* Used in SimulIDE fallback mode to accumulate sub-second ticks */
#ifdef SIMULIDE_MODE
volatile uint8_t g_tick_count = 0;
#define TICKS_PER_SECOND 100 /* see timer setup below */
#endif

/* Alarm-set sub-mode state */
typedef enum
{
  MODE_CLOCK,       /* normal timekeeping */
  MODE_SET_ALARM_H, /* user is setting alarm hours */
  MODE_SET_ALARM_M, /* user is setting alarm minutes */
  MODE_SET_ALARM_S  /* user is setting alarm seconds */
} AppMode;

volatile AppMode g_mode = MODE_CLOCK;

/* =========================================================
 * USART — serial output to terminal
 * ========================================================= */

static void usart_init(void)
{
  UBRRH = (uint8_t)(UBRR_VALUE >> 8);
  UBRRL = (uint8_t)(UBRR_VALUE);
  UCSRB = (1 << TXEN);                                /* TX only */
  UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0); /* 8N1 */
}

static void usart_putchar(char c)
{
  while (!(UCSRA & (1 << UDRE)))
    ;
  UDR = c;
}

static void usart_print(const char *str)
{
  while (*str)
    usart_putchar(*str++);
}

/* Print a formatted time string: "TIME: HH:MM:SS\r\n" */
static void usart_print_time(uint8_t h, uint8_t m, uint8_t s,
                             const char *label)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %02u:%02u:%02u\r\n", label, h, m, s);
  usart_print(buf);
}

/* =========================================================
 * GPIO initialisation
 * ========================================================= */

static void gpio_init(void)
{
  /* Outputs: LED and Buzzer on PORTB */
  DDRB |= (1 << WAKE_LED_PIN) | (1 << BUZZER_PIN);
  PORTB &= ~((1 << WAKE_LED_PIN) | (1 << BUZZER_PIN)); /* off */

  /* Inputs: buttons on PORTA with internal pull-ups (active LOW) */
  DDRA &= ~((1 << BTN_RESET) | (1 << BTN_SET_ALARM) | (1 << BTN_INCREMENT));
  PORTA |= (1 << BTN_RESET) | (1 << BTN_SET_ALARM) | (1 << BTN_INCREMENT);
}

/* =========================================================
 * Timer2 — asynchronous from 32.768 kHz crystal
 *
 * Real hardware path (default):
 *   AS2 = 1  → Timer2 clocked from TOSC1 pin (external crystal)
 *   Prescaler /128 → f_tick = 32768/128 = 256 Hz
 *   Timer2 is 8-bit → overflow after 256 counts
 *   Period = 256/256 = 1 second  ✓
 *
 * SimulIDE fallback (SIMULIDE_MODE defined):
 *   Timer2 uses internal clock, CTC mode, OCR2=77, prescaler /1024
 *   f_compare = 8000000 / (1024 * 78) ≈ 100 Hz
 *   Software counter accumulates 100 ticks → 1 second
 * ========================================================= */

static void timer2_init(void)
{
#ifndef SIMULIDE_MODE
  /* ---- Real hardware: async external crystal ---- */

  /* Step 1: Select asynchronous clock source */
  ASSR |= (1 << AS2);

  /* Step 2: Clear timer counter */
  TCNT2 = 0;

  /* Step 3: Set prescaler /128  (CS2[2:0] = 0b101) */
  TCCR2 = (1 << CS22) | (1 << CS20);

  /*
   * Step 4: Wait for the three async registers to finish updating.
   * This is MANDATORY. Writing Timer2 registers while busy flags are
   * set corrupts the values — the datasheet is explicit about this.
   * The flags clear once the async domain has latched the new values.
   */
  while (ASSR & ((1 << TCN2UB) | (1 << OCR2UB) | (1 << TCR2UB)))
    ;

  /* Step 5: Enable Timer2 overflow interrupt */
  TIMSK |= (1 << TOIE2);

#else
  /* ---- SimulIDE fallback: internal clock, CTC mode ---- */
  /*
   * SIMULATOR LIMITATION NOTE:
   * SimulIDE does not model the external 32.768 kHz crystal oscillator
   * on TOSC1/TOSC2. To demonstrate identical timekeeping logic, Timer2
   * is configured in CTC (Clear Timer on Compare) mode using the
   * internal CPU clock. OCR2=77 with prescaler /1024 gives ~100
   * compare-match interrupts per second. The ISR accumulates these in
   * g_tick_count and fires the timekeeping logic once per 100 ticks.
   * All other firmware behaviour (clock, alarm, LED, buzzer, sleep,
   * buttons) is unchanged.
   */
  TCCR2 = (1 << WGM21)                               /* CTC mode */
          | (1 << CS22) | (1 << CS21) | (1 << CS20); /* prescaler /1024 */
  OCR2 = 77;                                         /* 8000000 / (1024 * 78) = 100.1 Hz ≈ 100 ticks/sec */
  TIMSK |= (1 << OCIE2);                             /* Output Compare Match interrupt */
#endif
}

// ISR — fires every 1 second (real) or every ~10 ms (SimulIDE)
#ifndef SIMULIDE_MODE

ISR(TIMER2_OVF_vect)
{
  /* Increment clock */
  g_seconds++;
  if (g_seconds >= 60)
  {
    g_seconds = 0;
    g_minutes++;
  }
  if (g_minutes >= 60)
  {
    g_minutes = 0;
    g_hours++;
  }
  if (g_hours >= 24)
  {
    g_hours = 0;
  }

  /* Wake-up LED: toggle every WAKEUP_INTERVAL seconds */
  if (g_seconds % WAKEUP_INTERVAL == 0)
    PORTB ^= (1 << WAKE_LED_PIN);

  /* Alarm: check if current time matches alarm time */
  if (!g_alarm_ringing && g_hours == g_alarm_hours &&
      g_minutes == g_alarm_minutes && g_seconds == g_alarm_seconds)
  {
    g_alarm_ringing = true;
    g_buzz_countdown = ALARM_BUZZ_DURATION;
    PORTB |= (1 << BUZZER_PIN);
  }

  /* Buzzer countdown */
  if (g_alarm_ringing)
  {
    if (g_buzz_countdown > 0)
    {
      g_buzz_countdown--;
    }
    else
    {
      g_alarm_ringing = false;
      PORTB &= ~(1 << BUZZER_PIN);
    }
  }

  g_time_updated = true;
}

#else /* SimulIDE fallback uses compare-match interrupt */

ISR(TIMER2_COMP_vect)
{
  g_tick_count++;
  if (g_tick_count < TICKS_PER_SECOND)
    return; /* not a full second yet */
  g_tick_count = 0;

  /* Everything below is identical to the real ISR above */
  g_seconds++;
  if (g_seconds >= 60)
  {
    g_seconds = 0;
    g_minutes++;
  }
  if (g_minutes >= 60)
  {
    g_minutes = 0;
    g_hours++;
  }
  if (g_hours >= 24)
  {
    g_hours = 0;
  }

  if (g_seconds % WAKEUP_INTERVAL == 0)
    PORTB ^= (1 << WAKE_LED_PIN);

  if (!g_alarm_ringing && g_hours == g_alarm_hours &&
      g_minutes == g_alarm_minutes && g_seconds == g_alarm_seconds)
  {
    g_alarm_ringing = true;
    g_buzz_countdown = ALARM_BUZZ_DURATION;
    PORTB |= (1 << BUZZER_PIN);
  }

  if (g_alarm_ringing)
  {
    if (g_buzz_countdown > 0)
    {
      g_buzz_countdown--;
    }
    else
    {
      g_alarm_ringing = false;
      PORTB &= ~(1 << BUZZER_PIN);
    }
  }

  g_time_updated = true;
}

#endif /* SIMULIDE_MODE */

/* =========================================================
 * Button handling  (polling with simple debounce)
 *
 * Buttons are active LOW (pressed = 0, released = 1).
 * We read, delay 20 ms, read again — if both reads agree the
 * button is pressed, we act. This avoids spurious triggers from
 * mechanical bounce without needing a hardware capacitor.
 * ========================================================= */

static bool button_pressed(uint8_t pin)
{
  if (!(PINA & (1 << pin)))
  {                           /* first read: LOW = pressed */
    _delay_ms(20);            /* wait out bounce period */
    if (!(PINA & (1 << pin))) /* second read confirms */
      return true;
  }
  return false;
}

static void handle_buttons(void)
{
  /* --- Reset button (PA0): always returns clock to 00:00:00 --- */
  if (button_pressed(BTN_RESET))
  {
    cli(); /* disable interrupts while modifying shared variables */
    g_seconds = 0;
    g_minutes = 0;
    g_hours = 0;
    g_alarm_ringing = false;
    g_buzz_countdown = 0;
    PORTB &= ~((1 << WAKE_LED_PIN) | (1 << BUZZER_PIN));
    g_mode = MODE_CLOCK;
    sei();
    usart_print("TIME RESET TO 00:00:00\r\n");
    /* Wait for button release before continuing */
    while (!(PINA & (1 << BTN_RESET)))
      ;
    _delay_ms(20);
    return;
  }

  /* --- Set-alarm button (PA1): cycles through alarm-set sub-modes --- */
  if (button_pressed(BTN_SET_ALARM))
  {
    switch (g_mode)
    {
    case MODE_CLOCK:
      g_mode = MODE_SET_ALARM_H;
      break;
    case MODE_SET_ALARM_H:
      g_mode = MODE_SET_ALARM_M;
      break;
    case MODE_SET_ALARM_M:
      g_mode = MODE_SET_ALARM_S;
      break;
    case MODE_SET_ALARM_S:
      g_mode = MODE_CLOCK;
      break;
    }
    while (!(PINA & (1 << BTN_SET_ALARM)))
      ;
    _delay_ms(20);
  }

  /* --- Increment button (PA2): increments the field being set --- */
  if (button_pressed(BTN_INCREMENT))
  {
    switch (g_mode)
    {
    case MODE_CLOCK:
      break; /* does nothing in clock mode */
    case MODE_SET_ALARM_H:
      g_alarm_hours = (g_alarm_hours + 1) % 24;
      break;
    case MODE_SET_ALARM_M:
      g_alarm_minutes = (g_alarm_minutes + 1) % 60;
      break;
    case MODE_SET_ALARM_S:
      g_alarm_seconds = (g_alarm_seconds + 1) % 60;
      break;
    }
    while (!(PINA & (1 << BTN_INCREMENT)))
      ;
    _delay_ms(20);
  }
}

/* =========================================================
 * Display update — sends current state over USART
 * ========================================================= */

static void update_display(void)
{
  /* Take a snapshot with interrupts off so we read a consistent state */
  cli();
  uint8_t h = g_hours, m = g_minutes, s = g_seconds;
  uint8_t ah = g_alarm_hours, am = g_alarm_minutes, as_ = g_alarm_seconds;
  AppMode mode = g_mode;
  g_time_updated = false;
  sei();

  usart_print_time(h, m, s, "TIME: ");

  /* Show the alarm time and current mode */
  usart_print_time(ah, am, as_, "ALRM: ");

  switch (mode)
  {
  case MODE_CLOCK:
    usart_print("MODE: LOW POWER\r\n");
    break;
  case MODE_SET_ALARM_H:
    usart_print("MODE: SET ALARM HH\r\n");
    break;
  case MODE_SET_ALARM_M:
    usart_print("MODE: SET ALARM MM\r\n");
    break;
  case MODE_SET_ALARM_S:
    usart_print("MODE: SET ALARM SS\r\n");
    break;
  }
  usart_print("---\r\n");
}

/* =========================================================
 * Sleep — CPU sleeps between timer events to save power.
 *
 * Power-save mode keeps Timer2 and its async oscillator running
 * while the CPU and most other peripherals are halted.
 * The Timer2 overflow interrupt wakes the CPU every second.
 *
 * In SimulIDE this may not be fully modelled; the code still
 * compiles and runs correctly (sleep_cpu just resumes immediately).
 * ========================================================= */

static void enter_sleep(void)
{
  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  sleep_enable();
  sei();
  sleep_cpu();     /* CPU halts here until next interrupt */
  sleep_disable(); /* resume here after ISR returns */
}

/* =========================================================
 * main
 * ========================================================= */

int main(void)
{
  gpio_init();
  usart_init();
  timer2_init();
  sei(); /* enable global interrupts */

  usart_print("\r\n== ATmega32 Async Timer2 Timekeeping ==\r\n");
#ifdef SIMULIDE_MODE
  usart_print("(SimulIDE mode: internal clock fallback)\r\n");
#else
  usart_print("(Hardware mode: 32.768 kHz TOSC crystal)\r\n");
#endif
  usart_print("Buttons: PA0=Reset  PA1=SetAlarm  PA2=Increment\r\n");
  usart_print("---\r\n");

  /* Print initial state */
  update_display();

  while (1)
  {
    handle_buttons();

    /* Refresh terminal whenever the ISR has ticked a new second */
    if (g_time_updated)
      update_display();

    /*
     * Sleep between events.
     * In power-save mode the CPU halts, Timer2 keeps running,
     * and the overflow ISR wakes us up every second.
     */
    // enter_sleep();
  }

  return 0; /* never reached */
}
