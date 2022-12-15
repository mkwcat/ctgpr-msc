// LaunchError.hpp
//   Written by Palapeli
//
// SPDX-License-Identifier: MIT

#pragma once

enum class LaunchError {
    OK, // Display nothing
    NoSDCard, // Please insert an SD card
    NoCTGPR, // The inserted SD card does not contain CTGP-R
    CTGPCorrupt, // Could not load CTGP-R, the pack may be corrupted
    SDCardErr, // The inserted SD card could not be read
};
