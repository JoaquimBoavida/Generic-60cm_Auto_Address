/*******************************************************************************
 *  Logica do digito de 60 cm sem dependencias do Arduino (testada no PC com
 *  tools/test_digito.cpp): tipos de letra, mapa dos segmentos nos SCT2024 e
 *  receptor das tramas do protocolo (PROTOCOLO.md).
 ******************************************************************************/
#pragma once
#include <stdint.h>

/* ===========================================================================
 *  SEGMENTOS
 *  Glifo: bit 0..6 = a b c d e f g, bit 7 = ponto decimal.
 * ========================================================================= */
#define SEG_A   0x01
#define SEG_B   0x02
#define SEG_C   0x04
#define SEG_D   0x08
#define SEG_E   0x10
#define SEG_F   0x20
#define SEG_G   0x40
#define SEG_DP  0x80

static uint8_t glifo(char c)
{
  switch (c) {
    case '0': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
    case '1': return SEG_B | SEG_C;
    case '2': return SEG_A | SEG_B | SEG_D | SEG_E | SEG_G;
    case '3': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_G;
    case '4': return SEG_B | SEG_C | SEG_F | SEG_G;
    case '5': return SEG_A | SEG_C | SEG_D | SEG_F | SEG_G;
    case '6': return SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
    case '7': return SEG_A | SEG_B | SEG_C;
    case '8': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
    case '9': return SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G;
    case '-': return SEG_G;
    case '_': return SEG_D;
    case 'A': return SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;
    case 'b': return SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
    case 'C': return SEG_A | SEG_D | SEG_E | SEG_F;
    case 'c': return SEG_D | SEG_E | SEG_G;
    case 'd': return SEG_B | SEG_C | SEG_D | SEG_E | SEG_G;
    case 'E': return SEG_A | SEG_D | SEG_E | SEG_F | SEG_G;
    case 'F': return SEG_A | SEG_E | SEG_F | SEG_G;
    case 'G': return SEG_A | SEG_C | SEG_D | SEG_E | SEG_F;
    case 'H': return SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;
    case 'h': return SEG_C | SEG_E | SEG_F | SEG_G;
    case 'I': return SEG_E | SEG_F;
    case 'J': return SEG_B | SEG_C | SEG_D | SEG_E;
    case 'k': return SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;   /* como o "H" */
    case 'L': return SEG_D | SEG_E | SEG_F;
    case 'n': return SEG_C | SEG_E | SEG_G;
    case 'o': return SEG_C | SEG_D | SEG_E | SEG_G;
    case 'P': return SEG_A | SEG_B | SEG_E | SEG_F | SEG_G;
    case 'r': return SEG_E | SEG_G;
    case 'S': return SEG_A | SEG_C | SEG_D | SEG_F | SEG_G;
    case 't': return SEG_D | SEG_E | SEG_F | SEG_G;
    case 'U': return SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
    case 'u': return SEG_C | SEG_D | SEG_E;
    case 'y': return SEG_B | SEG_C | SEG_D | SEG_F | SEG_G;
    default:  return 0;                                        /* ' ' e o resto */
  }
}

/* Caracter da trama: bit 7 = ponto decimal, bits 0..6 = ASCII. */
static inline uint8_t glifoTrama(uint8_t c) { return glifo((char)(c & 0x7F)) | (c & 0x80); }

/* ===========================================================================
 *  MAPA NOS SCT2024  (OUTn = pino n+5 do SOP-24)
 *  Placa de cima : U1 OUT1/OUT2 = DP2/DP1, OUT3..6 = G, OUT7..15 = B
 *                  U2 OUT0..8 = F, OUT9..15 = A               (OUT0 do U1 livre)
 *  A placa de baixo e' igual mas rodada 180 graus: A -> d, B -> e, F -> c,
 *  G -> segunda metade do g. Assume-se B = cima a' direita e F = cima a' esquerda
 *  (confirmar com o teste de saidas, MODE1 + MODE2).
 * ========================================================================= */
