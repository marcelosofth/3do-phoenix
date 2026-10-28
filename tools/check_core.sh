#!/bin/sh
# check_core.sh <binario> <plataforma> [nm] [objdump]
# Confere o nucleo gerado: (1) exporta os 25 simbolos da API libretro e NADA alem disso (simbolos internos como cd_init/dsp_*
# nao podem vazar para o frontend); (2) no Windows nao depende de DLLs do GCC (libgcc_s_*, libwinpthread, libstdc++), que o
# RetroArch nao traz e dao "modulo nao encontrado". Retorna 0 = ok.
lib=$1; plat=$2; NM=${3:-nm}; OBJDUMP=${4:-objdump}
[ -f "$lib" ] || { echo "check-core: $lib nao existe"; exit 2; }
REQ="retro_api_version retro_cheat_reset retro_cheat_set retro_deinit retro_get_memory_data retro_get_memory_size retro_get_region
retro_get_system_av_info retro_get_system_info retro_init retro_load_game retro_load_game_special retro_reset retro_run retro_serialize
retro_serialize_size retro_set_audio_sample retro_set_audio_sample_batch retro_set_controller_port_device retro_set_environment
retro_set_input_poll retro_set_input_state retro_set_video_refresh retro_unload_game retro_unserialize"
fail=0
case "$plat" in
  win)
    exports=$($OBJDUMP -p "$lib" | awk '/Ordinal\/Name Pointer/{f=1;next} f&&/^[ \t]*\[ *[0-9]+\]/{print $NF} f&&/^$/{f=0}')
    deps=$($OBJDUMP -p "$lib" | awk '/DLL Name:/{print $3}')
    bad=$(echo "$deps" | grep -iE 'libgcc|libwinpthread|libstdc\+\+')
    [ -n "$bad" ] && { echo "check-core: depende de DLL do GCC (use -static-libgcc): $bad"; fail=1; }
    echo "check-core: DLLs importadas: $(echo $deps)"
    ;;
  osx|ios*|tvos*)
    exports=$($NM -gU "$lib" | awk '{print $3}' | sed 's/^_//')
    ;;
  emscripten)
    echo "check-core: biblioteca estatica (emscripten): conferencia de exports nao se aplica"; exit 0 ;;
  *)
    exports=$($NM -D --defined-only "$lib" | awk '{print $3}')
    ;;
esac
extra=""
for e in $exports; do case " $(echo $REQ) " in *" $e "*) ;; *) extra="$extra$e
";; esac; done
[ -n "$extra" ] && { echo "check-core: exporta simbolos fora da API libretro:"; echo "$extra" | head -20; fail=1; }
for s in $REQ; do echo "$exports" | grep -qx "$s" || { echo "check-core: falta exportar $s"; fail=1; }; done
[ $fail = 0 ] && echo "check-core: OK (exporta so os 25 simbolos da API libretro)"
exit $fail
