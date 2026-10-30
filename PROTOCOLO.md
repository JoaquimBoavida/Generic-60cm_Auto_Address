# Protocolo RS485 dos dígitos de 60 cm

Um controlador STM32 por dígito. Os dígitos ligam-se em serpentina com cabos 1:1
do `OUTPUT` de um dígito ao `INPUT` do seguinte. Esses cabos levam o RS485 em
paralelo e o `SEL_OUT` → `SEL_IN` usado no auto-endereçamento. Os dígitos **só
recebem**: o master nunca tem resposta.

- 9600 8N1.
- Uma cadeia por barramento. Duas cadeias no mesmo barramento teriam dois
  dígitos "primeiros", e esses dois ficavam com o mesmo endereço.

## Formato da trama

```
'#'  <cmd>  <dados...>  <chk>  '!'
```

- `chk` = XOR do `<cmd>` com todos os `<dados>`.
- O comprimento é fixo para cada comando (no `w` depende do `n`), por isso os
  dados podem ter qualquer valor, incluindo `#` e `!`.
- Uma pausa de mais de **20 ms** entre bytes descarta a trama a meio. Envia cada
  trama de seguida, sem pausas.

| cmd | Trama | Bytes |
|---|---|---|
| `r` | `'#' 'r' chk '!'` | 4 |
| `a` | `'#' 'a' n chk '!'` | 5 |
| `w` | `'#' 'w' início n c1..cn brilho chk '!'` | 7 + n |
| `b` | `'#' 'b' brilho grava chk '!'` | 6 |
| `t` | `'#' 't' modo chk '!'` | 5 |

### `r` — começar o endereçamento
Todos os dígitos esquecem o endereço que estão a usar, activam o `SEL_OUT` e
mostram `-`. O endereço gravado só muda quando o dígito recebe um `a`.

### `a n` — atribuir o endereço `n` (1..255)
Só responde o primeiro dígito da cadeia que ainda não tem endereço, ou seja, o
que tem o `SEL_IN` inactivo. Fica com o endereço `n`, desactiva o `SEL_OUT` (o
que liberta o dígito seguinte) e mostra o algarismo das unidades do endereço,
com o ponto aceso a partir de 10. O endereço fica gravado na flash e o dígito
continua com ele depois de desligado.

### `w início n c1..cn brilho` — escrever
O dígito com o endereço `e`, com `início ≤ e < início + n`, mostra
`c[e − início + 1]` com o `brilho` (0..255). Um `w` aceita até 64 dígitos.

- `c` = carácter ASCII. O bit 7 acende o ponto decimal (`'5' | 0x80` = "5.").
- Caracteres válidos: `0`–`9`, espaço, `-`, `_`, `A b C c d E F G H h I J k L n
  o P r S t U u y`. Qualquer outro fica apagado.
- O `w` também acaba com os testes `t 1` e `t 2`.

### `b brilho grava` — brilho de todos os dígitos
Aplica o brilho a todos os dígitos. Com `grava = 1`, guarda-o na flash como
brilho de arranque.

### `t modo` — teste remoto
- `0`: volta ao normal.
- `1`: todos os segmentos e o ponto acesos ("8.").
- `2`: mostra o endereço (unidades, com o ponto a partir de 10).

## Sequência de endereçamento (master)

```
r                     depois espera ≥ 100 ms
a 1                   depois espera ≥ 50 ms
a 2                   depois espera ≥ 50 ms
...
a N
```

- Os dígitos não respondem, por isso o master tem de saber quantos dígitos tem
  a cadeia (N).
- Endereçar uma vez na instalação chega, porque os endereços ficam gravados.
  Também se pode repetir a cada arranque.
- Os dígitos gravam na flash só quando o barramento está parado há 300 ms. A
  escrita pára o CPU durante alguns ms, e as tramas que cheguem nesse intervalo
  perdem-se. Por isso, depois do último `a`, faz uma pausa de ~0,5 s e só
  depois recomeça o `w`.

## Actualização dos valores

- O master envia `w` periodicamente com o valor de todos os dígitos, por
  exemplo um preço de 4 dígitos por trama: `w 1 4 "1.659" brilho` (o ponto vai
  no bit 7 do "1").
- Um dígito que reinicie mostra `8` até receber a primeira trama `w`, ou `-` se
  não tiver endereço.

Exemplos (hex):

```
r               23 72 72 21
a 1             23 61 01 60 21
w 1 4 "1234"    23 77 01 04 31 32 33 34 C8 BE 21     (brilho 200)
```

`tools/digito_pc.py` envia estes comandos a partir do PC por um adaptador
USB-RS485, por isso dá para testar os dígitos sem o master.
