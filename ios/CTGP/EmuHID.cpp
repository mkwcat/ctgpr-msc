// EmuHID.cpp
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#include "EmuHID.hpp"

// This is meant to be a fix for the issue where USB HID breaks after our fake
// IOS reload

#include <Debug/Log.hpp>
#include <Disk/USB.hpp>
#include <IOS/IPCLog.hpp>
#include <System/OS.hpp>
#include <algorithm>
#include <cstring>

static IOS::ResourceCtrl<USB::USBv5Ioctl> s_hidRM("/dev/usb/hid", 11);

static Queue<IOS::Request*> hidQueue(8);

static IOS::Request* s_deviceChangeReq;

static bool s_deviceChangeAvailable;
static USB::DeviceEntry* s_devices = nullptr;
static u32 s_deviceCount = 0;

static IOS::Request s_cbReq;
bool s_waitingForCb = false;

void IPCRequest(IOS::Request* req)
{
    switch (req->cmd) {
    case IOS::Command::Open:
    case IOS::Command::Close:
        req->reply(0);
        break;

    case IOS::Command::Ioctl:
        switch (USB::USBv5Ioctl(req->ioctl.cmd)) {
        case USB::USBv5Ioctl::GetDeviceChange: {
            PRINT(IOS_EmuHID, INFO, "Get device change!");

            if (s_deviceChangeAvailable) {
                // Just return immediately
                PRINT(IOS_EmuHID, INFO, "Get first device change! Reply = %d",
                      s_deviceCount);
                s_deviceChangeAvailable = false;
                memcpy(req->ioctl.io, s_devices,
                       std::min(req->ioctl.io_len,
                                sizeof(USB::DeviceEntry) * USB::MaxDevices));
                req->reply(s_deviceCount);
                s_deviceChangeReq = nullptr;
                break;
            }

            // Else enqueue
            PRINT(IOS_EmuHID, INFO, "Enqueuing dev change!");
            s_deviceChangeReq = req;
            break;
        }

        case USB::USBv5Ioctl::Shutdown: {
            // The best command
            PRINT(IOS_EmuHID, INFO, "Shutdown");
            if (s_deviceChangeReq != nullptr) {
                s_deviceChangeReq->reply(0);
                s_deviceChangeReq = nullptr;
            }
            req->reply(0);
            break;
        }

        case USB::USBv5Ioctl::AttachFinish: {
            // Once the caller calls AttachFinish we can start doing this again
            PRINT(IOS_EmuHID, INFO, "Attach Finish");
            // How does this fix anything..?
            // OK this literally doesn't make any sense why this fixes it but it
            // does? Why???
            usleep(30000);
            // I guess sleep can be like that sometimes
            req->reply(0);
            break;
        }

        default: {
            req->reply(s_hidRM.ioctl(USB::USBv5Ioctl(req->ioctl.cmd),
                                     req->ioctl.in, req->ioctl.in_len,
                                     req->ioctl.io, req->ioctl.io_len));
            break;
        }
        }
        break;

    case IOS::Command::Ioctlv: {
        req->reply(s_hidRM.ioctlv(USB::USBv5Ioctl(req->ioctlv.cmd),
                                  req->ioctlv.in_count, req->ioctlv.io_count,
                                  req->ioctlv.vec));
        break;
    }

    case IOS::Command::Reply: {
        if (req == &s_cbReq) {
            PRINT(IOS_EmuHID, INFO, "Got device change reply! %d", req->result);

            if (req->result >= 0) {
                s_deviceCount = req->result;
                u8* input = (u8*)IOS::Alloc(32);
                for (u32 i = 0; i < s_deviceCount; i++) {
                    memset(input, 0, 32);
                    write32(input, s_devices[i].devId);
                    // Ignore the result, it will return IOSError::Invalid if
                    // it's not attached, but that doesn't matter
                    s_hidRM.ioctl(USB::USBv5Ioctl::Attach, input, 32, nullptr,
                                  0);
                }
                IOS::Free(input);

                s_hidRM.ioctl(USB::USBv5Ioctl::AttachFinish, nullptr, 0,
                              nullptr, 0);
                s_hidRM.ioctlAsync(USB::USBv5Ioctl::GetDeviceChange, nullptr, 0,
                                   s_devices,
                                   sizeof(USB::DeviceEntry) * USB::MaxDevices,
                                   &hidQueue, &s_cbReq);
            } else {
                s_deviceCount = 0;
            }

            if (s_deviceChangeReq != nullptr) {
                PRINT(IOS_EmuHID, INFO, "Replying to enqueued device change");
                memcpy(s_deviceChangeReq->ioctl.io, s_devices,
                       std::min(s_deviceChangeReq->ioctl.io_len,
                                sizeof(USB::DeviceEntry) * USB::MaxDevices));
                s_deviceChangeReq->reply(req->result);
                s_deviceChangeAvailable = false;
                s_deviceChangeReq = nullptr;
                break;
            }

            PRINT(IOS_EmuHID, INFO, "No enqueued device change");
            s_deviceChangeAvailable = true;
            break;
        }

        PRINT(IOS_EmuHID, INFO, "Got weird unknown reply huh");
        break;
    }

    default:
        PRINT(IOS_EmuHID, ERROR, "Unknown command we just got! %d", req->cmd);
        req->reply(-4);
        break;
    }
}

// Automatically release everything on reload
void EmuHID::Reload()
{
    PRINT(IOS_EmuHID, INFO, "Doing reload!");

    if (s_deviceChangeReq != nullptr) {
        s_deviceChangeReq->reply(0);
        s_deviceChangeReq = nullptr;
    }

    u8* input = (u8*)IOS::Alloc(32);

    // Do suspend because CTGP really cares if the resume doesnt work
    for (u32 i = 0; i < s_deviceCount; i++) {
        memset(input, 0, 32);
        write32(input, s_devices[i].devId);
        write8(input + 0xB, 0);
        s_hidRM.ioctl(USB::USBv5Ioctl::SuspendResume, input, 32, nullptr, 0);
    }

    IOS::Free(input);

    // Force the device change to be available
    s_deviceChangeAvailable = true;
}

s32 EmuHID::ThreadEntry([[maybe_unused]] void* arg)
{
    PRINT(IOS_EmuHID, INFO, "Starting HID...");
    PRINT(IOS_EmuHID, INFO, "EmuHID thread ID: %d", IOS_GetThreadId());

    s32 ret = IOS_RegisterResourceManager("~dev/usb/hid", hidQueue.id());
    assert(ret == IOSError::OK);

    s_devices = (USB::DeviceEntry*)IOS::Alloc(sizeof(USB::DeviceEntry) *
                                              USB::MaxDevices);

    // Let's get the initial device change
    ret = s_hidRM.ioctlAsync(
        USB::USBv5Ioctl::GetDeviceChange, nullptr, 0, s_devices,
        sizeof(USB::DeviceEntry) * USB::MaxDevices, &hidQueue, &s_cbReq);

    IPCLog::sInstance->Notify(1);
    while (true) {
        IOS::Request* req = hidQueue.receive();
        IPCRequest(req);
    }
}
