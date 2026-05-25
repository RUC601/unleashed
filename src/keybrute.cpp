// KeyBrute — brute force Key2 + table to decrypt first entity
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "leechcore.h"
#include "vmmdll.h"

#ifndef HIDWORD
#define HIDWORD(u) ((DWORD)(((DWORDLONG)(u) >> 32) & 0xFFFFFFFF))
#endif
#ifndef __ROL4__
#define __ROL4__(x, n) _rotl(x, n)
#endif

static VMM_HANDLE  g_vmm  = 0;
static DWORD       g_pid  = 0;
static uint64_t    g_base = 0;
static uint64_t    g_size = 0;

#define R(a) Read<uint64_t>(a)

template<typename T> T Read(uint64_t a) {
    T b{}; VMMDLL_MemReadEx(g_vmm,g_pid,a,(PBYTE)&b,sizeof(T),0,
        VMMDLL_FLAG_NOCACHE|VMMDLL_FLAG_NOPAGING|VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    return b;
}

void read_buf(uint64_t a, uint8_t* b, size_t s) {
    for(size_t i=0;i<s;i+=8){uint64_t q=R(a+i);memcpy(b+i,&q,(s-i>=8)?8:(s-i));}
}

static uint64_t Decrypt(uint64_t p,uint8_t idx,uint64_t k1,uint64_t k2,uint64_t tb,uint32_t tm){
    if(!p||!k1||!k2)return 0;
    uint64_t v1=p,v2=(uintptr_t)1<<(uintptr_t)(idx&0x3F),v3=v2-1,v4=idx&0x3F,v5=idx/0x3F;
    uint64_t v6=R(v1+8*(uint32_t)v5+0x110);if(!v6)return 0;
    uint64_t v7=(v2&v6)>>v4,v8=(v3&v6)-(((v3&v6)>>1)&0x5555555555555555);
    uint64_t ip=R(v1+0x80);if(!ip)return 0;
    uint64_t v9=R(ip+8*(Read<uint8_t>((uint32_t)v5+v1+0x130)+
        ((0x101010101010101*(((v8&0x3333333333333333)+((v8>>2)&0x3333333333333333)+
        (((v8&0x3333333333333333)+((v8>>2)&0x3333333333333333))>>4)))&0xF0F0F0F0F0F0F0F0)>>0x38)));
    if(!v9)return 0;
    auto d=R(tb+((k1>>0x34)&tm)),d2=R(tb+(k1&tm));
    uint64_t v10=(unsigned int)v9|v9&0xFFFFFFFF00000000ui64^((uint64_t)((unsigned int)v9+0x71747EF8)<<0x20);
    uint64_t v11=k2^((unsigned int)v9|v10&0xFFFFFFFF00000000ui64^((uint64_t)(unsigned int)(v10+__ROL4__(HIDWORD(d),1))<<0x20));
    return -(int)v7&((unsigned int)v11|((unsigned int)v11|v11&0xFFFFFFFF00000000ui64^
        ((uint64_t)((unsigned int)v11^~(unsigned int)d2)<<0x20))&0xFFFFFFFF00000000ui64^
        ((uint64_t)((unsigned int)v11^0xDFBFA250)<<0x20));
}

int main(){
    printf("=== KeyBrute ===\n");
    LPSTR args[]={(LPSTR)"",(LPSTR)"-device",(LPSTR)"fpga://algo=0",(LPSTR)"-norefresh"};
    g_vmm=VMMDLL_Initialize(4,args);
    if(!g_vmm){printf("FAIL:DMA\n");return 1;}
    VMMDLL_PidGetFromName(g_vmm,(LPSTR)"Overwatch.exe",&g_pid);
    g_base=VMMDLL_ProcessGetModuleBaseU(g_vmm,g_pid,(LPSTR)"Overwatch.exe");
    PVMMDLL_MAP_MODULE pm=0;
    if(VMMDLL_Map_GetModuleU(g_vmm,g_pid,&pm,VMMDLL_MODULE_FLAG_NORMAL)){
        for(DWORD i=0;i<pm->cMap;i++)if(pm->pMap[i].vaBase==g_base){g_size=pm->pMap[i].cbImageSize;break;}
        VMMDLL_MemFree(pm);
    }
    if(!g_size)g_size=0x5000000;
    printf("Base=0x%llX Size=0x%llX PID=%u\n\n",g_base,g_size,g_pid);

    // Key1
    uint64_t key1_raw=R(g_base+0x40405F8);
    uint64_t Key1=key1_raw^0xF5;
    printf("[Key1] 0x%016llX\n",Key1);

    // Entity list
    uint64_t el=R(g_base+0x37EC5E0);
    printf("[Ent] list=0x%llX (RVA 0x%llX)\n",el,el-g_base);
    struct{uint64_t e;uint64_t p;}rl[4096];
    read_buf(el,(uint8_t*)rl,sizeof(rl));
    int n=0;for(int i=0;i<4096;i++)if(rl[i].e)n++;
    printf("Non-zero slots: %d\n\n",n);

    // Dump first 8 entity slots for debug
    printf("[Ent] First 8 slots:\n");
    for(int i=0;i<8;i++){
        printf("  [%d] e=0x%016llX p=0x%016llX",i,rl[i].e,rl[i].p);
        if(rl[i].e>=g_base&&rl[i].e<g_base+g_size)printf(" IN_GAME");
        if(rl[i].p>=g_base&&rl[i].p<g_base+g_size)printf(" pad_IN_GAME");
        printf("\n");
    }
    printf("\n");

    // Collect Key2 candidates
    struct{uint64_t v;int64_t o;}kc[200]={};
    int nc=0;
    for(int64_t o=-0x1000;o<=0x1000&&nc<200;o+=8){
        uint64_t v=R(g_base+0x40405F8+o);
        if(v>0x1000000000000000&&v!=key1_raw){kc[nc].v=v;kc[nc].o=o;nc++;}
    }
    // Simple sort
    for(int i=0;i<nc;i++)for(int j=i+1;j<nc;j++){
        int64_t ai=kc[i].o<0?-kc[i].o:kc[i].o,aj=kc[j].o<0?-kc[j].o:kc[j].o;
        if(aj<ai){auto t=kc[i];kc[i]=kc[j];kc[j]=t;}
    }

    // Test combinations
    printf("[Brute] Testing Key2 x Table x First8Entities:\n");
    printf("%-4s %-22s %-12s %-8s %s\n","Try","Key2","TableRVA","Links","Link0");

    uint64_t tables[]={g_base+0x38996E0,g_base+0x389A700,g_base+0x3800000};

    for(int ki=0;ki<50&&ki<nc;ki++){
        for(int ti=0;ti<3;ti++){
            int links=0;
            uint64_t link0=0;
            for(int e=0;e<8;e++){
                if(!rl[e].e)continue;
                uint64_t r=Decrypt(rl[e].e,0x34,Key1,kc[ki].v,tables[ti],0x7FF);
                if(r&&r>0x10000){
                    links++;
                    if(!link0)link0=r;
                }
            }
            if(links>0){
                printf("%-4d 0x%016llX 0x%07llX %-8d 0x%llX\n",
                    ki,kc[ki].v,tables[ti]-g_base,links,link0);
            }
        }
    }

    VMMDLL_Close(g_vmm);
    printf("Done.\n");
    return 0;
}
