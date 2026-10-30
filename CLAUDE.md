# Generic 60 cm Auto Address — notas do projeto

Dígitos de 60 cm para preços de combustível (não é o painel de acidentes da Indaqua).

## Hardware
- Controlador (PCB próprio, pads castelados): STM32F103C8 + SP3485 + EL357NB (SEL_IN) + transístor (SEL_OUT) + AP63203 (24 V → 3,3 V).
- PA9/PA10 USART1 RS485, PB1 DE+/RE (pull-down 10k, sempre LOW: só recebe), PB3 CLK, PB4 LE, PB5 SDI, PA8 /OE (TIM1_CH1, PWM2), PB6 SELIN (LOW = anterior activo), PB7 SELOUT (HIGH = activo), PB9 MODE1, PB8 MODE2, PB13 LED.
- Cadeia SDI → cima U1 → cima U2 → baixo U1 → baixo U2 (64 bits, o primeiro bit enviado acaba no OUT15 do último). Placa de baixo rodada 180°: A→d, B→e, F→c. Assume-se B = cima à direita (confirmar com MODE1+MODE2).
- SCT2024 com VDD 3,3 V vindo do controlador. Cadeias de 7 LEDs brancos nos 24 V têm pouca folga (~2,3 V).
- PB3/PB4 são do JTAG: `__HAL_AFIO_REMAP_SWJ_NOJTAG()` no arranque.

## Firmware
- Endereçamento comandado pelo master: `r` (todos activam o SEL_OUT) e depois `a n`; só aceita o que tem o SEL_IN inactivo, lido no `#` da trama (no fim do `a` o anterior já largou o SEL_OUT).
- Endereço e brilho na EEPROM emulada, gravados só com o barramento parado 300 ms (a escrita pára o CPU).
- Core STM32 3.x: classe `Uart` (não `HardwareSerial(rx, tx)`).
- O master ainda não está feito.
