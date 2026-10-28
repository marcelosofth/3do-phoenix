# Phoenix 3DO - nucleo libretro (phoenix_libretro) e testes.
#   make libretro                      -> nucleo para a plataforma desta maquina (Linux/Batocera .so, MinGW .dll, macOS .dylib)
#   make libretro CROSS=prefixo-       -> cross (ex.: CROSS=aarch64-linux-gnu-  CROSS=x86_64-w64-mingw32-  CROSS=i686-w64-mingw32-)
#   make libretro platform=<nome>      -> unix win osx ios-arm64 ios9 tvos-arm64 emscripten rpi1..rpi4 odroidxu classic_armv7_a7
#   android: ndk-build -C jni          (ver jni/Android.mk)
#   make check-core                    -> confere exports (so retro_*) e dependencias do binario gerado
#   make                               -> compila e roda os testes (nesta maquina)
# Objetos em obj/<plataforma>-<triple>/: builds de arquiteturas diferentes nao se misturam (make clean limpa tudo).
include Makefile.common

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
# -fwrapv: aritmetica de 32 bits com volta, como o hardware (o motor de CEL do Opera conta com isso)
# -fsigned-char: char com sinal em TODAS as plataformas (em ARM/aarch64 o padrao e sem sinal; o codigo foi escrito/testado com sinal)
CFLAGS := $(CFLAGS) $(INCFLAGS) -fwrapv -fsigned-char

# CROSS=prefixo-  ->  CC/AR/NM/OBJDUMP do toolchain cruzado (CC=... na linha de comando continua valendo)
ifneq ($(CROSS),)
  CC      := $(CROSS)gcc
  AR      := $(CROSS)ar
  NM      := $(CROSS)nm
  OBJDUMP := $(CROSS)objdump
endif
AR      ?= ar
NM      ?= nm
OBJDUMP ?= objdump

# ---------------------------------------------------------------- plataforma
# Sem platform=, deduz pelo triple do compilador (serve tambem para cross: CC=x86_64-w64-mingw32-gcc -> win).
TRIPLE = $(shell $(CC) -dumpmachine 2>/dev/null)
# (platform=... da linha de comando nao pode ser reatribuida aqui; por isso a variavel interna PLAT)
PLAT := $(platform)
ifeq ($(PLAT),)
  PLAT := unix
  ifneq (,$(findstring mingw,$(TRIPLE))$(findstring windows,$(TRIPLE))$(findstring cygwin,$(TRIPLE))$(findstring msys,$(TRIPLE)))
    PLAT := win
  else ifneq (,$(findstring darwin,$(TRIPLE))$(findstring apple,$(TRIPLE)))
    PLAT := osx
  endif
endif
PLATFORM_ID := $(PLAT)

LIBNAME        = phoenix_libretro
LIBEXT         = .so
PICFLAG        = -fPIC
VISIBILITY     = -fvisibility=hidden
SHARED         = -shared -Wl,--no-undefined -Wl,--version-script=link.T
LIBS           = -lm
STATIC_LINKING ?= 0
PLAT_CFLAGS    =
PLAT_LDFLAGS   =

ifneq (,$(findstring rpi,$(PLAT))$(findstring odroid,$(PLAT))$(findstring classic_armv7,$(PLAT)))
  # ARM de 32 bits (Raspberry Pi OS / classic minis). Em Pi3/Pi4 com SO de 64 bits (Batocera) use platform=unix com o gcc aarch64.
  ifneq (,$(findstring rpi1,$(PLAT)))
    PLAT_CFLAGS = -marm -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard
  else ifneq (,$(findstring rpi2,$(PLAT)))
    PLAT_CFLAGS = -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
  else ifneq (,$(findstring rpi3,$(PLAT)))
    PLAT_CFLAGS = -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
  else ifneq (,$(findstring rpi4,$(PLAT)))
    PLAT_CFLAGS = -mcpu=cortex-a72 -mfpu=neon-fp-armv8 -mfloat-abi=hard
  else ifneq (,$(findstring odroidxu,$(PLAT)))
    PLAT_CFLAGS = -march=armv7ve -mtune=cortex-a15.cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
  else ifneq (,$(findstring classic_armv7_a7,$(PLAT)))
    PLAT_CFLAGS = -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
  endif
  PLAT := unix
