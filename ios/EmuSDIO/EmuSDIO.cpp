// EmuSDIO.cpp
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#include "EmuSDIO.hpp"
#include <Debug/Log.hpp>
#include <Disk/DeviceMgr.hpp>
#include <IOS/IPCLog.hpp>
#include <System/OS.hpp>

int EmuSDIO::g_emuDevId = -1;

enum class SDIOIoctl {
    WriteHCReg = 0x01,
    ReadHCReg = 0x02,
    ReadCReg = 0x03,
    ResetCard = 0x04,
    WriteCReg = 0x05,
    SetClk = 0x06,
    SendCmd = 0x07,
    SetBusWidth = 0x08,
    ReadMCReg = 0x09,
    WriteMCReg = 0x0A,
    GetStatus = 0x0B,
    GetOCR = 0x0C,
    ReadData = 0x0D,
    WriteData = 0x0E,
};

enum {
    SDIO_STATUS_CARD_INSERTED = 0x1,
    SDIO_STATUS_CARD_INITIALIZED = 0x10000,
    SDIO_STATUS_CARD_SDHC = 0x100000,
};

enum {
    RET_OK,
    RET_FAIL,
};

enum class SDIOCommand : u32 {
    GoIdle = 0x00,
    AllSendCID = 0x02,
    SendRCA = 0x03,
    SetBusWidth = 0x06,
    Select = 0x07,
    Deselect = 0x07,
    SendIfCond = 0x08,
    SendCSD = 0x09,
    SendCID = 0x0A,
    SendStatus = 0x0D,
    SetBlockLen = 0x10,
    ReadBlock = 0x11,
    ReadMultiBlock = 0x12,
    WriteBlock = 0x18,
    WriteMultiBlock = 0x19,
    AppCmd = 0x37,
};

struct SDIORequest {
    SDIOCommand cmd;
    u32 cmdType;
    u32 rspType;
    u32 arg;
    u32 blkCnt;
    u32 blkSize;
    void* addr;
    u32 isDMA;
    u32 pad0;
};
static_assert(sizeof(SDIORequest) == 0x24);

s32 WriteWordOut(void* out, u32 outLen, u32 value)
{
    if (outLen < 4 || !aligned(out, 4)) {
        PRINT(IOS_EmuSDIO, ERROR, "outLen < 4 || !is_aligned(out, 4)");
        return IOSError::Invalid;
    }

    *reinterpret_cast<u32*>(out) = value;
    return IOSError::OK;
}

s32 WriteWordOutCmd(void* out, u32 outLen, u32 value)
{
    if (outLen < 4 || !aligned(out, 4)) {
        PRINT(IOS_EmuSDIO, ERROR, "outLen < 4 || !is_aligned(out, 4)");
        return RET_FAIL;
    }

    *reinterpret_cast<u32*>(out) = value;
    return RET_OK;
}

template <class T>
s32 WriteStructOut(void* out, u32 outLen, T data)
{
    if (outLen < sizeof(T) || !aligned(out, 4)) {
        PRINT(IOS_EmuSDIO, ERROR, "outLen < sizeof(T) || !is_aligned(out, 4)");
        return IOSError::Invalid;
    }

    memcpy(out, &data, sizeof(T));
    return IOSError::OK;
}

template <class T>
s32 WriteStructOutCmd(void* out, u32 outLen, T data)
{
    if (outLen < sizeof(T) || !aligned(out, 4)) {
        PRINT(IOS_EmuSDIO, ERROR, "outLen < sizeof(T) || !is_aligned(out, 4)");
        return RET_FAIL;
    }

    memcpy(out, &data, sizeof(T));
    return RET_OK;
}

