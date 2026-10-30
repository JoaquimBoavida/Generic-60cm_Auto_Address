/* Testa no PC a logica de digito.h (receptor de tramas e mapa dos segmentos).
 *   g++ -std=c++11 -Wall -o test_digito tools/test_digito.cpp && ./test_digito
 * (a partir da pasta Digito_STM32) */
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../Digito_STM32/digito.h"

static int falhas = 0;
#define CHECK(c) do { if (!(c)) { printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); falhas++; } } while (0)

/* Monta uma trama: '#' cmd dados chk '!' */
static std::vector<uint8_t> trama(uint8_t cmd, std::vector<uint8_t> d)
{
  std::vector<uint8_t> t = { '#', cmd };
  uint8_t x = cmd;
  for (uint8_t b : d) { t.push_back(b); x ^= b; }
  t.push_back(x);
  t.push_back('!');
  return t;
}

/* Da' os bytes ao receptor (1 ms entre bytes) e conta as tramas validas. */
static int alimentar(Receptor &r, const std::vector<uint8_t> &v, uint32_t &t, uint8_t *ultCmd = nullptr)
{
  int n = 0;
  for (uint8_t c : v) {
    if (receber(r, c, t)) { n++; if (ultCmd) *ultCmd = r.buf[1]; }
    t += 1;
  }
  return n;
}

int main(void)
{
  Receptor r = {};
  uint32_t t = 1000;
  uint8_t cmd = 0;

  /* Tramas simples */
  CHECK(alimentar(r, trama('r', {}), t, &cmd) == 1 && cmd == 'r');
  CHECK(alimentar(r, trama('a', { 7 }), t, &cmd) == 1 && cmd == 'a' && r.buf[2] == 7);
  CHECK(alimentar(r, trama('b', { 120, 1 }), t) == 1 && r.buf[2] == 120 && r.buf[3] == 1);
  CHECK(alimentar(r, trama('t', { 1 }), t) == 1);

  /* 'w' com dados iguais a '#' e '!' e brilho 35 ('#') */
  {
    auto w = trama('w', { 1, 4, '#', '!', '1' | 0x80, ' ', 35 });
    CHECK(alimentar(r, w, t) == 1);
    CHECK(r.buf[2] == 1 && r.buf[3] == 4 && r.buf[4] == '#' && r.buf[5] == '!' && r.buf[8] == 35);
  }

  /* Checksum errado, fim errado, n invalido, comando desconhecido */
  {
    auto w = trama('a', { 3 }); w[3] ^= 1;
    CHECK(alimentar(r, w, t) == 0);
    w = trama('a', { 3 }); w[4] = '?';
    CHECK(alimentar(r, w, t) == 0);
    CHECK(alimentar(r, trama('w', { 1, 0, 50 }), t) == 0);
    CHECK(alimentar(r, trama('w', { 1, W_MAX + 1, 50 }), t) == 0);
    CHECK(alimentar(r, trama('x', { 1 }), t) == 0);
    CHECK(alimentar(r, trama('a', { 9 }), t) == 1 && r.buf[2] == 9);   /* recupera */
  }

  /* Lixo antes, duas tramas seguidas, '#' repetido */
  {
    std::vector<uint8_t> v = { 0x55, '!', 0x00 };
    auto a = trama('a', { 1 }), b = trama('t', { 0 });
    v.insert(v.end(), a.begin(), a.end());
    v.insert(v.end(), b.begin(), b.end());
    CHECK(alimentar(r, v, t) == 2);
    v = { '#' };
    v.insert(v.end(), a.begin(), a.end());
    CHECK(alimentar(r, v, t) == 1);
  }

  /* Trama cortada: a pausa descarta-a e a seguinte e' aceite */
  {
    auto w = trama('w', { 1, 2, '1', '2', 100 });
    std::vector<uint8_t> meia(w.begin(), w.begin() + 4);
    CHECK(alimentar(r, meia, t) == 0);
    t += RX_GAP_MS + 1;
    CHECK(alimentar(r, trama('a', { 4 }), t) == 1 && r.buf[2] == 4);
    /* sem pausa a trama cortada "come" os bytes da seguinte */
    CHECK(alimentar(r, meia, t) == 0);
    CHECK(alimentar(r, trama('a', { 4 }), t) == 0);
    t += RX_GAP_MS + 1;
  }

  /* 'w' com 64 digitos */
  {
    std::vector<uint8_t> d = { 1, W_MAX };
    for (int i = 0; i < W_MAX; i++) d.push_back('0' + i % 10);
    d.push_back(200);
    CHECK(alimentar(r, trama('w', d), t) == 1 && r.buf[4 + W_MAX] == 200);
  }

  /* Mapa dos segmentos */
  {
    uint16_t m[NREG];
    mapaSegmentos(glifo('8') | SEG_DP, m);
    CHECK(m[REG_CIMA_U1] == 0xFFFE && m[REG_CIMA_U2] == 0xFFFF);
    CHECK(m[REG_BAIXO_U1] == 0xFFF8 && m[REG_BAIXO_U2] == 0xFFFF);
    mapaSegmentos(glifo('1'), m);                  /* b (cima U1) + c (baixo U2) */
    CHECK(m[REG_CIMA_U1] == M_B && m[REG_CIMA_U2] == 0 && m[REG_BAIXO_U1] == 0 && m[REG_BAIXO_U2] == M_F);
    mapaSegmentos(glifo('-'), m);
    CHECK(m[REG_CIMA_U1] == M_G && m[REG_BAIXO_U1] == M_G && m[REG_CIMA_U2] == 0 && m[REG_BAIXO_U2] == 0);
    mapaSegmentos(glifo(' '), m);
    CHECK(!m[0] && !m[1] && !m[2] && !m[3]);
    CHECK((M_A ^ M_F) == 0xFFFF && (M_A & M_F) == 0);
    CHECK((M_B | M_G | M_DP) == 0xFFFE && !(M_B & M_G) && !(M_G & M_DP));
    CHECK(glifoTrama('7' | 0x80) == (glifo('7') | SEG_DP));
  }

  printf(falhas ? "%d falhas\n" : "OK\n", falhas);
  return falhas != 0;
}