endif

ifeq ($(PLAT),unix)
  # padrao acima (Linux/Batocera, BSDs): .so com versao de simbolos
else ifeq ($(PLAT),win)
  # MinGW (x64 e i686). -static-libgcc: sem isso o .dll depende de libgcc_s_*.dll, que o RetroArch nao traz.
  LIBEXT   = .dll
  PICFLAG  =
  SHARED   = -shared -static-libgcc
else ifeq ($(PLAT),osx)
  # ARCH=arm64|x86_64 escolhe a arquitetura (sem ARCH usa a do compilador); MACOSX_DEPLOYMENT_TARGET do ambiente vale
  LIBEXT   = .dylib
  SHARED   = -dynamiclib
  LIBS     = -lm
  ifneq ($(ARCH),)
    PLAT_CFLAGS  = -arch $(ARCH)
    PLAT_LDFLAGS = -arch $(ARCH)
  endif
else ifneq (,$(findstring ios,$(PLAT)))
  LIBEXT   = _ios.dylib
  SHARED   = -dynamiclib
  IOSSDK  ?= $(shell xcrun -sdk iphoneos --show-sdk-path 2>/dev/null)
  ifeq ($(PLAT),ios-arm64)
    PLAT_CFLAGS = -arch arm64 -isysroot $(IOSSDK) -miphoneos-version-min=8.0
  else
    PLAT_CFLAGS = -arch armv7 -isysroot $(IOSSDK) -miphoneos-version-min=8.0
  endif
  PLAT_CFLAGS += -DIOS
  PLAT_LDFLAGS = $(filter-out -DIOS,$(PLAT_CFLAGS))
else ifeq ($(PLAT),tvos-arm64)
  LIBEXT   = _tvos.dylib
  SHARED   = -dynamiclib
  TVOSSDK ?= $(shell xcrun -sdk appletvos --show-sdk-path 2>/dev/null)
  PLAT_CFLAGS  = -arch arm64 -isysroot $(TVOSSDK) -mappletvos-version-min=11.0 -DIOS
  PLAT_LDFLAGS = -arch arm64 -isysroot $(TVOSSDK) -mappletvos-version-min=11.0
else ifeq ($(PLAT),emscripten)
  # biblioteca estatica (.bc, como o Opera) que o RetroArch web liga; precisa do emsdk (emcc/emar) no PATH
  CC             = emcc
  AR             = emar
  LIBEXT         = _emscripten.bc
  STATIC_LINKING = 1
  PICFLAG        =
  VISIBILITY     =
else ifeq ($(PLAT),android)
  $(error Android usa o ndk-build: ndk-build -C jni  (ver jni/Android.mk e jni/Application.mk))
else
  $(error platform=$(platform) desconhecida. Validas: unix win osx ios-arm64 ios9 tvos-arm64 emscripten rpi1..rpi4 odroidxu classic_armv7_a7)
endif
# Consoles (libnx, vita, ctr, psp1, wiiu, ps3...) nao foram portados: o mesmo padrao serve (make STATIC_LINKING=1 CC=... AR=... LIBEXT=_libretro_<plat>.a)
# desde que o SDK entregue gcc/clang C11. MSVC nao e suportado (o codigo usa __builtin_popcount/__builtin_clz).

LIBRETRO = $(LIBNAME)$(LIBEXT)
LIBRETRO_OBJS_FROM = libretro.c $(CORE)
OBJDIR ?= obj/$(PLATFORM_ID)-$(strip $(TRIPLE))$(if $(ARCH),-$(ARCH))

# PH_USE_VFS=1: no Windows liga o VFS do frontend (desligado la por padrao: derrubava o RetroArch ao carregar jogo; ver ph_file_init)
ifeq ($(PH_USE_VFS),1)
  CFLAGS += -DPH_USE_VFS
endif
ifeq ($(PH_NO_CHD),1)
  # build sem CHD: cd_chd.c vira stub e os objetos do rchd nem entram
  CFLAGS  += -DPH_NO_CHD
  CHD_OBJS =
else
  CHD_OBJS = $(addprefix $(OBJDIR)/chd/,$(CHD_SRC:.c=.o))
