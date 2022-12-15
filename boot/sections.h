#pragma once

#include <gctypes.h>

extern u32 STUB_START;
extern u32 STUB_END;

extern u32 TEXT_START;
extern u32 TEXT_END;

extern u32 RODATA_START;
extern u32 RODATA_END;

extern u32 DATA_START;
extern u32 DATA_END;

extern u32 __bss_start;
extern u32 __bss_end;

#define SECTION_SAVE_ADDR 0x92000000

#define SECTION_SAVE_MAGIC 0x53454354

typedef struct {
    u32 sectionsMagic;
    u32 stubStart;
    u32 stubEnd;
    u32 textStart;
    u32 textEnd;
    u32 rodataStart;
    u32 rodataEnd;
    u32 bssStart;
    u32 bssEnd;
    u32 rwDataSize;
    // RW data is copied here
} SectionSaveStruct;
