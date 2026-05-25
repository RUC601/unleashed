// Quick Diag 4 — scan for HeroID patterns directly in game memory
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

int main(){
    printf("=== Quick Diag 4 — HeroID Memory Scan ===\n\n");
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

    // Known HeroID values and their names
    uint64_t knownIDs[]={
        0x2E0000000000002,0x2E0000000000003,0x2E0000000000004,0x2E0000000000005,
        0x2E0000000000006,0x2E0000000000007,0x2E0000000000008,0x2E0000000000009,
        0x2E000000000000A,0x2E0000000000015,0x2E0000000000016,0x2E0000000000020,
        0x2E0000000000029,0x2E0000000000040,0x2E0000000000042,0x2E0000000000065,
        0x2E0000000000068,0x2E000000000006E,0x2E0000000000079,0x2E000000000007A,
        0x2E00000000000DD,0x2E000000000013B,0x2E000000000012E,0x2E000000000013E,
        0x2E000000000012F,0x2E00000000001A2,0x2E0000000000195,0x2E00000000001CA,
        0x2E00000000001EC,0x2E0000000000200,0x2E0000000000221,0x2E0000000000231,
        0x2E0000000000236,0x2E000000000023B,0x2E0000000000206,0x2E000000000028D,
        0x2E000000000032B
    };
    int nIDs=sizeof(knownIDs)/sizeof(knownIDs[0]);

    // Strategy: Find entity objects by looking for HeroID patterns
    // HeroIDs are typically stored at a known offset within the entity's hero component
    // The entity structure should be findable by scanning for entities that have HeroID-like values

    printf("=== [1] SCAN FOR HEROID VALUES IN ENTITY OBJECT REGION ===\n");

    // Entity objects seem to live around RVA 0x3C80000-0x3CA0000
    // Scan for HeroID-like values nearby
    std::vector<uint64_t> found_rvas;

    // Scan entity region for HeroID patterns
    uint64_t scan_start = std::max(g_base, g_base + 0x3C80000);
    uint64_t scan_end   = std::min(g_base + g_size, g_base + 0x3CB0000);

    printf("  Scanning 0x%llX - 0x%llX for HeroID values...\n",
           scan_start-g_base, scan_end-g_base);

    int found=0;
    for(uint64_t addr=scan_start; addr<scan_end && found<200; addr+=8){
        uint64_t v=R<uint64_t>(addr);
        if((v&0xFFFF000000000000)==0x2E00000000000000 && v>0x2E0000000000000 && v<0x2F0000000000000){
            const char* name=HeroName(v);
            if(name){
                printf("  RVA 0x%llX: HeroID=0x%016llX  ** %s **\n", addr-g_base, v, name);
                found_rvas.push_back(addr-g_base);
                found++;
            }
        }
    }
    printf("  Found %d HeroID instances\n", found);

    // For the found HeroIDs, examine the surrounding structure
    if(!found_rvas.empty()){
        printf("\n=== [2] STRUCTURE AROUND FIRST 5 HEROID LOCATIONS ===\n");
        for(int i=0;i<std::min(5,(int)found_rvas.size());i++){
            uint64_t addr=g_base+found_rvas[i];
            printf("\n  HeroID at RVA 0x%llX:\n", found_rvas[i]);
            uint8_t data[256];
            rb(addr-0x80, data, sizeof(data));
            printf("  -0x80 to +0x80:\n");
            for(int r=0;r<256;r+=32){
                printf("    %+04X: ", (int)(r-0x80));
                for(int c=0;c<32;c+=8){
                    uint64_t qw=*(uint64_t*)(data+r+c);
                    printf(" %016llX", qw);
                }
                printf("\n");
            }
        }
    }

    // ---- Strategy 2: Find the HeroID using the ENTITY LIST approach ----
    // The new entity list at 0x37EC5E0 has 0x30 stride entries
    // Each entry points to a 24-byte entity array
    // In those arrays, even entries are {name_ptr, 0, entity_obj_ptr}
    // Let's scan entity objects for HeroID

    printf("\n=== [3] WALK NEW ENTITY LIST AND FIND HEROID IN ENTITY OBJECTS ===\n");

    // Read the entity table entries (0x30 stride)
    struct EntTableEntry { uint64_t list_ptr; uint64_t common_ptr; };
    EntTableEntry tables[32];
    rb(g_base+0x37EC5E0, (uint8_t*)tables, sizeof(tables));

    int entityCount=0;
    for(int ti=0; ti<32 && entityCount<30; ti++){
        if(!looks(tables[ti].list_ptr)) continue;
        // Each table entry points to a list of entity descriptors
        // Try 24-byte entries: {name_ptr, ?, entity_ptr}
        for(int ei=0; ei<64 && entityCount<30; ei++){
            uint64_t name_ptr = R<uint64_t>(tables[ti].list_ptr + ei*24);
            uint64_t unk      = R<uint64_t>(tables[ti].list_ptr + ei*24 + 8);
            uint64_t ent_ptr  = R<uint64_t>(tables[ti].list_ptr + ei*24 + 16);
            if(!looks(ent_ptr)) continue;

            // Scan the entity object for HeroID-like values
            for(int off=0; off<0x400; off+=8){
                uint64_t v=R<uint64_t>(ent_ptr+off);
                if((v>>48)==0x2E00){
                    const char* nm=HeroName(v);
                    if(nm){
                        printf("  Table[%d].Entity[%d] obj=0x%llX  +0x%03X HeroID=0x%016llX ** %s **\n",
                               ti, ei, ent_ptr-g_base, off, v, nm);
                        entityCount++;

                        // Read health nearby if possible
                        for(int hoff=0;hoff<0x100;hoff+=4){
                            float fv=R<float>(ent_ptr+hoff);
                            if(fv>10.f&&fv<1000.f){
                                float fv2=R<float>(ent_ptr+hoff+4);
                                if(fv2>10.f&&fv2<1000.f&&fv<=fv2){
                                    printf("    Possible HP at +0x%03X: %.0f/%.0f\n", hoff, fv, fv2);
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    if(entityCount==0){
        printf("  No entities found via new list structure.\n");
        printf("  Trying alternative: scan all entity_ptr objects...\n");

        // Fallback: for each table entry, scan the entity objects
        for(int ti=0;ti<8;ti++){
            if(!looks(tables[ti].list_ptr)) continue;
            // Read first 8 entities from this table
            for(int ei=0;ei<8;ei++){
                uint64_t name_ptr=R<uint64_t>(tables[ti].list_ptr+ei*24);
                uint64_t ent_ptr=R<uint64_t>(tables[ti].list_ptr+ei*24+16);
                if(!looks(ent_ptr)) continue;

                printf("\n  Table[%d].Entity[%d] obj at RVA 0x%llX:\n", ti, ei, ent_ptr-g_base);
                // Dump structure
                uint8_t d[256];
                rb(ent_ptr, d, sizeof(d));
                for(int r=0;r<256;r+=32){
                    printf("    +%03X: ", r);
                    for(int c=0;c<32;c+=8){
                        uint64_t qw=*(uint64_t*)(d+r+c);
                        printf(" %016llX", qw);
                    }
                    printf("\n");
                }
                break; // just first one
            }
            break;
        }
    }

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter...\n");
    std::getchar();
    return 0;
}
