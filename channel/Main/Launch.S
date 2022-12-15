    .text
    .global LaunchTrampoline
LaunchTrampoline:
    mtctr 3

    li 0, 0
    lis 1, 0x8088
    ori 1, 1, 0x2390
    lis 2, 0x807C
    ori 2, 2, 0x9ECC
    li 3, 0
    li 4, 0
    li 5, 0x3988
    li 6, 0
    lis 7, 0x8098
    ori 7, 7, 0x747C
    lis 8, 0x8098
    ori 8, 8, 0x73A4
    li 9, -4
    li 10, 0
    li 11, 0
    lis 12, 0xCD00
    ori 12, 12, 0x6438
    lis 13, 0x807C
    ori 13, 13, 0xC100
    li 14, 0
    li 15, 0
    li 16, 0
    li 17, 0
    li 18, 0
    li 19, 0
    li 20, 0
    li 21, 0
    li 22, 0
    li 23, 0
    li 24, 2
    li 25, 4
    lis 26, 0x807C
    ori 26, 26, 0x2CC4
    li 27, 3
    li 28, 0xB4
    lis 29, 0x8098
    ori 29, 29, 0x73A8
    lis 30, 0x80AF
    ori 30, 30, 0x6488
    lis 31, 0x8130

    bctrl

    b 0
