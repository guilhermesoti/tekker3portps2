#!/usr/bin/env python3
"""Copia um arquivo de ../gerado para build/r/ com ajustes de desempenho.
gerado/ nunca é editado; só a cópia compilada muda.

1. "CPUState* restrict cpu" nas definições func_*: sem isso o compilador supõe
   que cada escrita na RAM do PS1 pode alterar cpu->gpr[] e relê tudo.
2. No despacho: cache direto (PC -> entrada) na frente da busca binária na
   tabela de 22.325 funções. A tabela é constante, então o cache nunca fica
   errado; a validação de código alterado (overlays) continua igual.
3. "cpu->read_word(" / "cpu->write_word(" (e half/byte) viram chamadas
   diretas às funções rápidas de fast_memory.h (antes eram macros que
   reescreviam o "cpu->", sem parênteses externos: auditoria V19 3.3).
   Também vale para SLUS_004.02_decls.h (psx_swl/psx_swr), copiado para
   build/r/ — o #include "..." dos shards acha a cópia primeiro.
"""
import re, sys

ent, sai = sys.argv[1], sys.argv[2]
s = open(ent, encoding='utf-8').read()
# 6. GTE em linha (V23): mtc2/mfc2/lwc2 sem chamada de função; o gancho
# de precisão de vértices (vazio no PS2) some.
s = re.sub(r'gte_precision_store_word\([^;]*\);', '', s)
s = s.replace('psx_cyc_lwc2_read(cpu, ', 'ps2_fast_read_word(')
s = s.replace('gte_write_data(cpu, ', 'gte_wr_data_i(cpu, ')
s = s.replace('gte_read_data(cpu, ', 'gte_rd_data_i(cpu, ')
# V30: comandos GTE com opcode constante -> wrapper especializado (gte.c)
_GTE_K = set('108041B 118043F 138041C 1400006 145545C 14B465D 178000C 180001 190003D 198003D 280030 41E012 486012 49E012 4A3268 4A465C 4A6412 A00428 A80428'.split())
def _gte_k(m):
    k = m.group(1).upper().lstrip('0') or '0'
    return 'gte_k_%s(cpu);' % k if k in _GTE_K else m.group(0)
s = re.sub(r'gte_execute\(cpu, 0x([0-9A-Fa-f]+)\);', _gte_k, s)
s, n_mem = re.subn(r'\bcpu->(read|write)_(word|half|byte)\(', r'ps2_fast_\1_\2(', s)
if re.search(r'cpu->(read|write)_(word|half|byte)\b', s):
    sys.exit('transformar_gerado: acesso de memoria por ponteiro que nao foi trocado')
s = re.sub(r'^(void func_[0-9A-Fa-f_]+)\(CPUState\* cpu\)', r'\1(CPUState* restrict cpu)', s, flags=re.M)

# 7. Rotinas nativas (V26, tools/nativo.py + runtime/nativo_ganchos.c): na
# entrada normal (cpu->pc == 0) a função chama a versão nativa.
import os
_lista = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'nativas.txt')
NATIVAS = [l.split('#')[0].strip().split(':')[0] for l in open(_lista)
           if l.split('#')[0].strip()]
# V32: conta as atualizações da lógica da luta (entrada de 8002AE58)
cab = 'void func_8002AE58(CPUState* restrict cpu)\n{\n'
if cab in s:
    s = s.replace(cab, cab + '    { extern unsigned long g_ps2_luta_atualiz; if (cpu->pc == 0u) g_ps2_luta_atualiz++; }\n', 1)
for a in NATIVAS:
    cab = 'void func_%s(CPUState* restrict cpu)\n{\n' % a
    if cab in s:
        s = s.replace(cab, cab + '''    { extern int ps2_nat_%s_gancho(CPUState *cpu);
      if (cpu->pc == 0u && ps2_nat_%s_gancho(cpu)) return; }
''' % (a, a), 1)

