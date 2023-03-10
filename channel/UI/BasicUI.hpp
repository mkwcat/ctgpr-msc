// BasicUI.hpp - Simple text-based UI
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#pragma once

#include <System/Util.h>
LIBOGC_SUCKS_BEGIN
#include <ogc/video.h>
LIBOGC_SUCKS_END

class BasicUI
{
public:
    static BasicUI* sInstance;

    BasicUI();
    void InitVideo();
    void Loop();

    enum class OptionType {
        StartGame,
        TestFS,
        Exit,
    };

private:
    enum class OptionStatus {
        Disabled,
        Hidden,
        Enabled,
        Waiting,
        Selected,
    };

    void ClearScreen();
    void DrawTitle();
    OptionStatus GetOptionStatus(OptionType opt);
    void DrawOptions();
    void UpdateOptions();
    void OnSelect(OptionType opt);

private:
    GXRModeObj* m_rmode;
#ifndef NDEBUG
    void* m_xfbConsole;
#endif
    void* m_xfbUI;

    int m_selectedOption;
    bool m_cursorEnabled;
    bool m_optionSelected;
};