static s32 ExecuteCommand(SDIORequest req, void* rwBuffer, u32 rwBufferLen,
                          void* out, u32 outLen)
{
    switch (req.cmd) {
    case SDIOCommand::GoIdle: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOCommand::GoIdle");
        return WriteWordOutCmd(out, outLen, 0);
    }

    case SDIOCommand::SendRCA: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOCommand::SendRCA");
        return WriteWordOutCmd(out, outLen, 0x9F62);
    }

    case SDIOCommand::Select: {
        // PRINT(IOS_EmuSDIO, INFO, "SDIOCommand::SelectCard");
        // Differentiate between select and deselect by whether or not the
        // caller provided the RCA.
        return WriteWordOutCmd(out, outLen,
                               (req.arg & 0xFFFF0000) ? 0x700 : 0x900);
    }

    case SDIOCommand::SendIfCond: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOCommand::SendIfCond");
        return WriteWordOutCmd(out, outLen, req.arg);
    }

    case SDIOCommand::SendCSD: {
        // I don't know how to do this, but is it ever used? No, it's not.
        PRINT(IOS_EmuSDIO, ERROR, "SDIOCommand::SendCSD not implemented!");
        return RET_FAIL;
    }

    case SDIOCommand::AllSendCID:
    case SDIOCommand::SendCID: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOCommand::SendCID");

        struct {
            u32 value[4];
        } data;

        data.value[0] = 0x80114D1C;
        data.value[1] = 0x80080000;
        data.value[2] = 0x8007B520;
        data.value[3] = 0x80080000;
        return WriteStructOutCmd(out, outLen, data);
    }

    case SDIOCommand::SetBlockLen: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOCommand::SetBlockLen");
        if (req.arg != 512) {
            PRINT(IOS_EmuSDIO, ERROR, "Invalid block length: %u!", req.arg);
            return RET_FAIL;
        }
        return WriteWordOutCmd(out, outLen, 0x900);
    }

    case SDIOCommand::AppCmd: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOCommand::AppCmd");
        return WriteWordOut(out, outLen, 0x920);
    }

    case SDIOCommand::SetBusWidth: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOCommand::SetBusWidth");
        return WriteWordOutCmd(out, outLen, 0x920);
    }

    case SDIOCommand::ReadMultiBlock: {
        // PRINT(IOS_EmuSDIO, INFO, "Read %u sectors from offset %u",
        // req.blkCnt,
        //       req.arg);

        if (!req.isDMA) {
            PRINT(IOS_EmuSDIO, ERROR, "Read multiple block without DMA");
            return RET_FAIL;
        }

        if (((void*)(u32(req.addr) & 0x7FFFFFFF) != rwBuffer) ||
            (req.blkCnt * 512 != rwBufferLen)) {
            PRINT(IOS_EmuSDIO, ERROR, "Invalid RW buffer supplied");
            PRINT(IOS_EmuSDIO, ERROR, "req.addr = %08X, rwBuffer = %08X",
                  req.addr, rwBuffer);
            PRINT(IOS_EmuSDIO, ERROR,
                  "req.blkCnt * 512 = %08X, rwBufferLen = %08X",
                  req.blkCnt * 512, rwBufferLen);
            return RET_FAIL;
        }

        if (!DeviceMgr::sInstance->DeviceRead(EmuSDIO::g_emuDevId, rwBuffer,
                                              req.arg, req.blkCnt)) {
            return RET_FAIL;
        }

        return WriteWordOutCmd(out, outLen, 0x900);
    }

    case SDIOCommand::WriteMultiBlock: {
        // PRINT(IOS_EmuSDIO, INFO, "Write %u sectors at offset %u", req.blkCnt,
        //       req.arg);

        if (!req.isDMA) {
            PRINT(IOS_EmuSDIO, ERROR, "Write multiple block without DMA");
            return RET_FAIL;
        }

        if ((void*)(u32(req.addr) & 0x7FFFFFFF) != rwBuffer ||
            req.blkCnt * 512 != rwBufferLen) {
            PRINT(IOS_EmuSDIO, ERROR, "Invalid RW buffer supplied");
            return RET_FAIL;
        }

        if (!DeviceMgr::sInstance->DeviceWrite(EmuSDIO::g_emuDevId, rwBuffer,
                                               req.arg, req.blkCnt)) {
            return RET_FAIL;
        }

        return WriteWordOutCmd(out, outLen, 0x900);
    }

    default:
        PRINT(IOS_EmuSDIO, ERROR, "Unknown SDIOCommand: %u", req.cmd);
        return RET_FAIL;
    }
}

