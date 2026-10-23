CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CFLAGS := $(CFLAGS) -Ilibretro-common/include
CORE = machine.c arm60.c clio.c madam.c sport.c vdlp.c cel.c celexec.c cd.c cd_chd.c dsp.c joybus.c ph_audit.c
HDRS = machine.h arm60.h clio.h madam.h sport.h vdlp.h cel.h celexec.h cd.h cd_chd.h dsp.h joybus.h ph_audit.h

# nucleo libretro: .so (Linux/Batocera) ou .dll (MinGW/MSYS2); -fPIC so onde faz sentido (o MinGW avisa que o ignora)
TRIPLE := $(shell $(CC) -dumpmachine 2>/dev/null)
ifneq (,$(or $(findstring mingw,$(TRIPLE)),$(findstring windows,$(TRIPLE)),$(findstring cygwin,$(TRIPLE))))
  LIBEXT = .dll
  PICFLAG =
else
  LIBEXT = .so
  PICFLAG = -fPIC
endif
LIBRETRO = phoenix_libretro$(LIBEXT)

# Leitor de CHD: rchd + codecs, copiados do libretro-common do opera-libretro (mesmos arquivos e flags do Makefile.common
# dele com HAVE_CHD=1). Compilados a parte, com -w, porque o codigo e de terceiros e nao segue o -std=c11/-Wextra daqui.
# Para um build sem CHD: make PH_NO_CHD=1 (cd_chd.c vira stub e estes objetos nem entram).
CHD_DIRS = libretro-common/formats/chd libretro-common/formats/7z libretro-common/formats/flac libretro-common/encodings libretro-common/features
vpath %.c $(CHD_DIRS)
ifeq ($(PH_NO_CHD),1)
  CFLAGS += -DPH_NO_CHD
  CHD_OBJS =
else
  CHD_OBJS = $(addprefix chdobj/,rchd.o r7z_lzma.o rflac.o encoding_crc32.o encoding_deflate.o encoding_huffman.o encoding_rzstd.o features_cpu.o ph_sleep.o)
endif
CHD_FLAGS = -O2 -std=gnu99 -w $(PICFLAG) -Ilibretro-common/include -DINLINE=inline -DHAVE_SYS_PARAM_H \
            -DHAVE_CHD -DHAVE_RCHD -DHAVE_RCHD_DEFLATE -DHAVE_RCHD_LZMA -DHAVE_RCHD_FLAC -DHAVE_RCHD_ZSTD -DHAVE_RFLAC
chdobj/%.o: %.c
	@mkdir -p chdobj
	$(CC) $(CHD_FLAGS) -c $< -o $@

all: test_dsp test_clio test_arm60 test_machine test_madam test_cel test_vdlp test_celexec test_cel_unpacked test_state test_cd test_libretro phaudit
	./test_clio
	./test_arm60
	./test_machine
	./test_madam
	./test_cel
	./test_vdlp
	./test_celexec
	./test_cel_unpacked
	./test_state
	./test_cd
	./test_libretro
test_clio: test_clio.c clio.c clio.h
	$(CC) $(CFLAGS) -o $@ test_clio.c clio.c
test_arm60: test_arm60.c arm60.c arm60.h
	$(CC) $(CFLAGS) -o $@ test_arm60.c arm60.c
test_machine: test_machine.c $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -o $@ test_machine.c $(CORE) $(CHD_OBJS) -lm
test_madam: test_madam.c madam.c sport.c madam.h sport.h
	$(CC) $(CFLAGS) -o $@ test_madam.c madam.c sport.c