if ent.endswith('_dispatch.c'):
    velho = 'static const PsxGameDispatchEntry* psx_game_find_entry(uint32_t addr) {'
    if velho not in s:
        sys.exit('transformar_gerado: busca do despacho nao encontrada')
    s = s.replace(velho, 'static const PsxGameDispatchEntry* psx_game_find_entry_busca(uint32_t addr) {', 1)
    i = s.index('psx_game_find_entry_busca(uint32_t addr) {')
    fim = s.index('\n}\n', i) + 3
    cache = '''
/* ps2: cache direto na frente da busca binária (tools/transformar_gerado.py) */
unsigned long g_ps2_desp_buscas, g_ps2_desp_acertos;
static const PsxGameDispatchEntry* psx_game_find_entry(uint32_t addr) {
    static struct { uint32_t addr; const PsxGameDispatchEntry* e; } cache[8192];
    uint32_t i = (addr >> 2) & 8191u;
    const PsxGameDispatchEntry* e;
    if (cache[i].e && cache[i].addr == addr) { g_ps2_desp_acertos++; return cache[i].e; }
    g_ps2_desp_buscas++;
    e = psx_game_find_entry_busca(addr);
    if (e) { cache[i].addr = addr; cache[i].e = e; }
    return e;
}
'''
    s = s[:fim] + cache + s[fim:]

    # 4. Perfil por função (V22): o tempo entre um despacho e o seguinte vai
    # para a função despachada antes (inclui o laço e as chamadas da BIOS que
    # ela fez; sobrevive ao longjmp das interrupções).
    velho = '''    entry->fn(cpu);
    return 1;
}'''
    if velho not in s:
        sys.exit('transformar_gerado: chamada do despacho nao encontrada')
    s = s.replace(velho, '''#ifdef _EE
    {
        uint32_t c;
        __asm__ volatile("mfc0 %0, $9" : "=r"(c));
        g_ps2_prof_fn[g_ps2_prof_ult] += c - g_ps2_prof_t;
        g_ps2_prof_t = c;
        g_ps2_prof_ult = (uint32_t)(entry - k_psx_game_dispatch);
    }
#endif
    entry->fn(cpu);
    return 1;
}

/* Imprime as funções que mais gastaram tempo desde a última chamada. */
void ps2_prof_funcoes(void)
{
    enum { TOP = 16 };
    uint32_t top_ad[TOP], top_c[TOP];
    unsigned long long total = 0;
    uint32_t i = 0, k;
    for (k = 0; k < TOP; k++) { top_ad[k] = 0; top_c[k] = 0; }
    while (i < PSX_GAME_DISPATCH_COUNT) {
        /* entradas seguidas da mesma função (continuações) somam juntas */
        void (*fn)(CPUState*) = k_psx_game_dispatch[i].fn;
        uint32_t ad = k_psx_game_dispatch[i].addr, soma = 0;
        while (i < PSX_GAME_DISPATCH_COUNT && k_psx_game_dispatch[i].fn == fn) {
            soma += g_ps2_prof_fn[i]; g_ps2_prof_fn[i] = 0; i++;
        }
        total += soma;
        for (k = 0; k < TOP; k++)
            if (soma > top_c[k]) {
                uint32_t j;
                for (j = TOP - 1; j > k; j--) { top_c[j] = top_c[j - 1]; top_ad[j] = top_ad[j - 1]; }
                top_c[k] = soma; top_ad[k] = ad;
                break;
            }
    }
    printf("funcoes (%% do tempo total, inclui GPU/GTE/IRQ feitos dentro delas; %llu Mciclos):", total / 1000000ull);
    for (k = 0; k < TOP && top_c[k]; k++)
        printf(" %08lx:%llu.%llu", (unsigned long)top_ad[k],
               (unsigned long long)top_c[k] * 100ull / (total ? total : 1),
               ((unsigned long long)top_c[k] * 1000ull / (total ? total : 1)) % 10ull);
    printf("\\n");
}''', 1)
    s = s.replace('/* ps2: cache direto na frente da busca binária', '''/* ps2: perfil por função (V22) */
#include <stdio.h>
static uint32_t g_ps2_prof_fn[PSX_GAME_DISPATCH_COUNT];
static uint32_t g_ps2_prof_t, g_ps2_prof_ult;
/* ps2: cache direto na frente da busca binária''', 1)

    # 8. V31: despacho mais barato.
    #  - validade do código: cada entrada guarda a "versão" (g_cod_ver) em que
    #    foi conferida; só confere de novo se alguma escrita sujou um grânulo
    #    limpo desde então (ram.h: ps2_cod_marcar sobe a versão);
    #  - sem a chamada de psx_vsync_query_hle_try (no PS2 sempre devolve 0);
    #  - checagem de interrupção em linha (fast_memory.h).
    velho = """    if (entry->range_count == 0 || !dirty_ram_text_native_ok_ranges_from(
            &k_psx_game_code_ranges[entry->range_index].lo, entry->range_count, addr)) return 0;
    psx_check_interrupts_dispatch_entry(cpu, addr);"""
    if velho not in s:
        sys.exit('transformar_gerado: validacao do despacho nao encontrada')
    s = s.replace(velho, """    {
        static uint32_t ver_ok[PSX_GAME_DISPATCH_COUNT];
        uint32_t idx = (uint32_t)(entry - k_psx_game_dispatch);
        if (__builtin_expect(ver_ok[idx] != g_cod_ver, 0)) {
            uint32_t v = g_cod_ver;
            if (entry->range_count == 0 || !dirty_ram_text_native_ok_ranges_from(
                    &k_psx_game_code_ranges[entry->range_index].lo, entry->range_count, addr)) return 0;
            ver_ok[idx] = v;
        }
    }
    psx_check_interrupts_at(cpu, addr);""", 1)
    velho = """    {
        extern int psx_vsync_query_hle_try(CPUState* cpu, uint32_t addr);
        if (psx_vsync_query_hle_try(cpu, addr)) return 1;
    }"""
    if velho not in s:
        sys.exit('transformar_gerado: vsync_query do despacho nao encontrado')
    s = s.replace(velho, "    /* (V31: psx_vsync_query_hle_try removido: no PS2 sempre 0) */", 1)
    if '#include "fast_memory.h"' not in s:
        s = s.replace('/* ps2: perfil por função (V22) */', '#include "fast_memory.h"\n/* ps2: perfil por função (V22) */', 1)

open(sai, 'w', encoding='utf-8').write(s)
