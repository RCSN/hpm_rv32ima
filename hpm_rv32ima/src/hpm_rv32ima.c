/*
 * Copyright (c) 2023, Jisheng Zhang <jszhang@kernel.org>. All rights reserved.
 *
 * Use some code of mini-rv32ima.c from https://github.com/cnlohr/mini-rv32ima
 * Copyright 2022 Charles Lohr
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "board.h"
#include "diskio.h"
#include "ff.h"
#include "hpm_l1c_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_uart_drv.h"

#define IMAGE_FILE_NAME "Image"
#define DTB_FILE_NAME "uc.dtb"

FATFS s_sd_disk;
FIL s_file;
DIR s_dir;
FRESULT fatfs_result;
BYTE work[FF_MAX_SS];
static uint8_t __attribute__((section(".framebuffer"), aligned(4)))
ram_image[10 * 1024 * 1024];
const TCHAR driver_num_buf[3] = {DEV_SD + '0', ':', '/'};

static uint64_t GetTimeMicroseconds();
static uint32_t HandleException(uint32_t ir, uint32_t retval);
static uint32_t HandleControlStore(uint32_t addy, uint32_t val);
static uint32_t HandleControlLoad(uint32_t addy);
static void HandleOtherCSRWrite(uint8_t *image, uint16_t csrno, uint32_t value);
static int32_t HandleOtherCSRRead(uint8_t *image, uint16_t csrno);
static void MiniSleep();
static int IsKBHit();
static int ReadKBByte();

// This is the functionality we want to override in the emulator.
//  think of this as the way the emulator's processor is connected to the
//  outside world.
#define MINIRV32WARN(x...) printf(x);
#define MINI_RV32_RAM_SIZE sizeof(ram_image)
#define MINIRV32_IMPLEMENTATION
#define MINIRV32_POSTEXEC(pc, ir, retval)                                      \
    {                                                                          \
        if (retval > 0) {                                                      \
            retval = HandleException(ir, retval);                              \
        }                                                                      \
    }
#define MINIRV32_HANDLE_MEM_STORE_CONTROL(addy, val)                           \
    if (HandleControlStore(addy, val))                                         \
        return val;
#define MINIRV32_HANDLE_MEM_LOAD_CONTROL(addy, rval)                           \
    rval = HandleControlLoad(addy);
#define MINIRV32_OTHERCSR_WRITE(csrno, value)                                  \
    HandleOtherCSRWrite(image, csrno, value);
#define MINIRV32_OTHERCSR_READ(csrno, value)                                   \
    value = HandleOtherCSRRead(image, csrno);

#include "mini-rv32ima.h"

static void DumpState(struct MiniRV32IMAState *core)
{
    unsigned int pc = core->pc;
    unsigned int *regs = (unsigned int *)core->regs;
    uint64_t thit, taccessed;

    printf("PC: %08x ", pc);
    printf("Z:%08x ra:%08x sp:%08x gp:%08x tp:%08x t0:%08x t1:%08x t2:%08x "
           "s0:%08x s1:%08x a0:%08x a1:%08x a2:%08x a3:%08x a4:%08x a5:%08x ",
           regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6],
           regs[7], regs[8], regs[9], regs[10], regs[11], regs[12], regs[13],
           regs[14], regs[15]);
    printf(
        "a6:%08x a7:%08x s2:%08x s3:%08x s4:%08x s5:%08x s6:%08x s7:%08x "
        "s8:%08x s9:%08x s10:%08x s11:%08x t3:%08x t4:%08x t5:%08x t6:%08x\n",
        regs[16], regs[17], regs[18], regs[19], regs[20], regs[21], regs[22],
        regs[23], regs[24], regs[25], regs[26], regs[27], regs[28], regs[29],
        regs[30], regs[31]);
}

static struct MiniRV32IMAState core;

static char dmabuf[64];
int main(void)
{
    FRESULT fresult;
    uint64_t flen;
    uint32_t fnum = 0;
    // l1c_dc_disable();
    board_init();
    clock_set_source_divider(clock_mchtmr0, clk_src_osc24m, 24);
restart:
    fresult = f_mount(&s_sd_disk, driver_num_buf, 1);
    if (fresult == FR_OK) {
        printf("SD card has been mounted successfully\n");
    } else {
        printf("Failed to mount SD card, cause\n");
        board_delay_ms(1000);
        goto restart;
    }
    fresult = f_chdrive(driver_num_buf);
    if (fatfs_result == FR_NO_FILESYSTEM) {
        printf("There is no File system available, please format the sdcard to "
               "FAT32\n");
        while (1) {
        }
    }
    printf("loading kernel Image \n");
    fresult = f_open(&s_file, IMAGE_FILE_NAME, FA_READ);
    if (fresult != FR_OK) {
        printf("Open file failed");
        return 0;
    } else {
        printf("Open file successfully\n");
    }
    flen = f_size(&s_file);
    if (flen > sizeof(ram_image)) {
        printf("Error: Could not fit RAM image (%ld bytes) into %d \n", flen,
               sizeof(ram_image));
        return 0;
    }
    memset(ram_image, 0, sizeof(ram_image));
    if (f_read(&s_file, ram_image, flen, &fnum) != FR_OK) {
        printf("Error: Could not load image\n");
        return 0;
    }
    f_close(&s_file);

    board_delay_ms(10);
    printf("loading dtb Image \n");
    fresult = f_open(&s_file, DTB_FILE_NAME, FA_READ);
    if (fresult != FR_OK) {
        printf("Open file failed %s\n", DTB_FILE_NAME);
        return 0;
    } else {
        printf("Open file successfully\n");
    }
    flen = f_size(&s_file);
    if (f_read(&s_file, ram_image + (sizeof(ram_image) - flen), flen, &fnum) !=
        FR_OK) {
        printf("Error: Could not load image\n");
        return 0;
    }
    f_close(&s_file);

    uint32_t *dtb = (uint32_t *)(ram_image + (sizeof(ram_image) - flen));
    if (dtb[0x13c / 4] == 0x00c0ff03) {
        uint32_t validram = (sizeof(ram_image) - flen);
        dtb[0x13c / 4] = (validram >> 24) | (((validram >> 16) & 0xff) << 8) |
                         (((validram >> 8) & 0xff) << 16) |
                         ((validram & 0xff) << 24);
    }

    core.pc = MINIRV32_RAM_IMAGE_OFFSET;
    core.regs[10] = 0x00; // hart ID
                          // dtb_pa must be valid pointer
    core.regs[11] = (uint32_t)((sizeof(ram_image) - flen)) + MINIRV32_RAM_IMAGE_OFFSET;
    core.extraflags |= 3; // Machine-mode.

    // Image is loaded.
    uint64_t lastTime = GetTimeMicroseconds();
    int instrs_per_flip = 1024;
    printf("RV32IMA starting\n");
    while (1) {
        int ret;
        uint64_t *this_ccount = ((uint64_t *)&core.cyclel);
        uint32_t elapsedUs = GetTimeMicroseconds() / 6 - lastTime;

        lastTime += elapsedUs;
        // Execute upto 1024 cycles before breaking out.
        ret = MiniRV32IMAStep(&core, ram_image, 0, elapsedUs, instrs_per_flip);
        switch (ret) {
        case 0:
            break;
        case 1:
            MiniSleep();
            *this_ccount += instrs_per_flip;
            break;
        case 3:
            break;

        // syscon code for restart
        case 0x7777:
            goto restart;

        // syscon code for power-off
        case 0x5555:
            printf("POWEROFF@0x%d %d\n", core.cycleh, core.cyclel);
            DumpState(&core);
            return 0;
        default:
            printf("Unknown failure\n");
            break;
        }
    }

    DumpState(&core);
    return 0;
}

//////////////////////////////////////////////////////////////////////////
// Platform-specific functionality
//////////////////////////////////////////////////////////////////////////
static void MiniSleep(void)
{
    board_delay_ms(1);
}

static uint64_t GetTimeMicroseconds()
{
    return mchtmr_get_count(HPM_MCHTMR);
}

static int ReadKBByte(void)
{
    static int is_escape_sequence = 0;
    int r;
    if (is_escape_sequence == 1) {
        is_escape_sequence++;
        return '[';
    }

    r = getchar();

    if (is_escape_sequence) {
        is_escape_sequence = 0;
        switch (r) {
        case 'H':
            return 'A'; // Up
        case 'P':
            return 'B'; // Down
        case 'K':
            return 'D'; // Left
        case 'M':
            return 'C'; // Right
        case 'G':
            return 'H'; // Home
        case 'O':
            return 'F'; // End
        default:
            return r; // Unknown code.
        }
    } else {
        switch (r) {
        case 13:
            return 10; // cr->lf
        case 224:
            is_escape_sequence = 1;
            return 27; // Escape arrow keys
        default:
            return r;
        }
    }
}

static int IsKBHit(void)
{
    return 1;
}

static uint32_t HandleException(uint32_t ir, uint32_t code)
{
    // Weird opcode emitted by duktape on exit.
    if (code == 3) {
        // Could handle other opcodes here.
    }
    return code;
}

static uint32_t HandleControlStore(uint32_t addy, uint32_t val)
{
    if (addy == 0x10000000) { // UART 8250 / 16550 Data Buffer
        printf("%c", val);
        fflush(stdout);
    }
    return 0;
}

static uint32_t HandleControlLoad(uint32_t addy)
{
    // Emulating a 8250 / 16550 UART
    if (addy == 0x10000005) {
        return 0x60 | IsKBHit();
    } else if (addy == 0x10000000 && IsKBHit()) {
        return ReadKBByte();
    }
    return 0;
}

static void HandleOtherCSRWrite(uint8_t *image, uint16_t csrno, uint32_t value)
{
    if (csrno == 0x136) {
        printf("%d", value);
        fflush(stdout);
    }
    if (csrno == 0x137) {
        printf("%08x", value);
        fflush(stdout);
    } else if (csrno == 0x138) {
        // Print "string"
        uint32_t ptrstart = value - MINIRV32_RAM_IMAGE_OFFSET;
        uint32_t ptrend = ptrstart;
        if (ptrstart >= MINI_RV32_RAM_SIZE) {
            printf("DEBUG PASSED INVALID PTR (%08x)\n", value);
        }
        while (ptrend < MINI_RV32_RAM_SIZE) {
            if (image[ptrend] == 0) {
                break;
            }
            ptrend++;
        }
        if (ptrend != ptrstart) {
            fwrite(image + ptrstart, ptrend - ptrstart, 1, stdout);
        }
    } else if (csrno == 0x139) {
        putchar(value);
        fflush(stdout);
    }
}

static int32_t HandleOtherCSRRead(uint8_t *image, uint16_t csrno)
{
    if (csrno == 0x140) {
        if (BOARD_CONSOLE_BASE->LSR & UART_LSR_DR_MASK) {
            return BOARD_CONSOLE_BASE->RBR & UART_RBR_RBR_MASK;
        } else {
            return -1;
        }
    }
    return 0;
}
