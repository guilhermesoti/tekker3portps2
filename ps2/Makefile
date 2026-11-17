# Makefile — Tekken 3 PS2 (ps2sdk). Uso no WSL/Linux:
#   export PS2DEV=~/ps2dev/ps2dev PS2SDK=$PS2DEV/ps2sdk
#   export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin
#   make -j4        -> tekken3.elf
#   make test       -> testes do runtime no PC (gcc normal)
#
# O código de ../gerado NÃO é editado: o caminho rápido de memória entra
# por "-include runtime/fast_memory.h" só nesses arquivos.
# Sem PSX_ENABLE_BLOCK_CYCLES (contagem de ciclos) — de propósito.

EE_BIN = tekken3.elf
REF_INC = ../referencia/Tekken3Recompiled/psxrecomp/runtime/include
EE_INCS = -I../gerado -I$(REF_INC) -Iruntime -I../kit/include -I$(PS2DEV)/gsKit/include
EE_CFLAGS = -O2 -G0 -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1
# Otimização só do código gerado do jogo (GEN_OPT=-Os gera código menor:
# teste de cache de instruções). A variante aparece no painel.
GEN_OPT ?= -O2
JOGO_SUFIXO_FLAG := $(if $(filter -O2,$(GEN_OPT)),,-DJOGO_SUFIXO='" $(subst -,,$(GEN_OPT))"')
# build/gen_opt muda quando GEN_OPT muda: força recompilar o que depende dele
$(shell mkdir -p build; echo '$(GEN_OPT)' | cmp -s - build/gen_opt || echo '$(GEN_OPT)' > build/gen_opt)
build/main.o build/video_gs.o: build/gen_opt
EE_DBGINFOFLAGS =
EE_WARNFLAGS =

GAME_SHARDS := $(addprefix ../gerado/SLUS_004.02_full_,$(addsuffix .c,$(shell seq -w 0 44)))
GAME_SOURCES := $(GAME_SHARDS) ../gerado/SLUS_004.02_dispatch.c
GAME_OBJS := $(patsubst ../gerado/%.c,build/%.o,$(GAME_SOURCES))
# Bibliotecas compartilhadas (../kit): controle, armazenamento, diagnóstico
KIT_DIR := ../kit
KIT_BUILD := build
include $(KIT_DIR)/kit.mk
# Rotinas do jogo traduzidas para C nativo (tools/nativo.py), lidas do .bin
BIN_JOGO ?= Tekken 3 (USA) (Track 1).bin
NAT_VERIFICAR ?= 0
RUNTIME_OBJS := build/nativo.o build/nativo_ganchos.o build/main.o build/ram.o build/memory.o build/io.o build/gpu.o build/gs_render.o build/video_gs.o build/mdec.o build/cdrom.o build/pad.o $(KIT_OBJS) build/interp.o build/codigo.o build/bios_hle.o build/gte.o build/stubs.o
EE_OBJS := $(GAME_OBJS) $(RUNTIME_OBJS)
EE_LDFLAGS += -L$(PS2DEV)/gsKit/lib
EE_LIBS += -lgskit -ldmakit -lpad -lfileXio -lpatches -lm


# Pasta pronta para o pendrive (OPL: mass:/APPS/TEKKEN3/). O .bin do jogo
# NÃO é incluído: o usuário copia o dele.
pacote: $(EE_BIN)
	@rm -rf build/pacote && mkdir -p build/pacote/APPS/TEKKEN3
	cp $(EE_BIN) build/pacote/APPS/TEKKEN3/tekken3.elf
	printf 'title=Tekken 3 (port nativo)\nboot=tekken3.elf\n' > build/pacote/APPS/TEKKEN3/title.cfg
	cp docs/PS2_REAL.md build/pacote/APPS/TEKKEN3/LEIA-ME.md
	cp $(EE_BIN) ../tekken3.elf
	@echo "Pacote em build/pacote/APPS/TEKKEN3 (copie o .bin do jogo para la); copia em ../tekken3.elf"

.DEFAULT_GOAL := all
DEBUG_GPU ?= 0
DEBUG_VRAM ?= 0
GPU_FLAGS := $(if $(filter 1,$(DEBUG_GPU)),-DPS2_DEBUG_GPU,)
VRAM_FLAGS := $(if $(filter 1,$(DEBUG_VRAM)),-DPS2_DEBUG_VRAM,)
# AUTO_START=1: aperta START sozinho (teste sem controle no PCSX2 headless)
AUTO_START ?= 0
VRAM_FLAGS += $(if $(filter 1,$(AUTO_START)),-DPS2_AUTO_START,)
# VER_VRAM=1: mostra a VRAM inteira do PS1 (1024x512) na tela (diagnóstico)
VER_VRAM ?= 0
VRAM_FLAGS += $(if $(filter 1,$(VER_VRAM)),-DPS2_VER_VRAM,)
# LOG_BLOCOS: grava o log a cada N relatórios de ~2 s (15 = ~30 s); 0 = sem log
LOG_BLOCOS ?= 15
LOG_FLAGS := -DPS2_LOG_BLOCOS=$(LOG_BLOCOS)
build/gpu.o: EE_CFLAGS += $(GPU_FLAGS)
# FORCAR_IOP=1: testa no PCSX2 o caminho do PS2 real (reinicia o IOP, módulos USB)
build/main.o: EE_CFLAGS += $(if $(filter 1,$(FORCAR_IOP)),-DPS2_FORCAR_IOP,) $(JOGO_SUFIXO_FLAG) $(LOG_FLAGS)
build/video_gs.o: EE_CFLAGS += $(VRAM_FLAGS) $(JOGO_SUFIXO_FLAG) $(LOG_FLAGS)
build/gpu.o build/gs_render.o build/video_gs.o build/main.o: build/debug_flags

