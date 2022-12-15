// EmuHID.hpp
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#pragma once

#include <System/Types.h>

namespace EmuHID
{

void Reload();
s32 ThreadEntry(void* arg);

} // namespace EmuHID
