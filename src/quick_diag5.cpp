// Quick Diag 5 — read ALL entity objects and try to identify local player
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

#include "leechcore.h"
#include "vmmdll.h"
#include "Memory/MemoryRanges.h"

static VMM_HANDLE g_vmm=nullptr;
static DWORD g_pid=0;
static uint64_t g_base=0,g_size=0;

template<typename T> T R(uint64_t a){T b{};if(a>0x7FFFFFFFFFFFULL&&!IsValidPhysicalAddress(a)){printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges\n",(unsigned long long)a);return b;}DWORD d=0;BOOL ok=VMMDLL_MemReadEx(g_vmm,g_pid,a,(PBYTE)&b,sizeof(T),&d,VMMDLL_FLAG_NOCACHE|VMMDLL_FLAG_NOPAGING_IO|VMMDLL_FLAG_ZEROPAD_ON_FAIL);if((!ok||d==0)&&a>0x7FFFFFFFFFFFULL){printf("[MMAP] DMA read failed at physical-looking address 0x%llX (%s valid physical ranges)\n",(unsigned long long)a,IsValidPhysicalAddress(a)?"inside":"outside");}return b;}
void rb(uint64_t a,uint8_t*b,size_t s){for(size_t i=0;i<s;i+=8){uint64_t cur=a+i;if(cur>0x7FFFFFFFFFFFULL&&!IsValidPhysicalAddress(cur)){memset(b+i,0,s-i);printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges; zero-filled %llu bytes\n",(unsigned long long)cur,(unsigned long long)(s-i));break;}uint64_t q=R<uint64_t>(cur);size_t c=(s-i>=8)?8:(s-i);memcpy(b+i,&q,c);}}
bool looks(uint64_t v){return v>=g_base&&v<g_base+g_size;}

const char* HeroName(uint64_t id){
    switch(id){
        case 0x2E0000000000002: return "Reaper";    case 0x2E0000000000003: return "Tracer";
        case 0x2E0000000000004: return "Mercy";     case 0x2E0000000000005: return "Hanzo";
        case 0x2E0000000000006: return "Torbjorn";   case 0x2E0000000000007: return "Reinhardt";
        case 0x2E0000000000008: return "Pharah";     case 0x2E0000000000009: return "Winston";
        case 0x2E000000000000A: return "Widowmaker"; case 0x2E0000000000015: return "Bastion";
        case 0x2E0000000000016: return "Symmetra";  case 0x2E0000000000020: return "Zenyatta";
        case 0x2E0000000000029: return "Genji";      case 0x2E0000000000040: return "Roadhog";
        case 0x2E0000000000042: return "McCree";     case 0x2E0000000000065: return "Junkrat";
        case 0x2E0000000000068: return "Zarya";      case 0x2E000000000006E: return "Soldier76";
        case 0x2E0000000000079: return "Lucio";      case 0x2E000000000007A: return "D.Va";
        case 0x2E00000000000DD: return "Mei";        case 0x2E000000000013B: return "Ana";
        case 0x2E000000000012E: return "Sombra";     case 0x2E000000000013E: return "Orisa";
        case 0x2E000000000012F: return "Doomfist";   case 0x2E00000000001A2: return "Moira";
        case 0x2E0000000000195: return "Brigitte";   case 0x2E00000000001CA: return "WreckingBall";
        case 0x2E00000000001EC: return "Sojourn";    case 0x2E0000000000200: return "Ashe";
        case 0x2E0000000000221: return "Baptiste";   case 0x2E0000000000231: return "Kiriko";
        case 0x2E0000000000236: return "JunkerQueen"; case 0x2E000000000023B: return "Sigma";
        case 0x2E0000000000206: return "Echo";        case 0x2E000000000028D: return "Ramattra";
        case 0x2E000000000032B: return "Venture";
        default: return nullptr;
    }
}

int main(){
    printf("=== Quick Diag 5 — Entity Object Analysis ===\n\n");
    LPSTR args[]={(LPSTR)"",(LPSTR)"-device",(LPSTR)"fpga://algo=0",(LPSTR)"-norefresh"};
    g_vmm=VMMDLL_Initialize(4,args);
    if(!g_vmm){printf("DMA fail\n");return 1;}
    VMMDLL_PidGetFromName(g_vmm,(LPSTR)"Overwatch.exe",&g_pid);
    g_base=VMMDLL_ProcessGetModuleBaseU(g_vmm,g_pid,(LPSTR)"Overwatch.exe");
    PVMMDLL_MAP_MODULE pMod=nullptr;
    if(VMMDLL_Map_GetModuleU(g_vmm,g_pid,&pMod,VMMDLL_MODULE_FLAG_NORMAL)){
        for(DWORD i=0;i<pMod->cMap;i++)if(pMod->pMap[i].vaBase==g_base){g_size=pMod->pMap[i].cbImageSize;break;}
        VMMDLL_MemFree(pMod);
    }
    if(!g_size)g_size=0x5000000;
    printf("Base=0x%llX Size=0x%X PID=%u\n\n",g_base,(unsigned)g_size,g_pid);

    // Read entity table entries (0x30 stride from 0x37EC5E0)
    struct EntTableEntry { uint64_t list_ptr; uint64_t common_ptr; };
    EntTableEntry tables[64];
    rb(g_base+0x37EC5E0, (uint8_t*)tables, sizeof(tables));

    printf("=== ENTITY TABLE DUMP ===\n");
    int tableCount=0;
    for(int ti=0; ti<64; ti++){
        if(!looks(tables[ti].list_ptr)) continue;
        printf("\nTable[%d]: list=0x%llX (RVA 0x%llX)\n", ti, tables[ti].list_ptr, tables[ti].list_ptr-g_base);

        // Each entity descriptor in the list is 24 bytes
        int entities=0;
        for(int ei=0; ei<100; ei++){
            uint64_t a=R<uint64_t>(tables[ti].list_ptr+ei*24);
            uint64_t b=R<uint64_t>(tables[ti].list_ptr+ei*24+8);
            uint64_t c=R<uint64_t>(tables[ti].list_ptr+ei*24+16);

            // Pattern: even entries have valid entity ptr in c[2]
            //          odd entries have small number in c[0]
            if(ei%2==0 && looks(a) && looks(c)){
                // This is an entity entry: a=name_ptr, c=entity_obj
                entities++;
                if(entities<=8){ // show first 8
                    // Read name string
                    char name[32]={};
                    rb(a, (uint8_t*)name, 32);
                    name[31]=0;
                    printf("  [%d] name@0x%llX: \"%s\"  obj@0x%llX (RVA 0x%llX)\n",
                           ei, a-g_base, name, c, c-g_base);

                    // Scan the entity object for HeroID-like values
                    // Try reading XOR'd values too
                    for(int off=0; off<0x200; off+=8){
                        uint64_t v=R<uint64_t>(c+off);
                        // Check plain
                        const char* nm=HeroName(v);
                        if(nm){
                            printf("      +0x%03X: HeroID=%016llX ** %s ** (PLAIN)\n", off, v, nm);
                        }
                        // Check XOR'd with common keys
                        for(uint64_t xk=0xF5; xk<=0xFF; xk++){
                            uint64_t xv=v^xk;
                            nm=HeroName(xv);
                            if(nm){
                                printf("      +0x%03X: HeroID=%016llX ** %s ** (XOR 0x%02llX)\n", off, xv, nm, xk);
                            }
                        }
                        // Check ROR'd
                        uint64_t ror_v=(v>>8)|(v<<56);
                        nm=HeroName(ror_v);
                        if(nm){
                            printf("      +0x%03X: HeroID=%016llX ** %s ** (ROR8)\n", off, ror_v, nm);
                        }
                    }
                }
            }
        }
        printf("  Total entities in this table: %d\n", entities);
        tableCount++;
        if(tableCount>=8) break;
    }

    // ---- Scan entire game memory for HeroID values ----
    printf("\n=== FULL MEMORY SCAN FOR HEROID (sampling) ===\n");
    // Sample every 0x1000 bytes across key sections
    struct Find { uint64_t rva; uint64_t val; const char* name; };
    std::vector<Find> finds;

    uint64_t sections[][2]={
        {0x3C00000, 0x3D00000}, // entity region
        {0x37E0000, 0x3800000}, // string/data
        {0x3000000, 0x3200000}, // another data section
        {0x4000000, 0x4100000}, // global data
    };

    for(auto& sec: sections){
        for(uint64_t rva=sec[0]; rva<sec[1]; rva+=0x1000){
            uint64_t v=R<uint64_t>(g_base+rva);
            const char* nm=HeroName(v);
            if(nm){
                finds.push_back({rva, v, nm});
                printf("  RVA 0x%07llX: %016llX  ** %s **\n", rva, v, nm);
            }
        }
    }

    if(finds.empty()){
        printf("  No HeroID values found in any scan region.\n");
        printf("  HeroIDs are encrypted/obfuscated.\n");
    } else {
        printf("\n  Found %zu HeroID instances.\n", finds.size());
    }

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter...\n");
    std::getchar();
    return 0;
}
