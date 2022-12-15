// BasicUI.cpp - Simple text-based UI
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#include "BasicUI.hpp"
#include <Main/LaunchState.hpp>
#include <Main/Saoirse.hpp>
#include <UI/Input.hpp>
#include <UI/debugPrint.h>
#include <cstdio>
#include <cstdlib>
LIBOGC_SUCKS_BEGIN
#include <ogc/consol.h>
#include <ogc/machine/processor.h>
#include <ogc/system.h>
#include <wiiuse/wpad.h>
LIBOGC_SUCKS_END

BasicUI* BasicUI::sInstance;

struct OptionDisplay {
    const char* title;
    BasicUI::OptionType type;
};

OptionDisplay options[] = {
    {
        "Exit",
        BasicUI::OptionType::Exit,
    },
};

BasicUI::BasicUI()
{
    m_rmode = nullptr;

#ifndef NDEBUG
    m_xfbConsole = nullptr;
#endif

    m_xfbUI = nullptr;
}

void BasicUI::InitVideo()
{
    // Initialize video.
    VIDEO_Init();

    // Get preferred video mode.
    m_rmode = VIDEO_GetPreferredMode(NULL);

    // Allocate framebuffers.
#ifndef NDEBUG
    m_xfbConsole = MEM_K0_TO_K1(SYS_AllocateFramebuffer(m_rmode));

    // Initialize debug console.
    console_init(m_xfbConsole, 20, 20, m_rmode->fbWidth, m_rmode->xfbHeight,
                 m_rmode->fbWidth * VI_DISPLAY_PIX_SZ);
#endif
    m_xfbUI = MEM_K0_TO_K1(SYS_AllocateFramebuffer(m_rmode));

    // Configure render mode.
    VIDEO_Configure(m_rmode);

    // Set framebuffer to UI.
    VIDEO_SetNextFramebuffer(m_xfbUI);

    // Clear UI framebuffer
    ClearScreen();

    // Disable VI black.
    VIDEO_SetBlack(0);

    VIDEO_Flush();
    VIDEO_WaitVSync();

    if (m_rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    printf("\x1b[2;0H");

    // Draw text to the screen.
    DebugPrint_Init(m_xfbUI, m_rmode->fbWidth, m_rmode->xfbHeight);
}

void BasicUI::Loop()
{
    m_cursorEnabled = true;
    m_optionSelected = false;
    m_selectedOption = 0;

    while (true) {
        UpdateOptions();
        DrawTitle();
        // DrawOptions();

        if (m_optionSelected) {
            OnSelect(options[m_selectedOption].type);
        }

        Input::sInstance->ScanButton();

#ifndef NDEBUG
        u32 buttonDown = Input::sInstance->GetButtonDown();
        u32 buttonUp = Input::sInstance->GetButtonUp();

        if (buttonDown & Input::BTN_DEBUG) {
            // Switch XFB to console.
            VIDEO_SetNextFramebuffer(m_xfbConsole);
            VIDEO_Flush();
        }

        if (buttonUp & Input::BTN_DEBUG) {
            // Switch XFB to UI.
            VIDEO_SetNextFramebuffer(m_xfbUI);
            VIDEO_Flush();
        }
#endif

        VIDEO_Flush();
        VIDEO_WaitVSync();
    }
}

void BasicUI::ClearScreen()
{
    for (int i = 0; i < m_rmode->fbWidth * m_rmode->xfbHeight * 2; i += 4) {
        write32(reinterpret_cast<u32>(m_xfbUI) + i, BACKGROUND_COLOUR);
    }
}

void BasicUI::DrawTitle()
{
    if (LaunchState::Get()->Error.state == LaunchError::OK)
        return;

    // Sometimes the first USB device change will return 0 devices for some
    // reason, so let's wait to display something
    static int waitTick = 30;
    if (waitTick != 0) {
        waitTick--;
        return;
    }

    Input::sInstance->Init();

    DebugPrint_Printf(2, 1, "CTGP-R MSC v1.0");

    switch (LaunchState::Get()->Error.state) {
    case LaunchError::NoSDCard:
        DebugPrint_Printf(7, 4, "Please insert an SD card or USB.");
        DebugPrint_Printf(8, 4, "                                ");
        break;

    case LaunchError::NoCTGPR:
        DebugPrint_Printf(7, 4, "The inserted SD card or USB does");
        DebugPrint_Printf(8, 4, "      not contain CTGP-R.       ");
        break;

    case LaunchError::CTGPCorrupt:
        DebugPrint_Printf(7, 4, "     Could not load CTGP-R.     ");
        DebugPrint_Printf(8, 4, "   Your pack may be corrupted.  ");
        break;

    default:
        DebugPrint_Printf(7, 4, "Error not implemented!");
        break;
    }
}

BasicUI::OptionStatus BasicUI::GetOptionStatus(OptionType opt)
{
    if (m_optionSelected && options[m_selectedOption].type == opt)
        return OptionStatus::Selected;

    switch (opt) {
    case OptionType::Exit:
        return OptionStatus::Enabled;

    default:
        return OptionStatus::Hidden;
    }
}

void BasicUI::DrawOptions()
{
}

void BasicUI::UpdateOptions()
{
    if (!m_cursorEnabled)
        return;

    u32 btn = Input::sInstance->GetButtonDown();

    if (btn & Input::BTN_HOME) {
        if (GetOptionStatus(options[m_selectedOption].type) ==
            OptionStatus::Enabled) {
            m_cursorEnabled = false;
            m_optionSelected = true;
        }
    }
}

extern void ReturnToLoader();

void BasicUI::OnSelect([[maybe_unused]] OptionType opt)
{
    Input::sInstance->Shutdown();
    ReturnToLoader();
}
