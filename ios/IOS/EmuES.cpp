// EmuFS.cpp - Proxy ES RM
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#include "EmuES.hpp"
#include <CTGP/EmuHID.hpp>
#include <Debug/Log.hpp>
#include <IOS/IPCLog.hpp>
#include <IOS/Patch.hpp>
#include <IOS/System.hpp>
#include <System/AES.hpp>
#include <System/Config.hpp>
#include <System/ES.hpp>
#include <System/Hollywood.hpp>
#include <System/OS.hpp>
#include <System/Util.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace EmuES
{

#define CTGPR_CHANNEL_ID 0x00010001524D4358

static Queue<IOS::Request*> queue(8);

static bool s_useTitleCtx = false;
static u64 s_titleID;
static ES::Ticket s_ticket;

static bool s_ctgpTicketAdded = false;
static u8 s_ctgpTitleKey[16];
static s32 s_ctgpStubCid = -1;
static s32 s_ctgpStubIndex = -1;
static s32 s_ctgpStubCfd = -1;

static u8 s_stubIvSave[16];
static u8 s_stubKeySave[16];

ES::ESError DIVerify(u64 titleID, const ES::Ticket* ticket)
{
    s_titleID = titleID;
    if (ticket->info.titleID != titleID)
        return ES::ESError::InvalidTicket;

    s_ticket = *ticket;

    s_useTitleCtx = true;
    return ES::ESError::OK;
}

static s32 PPCBootReloadThread(void* arg)
{
    PRINT(IOS_EmuES, INFO, "Entering the PPCBOOT thread");

    u32 ipcOldThread = read32(ipcThreadPtr);

    write32(ipcThreadPtr, 0xFFFE0000 + IOS_GetThreadId() * 0xB0);

    for (s32 i = 0; i < 32; i++) {
        IOS_Close(i);
    }

    write32(ipcThreadPtr, ipcOldThread);

    PRINT(IOS_EmuES, INFO, "Exiting the PPCBOOT thread");

    static IOS::Request replyReq = {};
    replyReq.cmd = IOS::Command::Reply;
    PRINT(IOS_EmuES, INFO, "Sending reply");
    queue.send(&replyReq);
    PRINT(IOS_EmuES, INFO, "Sent reply, bye!");

    // Command acknowledge
    if (bool(arg)) {
        mask32(0x0D800004, 0, 2);
    }
    return 0;
}

static void CloseAllPPC(bool ack)
{
    // Create a new system thread
    static u8 SystemThreadStack[0x800] ATTRIBUTE_ALIGN(32);

    s32 ret = IOS_CreateThread(
        PPCBootReloadThread, (void*)ack,
        reinterpret_cast<u32*>(SystemThreadStack + sizeof(SystemThreadStack)),
        sizeof(SystemThreadStack), 80, true);
    if (ret < 0)
        AbortColor(YUV_YELLOW);

    // Set new thread CPSR with system mode enabled
    u32 cpsr = 0x1F | ((u32)(PPCBootReloadThread)&1 ? 0x20 : 0);
    KernelWrite(0xFFFE0000 + ret * 0xB0, cpsr);

    ret = IOS_StartThread(ret);
    if (ret < 0)
        AbortColor(YUV_YELLOW);

    while (true) {
        // Wait for either close or the reply from the PPCBOOT
        // thread
        auto req = queue.receive();
        if (req->cmd == IOS::Command::Close) {
            // Closing...
            PRINT(IOS_EmuES, INFO, "Got the close command!");
            req->reply(-1);
        } else if (req->cmd == IOS::Command::Reply)
            break;
    }
}

/*
 * Handles ES ioctlv commands.
 */
static ES::ESError ReqIoctlv(ES::ESIoctl cmd, u32 inCount, u32 outCount,
                             IOS::Vector* vec)
{
    if (inCount >= 32 || outCount >= 32)
        return ES::ESError::Invalid;

    // NULL any zero length vectors to prevent any accidental writes.
    for (u32 i = 0; i < inCount + outCount; i++) {
        if (vec[i].len == 0)
            vec[i].data = nullptr;
    }

    PRINT(IOS_EmuES, INFO, "ES Ioctl: %u", cmd);

    switch (cmd) {
    case ES::ESIoctl::GetDeviceID: {
        if (inCount != 0 || outCount != 1) {
            PRINT(IOS_EmuES, ERROR, "GetDeviceID: Wrong vector count");
            return ES::ESError::Invalid;
        }

        if (vec[0].len != sizeof(u32) || !aligned(vec[0].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "GetDeviceID: Wrong device ID size or alignment");
            return ES::ESError::Invalid;
        }

        u32 deviceID = 0;

        auto ret = ES::sInstance->GetDeviceID(&deviceID);
        if (ret != ES::ESError::OK) {
            return ret;
        }

        if (IsWiiU() && deviceID < 0x20000000) {
            deviceID |= 0x20000000;
        }

        if (!IsWiiU() && deviceID >= 0x20000000) {
            deviceID &= 0x1FFFFFFF;
        }

        *reinterpret_cast<u32*>(vec[0].data) = deviceID;
        return ES::ESError::OK;
    }

    case ES::ESIoctl::AddTicket: {
        PRINT(IOS_EmuES, INFO, "AddTicket called!");

        if (inCount != 3 || outCount != 0) {
            PRINT(IOS_EmuES, ERROR, "AddTicket: Wrong vector count");
            return ES::ESError::Invalid;
        }

        if (vec[0].len != sizeof(ES::Ticket)) {
            PRINT(IOS_EmuES, ERROR, "AddTicket: Ticket size is wrong: %u",
                  vec[0].len);
            return ES::ESError::Invalid;
        }

        ES::Ticket ticket = *reinterpret_cast<ES::Ticket*>(vec[0].data);

        if (ticket.info.titleID != CTGPR_CHANNEL_ID) {
            PRINT(IOS_EmuES, INFO, "Not CTGP-R channel ticket");
            return ES::ESError(
                ES::sInstance->m_rm.ioctlv(cmd, inCount, outCount, vec));
        }

        u8 titleKeyBuffer[32] ATTRIBUTE_ALIGN(32);
        memcpy(titleKeyBuffer, ticket.titleKey, 16);

        // TODO: Get the keys and decrypt a more 'normal' way
        u8 key[16] ATTRIBUTE_ALIGN(32) = {
            0xeb, 0xe4, 0x2a, 0x22, 0x5e, 0x85, 0x93, 0xe4,
            0x48, 0xd9, 0xc5, 0x45, 0x73, 0x81, 0xaa, 0xf7,
        };

        u8 iv[16] = {0};
        memcpy(iv, &ticket.info.titleID, 8);

        auto ret2 = AES::sInstance->Decrypt(
            key, iv, titleKeyBuffer, sizeof(titleKeyBuffer), titleKeyBuffer);
        assert(ret2 == IOSError::OK);
        memcpy(s_ctgpTitleKey, titleKeyBuffer, 16);

        PRINT(IOS_EmuES, INFO, "Title key excerpt: %02X%02X%02X%02X",
              s_ctgpTitleKey[0], s_ctgpTitleKey[1], s_ctgpTitleKey[2],
              s_ctgpTitleKey[3]);

        skipSignCheck = true;
        auto ret = ES::ESError(
            ES::sInstance->m_rm.ioctlv(cmd, inCount, outCount, vec));
        skipSignCheck = false;

        s_ctgpTicketAdded = true;

        PRINT(IOS_EmuES, INFO, "ret: %d", ret);
        return ret;
    }

    case ES::ESIoctl::AddTitleStart: {
        PRINT(IOS_EmuES, INFO, "AddTitleStart called!");

        if (inCount != 4 || outCount != 0) {
            PRINT(IOS_EmuES, ERROR, "AddTitleStart: Wrong vector count");
            return ES::ESError::Invalid;
        }

        // todo check and edit tmd

        if (vec[0].len < sizeof(ES::TMDHeader)) {
            PRINT(IOS_EmuES, ERROR,
                  "AddTitleStart: TMD size < sizeof(ES::TMDHeader)");
            return ES::ESError::Invalid;
        }

        ES::TMD* tmd = reinterpret_cast<ES::TMD*>(vec[0].data);

        s_ctgpStubIndex = -1;
        s_ctgpStubCid = -1;
        s_ctgpStubCfd = -1;

        if (tmd->header.titleID != CTGPR_CHANNEL_ID) {
            PRINT(IOS_EmuES, ERROR,
                  "Attempt to add a title that's not CTGP-R: %08llX",
                  tmd->header.titleID);
            return ES::ESError::RequiredIOSNotInstalled;
        }

        u32 tmdSize = tmd->size();

        u8* tmdBlob = new u8[tmdSize];
        memcpy(tmdBlob, tmd, tmdSize);
        tmd = reinterpret_cast<ES::TMD*>(tmdBlob);

        static const u8 s_stubHash[0x14] = {
            0x8C, 0xE9, 0xA8, 0xCD, 0x74, 0xC0, 0x16, 0xFB, 0xFD, 0xFA,
            0x5F, 0xEE, 0x09, 0x9A, 0xD9, 0xFF, 0xFC, 0xAC, 0x6E, 0x84,
        };

        static constexpr u64 s_stubSize = 0x45360;

        // Search for stub (but skip the banner)
        u32 i = 1;
        for (; i < tmd->header.numContents; i++) {
            if (tmd->getContents()[i].size == s_stubSize &&
                !memcmp(tmd->getContents()[i].hash, s_stubHash, 0x14)) {
                PRINT(IOS_EmuES, INFO, "Found stub as index %u", i);
                break;
            }
        }

        if (i >= tmd->header.numContents) {
            PRINT(IOS_EmuES, WARN, "Could not find the stub hash!");
        } else {
            s_ctgpStubIndex = i;
            s_ctgpStubCid = tmd->getContents()[i].cid;
            memcpy(tmd->getContents()[i].hash, System::s_dolHash, 0x14);
            tmd->getContents()[i].size = System::s_dolSize;
        }

        vec[0].data = tmdBlob;

        skipSignCheck = true;
        auto ret = ES::ESError(
            ES::sInstance->m_rm.ioctlv(cmd, inCount, outCount, vec));
        skipSignCheck = false;

        PRINT(IOS_EmuES, INFO, "ret: %d", ret);
        return ret;
    }

    case ES::ESIoctl::AddContentStart: {
        if (inCount != 2 || outCount != 0) {
            PRINT(IOS_EmuES, ERROR, "AddContentStart: Wrong vector count");
            return ES::ESError::Invalid;
        }

        if (vec[0].len != sizeof(u64) || !aligned(vec[0].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "AddContentStart: Wrong title ID size or alignment");
            return ES::ESError::Invalid;
        }

        if (vec[1].len != sizeof(u32) || !aligned(vec[1].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "AddContentStart: Wrong CID size or alignment");
            return ES::ESError::Invalid;
        }

        u64 titleID = *reinterpret_cast<u64*>(vec[0].data);

        if (titleID != CTGPR_CHANNEL_ID) {
            PRINT(IOS_EmuES, ERROR,
                  "AddContentStart: Not CTGP-R channel: %016llX", titleID);
            return ES::ESError::Invalid;
        }

        auto ret = ES::sInstance->m_rm.ioctlv(cmd, inCount, outCount, vec);

        u32 cid = *reinterpret_cast<u32*>(vec[1].data);
        if (s_ctgpStubCid != -1 && cid == u32(s_ctgpStubCid)) {
            PRINT(IOS_EmuES, INFO,
                  "AddContentStart: Add CTGP-R stub content! cfd: %d", ret);
            if (cid == 0) {
                PRINT(IOS_EmuES, INFO, "WHY IS IT CID 0");
                return ES::ESError::Invalid;
            }
            if (ret >= 0)
                s_ctgpStubCfd = ret;
        }

        return ES::ESError(ret);
    }

    case ES::ESIoctl::AddContentData: {
        if (inCount != 2 || outCount != 0) {
            PRINT(IOS_EmuES, ERROR, "AddContentData: Wrong vector count");
            return ES::ESError::Invalid;
        }

        if (vec[0].len != sizeof(s32) || !aligned(vec[0].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "AddContentData: Wrong CFD size or alignment");
            return ES::ESError::Invalid;
        }
        s32 cfd = *reinterpret_cast<s32*>(vec[0].data);

        if (s_ctgpStubCfd != -1 && cfd == s_ctgpStubCfd) {
            return ES::ESError::OK;
        }

        auto ret = ES::ESError(
            ES::sInstance->m_rm.ioctlv(cmd, inCount, outCount, vec));

        PRINT(IOS_EmuES, INFO, "ret: %d", ret);
        return ret;
    }

    case ES::ESIoctl::AddContentFinish: {
        if (inCount != 1 || outCount != 0) {
            PRINT(IOS_EmuES, ERROR, "AddContentFinish: Wrong vector count");
            return ES::ESError::Invalid;
        }

        if (vec[0].len != sizeof(s32) || !aligned(vec[0].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "AddContentFinish: Wrong CFD size or alignment");
            return ES::ESError::Invalid;
        }
        s32 cfd = *reinterpret_cast<s32*>(vec[0].data);

        if (s_ctgpStubCfd != -1 && cfd == s_ctgpStubCfd) {
            PRINT(IOS_EmuES, INFO, "AddContentFinish: Swapping stub data!");

            s_ctgpStubCfd = -1;

            u8 key[16] = {};
            u8 iv[16] = {};

            memcpy(key, s_ctgpTitleKey, 16);
            iv[0] = (s_ctgpStubIndex >> 8) & 0xFF;
            iv[1] = s_ctgpStubIndex & 0xFF;

            static constexpr u32 WriteSize = 0x1000;
            u8* cryptData = new u8[WriteSize];

            for (u32 pos = 0; pos < System::s_dolSize; pos += WriteSize) {
                u32 size = std::min(System::s_dolSize - pos, WriteSize);

                auto aesRet = AES::sInstance->Encrypt(
                    key, iv, System::s_dolData + pos, size, cryptData);
                if (aesRet != 0) {
                    PRINT(IOS_EmuES, ERROR, "AES encryption failed: %d",
                          aesRet);
                    delete[] cryptData;
                    return ES::ESError::Invalid;
                }

                auto esRet =
                    ES::sInstance->AddContentData(cfd, cryptData, size);
                if (esRet != ES::ESError::OK) {
                    PRINT(IOS_EmuES, ERROR, "ES AddContentData failed: %d",
                          esRet);
                    delete[] cryptData;
                    return esRet;
                }
            }

            PRINT(IOS_EmuES, INFO, "Successfully imported our stub");
        }

        auto ret = ES::ESError(
            ES::sInstance->m_rm.ioctlv(cmd, inCount, outCount, vec));

        PRINT(IOS_EmuES, INFO, "ret: %d", ret);
        return ret;
    }

    case ES::ESIoctl::LaunchTitle: {
        if (inCount != 2 || outCount != 0) {
            PRINT(IOS_EmuES, ERROR, "LaunchTitle: Wrong vector count");
            return ES::ESError::Invalid;
        }

        if (vec[0].len != sizeof(u64) || !aligned(vec[0].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "LaunchTitle: Wrong title ID size or alignment");
            return ES::ESError::Invalid;
        }

        if (vec[1].len != sizeof(ES::TicketView) || !aligned(vec[1].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "LaunchTitle: Wrong ticket view size or alignment");
            return ES::ESError::Invalid;
        }

        u64 titleID = *reinterpret_cast<u64*>(vec[0].data);
        ES::TicketView view = *reinterpret_cast<ES::TicketView*>(vec[1].data);
        PRINT(IOS_EmuES, INFO, "LaunchTitle: Launching title %016llX", titleID);

        // Redirect to system menu on attempted IOS reload
        if (Config::sInstance->BlockIOSReload() && u64Hi(titleID) == 1 &&
            u64Lo(titleID) != 2) {
            PRINT(IOS_EmuES, WARN,
                  "LaunchTitle: Attempt to launch IOS title %016llX", titleID);

            // Close all open handles on PPCBOOT PID
            CloseAllPPC(true);

            PRINT(IOS_EmuES, INFO, "We're here");

            s32 immFd = IOS_Open("/dev/stm/immediate", 0);
            if (immFd >= 0) {
                u8 inBuf[0x20] alignas(32);
                u8 outBuf[0x20] alignas(32);
                IOS_Ioctl(immFd, 0x3002, inBuf, sizeof(inBuf), outBuf,
                          sizeof(outBuf));
                IOS_Close(immFd);
            }

            EmuHID::Reload();

            *(u32*)0x3140 = ((u64Lo(titleID) & 0xFFFF) << 16) |
                            view.info.ticketTitleVersion;
            IOS_FlushDCache((void*)0x3140, 4);

            usleep(400);

            return ES::ESError::OK;
        }

        // Nuclear strategy, fixes the crash on second boot, lol
        memset((void*)0x00004000, 0, 0x01800000 - 0x4000);
        // Flushing is pointless here as IOS reload flushes the whole cache
        // IOS_FlushDCache((void*)0x00004000, 0x01800000 - 0x4000);

        PRINT(IOS_EmuES, INFO, "LaunchTitle: Launching %016llX...", titleID);
        return ES::sInstance->LaunchTitle(titleID, &view);
    }

    case ES::ESIoctl::GetNumTicketViews: {
        if (inCount != 1 || outCount != 1) {
            PRINT(IOS_EmuES, ERROR, "GetNumTicketViews: Wrong vector count");
            return ES::ESError::Invalid;
        }

        if (vec[0].len != sizeof(u64) || !aligned(vec[0].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "GetNumTicketViews: Wrong title ID size or alignment");
            return ES::ESError::Invalid;
        }

        u64 titleID = *reinterpret_cast<u64*>(vec[0].data);

        if (u64Hi(titleID) == 0x00000001 && u64Lo(titleID) != 0x00000002) {
            PRINT(IOS_EmuES, ERROR,
                  "GetNumTicketViews: Denying %016llX in attempt to block IOS "
                  "reload",
                  titleID);
            CloseAllPPC(false);
            EmuHID::Reload();
            return ES::ESError::Invalid;
        }

        if (vec[1].len != sizeof(u32) || !aligned(vec[1].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "GetNumTicketViews: Wrong count vector size or alignment");
            return ES::ESError::Invalid;
        }

        return ES::sInstance->GetNumTicketViews(
            titleID, reinterpret_cast<u32*>(vec[1].data));
    }

    case ES::ESIoctl::GetTicketViews: {
        if (inCount != 2 || outCount != 1) {
            PRINT(IOS_EmuES, ERROR, "GetTicketViews: Wrong vector count");
            return ES::ESError::Invalid;
        }

        if (vec[0].len != sizeof(u64) || !aligned(vec[0].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "GetTicketViews: Wrong title ID size or alignment");
            return ES::ESError::Invalid;
        }

        u64 titleID = *reinterpret_cast<u64*>(vec[0].data);

        if (vec[1].len != sizeof(u32) || !aligned(vec[1].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "GetTicketViews: Wrong count vector size or alignment");
            return ES::ESError::Invalid;
        }

        // u64 to prevent multiply overflow
        u64 count = *reinterpret_cast<u32*>(vec[1].data);

        if (vec[2].len != count * sizeof(ES::TicketView) ||
            !aligned(vec[2].data, 4)) {
            PRINT(IOS_EmuES, ERROR,
                  "GetTicketViews: Wrong ticket view vector size or alignment");
            return ES::ESError::Invalid;
        }

        return ES::sInstance->GetTicketViews(
            titleID, count, reinterpret_cast<ES::TicketView*>(vec[2].data));
    }

    default:
        // Doesn't matter, just send it to real ES
        return ES::ESError(
            ES::sInstance->m_rm.ioctlv(cmd, inCount, outCount, vec));
    }
}

static s32 IPCRequest(IOS::Request* req)
{
    switch (req->cmd) {
    case IOS::Command::Open:
        if (strcmp(req->open.path, "~dev/es") != 0)
            return IOSError::NotFound;

        Config::sInstance->m_blockIOSReload = true;
        PRINT(IOS_EmuES, INFO, "ES opened");

        return IOSError::OK;

    case IOS::Command::Close:
        PRINT(IOS_EmuES, INFO, "ES closed");
        return IOSError::OK;

    case IOS::Command::Ioctlv:
        return static_cast<s32>(ReqIoctlv(
            static_cast<ES::ESIoctl>(req->ioctlv.cmd), req->ioctlv.in_count,
            req->ioctlv.io_count, req->ioctlv.vec));

    default:
        PRINT(IOS_EmuES, ERROR, "Invalid cmd: %d", static_cast<s32>(req->cmd));
        return static_cast<s32>(ES::ESError::Invalid);
    }
}

s32 ThreadEntry(void* arg)
{
    PRINT(IOS_EmuES, INFO, "Starting ES...");
    PRINT(IOS_EmuES, INFO, "EmuES thread ID: %d", IOS_GetThreadId());

    s32 ret = IOS_RegisterResourceManager("~dev/es", queue.id());
    assert(ret == IOSError::OK);

    IPCLog::sInstance->Notify(2);
    while (true) {
        IOS::Request* req = queue.receive();
        auto ret = IPCRequest(req);
        if (req->cmd != IOS::Command::Ioctlv ||
            req->ioctlv.cmd != u32(ES::ESIoctl::LaunchTitle) ||
            ret != s32(ES::ESError::OK)) {
            PRINT(IOS_EmuES, INFO, "Reply: %d", ret);
            req->reply(ret);
        }
    }
}

} // namespace EmuES
