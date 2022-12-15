// Blob.cpp
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#include "Blob.hpp"
#include <Debug/Log.hpp>
#include <EmuSDIO/EmuSDIO.hpp>
#include <IOS/IPCLog.hpp>
#include <IOS/Patch.hpp>
#include <System/AES.hpp>
#include <cassert>
#include <cstring>

static const u8 BlobKey[] alignas(32) = {
    0x90, 0x83, 0x00, 0x04, 0x90, 0xA3, 0x00, 0x08,
    0x90, 0xC3, 0x00, 0x0C, 0x4E, 0x80, 0x00, 0x20,
};

static const u8 BlobIv[] = {
    0x80, 0x63, 0x00, 0x04, 0x90, 0x83, 0x00, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20,
};

#define SECTOR_SIZE 512
#define BLOCK_SIZE_SEC 64
#define BLOCK_SIZE (SECTOR_SIZE * BLOCK_SIZE_SEC)

Blob::MountError Blob::Mount(u32 devId)
{
    if (m_opened) {
        auto fret = f_close(&m_fil);
        if (fret != FR_OK)
            return MountError::DiskError;
        m_opened = false;
        m_devId = -1;
    }

    // Create blob.bin path str.
    char str2[64] = "0:/ctgpr/blob.bin";
    str2[0] = devId + '0';

    auto fret = f_open(&m_fil, str2, FA_READ);
    if (fret != FR_OK) {
        PRINT(IOS_DevMgr, ERROR, "Failed to open '%s' fresult=%d", str2, fret);
        if (fret == FR_NO_FILE || fret == FR_NO_PATH)
            return MountError::FileNotFound;
        return MountError::DiskError;
    }

    m_opened = true;
    m_devId = devId;
    return MountError::OK;
}

void Blob::Reset()
{
    m_fil = {};
    m_devId = -1;
    m_opened = false;
}

static void blobEncodeIv(u32 sector, u8* iv)
{
    memcpy(iv, BlobIv, sizeof(BlobIv));
    u8* sectorVal = (u8*)&sector;
    iv[8] = sectorVal[0];
    iv[9] = sectorVal[1];
    iv[10] = sectorVal[2];
    iv[11] = sectorVal[3];
}

void BlobDecrypt(void* data, u8* iv, u32 sectorCount)
{
    while (sectorCount > 0) {
        static u8 cryptBuffer[SECTOR_SIZE * 8] ATTRIBUTE_ALIGN(32);
        if (sectorCount >= 8) {
            memcpy(cryptBuffer, data, 8 * SECTOR_SIZE);
            auto ret = AES::sInstance->Decrypt(BlobKey, iv, cryptBuffer,
                                               8 * SECTOR_SIZE, cryptBuffer);
            assert(ret == 0);
            // IOS_InvalidateDCache(cryptBuffer, 8 * SECTOR_SIZE);
            memcpy(data, cryptBuffer, 8 * SECTOR_SIZE);
            sectorCount -= 8;
            data += 8 * SECTOR_SIZE;
        } else {
            memcpy(cryptBuffer, data, sectorCount * SECTOR_SIZE);
            auto ret =
                AES::sInstance->Decrypt(BlobKey, iv, cryptBuffer,
                                        sectorCount * SECTOR_SIZE, cryptBuffer);
            assert(ret == 0);
            // IOS_InvalidateDCache(cryptBuffer, sectorCount * SECTOR_SIZE);
            memcpy(data, cryptBuffer, sectorCount * SECTOR_SIZE);
            return;
        }
    }
}

