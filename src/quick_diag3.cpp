// Quick Diag 3 — targeted entity investigation
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

static VMM_HANDLE g_vmm=nullptr;
static DWORD g_pid=0;
static uint64_t g_base=0,g_size=0;

template<typename T> T R(uint64_t a){T b{};if(a>0x7FFFFFFFFFFFULL&&!IsValidPhysicalAddress(a)){printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges\n",(unsigned long long)a);return b;}DWORD d=0;BOOL ok=VMMDLL_MemReadEx(g_vmm,g_pid,a,(PBYTE)&b,sizeof(T),&d,VMMDLL_FLAG_NOCACHE|VMMDLL_FLAG_NOPAGING_IO|VMMDLL_FLAG_ZEROPAD_ON_FAIL);if((!ok||d==0)&&a>0x7FFFFFFFFFFFULL){printf("[MMAP] DMA read failed at physical-looking address 0x%llX (%s valid physical ranges)\n",(unsigned long long)a,IsValidPhysicalAddress(a)?"inside":"outside");}return b;}
void rb(uint64_t a,uint8_t*b,size_t s){for(size_t i=0;i<s;i+=8){uint64_t cur=a+i;if(cur>0x7FFFFFFFFFFFULL&&!IsValidPhysicalAddress(cur)){memset(b+i,0,s-i);printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges; zero-filled %llu bytes\n",(unsigned long long)cur,(unsigned long long)(s-i));break;}uint64_t q=R<uint64_t>(cur);size_t c=(s-i>=8)?8:(s-i);memcpy(b+i,&q,c);}}
bool looks(uint64_t v){return v>=g_base&&v<g_base+g_size;}

void dumphex(const char* label, uint64_t addr, int len) {
    uint8_t d[256];
    rb(addr,d,len);
    printf("%s (0x%llX):\n  ",label,addr);
    for(int i=0;i<len;i++){printf("%02X ",d[i]);if((i+1)%16==0&&i+1<len)printf("\n  ");}
    printf("\n");
}

int main(){
    printf("=== Quick Diag 3 ===\n\n");
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

    // ---- 1. Dump old entity_base pointer area (0x37EC5D0 - 0x37EC9A0) ----
    printf("=== [1] OLD ENTITY BASE POINTER AREA (0x37EC5D0) ===\n");
    uint8_t area[256];
    rb(g_base+0x37EC5D0,area,sizeof(area));
    for(int i=0;i<256;i+=16){
        uint64_t* qw=(uint64_t*)(area+i);
        printf("  RVA 0x%07llX: 0x%016llX  0x%016llX",
               (unsigned long long)(0x37EC5D0+i), qw[0], qw[1]);
        if(looks(qw[0])) printf("  [P0->RVA 0x%llX]", qw[0]-g_base);
        if(looks(qw[1])) printf("  [P1->RVA 0x%llX]", qw[1]-g_base);
        printf("\n");
    }

    // ---- 2. What does entity_base POINT to? Raw dump ----
    uint64_t ent_list=R<uint64_t>(g_base+0x37EC5E0);
    printf("\n=== [2] CONTENT AT ENTITY LIST PTR (RVA 0x%llX) ===\n",ent_list-g_base);

    // Read in different interpretations
    // A) As continuous memory
    dumphex("  128 bytes as raw hex",ent_list,128);

    // B) Look at individual pointer pairs more carefully
    printf("\n  Interpreted as qword pairs (entity, pad):\n");
    for(int i=0;i<16;i++){
        uint64_t e=R<uint64_t>(ent_list+i*16);
        uint64_t p=R<uint64_t>(ent_list+i*16+8);
        if(!e&&!p) continue;
        printf("  [%2d] e=0x%016llX",i,e);
        if(looks(e)){
            uint64_t er=e-g_base;
            uint64_t f=R<uint64_t>(e); // first qword of potential entity
            printf("  [RVA 0x%07llX]",(unsigned)er);
            if(looks(f)) printf("  first=0x%016llX [RVA 0x%llX]",f,f-g_base);
            else printf("  first=0x%016llX",f);
        }
        printf("  | p=0x%016llX",p);
        printf("\n");
    }

    // C) Check if it's an array of 24-byte entries (like the decrypt table)
    printf("\n  Interpreted as 24-byte entries:\n");
    for(int i=0;i<8;i++){
        uint64_t v0=R<uint64_t>(ent_list+i*24);
        uint64_t v1=R<uint64_t>(ent_list+i*24+8);
        uint64_t v2=R<uint64_t>(ent_list+i*24+16);
        printf("  [%d] v0=0x%016llX v1=0x%016llX v2=0x%016llX",i,v0,v1,v2);
        if(looks(v0)) printf("  [v0->RVA 0x%llX]",v0-g_base);
        if(looks(v1)) printf("  [v1->RVA 0x%llX]",v1-g_base);
        printf("\n");
    }

    // D) 32-byte entries
    printf("\n  Interpreted as 32-byte entries:\n");
    for(int i=0;i<6;i++){
        uint64_t v0=R<uint64_t>(ent_list+i*32);
        uint64_t v1=R<uint64_t>(ent_list+i*32+8);
        uint64_t v2=R<uint64_t>(ent_list+i*32+16);
        uint64_t v3=R<uint64_t>(ent_list+i*32+24);
        printf("  [%d] 0x%016llX 0x%016llX 0x%016llX 0x%016llX\n",i,v0,v1,v2,v3);
    }

    // ---- 3. Find where REAL entity objects live ----
    // Entity objects have: vtable pointer + component pointers
    // Let's scan a narrower range for vtable-filled structures
    printf("\n=== [3] HUNT: POINTERS TO VTABLE-OBJECTS NEAR ENTITY RANGE ===\n");

    // Entity objects in old code lived around RVA 0x3C00000-0x3CA0000
    // Let's check what's in that region now
    printf("  Sampling RVA 0x3C00000 area (should be entity vtable region):\n");
    for(uint64_t probe=g_base+0x3C00000;probe<g_base+0x3C01000;probe+=0x100){
        uint64_t v=R<uint64_t>(probe);
        if(looks(v)&&(v-g_base)>0x10000&&(v-g_base)<0x3800000){
            printf("  RVA 0x%llX -> vtable? RVA 0x%llX\n",probe-g_base,v-g_base);
        }
    }

    // ---- 4. Check if entity structures now live at different offsets ----
    // The "entity" pointers in the old list had RVA ~0x37E8000 for even slots
    // and ~0x3C88000 for odd slots. The odd slots look like real objects (higher RVA).
    // Let's check one of those odd-slot targets
    printf("\n=== [4] DUMP ODD-SLOT TARGETS (potential real entities) ===\n");
    for(int i=1;i<16;i+=2){
        uint64_t e=R<uint64_t>(ent_list+i*16);
        uint64_t p=R<uint64_t>(ent_list+i*16+8);
        if(!e||!looks(e)) continue;
        printf("\n  Slot [%d] target: 0x%016llX (RVA 0x%llX)  pad=%lld\n",i,e,e-g_base,p);
        dumphex("  First 128 bytes",e,128);

        // Check key offsets
        uint64_t at80=R<uint64_t>(e+0x80);
        uint64_t at110=R<uint64_t>(e+0x110);
        uint8_t at130=R<uint8_t>(e+0x130);
        printf("  +0x80=0x%016llX +0x110=0x%016llX +0x130=0x%02X\n",at80,at110,at130);
        if(looks(at80)) printf("  >> +0x80 IS A VALID POINTER! (RVA 0x%llX)\n",at80-g_base);

        break; // just first one
    }

    // ---- 5. Quick check: what's at the even-slot targets? ----
    printf("\n=== [5] DUMP EVEN-SLOT TARGETS ===\n");
    for(int i=0;i<16;i+=2){
        uint64_t e=R<uint64_t>(ent_list+i*16);
        if(!e||!looks(e)) continue;
        printf("\n  Slot [%d] target: 0x%016llX (RVA 0x%llX):\n",i,e,e-g_base);
        // Check if it's a string table entry
        char str[32];
        rb(e, (uint8_t*)str, 32);
        str[31]=0;
        printf("  ASCII: \"%s\"\n", str);
        break;
    }

    // ---- 6. Scan for pointers to odd-slot-type regions in .data ----
    printf("\n=== [6] SCAN .DATA FOR ENTITY-LIST-LIKE POINTERS ===\n");
    // Odd slots point to ~0x3C88000 area. Let's find what points to arrays near there
    // Narrow scan: 0x37EC000-0x37EE000
    struct FCand { uint64_t rva; uint64_t ptr; int count; };
    std::vector<FCand> fcands;
    for(uint64_t probe=g_base+0x37EC000;probe<g_base+0x37EE000;probe+=8){
        uint64_t val=R<uint64_t>(probe);
        if(!looks(val)||val==g_base)continue;
        // Check if the target contains multiple valid pointers
        int ptrs=0;
        for(int j=0;j<8;j++){
            uint64_t e=R<uint64_t>(val+j*16);
            if(looks(e))ptrs++;
        }
        if(ptrs>=2) fcands.push_back({probe-g_base,val,ptrs});
    }
    std::sort(fcands.begin(),fcands.end(),[](auto&a,auto&b){return a.count>b.count;});
    printf("  Top candidates near 0x37EC5E0:\n");
    for(int i=0;i<std::min(12,(int)fcands.size());i++){
        auto&c=fcands[i];
        printf("  RVA 0x%07llX -> 0x%016llX  ptrs=%d\n",c.rva,c.ptr,c.count);
        // For top 5, show first 4 slots
        if(i<5){
            for(int j=0;j<4;j++){
                uint64_t e=R<uint64_t>(c.ptr+j*16);
                uint64_t p=R<uint64_t>(c.ptr+j*16+8);
                printf("    [%d] 0x%016llX 0x%016llX",j,e,p);
                if(looks(e))printf(" [RVA 0x%llX]",e-g_base);
                printf("\n");
            }
        }
    }

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter...\n");
    std::getchar();
    return 0;
}
