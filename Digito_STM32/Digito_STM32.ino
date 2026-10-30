/*******************************************************************************
 *  DIGITO DE 60 cm - controlador STM32F103C8 (placa "Digit_Controller_STM32")
 *
 *  Um controlador por digito, ligado a' placa de cima. Os 4 SCT2024 (2 na placa
 *  de cima, 2 na de baixo) formam uma cadeia de 64 bits. Os digitos ligam-se
 *  em serpentina (INPUT -> OUTPUT): RS485 em paralelo e SEL_OUT de um digito
 *  ao SEL_IN do seguinte, para o auto-enderecamento comandado pelo master.
 *  Protocolo em ../PROTOCOLO.md. So' recebe: o SP3485 fica sempre a receber.
 *
 *  Ligacoes (U1 = STM32F103C8T6, LQFP-48):
 *    PA9  TX / PA10 RX  USART1  -> SP3485 (DI / RO), 9600 8N1
 *    PB1  DE + /RE do SP3485    (pull-down de 10k, fica a LOW)
 *    PB3  CLK, PB4 LE, PB5 SDI  -> SCT2024 (VDD 3.3V)
 *    PA8  /OE (TIM1_CH1)        -> PWM do brilho, activo a LOW
 *    PB6  SELIN  <- EL357 (LOW = o digito anterior activou o SEL_OUT)
 *    PB7  SELOUT -> Q1 (HIGH = SEL_OUT a GND = activo)
 *    PB9  MODE1, PB8 MODE2      jumpers A1/A2 a GND (montado = LOW)
 *    PB13 LED de estado (activo a HIGH)
 *
 *  Jumpers lidos no arranque:
 *    MODE1                -> teste: 0..9 com o ponto, 1 s cada (nunca sai)
 *    MODE1 + MODE2        -> teste das 64 saidas, uma de cada vez (nunca sai)
 *    MODE2 sozinho        -> reservado (funcionamento normal)
 *
 *  LED de estado: pisca devagar = com endereco, depressa = sem endereco.
 *
 *  Arduino IDE  (core "STM32 MCU based boards" 2.x/3.x, STMicroelectronics)
 *    Board          : Generic STM32F1 series
 *    Board part nr  : BluePill F103C8
 *    U(S)ART support: Enabled (no generic 'Serial')
 *    Optimize       : Smallest (-Os default)
 ******************************************************************************/

#include <EEPROM.h>
#include <IWatchdog.h>
#include "digito.h"

/* ===========================================================================
 *  CONFIGURACAO
 * ========================================================================= */
#define PIN_OE         PA8
#define PIN_DE         PB1
#define PIN_SELIN      PB6
#define PIN_SELOUT     PB7
#define PIN_MODE1      PB9
#define PIN_MODE2      PB8
#define PIN_LED        PB13

#define BIT_CLK        (1u << 3)     /* PB3 */
#define BIT_LE         (1u << 4)     /* PB4 */
#define BIT_SDI        (1u << 5)     /* PB5 */

#define BRILHO_INICIAL 200           /* sem brilho gravado */
#define BRILHO_TESTE   200
#define LAMP_MS        1000UL        /* "8." ao ligar */
#define GRAVA_IDLE_MS  300UL         /* grava na flash so' com o barramento parado */

#if defined(STM32_CORE_VERSION_MAJOR) && STM32_CORE_VERSION_MAJOR >= 3
Uart RS(PA10, PA9);                  /* core 3.x */
#else
HardwareSerial RS(PA10, PA9);        /* core 2.x */
#endif

/* ===========================================================================
 *  DADOS PERSISTENTES (emulacao de EEPROM na ultima pagina da flash)
 * ========================================================================= */
#define PERSIST_MAGIC  0x44494731UL  /* "DIG1" */

struct Persist {
  uint32_t magic;
  uint8_t  endereco;                 /* 0 = sem endereco */
  uint8_t  brilho;
  uint8_t  livre[2];
  uint32_t crc;
};
static_assert(sizeof(Persist) <= E2END + 1, "Persist nao cabe na EEPROM emulada");

static Persist P;

/* FNV-1a sobre os bytes antes do crc. */
static uint32_t crcOf(const Persist &p)
{
  uint32_t h = 2166136261UL;
  const uint8_t *b = (const uint8_t *)&p;
  for (size_t i = 0; i < offsetof(Persist, crc); i++) { h ^= b[i]; h *= 16777619UL; }
  return h;
}

static void savePersist(void)
{
  P.crc = crcOf(P);
  const uint8_t *b = (const uint8_t *)&P;
  for (size_t i = 0; i < sizeof(P); i++) eeprom_buffered_write_byte(i, b[i]);
  eeprom_buffer_flush();
}