FRESULT Blob::ReadSectors(u32 sector, u32 count, void* data)
{
    FRESULT fret = FR_OK;
    u8 iv[16] alignas(32) = {};
    static u8 nextIv[16] = {};
    static u32 nextIvSector = -1;

    if ((sector % BLOCK_SIZE_SEC) == 0) {
        blobEncodeIv(sector, iv);
        if ((fret = f_lseek(&m_fil, sector * 512)) != 0)
            return fret;
    } else if (sector != nextIvSector) {
        if ((fret = f_lseek(&m_fil, (sector * 512) - 16)) != 0)
            return fret;
        UINT br = 0;
        fret = f_read(&m_fil, iv, 16, &br);
        if (fret != FR_OK)
            return fret;
    } else {
        // PRINT(IOS_DevMgr, INFO, "Reusing IV");
        memcpy(iv, nextIv, 16);
        if ((fret = f_lseek(&m_fil, sector * 512)) != 0)
            return fret;
    }

    UINT br;
    fret = f_read(&m_fil, data, 512 * count, &br);
    if (fret != FR_OK)
        return fret;

    u32 s = 0;

    while (s < count) {
        u32 blockEnd = BLOCK_SIZE_SEC - ((sector + s) % BLOCK_SIZE_SEC);
        u32 decryptCount = count < blockEnd ? count - s : blockEnd - s;

        // Store IV for a later decryption
        nextIvSector = sector + s + decryptCount;
        memcpy(nextIv, data + ((s + decryptCount) * SECTOR_SIZE) - 16, 16);

        BlobDecrypt(data + s * SECTOR_SIZE, iv, decryptCount);
        s += decryptCount;

        if (s < count) {
            blobEncodeIv(sector + s, iv);
        }
    }

    return FR_OK;
}

const u8 BootData[] = {
    0x63, 0x56, 0xEF, 0xA4, 0xF6, 0xE3, 0x9A, 0xFC, 0x59, 0xB6, 0x5E, 0xC9,
    0xAB, 0xE7, 0xFF, 0x0A, 0x6E, 0x13, 0xEF, 0xDF, 0xA4, 0x2B, 0x75, 0x34,
    0x25, 0x49, 0x98, 0xA7, 0x08, 0xF4, 0x41, 0xFB, 0xE5, 0x57, 0x5A, 0xB6,
    0x59, 0x87, 0x7D, 0xE0, 0x18, 0xF8, 0x2D, 0x95, 0x57, 0x5E, 0x29, 0xFE,
    0x9A, 0xA0, 0xDB, 0x78, 0x45, 0x68, 0x75, 0xC4, 0x45, 0xFB, 0x1E, 0xE3,
    0x62, 0x57, 0x57, 0xE7, 0xEE, 0x00, 0x2B, 0xA2, 0xE4, 0x77, 0x6F, 0x60,
    0x40, 0x55, 0x62, 0x0B, 0x73, 0xCB, 0x5D, 0xA4, 0x88, 0xE3, 0x7D, 0x61,
    0x1C, 0xE5, 0xBE, 0x14, 0x3D, 0x98, 0x0C, 0x15, 0x0E, 0x0F, 0x64, 0x9A,
    0x29, 0x72, 0x31, 0xBA, 0x35, 0x2E, 0x33, 0xB5, 0x05, 0xF1, 0x07, 0x8C,
    0x5C, 0xDD, 0xCA, 0xDF, 0x48, 0xE0, 0xE5, 0xE9, 0x9D, 0x3D, 0x7F, 0xC4,
    0x03, 0x0D, 0x5C, 0x22, 0x03, 0x52, 0xB8, 0x96,
};

struct DOL {
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
};

bool stubMode = false;

bool Blob::LaunchDOL(FIL* dolFile)
{
    DOL dol ATTRIBUTE_ALIGN(32);

    UINT br = 0;
    auto fret = f_read(dolFile, &dol, sizeof(DOL), &br);
    if (fret != FR_OK) {
        PRINT(IOS_DevMgr, ERROR, "Error reading DOL header: %d", fret);
        return false;
    }

    // Verify DOL header
    if (dol.dol_sect[0] != 0x00000100) {
        // Yeah... I guess that's enough, right?
        PRINT(IOS_DevMgr, ERROR, "Bad main.dol header!");
        return false;
    }

    memset((void*)(dol.dol_bss_addr & 0x7FFFFFFF), 0,
           round_up(dol.dol_bss_size, 32));
    IOS_FlushDCache((void*)(dol.dol_bss_addr & 0x7FFFFFFF),
                    round_up(dol.dol_bss_size, 32));

    // In stub mode only read the first section
    for (int i = 0; stubMode ? i < 1 : i < 7 + 11; i++) {
        if (dol.dol_sect_size[i] != 0) {
            PRINT(IOS_DevMgr, INFO, "Section %d : %08X : %08X : %08X", i,
                  dol.dol_sect[i], dol.dol_sect_addr[i], dol.dol_sect_size[i]);

            fret = f_lseek(dolFile, dol.dol_sect[i]);
            if (fret != FR_OK) {
                PRINT(IOS_DevMgr, INFO, "Failed to seek to position 0x%X",
                      dol.dol_sect[i]);
                return false;
            }

            fret = f_read(dolFile, (void*)(dol.dol_sect_addr[i] & 0x7FFFFFFF),
                          dol.dol_sect_size[i], &br);
            if (fret != FR_OK) {
                PRINT(IOS_DevMgr, INFO,
                      "Failed to read %X bytes from position 0x%X",
                      dol.dol_sect_size[i], dol.dol_sect[i]);
                return false;
            }

            IOS_FlushDCache((void*)(dol.dol_sect_addr[i] & 0x7FFFFFFF),
                            dol.dol_sect_size[i]);
        }
    }

    write32(0x00003400, dol.dol_entry_point);
    IOS_FlushDCache((void*)0x00003400, 4);
    PRINT(IOS_DevMgr, INFO, "Running for Wii, entry point = %08X",
          dol.dol_entry_point);

    // Copy boot data
    memset((void*)0x00001000, 0, 0x100);
    memcpy((void*)0x00001000, BootData, sizeof(BootData));
    IOS_FlushDCache((void*)0x00001000, sizeof(BootData));

    return true;
}

