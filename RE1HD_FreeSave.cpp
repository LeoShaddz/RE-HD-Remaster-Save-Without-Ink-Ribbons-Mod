// RE1HD_FreeSave v3 - Resident Evil HD Remaster (bhd.exe, Steam, 32 bits, DESEMPACOTADO com Steamless)
// NAO COMPILADO nem TESTADO (nao consigo rodar o jogo aqui).
//
// Salvar na maquina de escrever SEM ink ribbon. Base: seus dois logs (com e sem fita):
//   - ao usar a maquina, o jogo faz FindItem(ID 0x44 = 68 = ink ribbon) em 0x46B5E4; se nao acha (-1) mostra a mensagem 0x3A
//     ("precisa de fita") e NAO abre a tela de save; se acha mostra 0x39 e abre o save (CreateSaveGUI modo 1).
//   PATCH 1 (0x46B5F2): o "je" que desvia para o caminho "sem fita" vira NOP -> sempre segue o caminho "com fita".
//   PATCH 2 (0x4908D7, opcional): a chamada 0x4EF6D0(0x44) que TIRA 1 fita ao abrir o save vira "add esp,4" (mesmo efeito na
//            pilha, sem remover). So com ConsumeRibbon=0. Se voce nao tem fita, essa chamada ja nao faz nada (retorna 0).
// v2: RibbonToHerb - ao PEGAR um ink ribbon (ID 68) no cenario, o jogo passa a entregar erva verde (ID 34; vindo do seu log).
//     Troca feita em 0x551307 (funcao 0x551260 = tela de pegar item): depois que o jogo guarda o ID do item em [this+0x6244]
//     (e copia em [this+0x60D0]), se for 68 vira 34. A quantidade fica igual. A fita que voce ja tem no inventario nao e mexida.
//     O MODELO do ink ribbon no cenario continua igual (trocar o modelo exige achar onde o jogo cria o objeto no mapa).
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef unsigned char u8;
typedef unsigned int  u32;

static char  gDir[MAX_PATH] = "";
static FILE* gF = NULL;
static volatile LONG gLines = 0;
static char gLast[512] = "";
static int gLogOn = 0;   // Log=1 no .ini liga o arquivo de log (padrao: desligado)
static void Log(const char* f, ...) {
    if (!gLogOn) return;
    char b[512];
    va_list a; va_start(a, f); _vsnprintf(b, sizeof(b) - 1, f, a); va_end(a); b[sizeof(b) - 1] = 0;
    if (InterlockedIncrement(&gLines) > 30000) return;
    if (!gF) { char p[MAX_PATH]; strcpy(p, gDir); strcat(p, "RE1HD_FreeSave.log"); gF = fopen(p, "a"); }
    if (!gF) return;
    fprintf(gF, "%s\n", b); fflush(gF);
}
static void LogDedupe(const char* f, ...) {
    char b[512];
    va_list a; va_start(a, f); _vsnprintf(b, sizeof(b) - 1, f, a); va_end(a); b[sizeof(b) - 1] = 0;
    if (strcmp(b, gLast) == 0) return;
    strcpy(gLast, b);
    Log("%s", b);
}

static int gFree = 1, gConsume = 1, gLogItems = 0, gSwap = 1, gRibbonId = 68, gHerbId = 34;

struct Probe { u32 addr; int len; const char* name; u8 sig[10]; };
static const Probe kP[] = {
    { 0x54B140, 6, "SetSlotIdCount_54B140", { 0x8B,0x0D,0x28,0x2F,0xCF,0x00 } },   // (slot, id, qtd) - so com LogItems=1
    { 0x551307, 5, "PickupIdSet_551307",    { 0xA1,0x28,0x2F,0xCF,0x00 } },         // troca ribbon -> erva (RibbonToHerb=1)
};
static const int NP = (int)(sizeof(kP) / sizeof(kP[0]));

// f[0..7] = EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX; f[8] = EFLAGS; f[9] = retorno; f[10..] = argumentos
static void __cdecl OnProbe(int id, u32* f) {
    const Probe& p = kP[id];
    if (id == 1) {
        u32 t = f[1];                                                  // ESI = this da tela de pegar item
        if (t && !IsBadWritePtr((void*)(t + 0x6244), 4) && !IsBadWritePtr((void*)(t + 0x60D0), 4) && *(int*)(t + 0x6244) == gRibbonId) {
            Log("pickup: ink ribbon (ID %d) trocado por erva verde (ID %d), quantidade %d", gRibbonId, gHerbId, *(int*)(t + 0x6248));
            *(int*)(t + 0x6244) = gHerbId; *(int*)(t + 0x60D0) = gHerbId;
        }
        return;
    }
    LogDedupe("%s ret=%08X slot=%d id=%d qtd=%d", p.name, f[9], (int)f[10], (int)f[11], (int)f[12]);
}