#define M_A   0xFE00u      /* U2 */
#define M_F   0x01FFu      /* U2 */
#define M_B   0xFF80u      /* U1 */
#define M_G   0x0078u      /* U1 */
#define M_DP  0x0006u      /* U1, so' na placa de cima (DP1 + DP2) */

/* Ordem na cadeia: SDI -> cima U1 -> cima U2 -> baixo U1 -> baixo U2. */
enum { REG_CIMA_U1, REG_CIMA_U2, REG_BAIXO_U1, REG_BAIXO_U2, NREG };

static void mapaSegmentos(uint8_t g, uint16_t r[NREG])
{
  r[REG_CIMA_U1]  = (g & SEG_B ? M_B : 0) | (g & SEG_G ? M_G : 0) | (g & SEG_DP ? M_DP : 0);
  r[REG_CIMA_U2]  = (g & SEG_A ? M_A : 0) | (g & SEG_F ? M_F : 0);
  r[REG_BAIXO_U1] = (g & SEG_E ? M_B : 0) | (g & SEG_G ? M_G : 0);
  r[REG_BAIXO_U2] = (g & SEG_D ? M_A : 0) | (g & SEG_C ? M_F : 0);
}

/* ===========================================================================
 *  RECEPTOR DE TRAMAS
 *  '#' <cmd> <dados...> <chk> '!'   chk = XOR de <cmd> e dos <dados>.
 *  O comprimento vem do comando (e do n no 'w'), por isso os dados podem ter
 *  qualquer valor, incluindo '#' e '!'.
 * ========================================================================= */
#define CMD_RESET   'r'    /* '#' 'r' chk '!'                         */
#define CMD_ADDR    'a'    /* '#' 'a' n chk '!'                       */
#define CMD_WRITE   'w'    /* '#' 'w' inicio n c1..cn brilho chk '!'  */
#define CMD_BRILHO  'b'    /* '#' 'b' brilho grava chk '!'            */
#define CMD_TESTE   't'    /* '#' 't' modo chk '!'                    */

#define W_MAX       64     /* digitos por trama 'w' */
#define RX_GAP_MS   20u    /* pausa entre bytes que descarta a trama */

struct Receptor {
  uint8_t  buf[W_MAX + 8];
  uint8_t  n;              /* bytes recebidos (0 = a espera do '#') */
  uint8_t  total;          /* comprimento da trama (0 = ainda nao se sabe) */
  uint32_t tUltimo;
};

static uint8_t comprimento(const uint8_t *b, uint8_t n)
{
  switch (b[1]) {
    case CMD_RESET:  return 4;
    case CMD_ADDR:   return 5;
    case CMD_TESTE:  return 5;
    case CMD_BRILHO: return 6;
    case CMD_WRITE:
      if (n < 4) return 0;                       /* ainda falta o n */
      return (b[3] >= 1 && b[3] <= W_MAX) ? (uint8_t)(7 + b[3]) : 0xFF;
    default:         return 0xFF;                /* comando desconhecido */
  }
}

/* Da' um byte ao receptor. Devolve true quando ha' uma trama valida em r.buf
 * (r.buf[1] = comando, r.buf[2..] = dados); fica valida ate' ao byte seguinte. */
static bool receber(Receptor &r, uint8_t c, uint32_t agora)
{
  if (r.n && agora - r.tUltimo > RX_GAP_MS) r.n = 0;
  r.tUltimo = agora;

  if (r.n == 0) {
    if (c == '#') { r.buf[0] = c; r.n = 1; r.total = 0; }
    return false;
  }
  r.buf[r.n++] = c;
  if (r.total == 0 && r.n >= 2) {
    const uint8_t t = comprimento(r.buf, r.n);
    if (t == 0xFF) { r.n = (c == '#') ? 1 : 0; return false; }
    r.total = t;
  }
  if (r.total == 0 || r.n < r.total) return false;

  r.n = 0;
  if (c != '!') return false;
  uint8_t x = 0;
  for (uint8_t i = 1; i < r.total - 2; i++) x ^= r.buf[i];
  return x == r.buf[r.total - 2];
}
