// EmuSDIO.hpp
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#pragma once

#include <System/Types.h>

namespace EmuSDIO
{

extern int g_emuDevId;
s32 ThreadEntry(void* arg);

} // namespace EmuSDIO
