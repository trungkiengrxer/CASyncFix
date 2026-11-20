#include <stdio.h>
#include <cpuid.h>
// CPUID leaf 6 / leaf 1 feature bits that gate the power-management MSRs (userspace, unprivileged)
int main(void){
  unsigned a,b,c,d; __cpuid_count(6,0,a,b,c,d);
  const char *n[]={"DTS","TurboBoost","ARAT","-","PLN","ECMD","PTM","HWP(0x770,0x771,0x774,0x777)","HWP_Notify(0x773)","HWP_ActWindow","HWP_EPP","HWP_PkgReq(0x772)","-","HDC(0xDB0)","TBM3","HWP_Capable_Highest_Change","-","HWP_FastMSR","HWP_ReqLoad"};
  printf("leaf6 EAX=0x%08x ECX=0x%08x\n",a,c);
  for(int i=0;i<19;i++) if(n[i][0]!='-') printf("  bit%-2d %-36s %s\n",i,n[i],(a>>i)&1?"YES":"no");
  printf("  ECX bit3 EPB(0x1B0)                         %s\n",(c>>3)&1?"YES":"no");
  __cpuid(1,a,b,c,d); printf("leaf1 EAX=0x%08x (family/model)\n",a);
  return 0;
}
