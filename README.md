# Generic 60 cm Auto Address

Controlador STM32F103C8 para dígitos de 7 segmentos de 60 cm (preços de combustível).
Cada dígito tem uma placa de cima com o controlador e uma placa de baixo rodada 180°,
com 4 × SCT2024 (18 mA por saída) e LEDs brancos a 24 V. Os dígitos ligam-se em
serpentina (cabos 1:1 `OUTPUT` → `INPUT`) com RS485 e auto-endereçamento pelo
`SEL_IN`/`SEL_OUT`.

- `Digito_STM32/` — sketch Arduino (core STM32). A lógica sem Arduino está em `digito.h`.
- `PROTOCOLO.md` — tramas RS485 e sequência de endereçamento para o master.
- `tools/test_digito.cpp` — testes de `digito.h` no PC:
  `g++ -std=c++11 -Wall -o test_digito tools/test_digito.cpp && ./test_digito`
- `tools/digito_pc.py` — envia comandos por um adaptador USB-RS485 (sem master).

## Compilação
Arduino IDE, core "STM32 MCU based boards" (STMicroelectronics):
Generic STM32F1 → BluePill F103C8, U(S)ART "Enabled (no generic 'Serial')", Optimize -Os.

## Jumpers (lidos no arranque)
- MODE1: teste 0..9 com o ponto, 1 s cada.
- MODE1 + MODE2: acende as 64 saídas uma de cada vez (confirma o mapa dos segmentos).
