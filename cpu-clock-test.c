#include <stdio.h>
#include <time.h>
// Effective core clock: dependent imul chain, latency = 3 cycles on Skylake/Coffee Lake (fixed, no freq dependence)
static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
int main(void){
  for(int k=0;k<6;k++){
    unsigned long x=3, n=400000000UL; double a=now();
    __asm__ volatile("1:\n imul %0,%0\n imul %0,%0\n imul %0,%0\n imul %0,%0\n dec %1\n jnz 1b" : "+r"(x), "+r"(n));
    double s=now()-a; printf("%.2f GHz\n", 400e6*4*3/s/1e9);
  }
  return 0;
}
