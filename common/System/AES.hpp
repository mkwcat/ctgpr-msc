// AES.hpp - AES engine interface
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#pragma once
#include <System/OS.hpp>
#include <System/Types.h>
#include <System/Util.h>

class AES
{
public:
    static AES* sInstance;

private:
    enum class AESIoctl {
        Encrypt = 2,
        Decrypt = 3,
    };

    static constexpr u32 MaxInputSize = 0x10000;

public:
    /*
     * AES-128 CBC encrypt a block using the AES hardware engine.
     */
    s32 Encrypt(const u8* key, u8* iv, const void* input, u32 size,
                void* output)
    {
        IOS::IOVector<2, 2> vec;
        vec.in[0].data = input;
        vec.in[0].len = size;
        vec.in[1].data = key;
        vec.in[1].len = 16;
        vec.out[0].data = output;
        vec.out[0].len = size;
        vec.out[1].data = iv;
        vec.out[1].len = 16;
        return m_rm.ioctlv(AESIoctl::Encrypt, vec);
    }

    /*
     * AES-128 CBC decrypt a block using the AES hardware engine.
     */
    s32 Decrypt(const u8* key, u8* iv, const void* input, u32 size,
                void* output)
    {
        if (size < MaxInputSize) {
            IOS::IOVector<2, 2> vec;
            vec.in[0].data = input;
            vec.in[0].len = size;
            vec.in[1].data = key;
            vec.in[1].len = 16;
            vec.out[0].data = output;
            vec.out[0].len = size;
            vec.out[1].data = iv;
            vec.out[1].len = 16;
            return m_rm.ioctlv(AESIoctl::Decrypt, vec);
        }

        s32 ret = IOSError::OK;
        while (size > 0 || ret != IOSError::OK) {
            IOS::IOVector<2, 2> vec;

            vec.in[1].data = key;
            vec.in[1].len = 16;
            vec.out[1].data = iv;
            vec.out[1].len = 16;

            if (size > MaxInputSize) {
                vec.in[0].data = input;
                vec.in[0].len = MaxInputSize;
                vec.out[0].data = output;
                vec.out[0].len = MaxInputSize;

                size -= MaxInputSize;
                input += MaxInputSize;
                output += MaxInputSize;
            } else {
                vec.in[0].data = input;
                vec.in[0].len = size;
                vec.out[0].data = output;
                vec.out[0].len = size;

                size = 0;
            }

            ret = m_rm.ioctlv(AESIoctl::Decrypt, vec);
        }

        return ret;
    }

private:
    IOS::ResourceCtrl<AESIoctl> m_rm{"/dev/aes"};
};
