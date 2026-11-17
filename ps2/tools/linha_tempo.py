#!/usr/bin/env python3
"""Linha do tempo 'diag:' do port (V41d/V42): resumo quadro a quadro.
Uso: python3 linha_tempo.py LOG [n_despejo]
Colunas: duração do quadro (fim da espera -> fim da espera seguinte), espera,
contador do timer 2 no teste de recuperação (800295F4), passos da lógica,
duração do passo 1 e do passo 2, chamadas da IRQ do timer 2 no quadro (1ª =
a que libera o quadro), listas desenhadas inteiras (e ms), handler do VBlank."""
import re, sys
L = open(sys.argv[1], errors='replace').read().splitlines()
qual = int(sys.argv[2]) if len(sys.argv) > 2 else 0
desp, cur = [], None
for l in L:
    l = re.sub(r'^\[[^\]]*\]\s*', '', l)
    if l.startswith('diag: linha do tempo'):
        cur = []; desp.append(cur); continue
    m = re.match(r'diag:\s+(\d+) (.{14}) ([0-9a-f]{8})$', l)
    if m and cur is not None:
        cur.append((int(m.group(1)), m.group(2).strip(), int(m.group(3), 16)))
ev = desp[qual]
print('despejos:', len(desp), '| eventos neste:', len(ev), '| tempos em ms')
# quadro = de 'espera fim' até o próximo 'espera fim'
blocos, q = [], None
for t, e, a in ev:
    if e == 'espera fim':
        if q: q['fim'] = t; blocos.append(q)
        q = {'ini': t, 'flag': a, 'ev': []}
    elif q: q['ev'].append((t, e, a))
hdr = '%4s %6s %6s %5s %5s %12s %4s %15s %13s %6s %5s' % ('#', 'quadro', 'espera', 'recup', 'passo', 'p1 / p2', 'T2n', 'T2 1a + resto', 'listas(n)', 'VB', 'OTC')
print(hdr)
soma = {}
for i, q in enumerate(blocos):
    luta = [t for t, e, a in q['ev'] if e == 'luta']
    esp = [t for t, e, a in q['ev'] if e == 'espera ini']
    rec = [a for t, e, a in q['ev'] if e == 'checa recup']
    t_esp_ini = esp[-1] if esp else q['fim']
    h, t2, vb, li, l0 = None, [], [], [], None
    for t, e, a in q['ev']:
        if e == 'handler ini': h = (t, a)
        elif e == 'handler fim' and h:
            (t2 if h[1] == 0x80029784 else vb if h[1] == 0x80092d34 else []).append(t - h[0]); h = None
        elif e == 'lista ini': l0 = t
        elif e == 'lista fim' and l0 is not None:
            if a >= 200: li.append(t - l0)
            l0 = None
    otc = sum(1 for t, e, a in q['ev'] if e == 'OTC')
    dur = (q['fim'] - q['ini']) / 1000; espera = (q['fim'] - t_esp_ini) / 1000
    p1 = ((luta[1] if len(luta) > 1 else t_esp_ini) - luta[0]) / 1000 if luta else 0
    p2 = (t_esp_ini - luta[1]) / 1000 if len(luta) > 1 else 0
    print('%4d %6.1f %6.1f %5s %5d %5.1f /%5.1f %4d %6.2f +%6.2f %8.2f(%2d) %6.2f %5d' % (
        i, dur, espera, ','.join(str(r) for r in rec) or '-', len(luta), p1, p2, len(t2),
        t2[0] / 1000 if t2 else 0, sum(t2[1:]) / 1000, sum(li) / 1000, len(li), sum(vb) / 1000, otc))
    for k, v in (('quadro', dur), ('espera', espera), ('p1', p1), ('p2', p2), ('t2', sum(t2) / 1000),
                 ('listas', sum(li) / 1000), ('vb', sum(vb) / 1000), ('nlistas', len(li)), ('nt2', len(t2))):
        soma.setdefault(k, []).append(v)
n = len(blocos)
if n:
    print('media (%d quadros): ' % n + ', '.join('%s %.2f' % (k, sum(v) / len(v)) for k, v in soma.items()))
