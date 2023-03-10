// Log.hpp - Debug log
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#pragma once
#include <stdarg.h>
#ifdef TARGET_IOS
#include <FAT/ff.h>
#endif

namespace Log
{

enum class LogSource {
    Core,
    DVD,
    Loader,
    Payload,
    FST,
    PatchList,
    IOS,
    IOS_Loader,
    IOS_DevMgr,
    IOS_USB,
    IOS_EmuFS,
    IOS_EmuDI,
    IOS_EmuES,
    IOS_EmuSDIO,
    IOS_EmuHID,
};

enum class LogLevel {
    INFO,
    WARN,
    ERROR
};

enum class IPCLogIoctl {
    RegisterPrintHook,
    StartGameEvent,
    SetTime,
};

enum class IPCLogReply {
    Print,
    Notice,
    Close,
    SetLaunchState,
};

#ifdef TARGET_IOS
extern bool ipcLogEnabled;
#endif

bool IsEnabled();

void VPrint(LogSource src, const char* srcStr, const char* funcStr,
            LogLevel level, const char* format, va_list args);
void Print(LogSource src, const char* srcStr, const char* funcStr,
           LogLevel level, const char* format, ...);

#ifdef NDEBUG

#define PRINT(...)

#else

#define PRINT(CHANNEL, LEVEL, ...)                                             \
    Log::Print(Log::LogSource::CHANNEL, #CHANNEL, __FUNCTION__,                \
               Log::LogLevel::LEVEL, __VA_ARGS__)

#endif

} // namespace Log
