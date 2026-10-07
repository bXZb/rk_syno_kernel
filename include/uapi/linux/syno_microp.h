/*
 * Copyright (c) 2001-2025 Synology Inc. All rights reserved.
 *
 * Synology microP communication interface - User/Kernel shared definitions
 */
#ifndef _UAPI_LINUX_SYNO_MICROP_H
#define _UAPI_LINUX_SYNO_MICROP_H

/*
 * Common definitions for both v1 and v2 microP communication
 * Shared between userspace and kernel
 */
#define SYNO_MICROP_BUF_SIZE 128

/**
 * UART2_BUFFER - Buffer structure for microP communication
 * @szBuf: Command/response buffer
 * @size: Size of the buffer
 * @timeout: Timeout in milliseconds
 * @retry: Number of retries
 * @preempt: Whether to preempt other commands in queue
 *
 * Used by both v1 and v2 microP communication
 */
typedef struct _UART2_BUFFER {
	char szBuf[SYNO_MICROP_BUF_SIZE];
	int size;
	int timeout;
	int retry;
	int preempt;
} UART2_BUFFER;

#endif /* _UAPI_LINUX_SYNO_MICROP_H */
