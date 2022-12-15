// load.c - LZMA loader stub
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#include "LzmaDec.h"
#include "sections.h"
#include <ogc/cache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned int u32;

extern const u8 channel_dol_lzma[];
extern u32 channel_dol_lzma_end;

void LoaderAbort()
{
    // Do something
    int* i = (int*)0x90000000;
    int* j = (int*)0x90000000;
    while (1) {
        *i = *j + 1;
    }
}

#define DECODE_ADDR ((u8*)(0x81200000))

typedef struct {
    union {
        struct {
            u32 dol_text[7];
            u32 dol_data[11];
        };
        u32 dol_sect[7 + 11];
    };
    union {
        struct {
            u32 dol_text_addr[7];
            u32 dol_data_addr[11];
        };
        u32 dol_sect_addr[7 + 11];
    };
    union {
        struct {
            u32 dol_text_size[7];
            u32 dol_data_size[11];
        };
        u32 dol_sect_size[7 + 11];
    };
    u32 dol_bss_addr;
    u32 dol_bss_size;
    u32 dol_entry_point;
    u32 dol_pad[0x1C / 4];
} DOL;

static inline void clearWords(u32* data, u32 count)
{
    while (count--) {
        asm volatile("dcbz    0, %0\n"
                     //"sync\n"
                     "dcbf    0, %0\n" ::"r"(data));
        data += 8;
    }
}

static inline void copyWords(u32* dest, u32* src, u32 count)
{
    register u32 value = 0;
    while (count--) {
        asm volatile("dcbz    0, %1\n"
                     //"sync\n"
                     "lwz     %0, 0(%2)\n"
                     "stw     %0, 0(%1)\n"
                     "lwz     %0, 4(%2)\n"
                     "stw     %0, 4(%1)\n"
                     "lwz     %0, 8(%2)\n"
                     "stw     %0, 8(%1)\n"
                     "lwz     %0, 12(%2)\n"
                     "stw     %0, 12(%1)\n"
                     "lwz     %0, 16(%2)\n"
                     "stw     %0, 16(%1)\n"
                     "lwz     %0, 20(%2)\n"
                     "stw     %0, 20(%1)\n"
                     "lwz     %0, 24(%2)\n"
                     "stw     %0, 24(%1)\n"
                     "lwz     %0, 28(%2)\n"
                     "stw     %0, 28(%1)\n"
                     "dcbf    0, %1\n" ::"r"(value),
                     "r"(dest), "r"(src));
        dest += 8;
        src += 8;
    }
}

#define INLINE_MEMCPY(__dst, __src, __len)                                     \
    do {                                                                       \
        for (int __i = 0; __i < (__len); __i++) {                              \
            ((unsigned char*)(__dst))[__i] = ((unsigned char*)(__src))[__i];   \
        }                                                                      \
    } while (0)

void InitExceptionHandlers()
{
    register u32 branch = 0x4C000064;
    register u32 addr = 0x80000000;

#define EXCEPTION_HANDLER_ASM(offset)                                          \
    asm volatile("stwu    %1, " #offset "(%0)\n"                               \
                 "dcbf    0, %0\n"                                             \
                 "sync\n"                                                      \
                 "icbi    0, %0\n"                                             \
                 "isync\n"                                                     \
                 :                                                             \
                 : "r"(addr), "r"(branch));

    EXCEPTION_HANDLER_ASM(0x100); // 0100
    EXCEPTION_HANDLER_ASM(0x100); // 0200
    EXCEPTION_HANDLER_ASM(0x100); // 0300
    EXCEPTION_HANDLER_ASM(0x100); // 0400
    EXCEPTION_HANDLER_ASM(0x100); // 0500
    EXCEPTION_HANDLER_ASM(0x100); // 0600
    EXCEPTION_HANDLER_ASM(0x100); // 0700
    EXCEPTION_HANDLER_ASM(0x100); // 0800
    EXCEPTION_HANDLER_ASM(0x100); // 0900
    EXCEPTION_HANDLER_ASM(0x400); // 0D00
    EXCEPTION_HANDLER_ASM(0x200); // 0F00
    EXCEPTION_HANDLER_ASM(0x400); // 1300
    EXCEPTION_HANDLER_ASM(0x100); // 1400
    EXCEPTION_HANDLER_ASM(0x300); // 1700
}

__attribute__((noreturn)) void load()
{
    InitExceptionHandlers();

    clearWords(&__bss_start, ((u32)&__bss_end - (u32)&__bss_start) / 32);

    // Copy our sections to somewhere else in memory for the channel installer
    SectionSaveStruct* sections = (SectionSaveStruct*)SECTION_SAVE_ADDR;
    sections->sectionsMagic = 0;
    sections->stubStart = (u32)&STUB_START;
    sections->stubEnd = (u32)&STUB_END;
    sections->textStart = (u32)&TEXT_START;
    sections->textEnd = (u32)&TEXT_END;
    sections->rodataStart = (u32)&RODATA_START;
    sections->rodataEnd = (u32)&RODATA_END;
    sections->bssStart = (u32)&__bss_start;
    sections->bssEnd = (u32)&__bss_end;
    sections->rwDataSize = (u32)&DATA_END - (u32)&DATA_START;

    void* rwData = (void*)(sections + 1);
    INLINE_MEMCPY(rwData, &DATA_START, sections->rwDataSize);

    sections->sectionsMagic = SECTION_SAVE_MAGIC;

    ELzmaStatus status;

    u32 channel_dol_lzma_size =
        (const u8*)&channel_dol_lzma_end - channel_dol_lzma;
    size_t destLen = 0x700000;
    size_t inLen = channel_dol_lzma_size - 5;
    SRes ret = LzmaDecode(DECODE_ADDR, &destLen, channel_dol_lzma + 0xD, &inLen,
                          channel_dol_lzma, LZMA_PROPS_SIZE, LZMA_FINISH_END,
                          &status, 0);

    if (ret != SZ_OK)
        LoaderAbort();

    DOL* dol = (DOL*)DECODE_ADDR;
    clearWords((u32*)dol->dol_bss_addr, dol->dol_bss_size / 4);

    for (int i = 0; i < 7 + 11; i++) {
        if (dol->dol_sect_size[i] != 0) {
            copyWords((u32*)dol->dol_sect_addr[i],
                      (u32*)(DECODE_ADDR + dol->dol_sect[i]),
                      (dol->dol_sect_size[i] / 4) / 8);
        }
    }

    (*(void (*)(void))dol->dol_entry_point)();
    while (1) {
    }
}