endif
vpath %.c $(CHD_DIRS)
# codigo de terceiros: -w e -std=gnu99, como no Makefile.common do Opera
CHD_FLAGS = -O2 -std=gnu99 -w $(PICFLAG) $(PLAT_CFLAGS) $(INCFLAGS) $(CHD_DEFS)

DEBUG ?= 0
LR_CFLAGS = $(CFLAGS) $(PICFLAG) $(VISIBILITY) $(PLAT_CFLAGS)
ifeq ($(DEBUG),1)
  LR_CFLAGS += -O0 -g -DDEBUG
endif
LR_OBJS = $(addprefix $(OBJDIR)/,$(LIBRETRO_OBJS_FROM:.c=.o))
LR_LDFLAGS = $(LDFLAGS) $(PLAT_LDFLAGS)

$(OBJDIR)/chd/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CHD_FLAGS) -c $< -o $@
$(OBJDIR)/%.o: %.c $(HDRS) libretro.h
	@mkdir -p $(dir $@)
	$(CC) $(LR_CFLAGS) -c $< -o $@

all: test_dsp test_clio test_arm60 test_machine test_madam test_cel test_vdlp test_celexec test_cel_unpacked test_state test_cd test_hires test_libretro phaudit
	./test_clio
	./test_arm60
	./test_machine
	./test_madam
	./test_cel
	./test_vdlp
	./test_celexec
	./test_celexec legacy
	./test_cel_unpacked
	./test_state
	./test_cd
	./test_hires
	./test_libretro
test_hires: test_hires.c $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -o $@ test_hires.c $(CORE) $(CHD_OBJS) -lm
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
# nucleo para o RetroArch/Batocera/Windows/macOS/iOS/emscripten
.PHONY: libretro bios-test asan clean check-core
# A biblioteca e ligada dentro de $(OBJDIR) e COPIADA para a pasta do projeto: so existe um arquivo por nome na raiz, e sem isso
# trocar de plataforma (x64 -> i686 -> x64) deixaria o make achar que o binario antigo, de outra arquitetura, esta em dia.
libretro: $(OBJDIR)/$(LIBRETRO)
	cp -f $(OBJDIR)/$(LIBRETRO) $(LIBRETRO)
$(OBJDIR)/$(LIBRETRO): $(LR_OBJS) $(CHD_OBJS)
ifeq ($(STATIC_LINKING),1)
	$(AR) rcs $@ $^
else
	$(CC) $(LR_LDFLAGS) $(SHARED) -o $@ $^ $(LIBS)
endif
check-core: libretro
	sh tools/check_core.sh $(LIBRETRO) $(PLAT) $(NM) $(OBJDUMP)
asan: test_machine.c test_madam.c test_vdlp.c test_celexec.c test_cd.c test_hires.c test_libretro.c libretro.c $(CORE) $(HDRS) $(CHD_OBJS)
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
	$(CC) -g -O1 -std=c11 -Ilibretro-common/include -fsanitize=address,undefined -o test_hires_asan test_hires.c $(CORE) $(CHD_OBJS) -lm
	./test_hires_asan
	$(CC) -g -O1 -std=c11 -Ilibretro-common/include -fsanitize=address,undefined -o test_libretro_asan test_libretro.c $(CORE) $(CHD_OBJS) -lm
	./test_libretro_asan
clean:
	rm -f test_clio test_arm60 test_machine test_madam test_cel test_vdlp test_celexec test_cd test_libretro test_celexec_asan test_machine_asan test_madam_asan test_cel_asan test_vdlp_asan test_libretro_asan test_cd_asan test_hires test_state test_cel_unpacked test_dsp phoenix_libretro.so phoenix_libretro.dll phoenix_libretro.dylib phoenix_libretro_ios.dylib phoenix_libretro_tvos.dylib phoenix_libretro_emscripten.bc bios_test.ppm test_cd_tmp.iso phaudit test_cd_tmp.chd test_cd_tmp_raw.iso
	rm -rf obj chdobj

test_dsp: test_dsp.c dsp.c dsp.h clio.c clio.h
	$(CC) $(CFLAGS) -o test_dsp test_dsp.c dsp.c clio.c -lm
test_perifericos: test_perifericos.c libretro.c libretro.h $(CORE) $(HDRS) $(CHD_OBJS)
	$(CC) $(CFLAGS) -o $@ test_perifericos.c $(CORE) $(CHD_OBJS) -lm