static void loadPersist(void)
{
  eeprom_buffer_fill();
  uint8_t *b = (uint8_t *)&P;
  for (size_t i = 0; i < sizeof(P); i++) b[i] = eeprom_buffered_read_byte(i);
  if (P.magic == PERSIST_MAGIC && P.crc == crcOf(P)) return;
  memset(&P, 0, sizeof(P));
  P.magic  = PERSIST_MAGIC;
  P.brilho = BRILHO_INICIAL;
}

/* ===========================================================================
 *  ESTADO
 * ========================================================================= */
static Receptor rx;
static uint8_t  endereco;            /* em uso (0 = sem endereco) */
static uint8_t  glifoAtual;
static uint8_t  brilho;
static uint8_t  teste;               /* 't': 0 = normal, 1 = "8.", 2 = endereco */
static bool     gravaPendente;
static bool     selNoInicio;         /* SEL_IN activo quando chegou o '#' da trama */
static HardwareTimer *pwm;

/* ===========================================================================
 *  SCT2024
 * ========================================================================= */
static inline void espera(void)
{
  for (uint8_t i = 0; i < 16; i++) __NOP();         /* ~0.5 us por meio periodo */
}

static void enviarRegistos(const uint16_t r[NREG])
{
  /* O primeiro bit enviado acaba no OUT15 do ultimo SCT2024 da cadeia. */
  for (int8_t k = NREG - 1; k >= 0; k--) {
    for (int8_t b = 15; b >= 0; b--) {
      GPIOB->BSRR = (r[k] >> b) & 1 ? BIT_SDI : (BIT_SDI << 16);
      espera();
      GPIOB->BSRR = BIT_CLK;
      espera();
      GPIOB->BSRR = BIT_CLK << 16;
    }
  }
  GPIOB->BSRR = BIT_LE;
  espera();
  GPIOB->BSRR = BIT_LE << 16;
}

static void mostrarGlifo(uint8_t g)
{
  uint16_t r[NREG];
  mapaSegmentos(g, r);
  enviarRegistos(r);
}

/* PWM2 no /OE: saida a LOW (LEDs acesos) enquanto CNT < CCR. CCR = 256 > ARR
 * deixa-a sempre a LOW (brilho maximo); 0 apaga. */
static void aplicarBrilho(uint8_t b)
{
  pwm->setCaptureCompare(1, b == 255 ? 256 : b, TICK_FORMAT);
}

static void atualizar(void)
{
  if (teste == 1)      mostrarGlifo(glifo('8') | SEG_DP);
  else if (teste == 2) mostrarGlifo(glifo('0' + endereco % 10) | (endereco >= 10 ? SEG_DP : 0));
  else                 mostrarGlifo(glifoAtual);
  aplicarBrilho(brilho);
}

/* ===========================================================================
 *  ENDERECAMENTO
 * ========================================================================= */
static inline void selOut(bool activo) { digitalWrite(PIN_SELOUT, activo ? HIGH : LOW); }

/* SEL_IN activo = o digito anterior ainda nao tem endereco. */
static bool selInActivo(void)
{
  uint8_t n = 0;
  for (uint8_t i = 0; i < 3; i++) { n += digitalRead(PIN_SELIN) == LOW; delayMicroseconds(50); }
  return n >= 2;
}

/* ===========================================================================
 *  COMANDOS
 * ========================================================================= */
static void comando(const uint8_t *b)
{
  switch (b[1]) {
    case CMD_RESET:                  /* todos ficam sem endereco e bloqueiam o seguinte */
      endereco = 0;
      selOut(true);
      glifoAtual = glifo('-');
      teste = 0;
      break;

    case CMD_ADDR:                   /* so' o primeiro sem endereco da cadeia */
      if (endereco != 0 || b[2] == 0 || selNoInicio) return;
      endereco = b[2];
      selOut(false);
      teste = 2;                     /* mostra o endereco ate' a' primeira trama 'w' */
      if (P.endereco != endereco) { P.endereco = endereco; gravaPendente = true; }
      break;

    case CMD_WRITE: {
      const uint8_t ini = b[2], n = b[3];
      if (endereco == 0 || endereco < ini || endereco - ini >= n) return;
      glifoAtual = glifoTrama(b[4 + endereco - ini]);
      brilho     = b[4 + n];
      teste      = 0;
      break;
    }

    case CMD_BRILHO:
      brilho = b[2];
      if (b[3] == 1 && P.brilho != brilho) { P.brilho = brilho; gravaPendente = true; }
      break;

    case CMD_TESTE:
      teste = b[2] <= 2 ? b[2] : 0;
      break;
  }
  atualizar();
}

