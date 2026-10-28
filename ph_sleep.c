/* Phoenix: implementacao minima de retro_sleep_us para o MinGW.
 * Exigida pelo features_cpu.o (parte do suporte a CHD); o Phoenix nao a usa. */
#ifdef _WIN32
#include <windows.h>
void retro_sleep_us(unsigned usec)
{
   Sleep((usec + 500) / 1000);
}
#endif
