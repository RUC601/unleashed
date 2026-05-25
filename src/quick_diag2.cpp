// Quick Diag 2 — scan for REAL entity list, use corrected decryption table
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <intrin.h>

#include "leechcore.h"
#include "vmmdll.h"
#include "Memory/MemoryRanges.h"

#ifndef HIDWORD
#define HIDWORD(_ui64) ((DWORD)(((DWORDLONG)(_ui64) >> 32) & 0xFFFFFFFF))
#endif
#ifndef LODWORD
#define LODWORD(_ui64) ((DWORD)((_ui64) & 0xFFFFFFFF))
#endif
#ifndef __ROL4__
#define __ROL4__(x, n) _rotl(x, n)
#endif

static VMM_HANDLE g_vmm = nullptr;
static DWORD g_pid = 0;
static uint64_t g_base = 0, g_size = 0;
static uint64_t K1 = 0, K2 = 0;

template<typename T>
T R(uint64_t a) {
    T b{};
    if (a > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(a)) {
        printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges\n",
            (unsigned long long)a);
        return b;
    }
    DWORD d=0;
    BOOL ok=VMMDLL_MemReadEx(g_vmm,g_pid,a,(PBYTE)&b,sizeof(T),&d,VMMDLL_FLAG_NOCACHE|VMMDLL_FLAG_NOPAGING_IO|VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    if ((!ok || d == 0) && a > 0x7FFFFFFFFFFFULL) {
        printf("[MMAP] DMA read failed at physical-looking address 0x%llX (%s valid physical ranges)\n",
            (unsigned long long)a,
            IsValidPhysicalAddress(a) ? "inside" : "outside");
    }
    return b;
}
void rb(uint64_t a, uint8_t* b, size_t s) { for(size_t i=0;i<s;i+=8){uint64_t cur=a+i;if(cur>0x7FFFFFFFFFFFULL&&!IsValidPhysicalAddress(cur)){memset(b+i,0,s-i);printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges; zero-filled %llu bytes\n",(unsigned long long)cur,(unsigned long long)(s-i));break;}uint64_t q=R<uint64_t>(cur);size_t c=(s-i>=8)?8:(s-i);memcpy(b+i,&q,c);} }
bool looks(uint64_t v) { return v >= g_base && v < g_base + g_size; }

// Corrected table read: new table at 0x3800000, 24-byte entries, read v0 only
static uint64_t TableRead(uint64_t idx) {
    return R<uint64_t>(g_base + 0x3800000 + (idx & 0x7FF) * 24);
}

static inline uint64_t ROR64(uint64_t x, int b) { b&=63; return (x>>b)|(x<<(64-b)); }

// DecryptComponent with CORRECTED table
static uint64_t DecryptComponent(uint64_t parent, uint8_t idx) {
    if (!parent || !K1 || !K2) return 0;
    uint64_t v1 = parent, v5 = idx / 0x3F;
    uint64_t v6 = R<uint64_t>(v1 + 8*(uint32_t)v5 + 0x110);
    if (!v6) return 0;
    uint64_t v7 = ((1ull<<(idx&0x3F)) & v6) >> (idx&0x3F);
    uint64_t v3 = (1ull<<(idx&0x3F)) - 1;
    uint64_t v8 = (v3 & v6) - (((v3 & v6) >> 1) & 0x5555555555555555);
    uint64_t inner = R<uint64_t>(v1 + 0x80);
    if (!inner || !looks(inner)) return 0;
    uint64_t v9 = R<uint64_t>(
        inner + 8 * (R<uint8_t>((uint32_t)v5 + v1 + 0x130) +
        ((0x101010101010101 * (
            ((v8 & 0x3333333333333333) + ((v8 >> 2) & 0x3333333333333333) +
            (((v8 & 0x3333333333333333) + ((v8 >> 2) & 0x3333333333333333)) >> 4))
        ) & 0xF0F0F0F0F0F0F0F0) >> 0x38))
    );
    if (!v9) return 0;
    auto dummy  = TableRead(K1 >> 0x34);
    auto dummy2 = TableRead(K1);
    uint64_t v10 = (unsigned int)v9 | v9 & 0xFFFFFFFF00000000ui64 ^ ((uint64_t)((unsigned int)v9 + 0x71747EF8) << 0x20);
    uint64_t v11 = K2 ^ ((unsigned int)v9 | v10 & 0xFFFFFFFF00000000ui64 ^ ((uint64_t)(unsigned int)(v10 + __ROL4__(HIDWORD(dummy), 1)) << 0x20));
    uint64_t v12 = -(int)v7 & ((unsigned int)v11 | ((unsigned int)v11 | v11 & 0xFFFFFFFF00000000ui64 ^ ((uint64_t)((unsigned int)v11 ^ ~(unsigned int)dummy2) << 0x20)) & 0xFFFFFFFF00000000ui64 ^ ((uint64_t)((unsigned int)v11 ^ 0xDFBFA250) << 0x20));
    return v12;
}

static const char* HeroName(uint64_t id) {
    switch(id){
        case 0x2E0000000000002: return "Reaper";
        case 0x2E0000000000003: return "Tracer";
        case 0x2E0000000000004: return "Mercy";
        case 0x2E0000000000005: return "Hanzo";
        case 0x2E0000000000006: return "Torbjorn";
        case 0x2E0000000000007: return "Reinhardt";
        case 0x2E0000000000008: return "Pharah";
        case 0x2E0000000000009: return "Winston";
        case 0x2E000000000000A: return "Widowmaker";
        case 0x2E0000000000015: return "Bastion";
        case 0x2E0000000000016: return "Symmetra";
        case 0x2E0000000000020: return "Zenyatta";
        case 0x2E0000000000029: return "Genji";
        case 0x2E0000000000040: return "Roadhog";
        case 0x2E0000000000042: return "McCree";
        case 0x2E0000000000065: return "Junkrat";
        case 0x2E0000000000068: return "Zarya";
        case 0x2E000000000006E: return "Soldier76";
        case 0x2E0000000000079: return "Lucio";
        case 0x2E000000000007A: return "D.Va";
        case 0x2E00000000000DD: return "Mei";
        case 0x2E000000000013B: return "Ana";
        case 0x2E000000000012E: return "Sombra";
        case 0x2E000000000013E: return "Orisa";
        case 0x2E000000000012F: return "Doomfist";
        case 0x2E00000000001A2: return "Moira";
        case 0x2E0000000000195: return "Brigitte";
        case 0x2E00000000001CA: return "WreckingBall";
        case 0x2E00000000001EC: return "Sojourn";
        case 0x2E0000000000200: return "Ashe";
        case 0x2E0000000000221: return "Baptiste";
        case 0x2E0000000000231: return "Kiriko";
        case 0x2E0000000000236: return "JunkerQueen";
        case 0x2E000000000023B: return "Sigma";
        case 0x2E0000000000206: return "Echo";
        case 0x2E000000000028D: return "Ramattra";
        case 0x2E000000000032B: return "Venture";
        default: return nullptr;
    }
}

int main() {
    printf("=== Quick Diag 2 — Real Entity Hunt ===\n\n");

    LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0", (LPSTR)"-norefresh" };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) { printf("DMA fail\n"); return 1; }
    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");
    PVMMDLL_MAP_MODULE pMod=nullptr;
    if(VMMDLL_Map_GetModuleU(g_vmm,g_pid,&pMod,VMMDLL_MODULE_FLAG_NORMAL)){
        for(DWORD i=0;i<pMod->cMap;i++) if(pMod->pMap[i].vaBase==g_base){g_size=pMod->pMap[i].cbImageSize;break;}
        VMMDLL_MemFree(pMod);
    }
    if(!g_size)g_size=0x5000000;
    printf("Base=0x%llX Size=0x%X PID=%u\n\n",g_base,(unsigned)g_size,g_pid);

    // Keys
    uint64_t kr = R<uint64_t>(g_base+0x40405F8);
    K1=kr^0xF5;
    for(int64_t o=-0x200;o<=0x200;o+=8){if(o==0)continue;uint64_t v=R<uint64_t>(g_base+0x40405F8+o);if(v>0x1000000000000000&&v!=kr&&v!=K1){K2=v;break;}}
    printf("[KEY] K1=0x%016llX K2=0x%016llX\n\n",K1,K2);

    // ---- 1. Validate new table format ----
    printf("=== [1] TABLE VALIDATION ===\n");
    printf("  Table[%u] = 0x%016llX\n", (unsigned)((K1>>52)&0x7FF), TableRead(K1>>0x34));
    printf("  Table[%u] = 0x%016llX\n", (unsigned)(K1&0x7FF), TableRead(K1));

    // ---- 2. Scan for REAL entity list ----
    // The old entity_base (0x37EC5E0) points to a string table.
    // Entity objects should have valid vtable pointers (pointing into .text section, ~0x1xxxxxx-0x4xxxxxx)
    // Let's scan the .data section for pointers that:
    //   a) Point into game memory (not string section)
    //   b) The target has a vtable-like first qword (pointing to .text)
    printf("=== [2] SCANNING FOR REAL ENTITY LIST ===\n");

    struct Candidate { uint64_t rva; uint64_t ptr; int vtable_count; };
    std::vector<Candidate> cands;

    // Look for pointers that point to arrays of entity-like objects
    // Entity objects typically have their first 8 bytes as a vtable pointer
    // The vtable pointer should point into .text (~0x1000000-0x3800000)
    for (uint64_t probe = g_base + 0x3700000; probe < g_base + 0x4000000; probe += 8) {
        uint64_t val = R<uint64_t>(probe);
        if (!looks(val) || val == g_base) continue;
        if (val - g_base < 0x10000) continue; // skip low addresses (PE header)

        // Read 16 potential entity slots (as qword pairs: entity, padding)
        int vtables = 0, non_null = 0;
        for (int i = 0; i < 8; i++) {
            uint64_t ent = R<uint64_t>(val + i*16);     // entity ptr
            uint64_t pad = R<uint64_t>(val + i*16 + 8); // padding
            if (!ent) continue;
            non_null++;
            // Check if first qword of target looks like a vtable
            uint64_t first = R<uint64_t>(ent);
            if (looks(first) && (first - g_base) > 0x10000 && (first - g_base) < 0x3800000) {
                vtables++;
            }
        }

        // Score: vtable count indicates entity-like objects
        if (vtables >= 1 && non_null >= 2) {
            cands.push_back({probe - g_base, val, vtables});
        }
    }

    std::sort(cands.begin(), cands.end(), [](auto& a, auto& b){ return a.vtable_count > b.vtable_count; });

    printf("  Top entity list candidates:\n");
    for (int i = 0; i < std::min(15, (int)cands.size()); i++) {
        auto& c = cands[i];
        printf("  [%d] RVA 0x%07llX -> 0x%016llX  vtables=%d\n", i, c.rva, c.ptr, c.vtable_count);

        // Dump first 4 potential entities for top 3
        if (i < 3) {
            for (int j = 0; j < 4; j++) {
                uint64_t ent = R<uint64_t>(c.ptr + j*16);
                uint64_t pad = R<uint64_t>(c.ptr + j*16 + 8);
                if (!ent) continue;
                uint64_t first = R<uint64_t>(ent);
                printf("      [%d] ent=0x%016llX  first_qw=0x%016llX", j, ent, first);
                if (looks(first)) {
                    uint64_t frva = first - g_base;
                    printf("  (vtable RVA 0x%llX)", frva);
                }
                printf("\n");
            }
        }
    }

    if (cands.empty()) {
        printf("  No candidates found!\n");
    }

    // ---- 3. For top candidate, try DecryptComponent ----
    if (!cands.empty()) {
        printf("\n=== [3] TEST DECRYPT ON TOP CANDIDATE ===\n");
        auto& best = cands[0];
        printf("  Using entity list at RVA 0x%llX\n", best.rva);

        int found = 0;
        for (int i = 0; i < 32 && found < 6; i++) {
            uint64_t ent = R<uint64_t>(best.ptr + i*16);
            uint64_t pad = R<uint64_t>(best.ptr + i*16 + 8);
            if (!ent || !looks(ent)) continue;

            // Try decrypting LINK component
            uint64_t link = DecryptComponent(ent, 0x34);
            if (!link) continue;

            // Try decrypting HEROID component
            uint64_t hero_base = DecryptComponent(link, 0x54);
            if (!hero_base) continue;

            uint64_t heroid = R<uint64_t>(hero_base + 0xD0);
            const char* name = HeroName(heroid);

            // Try health too
            uint64_t health_base = DecryptComponent(ent, 0x3B);
            float hp = 0, hpmax = 0;
            if (health_base) {
                hp = R<float>(health_base + 0xE0);
                hpmax = R<float>(health_base + 0xDC);
            }

            // Try player controller
            uint64_t pc = DecryptComponent(link, 0x43);

            printf("  [%d] ent=0x%016llX link=0x%016llX hero=0x%016llX",
                   i, ent, link, heroid);
            if (name) printf("  ** %s **", name);
            else printf("  (unknown hero 0x%llX)", heroid);
            printf("  HP=%.0f/%.0f", hp, hpmax);
            if (pc) printf("  [LOCAL?]");
            printf("\n");
            found++;
        }
        if (found == 0) {
            printf("  No entities decrypted successfully. Trying raw entity dump...\n");
            for (int i = 0; i < 4; i++) {
                uint64_t ent = R<uint64_t>(best.ptr + i*16);
                uint64_t pad = R<uint64_t>(best.ptr + i*16 + 8);
                if (!ent || !looks(ent)) continue;
                printf("  Raw entity [%d] at 0x%016llX (RVA 0x%llX):\n", i, ent, ent-g_base);
                uint8_t d[0x180];
                rb(ent, d, sizeof(d));
                for (int r=0;r<16;r++) {
                    printf("    +0x%02X: ", r*16);
                    for (int c=0;c<16;c++) printf("%02X ", d[r*16+c]);
                    printf("\n");
                }
                break; // just first one
            }
        }
    }

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter...\n");
    std::getchar();
    return 0;
}