DWORD blobClmt[0x1000] = {0};
DWORD dolClmt[0x100] = {0};

bool Blob::LaunchMainDOL(u32 devId)
{
    if (!m_opened)
        return false;

    PRINT(IOS_DevMgr, INFO, "Opening channel main.dol");

    u64 timeStart = System::GetTime();

    // Blob.bin FAST SEEK
    u32 clmtSize = sizeof(blobClmt) / sizeof(DWORD);

    m_fil.cltbl = blobClmt;
    blobClmt[0] = clmtSize;

    f_lseek(&m_fil, CREATE_LINKMAP);

    FIL dolFile;
    bool dolret;
    FRESULT fret;

    // Attempt to read from blob.bin
    char str2[64] = "0:/packages/chan/stub.dol";
    str2[0] = devId + '0';

    fret = f_open(&dolFile, str2, FA_READ);
    if (fret != FR_OK) {
        PRINT(IOS_DevMgr, ERROR, "Failed to open '%s' fresult=%d", str2, fret);
        return false;
    }

    PRINT(IOS_DevMgr, INFO, "Successfully opened channel stub.dol");

    stubMode = true;
    dolret = LaunchDOL(&dolFile);
    PRINT(IOS_DevMgr, INFO, "dolret: %d", dolret);

    // Some stub patches required for room sync
    const u32 stubBase = 0x4000;
    write32(stubBase + 0x5E98, 0x4E800020);
    IOS_FlushDCache((void*)(stubBase + 0x5E98), 4);
    write32(stubBase + 0x5EA0, 0x4E800020);
    IOS_FlushDCache((void*)(stubBase + 0x5EA0), 4);

    f_close(&dolFile);

    char str3[64] = "0:/packages/chan/main.dol";
    str3[0] = devId + '0';

    fret = f_open(&dolFile, str3, FA_READ);
    if (fret != FR_OK) {
        PRINT(IOS_DevMgr, ERROR, "Failed to open '%s' fresult=%d", str3, fret);
        return false;
    }

    PRINT(IOS_DevMgr, INFO, "Successfully opened channel main.dol");

    // FatFS fast seek feature
    // Use FatFS fast seek function to speed up long backwards seeks
    // Distribute cluster map equally across the two parts
    clmtSize = sizeof(dolClmt) / sizeof(DWORD);

    dolFile.cltbl = dolClmt;
    dolClmt[0] = clmtSize;

    fret = f_lseek(&dolFile, CREATE_LINKMAP);

    stubMode = false;
    dolret = LaunchDOL(&dolFile);
    PRINT(IOS_DevMgr, INFO, "dolret: %d", dolret);

    f_close(&dolFile);

    u64 timeEnd = System::GetTime();
    PRINT(IOS_DevMgr, INFO, "Time elapsed: %lld", timeEnd - timeStart);

    if (dolret) {
        EmuSDIO::g_emuDevId = m_devId;
        IPCLog::sInstance->Notify(0);
    }

    return dolret;
}