static bool InstallProbe(int i) {
    const Probe& p = kP[i];
    if (IsBadReadPtr((void*)p.addr, 16)) return false;
    if (memcmp((void*)p.addr, p.sig, p.len) != 0) {
        const u8* c = (const u8*)p.addr;
        Log("%s: assinatura nao confere em %08X: %02X %02X %02X %02X %02X %02X %02X %02X", p.name, p.addr, c[0],c[1],c[2],c[3],c[4],c[5],c[6],c[7]);
        return true;
    }
    u8* tr = (u8*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return false;
    u8* q = tr;
    *q++ = 0x9C; *q++ = 0x60;                                          // pushfd; pushad
    *q++ = 0x54;                                                       // push esp  (-> f)
    *q++ = 0x68; *(u32*)q = (u32)i; q += 4;                            // push id
    *q++ = 0xE8; *(u32*)q = (u32)&OnProbe - (u32)(q + 4); q += 4;      // call OnProbe
    *q++ = 0x83; *q++ = 0xC4; *q++ = 0x08;                             // add esp,8
    *q++ = 0x61; *q++ = 0x9D;                                          // popad; popfd
    memcpy(q, (void*)p.addr, p.len); q += p.len;                       // instrucoes originais (sem referencias relativas)
    *q++ = 0xE9; *(u32*)q = (p.addr + p.len) - (u32)(q + 4); q += 4;
    FlushInstructionCache(GetCurrentProcess(), tr, 64);
    DWORD old;
    if (!VirtualProtect((void*)p.addr, p.len, PAGE_EXECUTE_READWRITE, &old)) return false;
    u8* t = (u8*)p.addr;
    t[0] = 0xE9; *(u32*)(t + 1) = (u32)tr - (p.addr + 5);
    for (int k = 5; k < p.len; ++k) t[k] = 0x90;
    VirtualProtect((void*)p.addr, p.len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)p.addr, p.len);
    Log("%s: sonda instalada em %08X", p.name, p.addr);
    return true;
}

static bool PatchBytes(u32 addr, const u8* expect, const u8* repl, int len, const char* name) {
    if (IsBadReadPtr((void*)addr, len)) return false;
    if (memcmp((void*)addr, repl, len) == 0) { Log("%s: ja aplicado", name); return true; }
    if (memcmp((void*)addr, expect, len) != 0) {
        const u8* c = (const u8*)addr;
        Log("%s: bytes inesperados em %08X: %02X %02X %02X %02X %02X %02X (nao alterado)", name, addr, c[0],c[1],c[2],c[3],c[4],c[5]);
        return true;                                                   // nao tenta de novo
    }
    DWORD old;
    if (!VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)addr, repl, len);
    VirtualProtect((void*)addr, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, len);
    Log("%s: aplicado em %08X", name, addr);
    return true;
}
static bool ApplyPatches() {
    bool ok = true;
    if (gFree) {
        static const u8 ex1[5] = { 0x83,0xC0,0x01,0x74,0x1C }, rp1[5] = { 0x83,0xC0,0x01,0x90,0x90 };
        ok &= PatchBytes(0x46B5EF, ex1, rp1, 5, "patch1 (sem fita)");
    }
    if (!gConsume) {
        static const u8 ex2[5] = { 0xE8,0xF4,0xED,0x05,0x00 }, rp2[5] = { 0x83,0xC4,0x04,0x90,0x90 };
        ok &= PatchBytes(0x4908D7, ex2, rp2, 5, "patch2 (nao consumir)");
    }
    return ok;
}

static DWORD WINAPI Init(LPVOID) {
    char ini[MAX_PATH]; HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Init, &self);
    GetModuleFileNameA(self, ini, MAX_PATH);
    char* s = strrchr(ini, '\\'); if (s) *(s + 1) = 0;
    strcpy(gDir, ini); strcat(ini, "RE1HD_FreeSave.ini");
    if (!GetPrivateProfileIntA("Main", "Enable", 1, ini)) return 0;
    gFree     = GetPrivateProfileIntA("Main", "FreeSave", 1, ini);
    gConsume  = GetPrivateProfileIntA("Main", "ConsumeRibbon", 1, ini);
    gLogItems = GetPrivateProfileIntA("Main", "LogItems", 0, ini);
    gLogOn    = GetPrivateProfileIntA("Main", "Log", 0, ini) != 0 || gLogItems != 0;
    gSwap     = GetPrivateProfileIntA("Main", "RibbonToHerb", 1, ini);
    gRibbonId = GetPrivateProfileIntA("Main", "RibbonItemId", 68, ini);
    gHerbId   = GetPrivateProfileIntA("Main", "HerbItemId", 34, ini);
    u32 base = (u32)GetModuleHandleA(NULL);
    Log("RE1HD_FreeSave v3 (base=%08X, esperado 00400000; FreeSave=%d ConsumeRibbon=%d RibbonToHerb=%d ids %d->%d LogItems=%d)", base, gFree, gConsume, gSwap, gRibbonId, gHerbId, gLogItems);
    if (base != 0x400000) { Log("exe em base diferente: nada alterado"); return 0; }
    bool done[8] = {}, patched = false;
    for (int t = 0; t < 240; ++t) {                                    // o codigo pode demorar a ficar legivel
        if (!patched) patched = ApplyPatches();
        int n = 0;
        for (int i = 0; i < NP; ++i) {
            bool want = (i == 0) ? (gLogItems != 0) : (gSwap != 0);
            if (!want) { done[i] = true; ++n; continue; }
            if (!done[i]) done[i] = InstallProbe(i);
            if (done[i]) ++n;
        }
        if (patched && n == NP) break;
        Sleep(500);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); CreateThread(NULL, 0, Init, NULL, 0, NULL); }
    return TRUE;
}
