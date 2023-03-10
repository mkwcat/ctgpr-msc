// IPCLog.cpp - IOS to PowerPC logging through IPC
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#include "IPCLog.hpp"
#include <Debug/Log.hpp>
#include <IOS/System.hpp>
#include <cstring>

IPCLog* IPCLog::sInstance;

IPCLog::IPCLog() : m_ipcQueue(8), m_responseQueue(1), m_startRequestQueue(1)
{
    s32 ret = IOS_RegisterResourceManager("/dev/saoirse", m_ipcQueue.id());
    if (ret < 0)
        AbortColor(YUV_WHITE);
}

void IPCLog::Print(const char* buffer)
{
    IOS::Request* req = m_responseQueue.receive();
    memcpy(req->ioctl.io, buffer, printSize);
    req->reply(0);
}

void IPCLog::Notify(u32 id)
{
    IOS::Request* req = m_responseQueue.receive();
    u32 id32 = u32(id);
    memcpy(req->ioctl.io, &id32, sizeof(u32));
    req->reply(1);
}

void IPCLog::SetLaunchState(LaunchError state)
{
    IOS::Request* req = m_responseQueue.receive();
    u32 state32 = u32(state);
    memcpy(req->ioctl.io, &state32, sizeof(u32));
    req->reply(3);
}

static void* s_dolAddr = nullptr;
static u32 s_dolSize = 0;

void IPCLog::HandleRequest(IOS::Request* req)
{
    switch (req->cmd) {
    case IOS::Command::Open:
        if (strcmp(req->open.path, "/dev/saoirse") != 0) {
            req->reply(IOSError::NotFound);
            break;
        }

        if (!Log::ipcLogEnabled) {
            req->reply(IOSError::NotFound);
            break;
        }

        req->reply(IOSError::OK);
        break;

    case IOS::Command::Close:
        Log::ipcLogEnabled = false;
        // Wait for any ongoing requests to finish. TODO: This could be done
        // better with a mutex maybe?
        usleep(10000);
        m_responseQueue.receive()->reply(2);
        req->reply(IOSError::OK);
        break;

    case IOS::Command::Ioctl:
        switch (static_cast<Log::IPCLogIoctl>(req->ioctl.cmd)) {
        case Log::IPCLogIoctl::RegisterPrintHook:
            // Read from console
            if (req->ioctl.io_len != printSize || !aligned(req->ioctl.in, 32)) {
                req->reply(IOSError::Invalid);
                break;
            }

            // Will reply on next print
            m_responseQueue.send(req);
            break;

        case Log::IPCLogIoctl::StartGameEvent:
            // Start game IOS command
            s_dolAddr = req->ioctl.in;
            s_dolSize = req->ioctl.in_len;

            m_startRequestQueue.send(0);
            req->reply(IOSError::OK);
            break;

        case Log::IPCLogIoctl::SetTime:
            if (req->ioctl.in_len != sizeof(u32) + sizeof(u64) ||
                !aligned(req->ioctl.in, 4)) {
                req->reply(IOSError::Invalid);
                break;
            }

            System::SetTime(*reinterpret_cast<u32*>(req->ioctl.in),
                            *reinterpret_cast<u64*>(req->ioctl.in + 4));
            req->reply(IOSError::OK);
            break;

        default:
            req->reply(IOSError::Invalid);
            break;
        }
        break;

    default:
        req->reply(IOSError::Invalid);
        break;
    }
}

void IPCLog::Run()
{
    while (true) {
        IOS::Request* req = m_ipcQueue.receive();
        HandleRequest(req);
    }
}

void IPCLog::WaitForStartRequest(void** dolAddr, u32* dolSize)
{
    m_startRequestQueue.receive();

    *dolAddr = s_dolAddr;
    *dolSize = s_dolSize;
}
