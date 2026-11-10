// Harness do ares (so o core Nintendo 64, sem janela): roda uma ROM .z64 por N quadros, salva capturas de tela (PPM) e
// imprime o que a ROM escreve no ISViewer + o PC do processador. Serve para testar o port Driver 2 N64 sem abrir o ares.
// Uso: harness rom.z64 <quadros> <pasta_saida> [captura_a_cada_N_quadros] [entradas.txt]
// entradas.txt (opcional): linhas "quadro botoes" onde botoes = lista de A B Z S(start) U D L R CU CD CL CR (ou - para soltar).
#include <ares/ares.hpp>
#include <n64/n64.hpp>
#include <mia/mia.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

using namespace nall;

struct Host : ares::Platform {
  std::shared_ptr<mia::Pak> system, game, gamepad;
  ares::Node::System root;
  std::vector<u32> frame;
  u32 fw = 0, fh = 0, frames = 0;
  std::map<std::string, ares::Node::Input::Input> inputs;
  std::map<std::string, bool> pressed;

  auto attach(ares::Node::Object node) -> void override {
    if(auto in = std::dynamic_pointer_cast<ares::Core::Input::Input>(node)) {
      // nomes dos botoes do Gamepad do port 1
    }
  }
  auto pak(ares::Node::Object node) -> std::shared_ptr<vfs::directory> override {
    if(node->name() == "Nintendo 64") return system->pak;
    if(node->name() == "Nintendo 64 Cartridge") return game->pak;
    if(node->name() == "Controller Pak" || node->name() == "Gamepad") return gamepad->pak;
    return {};
  }
  auto video(ares::Node::Video::Screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
    fw = width; fh = height;
    frame.resize((size_t)width * height);
    for(u32 y = 0; y < height; y++) memcpy(&frame[(size_t)y * width], (const u8*)data + (size_t)y * pitch, width * 4);
    frames++;
  }
  auto log(ares::Node::Debugger::Tracer::Tracer, string_view message) -> void override {
    fwrite(message.data(), 1, message.size(), stdout);
    fflush(stdout);
  }
  auto input(ares::Node::Input::Input node) -> void override {
    // chamado pelo core a cada leitura de controle: devolve o estado do botao pelo nome
    auto name = std::string(node->name().data());
    if(auto button = std::dynamic_pointer_cast<ares::Core::Input::Button>(node)) {
      auto it = pressed.find(name);
      button->setValue(it != pressed.end() && it->second);
    }
  }
};

