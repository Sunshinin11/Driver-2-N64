// Teste no PC: GTE nativa (port/compat/d2_gte_inline.h + port/n64/gte_native.cpp) contra o emulador do PsyX, 200 mil casos.
// g++ -O1 -w -fpermissive -DUSE_PGXP=0 -DD2_GTE_NATIVE -I$S/include -I$S/include/psx -Iport/compat -I$S/src gte_test.cpp port/n64/gte_native.cpp $S/src/gte/PsyX_GTE.cpp
//   (S = port/redriver2/src_rebuild/PsyCross)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx/types.h"
#include "psx/gtereg.h"
extern "C" int GTE_operator(int op);
extern "C" int doCOP2(int op) { return GTE_operator(op); }
#define D2_GTE_NATIVE 1
#include "d2_gte_inline.h"
extern "C" const unsigned char d2_unr_table[257];

int main(){
  srand(1); int bad=0;
  unsigned ops[]={0x0180001,0x0280030,0x01400006,0x0158002D,0x0168002E,0x0480012,0x0486012,0x049E012};
  for(int it=0;it<200000;it++){
    GTERegisters init; unsigned char*b=(unsigned char*)&init; for(size_t i=0;i<sizeof init;i++) b[i]=rand();
    // valores razoaveis: matriz 4.12 pequena, H, ofs
    init.CP2C.p[26].sw.l = 200 + rand()%800;  // H
    for(int i=0;i<5;i++) { init.CP2C.p[i].sw.l = (rand()%8192)-4096; init.CP2C.p[i].sw.h=(rand()%8192)-4096; }
    init.CP2C.p[5].sd=(rand()%20000)-10000; init.CP2C.p[6].sd=(rand()%20000)-10000; init.CP2C.p[7].sd=(rand()%30000)+100;
    for(int i=0;i<6;i++){ init.CP2D.p[i].sw.l=(rand()%4000)-2000; init.CP2D.p[i].sw.h=(rand()%4000)-2000; }
    unsigned op=ops[it%8];
    gteRegs=init; GTE_operator(op); GTERegisters ref=gteRegs;
    gteRegs=init; d2n_cop2(op); GTERegisters nat=gteRegs;
    for(int r=0;r<32;r++){ if(r==24&&op<0x01400000&&0) continue;
      if(ref.CP2D.p[r].d!=nat.CP2D.p[r].d){ if(bad<15) printf("op %07x reg %d ref %08x nat %08x\n",op,r,ref.CP2D.p[r].d,nat.CP2D.p[r].d); bad++; } }
  }
  printf("diferencas: %d\n",bad);
}