build/debug_flags: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' 'DEBUG_GPU=$(DEBUG_GPU) DEBUG_VRAM=$(DEBUG_VRAM) AUTO_START=$(AUTO_START) VER_VRAM=$(VER_VRAM) FORCAR_IOP=$(FORCAR_IOP) LOG_BLOCOS=$(LOG_BLOCOS)' > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

all: $(EE_BIN)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

# Cópia do código gerado com "restrict" no ponteiro da CPU (gerado/ não é
# editado): sem isso o compilador supõe que cada escrita na RAM do PS1 pode
# alterar cpu->gpr[] e relê os registros depois de cada store.
build/r/%.c: ../gerado/%.c tools/transformar_gerado.py tools/nativas.txt
	@mkdir -p $(@D)
	python3 tools/transformar_gerado.py $< $@

build/r/nativo.c: tools/nativo.py tools/nativas.txt
	@mkdir -p build/r
	python3 tools/nativo.py "$(BIN_JOGO)" $@ tools/nativas.txt

build/nativo.o: build/r/nativo.c runtime/fast_memory.h runtime/gte_regs.h runtime/ram.h
	$(EE_C_COMPILE) -c $< -o $@

build/nativo_ganchos.o: EE_CFLAGS += $(if $(filter 1,$(NAT_VERIFICAR)),-DPS2_NAT_VERIFICAR,)

build/r/SLUS_004.02_decls.h: ../gerado/SLUS_004.02_decls.h tools/transformar_gerado.py
	@mkdir -p build/r
	python3 tools/transformar_gerado.py $< $@

build/%.o: build/r/%.c build/r/SLUS_004.02_decls.h runtime/fast_memory.h runtime/ram.h build/gen_opt
	@mkdir -p $(@D)
	$(EE_C_COMPILE) $(GEN_OPT) -MMD -MP -DPS2_GENERATED_FAST_MEMORY=1 -include runtime/fast_memory.h -c $< -o $@

build/%.o: runtime/%.c runtime/ram.h runtime/memory.h runtime/io.h runtime/mdec.h runtime/bios_hle.h runtime/cdrom.h runtime/pad.h runtime/interp.h
	@mkdir -p $(@D)
	$(EE_C_COMPILE) -MMD -MP -c $< -o $@

.PRECIOUS: build/r/%.c

# Dependências geradas pelo compilador (-MMD): qualquer header alterado
# recompila quem o usa (achado F8 da revisão técnica).
-include $(EE_OBJS:.o=.d)

test:
	@mkdir -p build/tests
	gcc -std=c99 -Wall -Wextra -Werror runtime/ram.c runtime/codigo.c tests/ram_test.c -o build/tests/ram_test
	build/tests/ram_test
	gcc -std=c99 -Wall -Wextra -Werror -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -I$(REF_INC) -Iruntime \
	    runtime/ram.c runtime/codigo.c runtime/memory.c runtime/io.c runtime/gpu.c runtime/mdec.c runtime/cdrom.c runtime/pad.c tests/memory_test.c -o build/tests/memory_test
	build/tests/memory_test
	gcc -std=c99 -Wall -Wextra -Werror -I$(REF_INC) runtime/gte.c tests/gte_test.c -o build/tests/gte_test
	build/tests/gte_test
	gcc -std=c99 -Wall -Wextra -Werror runtime/gpu.c tests/gpu_test.c -o build/tests/gpu_test
	build/tests/gpu_test
	gcc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iruntime runtime/mdec.c runtime/ram.c runtime/codigo.c tests/mdec_test.c -lm -o build/tests/mdec_test
	build/tests/mdec_test
	gcc -std=c99 -Wall -Wextra -Werror -Dclock=relogio_teste -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -I$(REF_INC) -Iruntime \
	    tests/revisao_test.c runtime/ram.c runtime/codigo.c runtime/memory.c runtime/io.c runtime/gpu.c \
	    runtime/mdec.c runtime/cdrom.c runtime/pad.c runtime/bios_hle.c -o build/tests/revisao_test
	build/tests/revisao_test
	gcc -std=c99 -Wall -Wextra -Werror -Iruntime tests/revisao_cd_test.c runtime/ram.c runtime/codigo.c -o build/tests/revisao_cd_test
	build/tests/revisao_cd_test
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -Iruntime tests/cd_bfrd_test.c -o build/tests/cd_bfrd_test
	build/tests/cd_bfrd_test
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
	    -I$(REF_INC) -Iruntime tests/dma_ciclo_test.c -o build/tests/dma_ciclo_test
	build/tests/dma_ciclo_test > /dev/null
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
	    -I$(REF_INC) -Iruntime tests/dedup_test.c -o build/tests/dedup_test
	build/tests/dedup_test
	gcc -std=c99 -Wall -Wextra -Werror -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -I$(REF_INC) -Iruntime \
	    tests/revisao_bios_test.c runtime/ram.c runtime/codigo.c runtime/io.c runtime/gpu.c runtime/mdec.c \
	    runtime/cdrom.c runtime/pad.c -o build/tests/revisao_bios_test
	build/tests/revisao_bios_test
	gcc -std=c99 -fsanitize=undefined -fno-sanitize-recover=all -I$(REF_INC) runtime/gte.c tests/gte_test.c -o build/tests/gte_ubsan
	build/tests/gte_ubsan > /dev/null
	@echo "Testes OK"

clean:
	rm -rf build $(EE_BIN)

FORCE:

.PHONY: all test clean FORCE