static void save_ppm(const Host& h, const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if(!f) return;
  fprintf(f, "P6\n%u %u\n255\n", h.fw, h.fh);
  for(size_t i = 0; i < (size_t)h.fw * h.fh; i++) {
    u32 p = h.frame[i];
    u8 rgb[3] = {(u8)(p >> 16), (u8)(p >> 8), (u8)p};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void dump_state(const char* why) {
  auto& cpu = ares::Nintendo64::cpu;
  auto& ipu = cpu.ipu;
  printf("[harness] ESTADO (%s): PC=%08x RA=%08x SP=%08x EPC=%08x CAUSE=%u BADVADDR=%08x\n", why,
    (u32)ipu.pc, (u32)ipu.r[31].u64, (u32)ipu.r[29].u64, (u32)cpu.scc.epc, (u32)cpu.scc.cause.exceptionCode, (u32)cpu.scc.badVirtualAddress);
  printf("[harness] REGS:");
  for(int i = 1; i < 32; i++) printf(" r%d=%08x", i, (u32)ipu.r[i].u64);
  printf("\n[harness] PILHA(sp..sp+0x400), enderecos que parecem codigo:");
  u32 sp = (u32)ipu.r[29].u64;
  for(u32 a = sp; a < sp + 0x400; a += 4) {
    u32 w = (u32)cpu.readDebug<4>((s32)a);
    if(w >= 0x80000400 && w < 0x800c0000) printf(" %08x", w);
  }
  printf("\n");
  fflush(stdout);
}

int main(int argc, char** argv) {
  if(argc < 4) { fprintf(stderr, "uso: harness rom.z64 quadros pasta_saida [a_cada] [entradas.txt]\n"); return 2; }
  std::string romPath = argv[1];
  int total = atoi(argv[2]);
  std::string outDir = argv[3];
  int every = argc > 4 ? atoi(argv[4]) : 60;
  std::map<int, std::string> script;
  if(argc > 5) {
    FILE* f = fopen(argv[5], "r");
    char line[256];
    while(f && fgets(line, sizeof line, f)) {
      int fr; char btn[200] = "";
      if(sscanf(line, "%d %199[^\n]", &fr, btn) >= 1) script[fr] = btn;
    }
    if(f) fclose(f);
  }

  Host host;
  ares::platform = &host;
  host.system = mia::System::create("Nintendo 64");
  if(host.system->load() != successful) { fprintf(stderr, "falha ao criar pak do sistema\n"); return 1; }
  host.game = mia::Medium::create("Nintendo 64");
  if(host.game->load(romPath.c_str()) != successful) { fprintf(stderr, "falha ao carregar ROM %s\n", romPath.c_str()); return 1; }
  host.gamepad = mia::Pak::create("Nintendo 64");
  host.gamepad->pak->append("save.pak", 32_KiB);

  ares::Nintendo64::option("Enable GPU acceleration", getenv("D2_VULKAN") != nullptr);
  ares::Nintendo64::option("Homebrew Mode", true);
  ares::Nintendo64::option("Deterministic Entropy", true);
  ares::Nintendo64::option("Recompiler", false);
  ares::Nintendo64::option("Expansion Pak", true);
  if(!ares::Nintendo64::load(host.root, "[Nintendo] Nintendo 64 (NTSC)")) { fprintf(stderr, "ares: falha ao carregar o sistema\n"); return 1; }
  if(auto port = host.root->find<ares::Node::Port>("Cartridge Slot")) { port->allocate(); port->connect(); }
  if(auto port = host.root->find<ares::Node::Port>("Controller Port 1")) {
    auto peripheral = port->allocate("Gamepad");
    port->connect();
    if(auto pp = peripheral->find<ares::Node::Port>("Pak")) { pp->allocate("Controller Pak"); pp->connect(); }
  }
  host.root->power();

  std::atomic<bool> profStop{false}; std::atomic<bool> profOn{false}; std::map<u32,u32> profHist; std::thread profThr;
  struct Snap { s64 cpu, ic_h, ic_m, dc_h, dc_m, rsp, rsph; } snap0{}; bool snapOn = false;
  auto takeSnap = [&]{ auto& p = ares::Nintendo64::cpu.profile; return Snap{p.cpuCycles, p.icacheHits, p.icacheMisses, p.dcacheHits, p.dcacheMisses, ares::Nintendo64::rsp.profile.cycles, ares::Nintendo64::rsp.profile.haltedCycles}; };
  if(getenv("D2_PROF")) profThr = std::thread([&]{ while(!profStop) { std::this_thread::sleep_for(std::chrono::microseconds(500)); if(profOn) { static s64 last = 0; s64 c = ares::Nintendo64::cpu.profile.cpuCycles; if(last && c > last) profHist[(u32)ares::Nintendo64::cpu.ipu.pc & ~3u] += (u32)(c - last); last = c; } } });
  int profFrom = getenv("D2_PROF_FROM") ? atoi(getenv("D2_PROF_FROM")) : 0;
  std::vector<int> dumpAt; if(const char* e = getenv("D2_DUMP")) { char b[256]; strncpy(b, e, 255); b[255]=0; for(char* t = strtok(b, ","); t; t = strtok(nullptr, ",")) dumpAt.push_back(atoi(t)); }
  std::string cur; bool stopped = false; bool stopOnExc = getenv("D2_STOP_ON_EXC") != nullptr;
  for(int fr = 0; fr < total; fr++) {
    auto it = script.find(fr);
    if(it != script.end()) {
      host.pressed.clear();
      // nomes do ares: "A","B","Z","Start","Up","Down","Left","Right","L","R","C-Up",...
      char buf[200]; strncpy(buf, it->second.c_str(), sizeof buf - 1); buf[sizeof buf - 1] = 0;
      for(char* t = strtok(buf, " "); t; t = strtok(nullptr, " ")) {
        std::string s = t;
        if(s == "S") s = "Start"; else if(s == "U") s = "Up"; else if(s == "D") s = "Down";
        else if(s == "L") s = "L"; else if(s == "R") s = "R";
        else if(s == "CU") s = "C-Up"; else if(s == "CD") s = "C-Down"; else if(s == "CL") s = "C-Left"; else if(s == "CR") s = "C-Right";
        if(s == "-") continue;
        host.pressed[s] = true;
      }
    }
    if(fr == profFrom) { profOn = true; snap0 = takeSnap(); snapOn = true; }
    host.root->run();
    for(int d : dumpAt) if(d == fr) { char w[32]; snprintf(w, sizeof w, "quadro %d", fr); dump_state(w); }
    { auto& scc = ares::Nintendo64::cpu.scc;
      if(scc.cause.exceptionCode != 0 && scc.status.exceptionLevel && !stopped) { stopped = true; dump_state("excecao"); if(stopOnExc) { total = fr + 2; } } }
    static int capFrom = getenv("D2_CAP_FROM") ? atoi(getenv("D2_CAP_FROM")) : 0, capTo = getenv("D2_CAP_TO") ? atoi(getenv("D2_CAP_TO")) : 1<<30;
    if(every > 0 && (fr % every) == 0 && host.fw && fr >= capFrom && fr <= capTo) {
      char name[512]; snprintf(name, sizeof name, "%s/frame_%05d.ppm", outDir.c_str(), fr);
      save_ppm(host, name);
      auto pc = ares::Nintendo64::cpu.ipu.pc;
      printf("[harness] quadro %d  PC=%016llx  video=%ux%u\n", fr, (unsigned long long)pc, host.fw, host.fh);
      fflush(stdout);
    }
  }
  if(host.fw) { save_ppm(host, outDir + "/final.ppm"); }
  if(const char* ca = getenv("D2_CRASH_ADDR")) {
    u32 a0 = (u32)strtoul(ca, nullptr, 16); auto& cpu = ares::Nintendo64::cpu;
    u32 w0 = (u32)cpu.readDebug<4>((s32)a0);
    if((w0 & 0xffff0000u) == 0xC0DE0000u) {
      printf("[crash] EPC=%08x BADV=%08x code=%u\n", (u32)cpu.readDebug<4>((s32)(a0+4)), (u32)cpu.readDebug<4>((s32)(a0+8)), (u32)cpu.readDebug<4>((s32)(a0+12)));
      printf("[crash] regs:"); for(int i = 1; i < 32; i++) printf(" r%d=%08x", i, (u32)cpu.readDebug<4>((s32)(a0 + 16 + 4*i)));
      printf("\n[crash] pilha:"); for(u32 i = 0; i < (w0 & 0xffff); i++) printf(" %08x", (u32)cpu.readDebug<4>((s32)(a0 + 160 + 4*i)));
      printf("\n");
      const u32 fs = a0 + 140 * 4;
      if ((u32)cpu.readDebug<4>((s32)fs) == 0x44324653) {
        char asset[65] = {};
        for (int i = 0; i < 64; ++i)
          asset[i] = (char)cpu.readDebug<1>((s32)(fs + 24 + i));
        printf("[crash] asset=%s dest=%08x sector=%d count=%d read=%d file_bytes=%d\n",
          asset, (u32)cpu.readDebug<4>((s32)(fs + 4)),
          (s32)cpu.readDebug<4>((s32)(fs + 8)), (s32)cpu.readDebug<4>((s32)(fs + 12)),
          (s32)cpu.readDebug<4>((s32)(fs + 16)), (s32)cpu.readDebug<4>((s32)(fs + 20)));
      }
    } else printf("[crash] sem excecao registrada\n");
  }
  if(const char* va = getenv("D2_VRAM_ADDR")) {   // despeja a VRAM (1024x512 u16 nativos) em PPM 1024x512 (BGR555 -> RGB)
    u32 a0 = (u32)strtoul(va, nullptr, 16); auto& cpu = ares::Nintendo64::cpu;
    FILE* f = fopen((outDir + "/vram.ppm").c_str(), "wb");
    if(f) { fprintf(f, "P6\n1024 512\n255\n");
      for(int i = 0; i < 1024*512; i++) { u16 w = (u16)cpu.readDebug<2>((s32)(a0 + i*2)); unsigned r = (w & 31) << 3, g = ((w >> 5) & 31) << 3, b = ((w >> 10) & 31) << 3; unsigned char px[3] = {(unsigned char)r,(unsigned char)g,(unsigned char)b}; fwrite(px,1,3,f); }
      fclose(f); }
  }
  if(snapOn) { Snap e = takeSnap(); printf("[stats] frames=%d cpu=%lld icache hit/miss=%lld/%lld dcache hit/miss=%lld/%lld rsp=%lld halted=%lld\n", total - profFrom, (long long)(e.cpu-snap0.cpu), (long long)(e.ic_h-snap0.ic_h), (long long)(e.ic_m-snap0.ic_m), (long long)(e.dc_h-snap0.dc_h), (long long)(e.dc_m-snap0.dc_m), (long long)(e.rsp-snap0.rsp), (long long)(e.rsph-snap0.rsph)); fflush(stdout); }
  if(profThr.joinable()) { profOn = false; profStop = true; profThr.join(); FILE* pf = fopen("/tmp/prof.txt","w"); for(auto& kv : profHist) fprintf(pf, "%08x %u\n", kv.first, kv.second); fclose(pf); }
  dump_state("fim");
  auto& ipu = ares::Nintendo64::cpu.ipu;
  printf("[harness] fim: PC=%016llx RA=%016llx SP=%016llx\n", (unsigned long long)ipu.pc, (unsigned long long)ipu.r[31].u64, (unsigned long long)ipu.r[29].u64);
  host.root->unload();
  return 0;
}
