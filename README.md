# CTGP-R MSC

CTGP-R MSC (Mass Storage Class) is a custom launcher for CTGP-R that adds support for loading CTGP-R's files off a USB storage device. It works by patching IOS to catch and redirect commands directed to the SD Card. This makes it possible to use CTGP-R on a Wii Mini, or a Wii/Wii U with a broken SD Card slot.

Note that this does NOT mean that you can use an ISO, you still need a Mario Kart Wii game disc. This project is unrelated to and not compatible with USB loaders.

This project is based on [Saoirse](https://github.com/TheLordScruffy/saoirse).

## Installing

Make sure your USB device is formatted to FAT32, then follow the [guide on the ChadSoft website](https://chadsoft.co.uk/install-guide/) for regular CTGP-R, using a USB device in place of an SD Card. Download the latest release of CTGP-R MSC from here and then launch it using the Homebrew Channel. It can be installed to the Wii Menu as well just by using the regular CTGP-R channel installer. If you have already installed the regular CTGP-R channel, simply remove it and then add it again when launching through CTGP-R MSC.

## License

All code in this repository is licensed under the MIT license unless stated otherwise in an individual file header. The full text of the license can be found in the 'LICENSE' file.
