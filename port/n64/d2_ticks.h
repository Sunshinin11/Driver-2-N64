#pragma once
// Ticks do contador CP0 acumulados por regiao do quadro; lidos e zerados pela telemetria em psyx_rdp.cpp.
#ifdef __cplusplus
extern "C" { extern unsigned int d2_tk_cars, d2_tk_blds, d2_tk_tiles; }
struct D2TickScope {
    unsigned int *acc; unsigned int t0;
    D2TickScope(unsigned int *a) : acc(a) { __asm__ volatile("mfc0 %0,$9" : "=r"(t0)); }
    ~D2TickScope() { unsigned int t1; __asm__ volatile("mfc0 %0,$9" : "=r"(t1)); *acc += t1 - t0; }
};
#endif
