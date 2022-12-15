// Saoirse.cpp
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#include "Saoirse.hpp"
#include "Arch.hpp"
#include "GlobalsConfig.hpp"
#include "IOSBoot.hpp"
#include <DVD/DI.hpp>
#include <Debug/Log.hpp>
#include <Main/LaunchState.hpp>
#include <System/ISFS.hpp>
#include <System/Util.h>
#include <UI/BasicUI.hpp>
#include <UI/Input.hpp>
#include <cstring>
#include <stdio.h>
#include <unistd.h>
LIBOGC_SUCKS_BEGIN
#include <ogc/context.h>
#include <ogc/exi.h>
#include <ogc/machine/processor.h>
#include <wiiuse/wpad.h>
LIBOGC_SUCKS_END

bool g_calledFromHBC = false;

#define HBC_TITLE_0 0x000100014C554C5A
#define HBC_TITLE_1 0x000100014F484243

void ReturnToLoader()
{
    if (g_calledFromHBC) {
        WII_LaunchTitle(HBC_TITLE_0);
        WII_LaunchTitle(HBC_TITLE_1);
    }

    WII_ReturnToMenu();
}

bool RTCRead(u32 offset, u32* value)
{
    if (EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_1, NULL) == 0)
        return false;
    if (EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_1, EXI_SPEED8MHZ) == 0) {
        EXI_Unlock(EXI_CHANNEL_0);
        return false;
    }

    bool ret = true;
    if (EXI_Imm(EXI_CHANNEL_0, &offset, 4, EXI_WRITE, NULL) == 0)
        ret = false;
    if (EXI_Sync(EXI_CHANNEL_0) == 0)
        ret = false;
    if (EXI_Imm(EXI_CHANNEL_0, value, 4, EXI_READ, NULL) == 0)
        ret = false;
    if (EXI_Sync(EXI_CHANNEL_0) == 0)
        ret = false;
    if (EXI_Deselect(EXI_CHANNEL_0) == 0)
        ret = false;
    EXI_Unlock(EXI_CHANNEL_0);

    return ret;
}

bool RTCWrite(u32 offset, u32 value)
{
    if (EXI_Lock(EXI_CHANNEL_0, EXI_DEVICE_1, NULL) == 0)
        return false;
    if (EXI_Select(EXI_CHANNEL_0, EXI_DEVICE_1, EXI_SPEED8MHZ) == 0) {
        EXI_Unlock(EXI_CHANNEL_0);
        return false;
    }

    // Enable write mode
    offset |= 0x80000000;

    bool ret = true;
    if (EXI_Imm(EXI_CHANNEL_0, &offset, 4, EXI_WRITE, NULL) == 0)
        ret = false;
    if (EXI_Sync(EXI_CHANNEL_0) == 0)
        ret = false;
    if (EXI_Imm(EXI_CHANNEL_0, &value, 4, EXI_WRITE, NULL) == 0)
        ret = false;
    if (EXI_Sync(EXI_CHANNEL_0) == 0)
        ret = false;
    if (EXI_Deselect(EXI_CHANNEL_0) == 0)
        ret = false;
    EXI_Unlock(EXI_CHANNEL_0);

    return ret;
}

// Re-enables holding the power button to turn off the console on vWii
bool WiiUEnableHoldPower()
{
    // RTC_CONTROL1 |= 4COUNT_EN

    u32 flags = 0;
    bool ret = RTCRead(0x21000D00, &flags);
    if (!ret)
        return false;

    ret = RTCWrite(0x21000D00, flags | 1);
    if (!ret)
        return false;

    return true;
}

static void PIErrorHandler([[maybe_unused]] u32 nIrq,
                           [[maybe_unused]] void* pCtx)
{
    // u32 cause = read32(0x0C003000); // INTSR
    write32(0x0C003000, 1); // Reset
}

void abort()
{
    LIBOGC_SUCKS_BEGIN
    u32 lr = mfspr(8);
    LIBOGC_SUCKS_END

    PRINT(Core, ERROR, "Abort called. LR = 0x%08X\n", lr);

    sleep(2);
    exit(0);

    // If this somehow returns then halt.
    IRQ_Disable();
    while (1) {
    }
}

[[maybe_unused]] static s32 UIThreadEntry([[maybe_unused]] void* arg)
{
    BasicUI::sInstance->Loop();
    return 0;
}

typedef struct {
    union {
        u32 dol_sect[7 + 11];
    };
    union {
        u32 dol_sect_addr[7 + 11];
    };
    union {
        u32 dol_sect_size[7 + 11];
    };
    u32 dol_bss_addr;
    u32 dol_bss_size;
    u32 dol_entry_point;
    u32 dol_pad[0x1C / 4];
} DOL;

extern "C" {
void LaunchTrampoline(u32 entry);
}

