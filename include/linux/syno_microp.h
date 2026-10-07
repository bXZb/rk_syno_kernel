/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2001-2025 Synology Inc. All rights reserved.
 *
 * Synology microP communication interface - Kernel internal header
 * Common definitions (UART2_BUFFER, etc.) are in <uapi/linux/syno_microp.h>
 * V2-specific kernel functions are declared here.
 */
#ifndef _LINUX_SYNO_MICROP_H
#define _LINUX_SYNO_MICROP_H

#include <linux/types.h>
#include <uapi/linux/syno_microp.h>

/*
 * V2-specific definitions and functions (kernel only)
 */
#ifdef CONFIG_SYNO_MICROP_COMMAND_V2

#define SYNO_MICROP_FIFO_SIZE 1024
#define SYNO_UP_CMD_V2_DEFAUT_TIMEOUT 1000
#define SYNO_UP_CMD_V2_DEFAULT_RETRY 3

/* Log switch - defined and exported by kernel for v2 */
extern int micropLogSwitch;

/**
 * syno_microp_v2_open - Open microP v2 communication channel
 *
 * Initialize the microP v2 communication subsystem.
 * Called automatically on first read/write operation.
 *
 * Return: 0 on success, negative error code on failure
 */
int syno_microp_v2_open(void);

/**
 * syno_microp_v2_close - Close microP v2 communication channel
 *
 * Cleanup and close the microP v2 communication subsystem.
 * Called by serial_core when UART is removed.
 */
void syno_microp_v2_close(void);

/**
 * syno_microp_ttyS_write - Write command to microP
 * @buffer: Buffer containing command to write
 *
 * Send a command to microP. The command is queued and processed
 * asynchronously.
 *
 * Return: 0 on success, negative error code on failure
 */
int syno_microp_ttyS_write(UART2_BUFFER *buffer);

/**
 * syno_microp_write - Simplified wrapper to send command to microP
 * @command: Command string to send
 *
 * This function wraps UART2_BUFFER and calls syno_microp_ttyS_write().
 * It checks if microP v2 is supported before sending.
 * Uses default timeout (SYNO_UP_CMD_V2_DEFAUT_TIMEOUT), retry (SYNO_UP_CMD_V2_DEFAULT_RETRY),
 * and preempt (false).
 *
 * Return: 0 on success, -ENOTSUPP if v2 not supported, negative error code on failure
 */
int syno_microp_write(const char *command);

/**
 * syno_microp_ttyS_read - Read response from microP
 * @buffer: Buffer to store response (szBuf contains command to send)
 *
 * Send a command and wait for response from microP.
 *
 * Return: 0 on success, negative error code on failure
 */
int syno_microp_ttyS_read(UART2_BUFFER *buffer);

/**
 * syno_microp_v2_is_open - Check if microP v2 channel is open
 *
 * Return: 1 if open, 0 if not
 */
int syno_microp_v2_is_open(void);

/* Internal function called by tty_buffer.c */
void syno_microp_v2_wakeup(void);

#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */

#endif /* _LINUX_SYNO_MICROP_H */
