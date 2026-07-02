#define F_CPU 8000000UL

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <util/delay.h>

// configs
#define WAKE_LED_PIN PB0
#define BUZZER_PIN PB1
#define BTN_RESET PA0
#define BTN_SET_ALARM PA1
#define BTN_INCREMENT PA2

#define ALARM_BUZZ_DURATION 3

#define WAKEUP_INTERVAL 10

#define BAUD 9600
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1) // = 51 at 8 MHz 

volatile uint8_t g_seconds = 0;
volatile uint8_t g_minutes = 0;
volatile uint8_t g_hours = 0;

volatile uint8_t g_alarm_seconds = 0;
volatile uint8_t g_alarm_minutes = 0;
volatile uint8_t g_alarm_hours = 0;

volatile bool g_time_updated = false; 
volatile bool g_alarm_ringing = false;
volatile uint8_t g_buzz_countdown = 0; 

#ifdef SIMULIDE_MODE
volatile uint8_t g_tick_count = 0;
#define TICKS_PER_SECOND 100 
#endif

typedef enum
{
  MODE_CLOCK,       
  MODE_SET_ALARM_H, 
  MODE_SET_ALARM_M, 
  MODE_SET_ALARM_S  
} AppMode;

volatile AppMode g_mode = MODE_CLOCK;


static void usart_init(void)
{
  UBRRH = (uint8_t)(UBRR_VALUE >> 8);
  UBRRL = (uint8_t)(UBRR_VALUE);
  UCSRB = (1 << TXEN);                                
  UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0); 
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

static void usart_print_time(uint8_t h, uint8_t m, uint8_t s,
                             const char *label)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %02u:%02u:%02u\r\n", label, h, m, s);
  usart_print(buf);
}

static void gpio_init(void)
{
  DDRB |= (1 << WAKE_LED_PIN) | (1 << BUZZER_PIN) | (1 << PB2) | (1 << PB3);
  PORTB &= ~((1 << WAKE_LED_PIN) | (1 << BUZZER_PIN) | (1 << PB2) | (1 << PB3)); 

  DDRA &= ~((1 << BTN_RESET) | (1 << BTN_SET_ALARM) | (1 << BTN_INCREMENT));
  PORTA |= (1 << BTN_RESET) | (1 << BTN_SET_ALARM) | (1 << BTN_INCREMENT);
}

static void timer2_init(void)
{
#ifndef SIMULIDE_MODE
  //Async clock source
  ASSR |= (1 << AS2);

  TCNT2 = 0;

  TCCR2 = (1 << CS22) | (1 << CS20);

  while (ASSR & ((1 << TCN2UB) | (1 << OCR2UB) | (1 << TCR2UB)))
    ;
  TIMSK |= (1 << TOIE2);

#else
  TCCR2 = (1 << WGM21)                               
          | (1 << CS22) | (1 << CS21) | (1 << CS20); 
  OCR2 = 77;                                         
  TIMSK |= (1 << OCIE2);                            
#endif
}

#ifndef SIMULIDE_MODE

ISR(TIMER2_OVF_vect)
{
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

  PORTB ^= (1 << PB3);
  g_time_updated = true;
}

#else 
ISR(TIMER2_COMP_vect)
{
  g_tick_count++;
  if (g_tick_count < TICKS_PER_SECOND)
    return; 
  g_tick_count = 0;

  
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

  PORTB ^= (1 << PB3);
  g_time_updated = true;
}

#endif 

static bool button_pressed(uint8_t pin)
{
  if (!(PINA & (1 << pin)))
  {                           
    _delay_ms(20);            
    if (!(PINA & (1 << pin))) 
      return true;
  }
  return false;
}

static void handle_buttons(void)
{
  if (button_pressed(BTN_RESET))
  {
    cli(); 
    g_seconds = 0;
    g_minutes = 0;
    g_hours = 0;
    g_alarm_ringing = false;
    g_buzz_countdown = 0;
    PORTB &= ~((1 << WAKE_LED_PIN) | (1 << BUZZER_PIN));
    g_mode = MODE_CLOCK;
    sei();
    usart_print("TIME RESET TO 00:00:00\r\n");
    while (!(PINA & (1 << BTN_RESET)))
      ;
    _delay_ms(20);
    return;
  }

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

  if (button_pressed(BTN_INCREMENT))
  {
    switch (g_mode)
    {
    case MODE_CLOCK:
      break; 
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

static void update_display(void)
{
  cli();
  uint8_t h = g_hours, m = g_minutes, s = g_seconds;
  uint8_t ah = g_alarm_hours, am = g_alarm_minutes, as_ = g_alarm_seconds;
  AppMode mode = g_mode;
  g_time_updated = false;
  sei();

  usart_print_time(h, m, s, "TIME: ");

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

static void enter_sleep(void)
{
#ifdef SIMULIDE_MODE
  return; 
#else
  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  sleep_enable();
  sei();
  sleep_cpu();
  sleep_disable();
#endif
}

int main(void)
{
  gpio_init();
  usart_init();
  timer2_init();
  sei(); 

  usart_print("\r\n== ATmega32 Async Timer2 Timekeeping ==\r\n");
#ifdef SIMULIDE_MODE
  usart_print("(SimulIDE mode: internal clock fallback)\r\n");
#else
  usart_print("(Hardware mode: 32.768 kHz TOSC crystal)\r\n");
#endif
  usart_print("Buttons: PA0=Reset  PA1=SetAlarm  PA2=Increment\r\n");
  usart_print("---\r\n");

  update_display();

  while (1)
  {
    PORTB |= (1 << PB2);
    handle_buttons();

    if (g_time_updated)
      update_display();
    PORTB &= ~(1 << PB2);
    enter_sleep();
  }

  return 0;
}