void LaunchGame()
{
#ifndef DISABLE_UI
    VIDEO_SetBlack(true);
    VIDEO_Flush();
    VIDEO_WaitVSync();
#endif

    u32 entryPoint = *(u32*)0xC0003400;

    delete IOSBoot::IPCLog::sInstance;

    // TODO: Proper shutdown
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    IRQ_Disable();

    memset((void*)0x80001800, 0, 0x1800);
    DCFlushRange((void*)0x80001800, 0x1800);
    memset((void*)0x80003400, 0, 0xB00);
    DCFlushRange((void*)0x80003400, 0xB00);

    SetupGlobals(0);

    LaunchTrampoline(entryPoint);

    /* Unreachable! */
    abort();
}

void TestISFS()
{
}

#include <fat.h>

void TestISFSReadDir()
{
}

void TestDirectOpen()
{
}

#include "../../boot/sections.h"

u32 totalDolSize = 0;
void* dolData = nullptr;

void MakeDOLForInstaller(SectionSaveStruct* sections)
{
    u32 stubSize = sections->stubEnd - sections->stubStart;
    u32 textSize = sections->textEnd - sections->textStart;
    u32 rodataSize = sections->rodataEnd - sections->rodataStart;
    u32 bssSize = sections->bssEnd - sections->bssStart;

    totalDolSize = sizeof(DOL) + round_up(stubSize, 0x40) +
                   round_up(textSize, 0x40) + round_up(rodataSize, 4) +
                   sections->rwDataSize;
    PRINT(Core, INFO, "total DOL size: %08X", totalDolSize);

    dolData = new u8[totalDolSize];
    assert(dolData != nullptr);
    PRINT(Core, INFO, "Alloc: %08X", dolData);

    memset(dolData, 0, totalDolSize);
    DOL* dol = (DOL*)dolData;

    dol->dol_sect[0] = sizeof(DOL);
    dol->dol_sect_addr[0] = sections->stubStart;
    dol->dol_sect_size[0] = stubSize;
    dol->dol_sect[7] = dol->dol_sect[0] + round_up(stubSize, 0x40);
    dol->dol_sect_addr[7] = sections->textStart;
    dol->dol_sect_size[7] = textSize;
    dol->dol_sect[8] = dol->dol_sect[7] + round_up(textSize, 0x40);
    dol->dol_sect_addr[8] = sections->rodataStart;
    dol->dol_sect_size[8] = round_up(rodataSize, 4) + sections->rwDataSize;

    memcpy(dolData + dol->dol_sect[0], (void*)sections->stubStart, stubSize);
    memcpy(dolData + dol->dol_sect[7], (void*)sections->textStart, textSize);
    memcpy(dolData + dol->dol_sect[8], (void*)sections->rodataStart,
           rodataSize);
    memcpy(dolData + dol->dol_sect[8] + round_up(rodataSize, 4),
           (void*)(sections + 1), sections->rwDataSize);

    dol->dol_bss_addr = sections->bssStart;
    dol->dol_bss_size = bssSize;
    dol->dol_entry_point = 0x80003400;
}

s32 main([[maybe_unused]] s32 argc, [[maybe_unused]] char** argv)
{
    // Properly handle PI errors
    IRQ_Request(IRQ_PI_ERROR, PIErrorHandler, nullptr);
    __UnmaskIrq(IM_PI_ERROR);

    if (*(u32*)(0x80001804) == 0x53545542) {
        g_calledFromHBC = true;
        *(u32*)(0x80001804) = 0;
        DCFlushRange((void*)0x80001804, 4);
    }

    // This is a nice thing to enable for development, but we should probably
    // leave it disabled for the end user, unless we can figure out why it was
    // disabled in the first place.
    // WiiUEnableHoldPower();

    LaunchState::Get()->Error.available = true;
    if (IOS_ReloadIOS(58) < 0) {
        abort();
    }

#ifndef DISABLE_UI
    Input::sInstance = new Input();
    BasicUI::sInstance = new BasicUI();
    BasicUI::sInstance->InitVideo();
    new Thread(UIThreadEntry, nullptr, nullptr, 0x1000, 80);

    PRINT(Core, WARN, "Debug console initialized");
    VIDEO_WaitVSync();
#endif

    // Let's make the DOL!
    SectionSaveStruct* sections = (SectionSaveStruct*)SECTION_SAVE_ADDR;
    if (sections->sectionsMagic == SECTION_SAVE_MAGIC) {
        MakeDOLForInstaller(sections);
    }

    // Setup main data archive
    extern const char data_ar[];
    extern const char data_ar_end[];
    Arch::sInstance = new Arch(data_ar, data_ar_end - data_ar);

    // Launch Saoirse IOS
    IOSBoot::LaunchSaoirseIOS();

    PRINT(Core, INFO, "Send start game IOS request!");
    IOSBoot::IPCLog::sInstance->startGameIOS(dolData, totalDolSize);

    LaunchState::Get()->DiscInserted.state = true;
    LaunchState::Get()->DiscInserted.available = true;
    LaunchState::Get()->ReadDiscID.state = true;
    LaunchState::Get()->ReadDiscID.available = true;
    LaunchState::Get()->LaunchReady.state = true;
    LaunchState::Get()->LaunchReady.available = true;

    // usleep(32000);
    LaunchGame();

    LWP_SuspendThread(LWP_GetSelf());
}
