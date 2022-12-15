// Blob.hpp
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#pragma once

#include <FAT/ff.h>
#include <System/Types.h>
#include <System/Util.h>

class Blob
{
public:
    enum class MountError {
        OK,
        FileNotFound,
        DiskError,
    };

    MountError Mount(u32 devId);
    void Reset();
    FRESULT ReadSectors(u32 sector, u32 count, void* data);

    bool LaunchDOL(FIL* dolFile);
    bool LaunchMainDOL(u32 devId);

public:
    bool m_mounted;

    FIL m_fil ATTRIBUTE_ALIGN(32);
    int m_devId = -1;

    bool m_opened;
};