test_vdlp: test_vdlp.c $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -o $@ test_vdlp.c $(CORE) $(CHD_OBJS) -lm
# auditoria em lote (uma execucao cobre a biblioteca inteira): make phaudit && ./phaudit -n 1800 -o auditoria BIOS/fz10.bin /pasta/dos/jogos
phaudit: phaudit.c $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ phaudit.c $(CORE) $(CHD_OBJS) -lm
test_cel_unpacked: test_cel_unpacked.c $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -o $@ test_cel_unpacked.c $(CORE) $(CHD_OBJS) -lm
test_state: test_state.c $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -o $@ test_state.c $(CORE) $(CHD_OBJS) -lm
test_cel: test_cel.c cel.c cel.h
	$(CC) $(CFLAGS) -o $@ test_cel.c cel.c -lm
test_celexec: test_celexec.c $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -o $@ test_celexec.c $(CORE) $(CHD_OBJS) -lm
# test_libretro.c inclui libretro.c (para enxergar os estaticos); nao linkar libretro.c junto
test_cd: test_cd.c $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -o $@ test_cd.c $(CORE) $(CHD_OBJS) -lm
test_libretro: test_libretro.c libretro.c libretro.h $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -o $@ test_libretro.c $(CORE) $(CHD_OBJS) -lm
# boot com a BIOS real: make bios-test [N=600] [SYS=.]  (procura fz10.bin etc. em $(SYS), $(SYS)/BIOS, ...; grava bios_test.ppm)
N ?= 600
SYS ?= .
bios-test: test_libretro
	./test_libretro $(SYS) bios_test.ppm $(N)
# nucleo para o RetroArch/Batocera
.PHONY: libretro bios-test asan clean
libretro: $(LIBRETRO)
$(LIBRETRO): libretro.c libretro.h $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) $(PICFLAG) -shared -o $@ libretro.c $(CORE) $(CHD_OBJS) -lm
asan: test_machine.c test_madam.c test_vdlp.c test_celexec.c test_cd.c test_libretro.c libretro.c $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) -g -O1 -std=c11 -Ilibretro-common/include -fsanitize=address,undefined -o test_machine_asan test_machine.c $(CORE) $(CHD_OBJS) -lm
	$(CC) -g -O1 -std=c11 -Ilibretro-common/include -fsanitize=address,undefined -o test_madam_asan test_madam.c madam.c sport.c
	./test_machine_asan
	./test_madam_asan
	$(CC) -g -O1 -std=c11 -Ilibretro-common/include -fsanitize=address,undefined -o test_cel_asan test_cel.c cel.c -lm
	./test_cel_asan
	$(CC) -g -O1 -std=c11 -Ilibretro-common/include -fsanitize=address,undefined -o test_vdlp_asan test_vdlp.c $(CORE) $(CHD_OBJS) -lm
	./test_vdlp_asan
	$(CC) -g -O1 -std=c11 -Ilibretro-common/include -fsanitize=address,undefined -o test_celexec_asan test_celexec.c $(CORE) $(CHD_OBJS) -lm
	./test_celexec_asan
	$(CC) -g -O1 -std=c11 -Ilibretro-common/include -fsanitize=address,undefined -o test_cd_asan test_cd.c $(CORE) $(CHD_OBJS) -lm
	./test_cd_asan
	$(CC) -g -O1 -std=c11 -Ilibretro-common/include -fsanitize=address,undefined -o test_libretro_asan test_libretro.c $(CORE) $(CHD_OBJS) -lm
	./test_libretro_asan
clean:
	rm -f test_clio test_arm60 test_machine test_madam test_cel test_vdlp test_celexec test_cd test_libretro test_celexec_asan test_machine_asan test_madam_asan test_cel_asan test_vdlp_asan test_libretro_asan test_cd_asan phoenix_libretro.so phoenix_libretro.dll bios_test.ppm test_cd_tmp.iso phaudit test_cel_unpacked test_cd_tmp.chd test_cd_tmp_raw.iso
	rm -rf chdobj

test_dsp: test_dsp.c dsp.c dsp.h clio.c clio.h
	$(CC) $(CFLAGS) -o test_dsp test_dsp.c dsp.c clio.c -lm
