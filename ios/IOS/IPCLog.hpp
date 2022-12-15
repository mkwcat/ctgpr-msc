// IPCLog.hpp - IOS to PowerPC logging through IPC
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#pragma once
#include <System/LaunchError.hpp>
#include <System/OS.hpp>
#include <System/Types.h>

class IPCLog
{
public:
    static IPCLog* sInstance;

    static constexpr int printSize = 256;

    IPCLog();
    void Run();
    void Print(const char* buffer);
    void Notify(u32 id);
    void SetLaunchState(LaunchError state);

    void WaitForStartRequest(void** dolAddr, u32* dolSize);

protected:
    void HandleRequest(IOS::Request* req);

    Queue<IOS::Request*> m_ipcQueue;
    Queue<IOS::Request*> m_responseQueue;
    Queue<int> m_startRequestQueue;
};
