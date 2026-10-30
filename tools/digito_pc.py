"""Envia comandos aos digitos de 60 cm por um adaptador USB-RS485 (sem o master).

    pip install pyserial
    python digito_pc.py COM5 enderecar 4          # enderecos 1..4 pela ordem da cadeia
    python digito_pc.py COM5 escrever 1 "12.34" 200
    python digito_pc.py COM5 brilho 120 [--grava]
    python digito_pc.py COM5 teste 1               # 0 = normal, 1 = "8.", 2 = endereco

Protocolo em ../PROTOCOLO.md.
"""
import argparse
import time

import serial


def trama(cmd, dados=b""):
    chk = ord(cmd)
    for b in dados:
        chk ^= b
    return b"#" + cmd.encode() + bytes(dados) + bytes([chk]) + b"!"


def texto_para_digitos(txt):
    """'12.34' -> [ '1', '2'|0x80, '3', '4' ] (o ponto acende o DP do digito anterior)."""
    out = []
    for ch in txt:
        if ch == "." and out:
            out[-1] |= 0x80
        else:
            out.append(ord(ch) & 0x7F)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("porta")
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("enderecar")
    e.add_argument("n", type=int)
    w = sub.add_parser("escrever")
    w.add_argument("inicio", type=int)
    w.add_argument("texto")
    w.add_argument("brilho", type=int, nargs="?", default=200)
    b = sub.add_parser("brilho")
    b.add_argument("valor", type=int)
    b.add_argument("--grava", action="store_true")
    t = sub.add_parser("teste")
    t.add_argument("modo", type=int)
    a = ap.parse_args()

    with serial.Serial(a.porta, 9600) as s:
        if a.cmd == "enderecar":
            s.write(trama("r"))
            s.flush()
            time.sleep(0.1)
            for n in range(1, a.n + 1):
                s.write(trama("a", [n]))
                s.flush()
                time.sleep(0.05)
        elif a.cmd == "escrever":
            d = texto_para_digitos(a.texto)
            s.write(trama("w", [a.inicio, len(d)] + d + [a.brilho]))
        elif a.cmd == "brilho":
            s.write(trama("b", [a.valor, 1 if a.grava else 0]))
        elif a.cmd == "teste":
            s.write(trama("t", [a.modo]))
        s.flush()


if __name__ == "__main__":
    main()
