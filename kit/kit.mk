# kit.mk — inclua no Makefile do projeto (ps2sdk) ANTES de montar EE_OBJS:
#   KIT_DIR := ../kit
#   KIT_BUILD := build
#   include $(KIT_DIR)/kit.mk
#   EE_OBJS += $(KIT_OBJS)
#   EE_INCS += $(KIT_INCS)
#   EE_LIBS += $(KIT_LIBS)
# Requer PS2SDK. Os módulos USB do IOP vão embutidos no ELF (bin2c).

KIT_DIR   ?= kit
KIT_BUILD ?= build
KIT_INCS  := -I$(KIT_DIR)/include
KIT_LIBS  := -lpad -lfileXio -lpatches
KIT_IRX_NOMES := iomanX fileXio usbd bdm bdmfs_fatfs usbmass_bd
KIT_IRX_OBJS  := $(addprefix $(KIT_BUILD)/kit_irx_,$(addsuffix .o,$(KIT_IRX_NOMES)))
KIT_OBJS  := $(KIT_BUILD)/kit_controle.o $(KIT_BUILD)/kit_armazenamento.o \
             $(KIT_BUILD)/kit_diagnostico.o $(KIT_IRX_OBJS)

$(KIT_BUILD)/kit_%.o: $(KIT_DIR)/src/%.c $(wildcard $(KIT_DIR)/include/kit/*.h)
	@mkdir -p $(@D)
	$(EE_CC) -D_EE -G0 -O2 -Wall $(KIT_INCS) -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -c $< -o $@

$(KIT_BUILD)/kit_irx_%.c: $(PS2SDK)/iop/irx/%.irx
	@mkdir -p $(@D)
	$(PS2SDK)/bin/bin2c $< $@ $*_irx

$(KIT_BUILD)/kit_irx_%.o: $(KIT_BUILD)/kit_irx_%.c
	$(EE_CC) -D_EE -G0 -O2 -c $< -o $@

.PRECIOUS: $(KIT_BUILD)/kit_irx_%.c