/* ===========================================================================
 *  TESTES LOCAIS (jumpers)
 * ========================================================================= */
static void testeDigitos(void)
{
  brilho = BRILHO_TESTE;
  aplicarBrilho(brilho);
  for (;;) {
    for (char c = '0'; c <= '9'; c++) {
      mostrarGlifo(glifo(c) | SEG_DP);
      digitalWrite(PIN_LED, !digitalRead(PIN_LED));
      delay(1000);
      IWatchdog.reload();
    }
  }
}

/* Acende uma saida de cada vez pela ordem da cadeia: cima U1 OUT0..15, cima U2,
 * baixo U1, baixo U2. Serve para confirmar o mapa dos segmentos. */
static void testeSaidas(void)
{
  brilho = BRILHO_TESTE;
  aplicarBrilho(brilho);
  for (;;) {
    for (uint8_t i = 0; i < NREG * 16; i++) {
      uint16_t r[NREG] = { 0, 0, 0, 0 };
      r[i / 16] = (uint16_t)(1u << (i % 16));
      enviarRegistos(r);
      digitalWrite(PIN_LED, !digitalRead(PIN_LED));
      delay(500);
      IWatchdog.reload();
    }
  }
}

/* ===========================================================================
 *  ARRANQUE E CICLO
 * ========================================================================= */
void setup(void)
{
  /* /OE a HIGH antes de mais nada: os SCT2024 arrancam com dados aleatorios. */
  pinMode(PIN_OE, OUTPUT);
  digitalWrite(PIN_OE, HIGH);

  /* PB3/PB4 sao do JTAG por omissao: fica so' o SWD. */
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_AFIO_REMAP_SWJ_NOJTAG();
  pinMode(PB3, OUTPUT); digitalWrite(PB3, LOW);
  pinMode(PB4, OUTPUT); digitalWrite(PB4, LOW);
  pinMode(PB5, OUTPUT); digitalWrite(PB5, LOW);

  pinMode(PIN_DE, OUTPUT);     digitalWrite(PIN_DE, LOW);       /* so' recebe */
  pinMode(PIN_SELOUT, OUTPUT); selOut(false);
  pinMode(PIN_SELIN, INPUT_PULLUP);
  pinMode(PIN_MODE1, INPUT_PULLUP);
  pinMode(PIN_MODE2, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);    digitalWrite(PIN_LED, HIGH);

  mostrarGlifo(0);

  /* TIM1: 72 MHz / 70 / 256 = ~4 kHz, PWM2 no canal 1 (PA8). */
  pwm = new HardwareTimer(TIM1);
  pwm->setPrescaleFactor(70);
  pwm->setOverflow(256, TICK_FORMAT);
  pwm->setMode(1, TIMER_OUTPUT_COMPARE_PWM2, PIN_OE);
  pwm->setCaptureCompare(1, 0, TICK_FORMAT);
  pwm->resume();

  loadPersist();
  endereco = P.endereco;
  brilho   = P.brilho;

  IWatchdog.begin(4000000UL);                       /* 4 s */

  const bool m1 = digitalRead(PIN_MODE1) == LOW;
  const bool m2 = digitalRead(PIN_MODE2) == LOW;
  if (m1 && m2) testeSaidas();                      /* nao regressam */
  if (m1)       testeDigitos();

  /* Teste de segmentos e depois "8" (ou "-" sem endereco) ate' chegar uma trama. */
  teste = 1;
  atualizar();
  delay(LAMP_MS);
  teste = 0;
  glifoAtual = endereco ? glifo('8') : glifo('-');
  atualizar();

  RS.begin(9600);
}

void loop(void)
{
  IWatchdog.reload();

  while (RS.available()) {
    /* O SEL_IN le-se no inicio da trama: no fim do 'a' o digito anterior larga
     * o SEL_OUT e o seguinte nao pode ver isso antes de tempo. */
    if (rx.n == 0) selNoInicio = selInActivo();
    if (receber(rx, (uint8_t)RS.read(), millis())) comando(rx.buf);
  }

  /* A escrita na flash para o CPU uns ms (perdem-se bytes): so' com o barramento
   * parado. O master repete as tramas, por isso nada se perde de vez. */
  if (gravaPendente && millis() - rx.tUltimo > GRAVA_IDLE_MS) {
    gravaPendente = false;
    savePersist();
  }

  static uint32_t tLed = 0;
  if (millis() - tLed >= (endereco ? 500UL : 100UL)) {
    tLed = millis();
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }
}