static s32 ReqIoctlv(SDIOIoctl cmd, u32 inCount, u32 outCount, IOS::Vector* vec)
{
    if (cmd != SDIOIoctl::SendCmd) {
        PRINT(IOS_EmuSDIO, ERROR, "Unknown ioctlv: %u", cmd);
        return IOSError::Invalid;
    }

    if (inCount != 2 || outCount != 1) {
        PRINT(IOS_EmuSDIO, ERROR, "Improper vector counts");
        return IOSError::Invalid;
    }

    if (vec[0].len < sizeof(SDIORequest) || !aligned(vec[0].data, 4)) {
        PRINT(IOS_EmuSDIO, ERROR, "vec[0] not properly sized or misaligned");
        return IOSError::Invalid;
    }

    // PRINT(IOS_EmuSDIO, INFO, "Ioctlv send command");

    SDIORequest* req = reinterpret_cast<SDIORequest*>(vec[0].data);

    return ExecuteCommand(*req, vec[1].data, vec[1].len, vec[2].data,
                          vec[2].len);
}

static s32 ReqIoctl(SDIOIoctl cmd, const void* in, u32 inLen, void* out,
                    u32 outLen)
{
    switch (cmd) {
    case SDIOIoctl::WriteHCReg: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOIoctl::WriteHCReg");
        return IOSError::OK;
    }

    case SDIOIoctl::ReadHCReg: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOIoctl::ReadHCReg");
        return WriteWordOut(out, outLen, 0);
    }

    case SDIOIoctl::ResetCard: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOIoctl::ResetCard");
        return WriteWordOut(out, outLen, 0x9F620000);
    }

    case SDIOIoctl::SetClk: {
        return IOSError::OK;
    }

    case SDIOIoctl::SendCmd: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOIoctl::SendCmd");

        if (inLen < sizeof(SDIORequest) || !aligned(in, 4)) {
            PRINT(IOS_EmuSDIO, ERROR, "in not properly sized or misaligned");
            return IOSError::Invalid;
        }

        auto req = reinterpret_cast<const SDIORequest*>(in);
        return ExecuteCommand(*req, nullptr, 0, out, outLen);
    }

    case SDIOIoctl::GetStatus: {
        u32 status = 0;
        if (DeviceMgr::sInstance->IsInserted(EmuSDIO::g_emuDevId)) {
            status |= SDIO_STATUS_CARD_INSERTED | SDIO_STATUS_CARD_INITIALIZED |
                      SDIO_STATUS_CARD_SDHC;
        }

        PRINT(IOS_EmuSDIO, INFO, "SDIOIoctl::GetStatus = %08X", status);
        return WriteWordOut(out, outLen, status);
    }

    case SDIOIoctl::GetOCR: {
        PRINT(IOS_EmuSDIO, INFO, "SDIOIoctl::GetOCR");
        return WriteWordOut(out, outLen, 0);
    }

    default:
        PRINT(IOS_EmuSDIO, ERROR, "Unknown ioctl: %u", cmd);
        return IOSError::Invalid;
    }
}

static s32 IPCRequest(IOS::Request* req)
{
    switch (req->cmd) {
    case IOS::Command::Open:
        if (strcmp(req->open.path, "~dev/sdio/slot0") != 0)
            return IOSError::NotFound;

        return IOSError::OK;

    case IOS::Command::Close:
        return IOSError::OK;

    case IOS::Command::Ioctl:
        return ReqIoctl(SDIOIoctl(req->ioctl.cmd), req->ioctl.in,
                        req->ioctl.in_len, req->ioctl.io, req->ioctl.io_len);

    case IOS::Command::Ioctlv:
        return ReqIoctlv(static_cast<SDIOIoctl>(req->ioctlv.cmd),
                         req->ioctlv.in_count, req->ioctlv.io_count,
                         req->ioctlv.vec);

    default:
        PRINT(IOS_EmuSDIO, ERROR, "Invalid cmd: %d",
              static_cast<s32>(req->cmd));
        return static_cast<s32>(IOSError::Invalid);
    }
}

s32 EmuSDIO::ThreadEntry([[maybe_unused]] void* arg)
{
    PRINT(IOS_EmuSDIO, INFO, "Starting SDIO...");
    PRINT(IOS_EmuSDIO, INFO, "EmuSDIO thread ID: %d", IOS_GetThreadId());

    Queue<IOS::Request*> queue(8);
    s32 ret = IOS_RegisterResourceManager("~dev/sdio/slot0", queue.id());
    assert(ret == IOSError::OK);

    while (EmuSDIO::g_emuDevId == -1) {
        usleep(32000);
    }

    PRINT(IOS_EmuSDIO, INFO, "Device inserted, starting emulation...");
    IPCLog::sInstance->Notify(3);
    while (true) {
        IOS::Request* req = queue.receive();
        req->reply(IPCRequest(req));
    }
}
