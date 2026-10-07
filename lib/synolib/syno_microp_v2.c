#ifdef CONFIG_SYNO_MICROP_COMMAND_V2

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/syno_microp.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#define SYNO_TTY_MAX_RETRY 5
#define SYNO_TTY_TIMEOUT_MS 20000

#define QUEUE_SIZE 64

#define SYNO_UP_START_SYMBOL  0x5B
#define SYNO_UP_ESCAPE_SYMBOL 0x5C
#define SYNO_UP_END_SYMBOL    0x5D

#define SYNO_UP_CMD_MINIMAL_LEN (1+1+1+2+1)  // start + sn + flag + 2 bytes crc + end

#define SYNO_UP_CMD_FLAG_SEND 'S'
#define SYNO_UP_CMD_FLAG_ACK  'A'
#define SYNO_UP_CMD_FLAG_ERR  'E'

/* TTY path */
#define SYNO_TTYS_PATH        "/dev/ttyS"
#define SYNO_UART_TTYS_INDEX  "1"
#define SYNO_UART_TTYS_PATH   SYNO_TTYS_PATH SYNO_UART_TTYS_INDEX

struct command {
	u8 *data;
	u8 *payload;
	int payload_size;
	bool preempt;
	int timeout;
	int retry;
	struct completion comp;
	struct kref refcount;
};

static DECLARE_WAIT_QUEUE_HEAD(queue_wq);

static struct command *command_queue[QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static struct mutex queue_lock;
static struct task_struct *queue_thread;

struct completion micropReceived;

static u8 g_rx_buf[SYNO_MICROP_FIFO_SIZE];
static int g_rx_len = 0;
static int g_rx_head = 0;

/* Reference counting for open/close */
static DEFINE_MUTEX(open_lock);
static int open_count = 0;
static struct file *tty_filp = NULL;

int micropLogSwitch = 0;
EXPORT_SYMBOL(micropLogSwitch);

extern int gSynoMicropSeries;

noinline static void syno_cmd_release(struct kref *ref)
{
	struct command *cmd = NULL;

	if (NULL == ref) {
		printk("%s: parameter error\n", __func__);
		return;
	}

	cmd = container_of(ref, struct command, refcount);

	if (cmd->data) {
		kfree(cmd->data);
	}
	if (cmd->payload) {
		kfree(cmd->payload);
	}
	kfree(cmd);
}

static u16 crc16_xmodem(const u8 *data, int length) {
	u16 crc = 0x0000;
	int i = 0;
	int j = 0;

	for (i = 0; i < length; i++) {
		crc ^= (data[i] << 8);
		for (j = 0; j < 8; j++) {
			if (crc & 0x8000) {
				crc = (crc << 1) ^ 0x1021;
			} else {
				crc = crc << 1;
			}
		}
	}
	return crc;
}

/*
 * uP Command format:
 * --------------------------------------------------------
 * | 1 byte | 1 byte | 1 byte | n byte  | 2 byte | 1 byte |
 * --------------------------------------------------------
 * | start  |   sn   |  flag  | payload |  crc   |  end   |
 * --------------------------------------------------------
 */

/*
 * Assemble the command to uP format
 * @param sn: [IN] the sequence number of the command
 * @param flag: [IN] the flag of the command
 * @param cmd_in: [IN] the command to assemble
 * @param cmd_out: [OUT] the buffer to store the assembled command
 * @param cmd_out_len: [IN] the length of the buffer
 *
 * @return -1: failed
 *        else: the length of the assembled command
 */

static int syno_up_cmd_assemble(u8 sn, u8 flag, const u8 *cmd_in, u8 *cmd_out, int cmd_out_len)
{
	int i = 0;
	int idx = 0;
	int escape_count = 0;
	int ret = -EINVAL;
	u16 checksum = 0;
	u8 tmp_cmd[SYNO_MICROP_BUF_SIZE] = {'\0'};
	int tmp_cmd_len = 0;

	if (NULL == cmd_in || NULL == cmd_out || 0 >= cmd_out_len) {
		printk("%s: parameter error\n", __func__);
		goto END;
	}

	tmp_cmd[0] = sn;
	tmp_cmd[1] = flag;
	//check cmd_in length
	if (0 < strlen(cmd_in) && strlen(cmd_in) + 2 < sizeof(tmp_cmd)) {
		memcpy(&tmp_cmd[2], cmd_in, strlen(cmd_in));
	}
	checksum = crc16_xmodem(tmp_cmd, strlen(tmp_cmd));
	tmp_cmd_len = strlen(tmp_cmd);

	tmp_cmd[tmp_cmd_len++] = (checksum >> 8) & 0xFF;
	tmp_cmd[tmp_cmd_len++] = checksum & 0xFF;

	for (i = 0; i < tmp_cmd_len; i++) {
		if (SYNO_UP_START_SYMBOL == tmp_cmd[i] || SYNO_UP_ESCAPE_SYMBOL == tmp_cmd[i] || SYNO_UP_END_SYMBOL == tmp_cmd[i]) {
			escape_count++;
		}
	}

	// add 2 for start and end symbol
	if (escape_count + tmp_cmd_len + 2 >= cmd_out_len) {
		printk("%s: buffer overflow\n", __func__);
		goto END;
	}

	idx = 0;
	cmd_out[idx++] = SYNO_UP_START_SYMBOL;

	for (i = 0; i < tmp_cmd_len; i++) {
		if (SYNO_UP_START_SYMBOL == tmp_cmd[i] || SYNO_UP_ESCAPE_SYMBOL == tmp_cmd[i] || SYNO_UP_END_SYMBOL == tmp_cmd[i]) {
			cmd_out[idx++] = SYNO_UP_ESCAPE_SYMBOL;
		}
		cmd_out[idx++] = tmp_cmd[i];
	}
	cmd_out[idx++] = SYNO_UP_END_SYMBOL;
	cmd_out[idx] = '\0';

	ret = idx;

END:
	return ret;
}

/*
 * Set TTY termios for microP communication
 */
static int syno_ttyS_set_termios(struct file *filp)
{
	int ret = -1;
	struct ktermios new_termios;
	struct tty_struct *tty = NULL;
	struct ktermios old_termios;

	if (NULL == filp) {
		printk("%s: parameter error\n", __func__);
		goto END;
	}

	tty = ((struct tty_file_private *)filp->private_data)->tty;
	memcpy(&new_termios, &tty->termios, sizeof(struct ktermios));

	new_termios.c_iflag = 0;
	new_termios.c_oflag = 0;
	new_termios.c_lflag = 0;
	new_termios.c_cflag = CS8 | CLOCAL | CREAD;
	new_termios.c_cflag |= B9600;

	down_write(&tty->termios_rwsem);
	old_termios = tty->termios;
	tty->termios = new_termios;

	if (tty && tty->ops && tty->ops->set_termios) {
		tty->ops->set_termios(tty, &old_termios);
	}

	if (tty->ldisc && tty->ldisc->ops && tty->ldisc->ops->set_termios) {
		tty->ldisc->ops->set_termios(tty, &old_termios);
	}

	up_write(&tty->termios_rwsem);
	ret = 0;

END:
	return ret;
}

/*
 * Write data to ttyS
 * @param szCmd: [IN] the command to write
 * @param cmd_len: [IN] the length of the command
 * @return -1: failed
 *	 else: the length of the data written
 */
static int syno_ttyS_write(const u8 *szCmd, int cmd_len)
{
	int ret = -1;
	int len = -1;

	if (!tty_filp) {
		printk("Need open %s before write\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	if (!szCmd) {
		printk("Can't write empty command to %s\n", SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	if (micropLogSwitch) {
		print_hex_dump(KERN_NOTICE, "syno_ttyS_write hex: ", DUMP_PREFIX_NONE,
			       16, 1, szCmd, cmd_len, false);
	}

	/* If all platform kernel version >= 3.10, can use kernel_write instead of vfs_write */
	len = kernel_write(tty_filp, szCmd, cmd_len, &tty_filp->f_pos);
	if (len < 0) {
		printk("Write %s to %s failed\n", szCmd, SYNO_UART_TTYS_PATH);
		goto ERR;
	}

	ret = len;

ERR:
	return ret;
}

/*
 * Verify the package format and checksum
 *
 * @param cmd: [IN] the command to verify
 * @param cmd_len: [IN] the length of the command
 * @param sn: [IN] the sequence number of the command
 *
 * @return 0: success
 *        -1: failed
 */
static int syno_package_verify(const u8 *cmd, int cmd_len, int sn)
{
	u8 start_byte = 0;
	u8 sn_byte = 0;
	u8 end_byte = 0;
	u16 received_crc = 0;
	u16 calculated_crc = 0;
	int ret = -1;
	int payload_len = 0;

	if (NULL == cmd) {
		goto END;
	}

	if (SYNO_UP_CMD_MINIMAL_LEN > cmd_len) {
		printk("%s cmd_len is too small\n", __func__);
		goto END;
	}

	start_byte = cmd[0];
	sn_byte = cmd[1];
	end_byte = cmd[cmd_len - 1];
	received_crc = ((u16)cmd[cmd_len - 3] << 8) | (u16)cmd[cmd_len - 2];

	if (SYNO_UP_START_SYMBOL != start_byte || SYNO_UP_END_SYMBOL != end_byte) {
		printk("%s start_byte or end_byte is wrong\n", __func__);
		goto END;
	}

	payload_len = cmd_len - SYNO_UP_CMD_MINIMAL_LEN;
	calculated_crc = crc16_xmodem(&cmd[1], payload_len + 1 + 1); // sn + flag + Payload

	if (calculated_crc != received_crc) {
		printk("%s crc is wrong calculated_crc=%04x received_crc=%04x\n", __func__, calculated_crc, received_crc);
		goto END;
	}

	if (sn_byte != sn) {
		printk("%s sn not match got sn=%02x, wanted sn=%02x\n", __func__, sn_byte & 0xFF, sn & 0xFF);
		goto END;
	}

	ret = 0;

END:
	return ret;
}

/*
 * Process the packet from ttyS
 *
 * @param head: [IN] the start index in g_rx_buf
 * @param len: [IN] the length of valid data
 * @param cmd_out: [OUT] the buffer to store the command
 * @param cmd_out_len: [IN] the length of the buffer
 * @param sn: [IN] the sequence number of the command
 * @param consumed: [OUT] the length of data consumed
 *
 * @return -1: failed
 *        else: the length of the command
 */
static int syno_process_packet_from_buffer(int head, int len, u8 *cmd_out, int cmd_out_len, int sn, int *consumed)
{
	int i = 0;
	int ret = 0;
	int start_idx = -1;
	int end_idx = -1;
	u8 packet[SYNO_MICROP_BUF_SIZE] = {'\0'};
	int packet_idx = 0;
	bool escape = false;
	int buf_size = sizeof(g_rx_buf);
	int curr_idx = 0;
	u8 byte = 0;

	if (NULL == cmd_out || 0 >= cmd_out_len || NULL == consumed) {
		printk("%s: parameter error\n", __func__);
		ret = -EINVAL;
		goto END;
	}

	*consumed = 0;

	// Find START
	for (i = 0; i < len; i++) {
		curr_idx = (head + i) % buf_size;
		if (SYNO_UP_START_SYMBOL == g_rx_buf[curr_idx]) {
			start_idx = i;
			break;
		}
	}

	if (-1 == start_idx) {
		// No start symbol found, discard all
		*consumed = len;
		goto END;
	}

	// Discard garbage before START
	if (0 < start_idx) {
		*consumed = start_idx;
		goto END;
	}

	// Now g_rx_buf[head] is START.
	// Parse packet with unescaping
	curr_idx = head;
	packet[packet_idx++] = g_rx_buf[curr_idx]; // START

	for (i = 1; i < len; i++) {
		curr_idx = (head + i) % buf_size;
		byte = g_rx_buf[curr_idx];

		if (escape) {
			packet[packet_idx++] = byte;
			escape = false;
		} else {
			if (SYNO_UP_ESCAPE_SYMBOL == byte) {
				escape = true;
			} else if (SYNO_UP_START_SYMBOL == byte) {
				// Unexpected START inside packet?
				// Previous packet was incomplete/corrupt.
				// Restart from this new START.
				*consumed = i; // Consume up to this new start
				goto END;
			} else if (SYNO_UP_END_SYMBOL == byte) {
				packet[packet_idx++] = byte;
				end_idx = i;
				break;
			} else {
				packet[packet_idx++] = byte;
			}
		}

		if (packet_idx >= SYNO_MICROP_BUF_SIZE) {
			// Packet too large, invalid.
			// Consume this start byte and retry search
			*consumed = 1;
			goto END;
		}
	}

	if (-1 != end_idx) {
		// Found END
		*consumed = end_idx + 1;

		// Verify
		if (0 == syno_package_verify(packet, packet_idx, sn)) {
			// Success
			if (packet_idx <= cmd_out_len) {
				memcpy(cmd_out, packet, packet_idx);
				ret = packet_idx;
				if (micropLogSwitch) {
					print_hex_dump(KERN_NOTICE, "syno_process_packet hex: ", DUMP_PREFIX_NONE,
						       16, 1, packet, packet_idx, false);
					printk(KERN_NOTICE "verify: success\n");
				}
			} else {
				// Output buffer too small
				ret = -EOVERFLOW;
				goto END;
			}
		} else {
			// Verification failed
			if (micropLogSwitch) {
				print_hex_dump(KERN_NOTICE, "syno_process_packet hex: ", DUMP_PREFIX_NONE,
					       16, 1, packet, packet_idx, false);
				printk(KERN_NOTICE "verify: failed\n");
			}
			ret = -EIO;
			goto END;
		}
	}

END:
	return ret;
}

/*
 * Read data from ttyS
 *
 * @param cmd_out: [OUT] the buffer to store the command
 * @param cmd_out_len: [IN] the length of the buffer
 * @param timeout: [IN] the timeout of the read
 * @param sn: [IN] the sequence number of the command
 *
 * @return -1: failed
 *       else: the length of the command
 *       -ETIMEDOUT: timeout
 */
/* External function to read from kfifo */
extern int syno_microp_kfifo_read(unsigned char *buf, int buf_size);

static int syno_ttyS_read(u8 *cmd_out, int cmd_out_len, unsigned long timeout, int sn)
{
	int ret = -ETIMEDOUT;
	int consumed = 0;
	int packet_len = 0;
	unsigned long expire = 0;
	unsigned long remain = 0;
	int read_len = 0;
	int buf_size = sizeof(g_rx_buf);
	int tail = 0;
	int free_space = 0;
	int chunk_len = 0;
	u8 tmp_buf[SYNO_MICROP_BUF_SIZE * 2] = {'\0'};

	if (NULL == cmd_out || 0 >= cmd_out_len) {
		printk("%s: parameter error\n", __func__);
		ret = -EINVAL;
		goto END;
	}

	expire = jiffies + msecs_to_jiffies(timeout);
	remain = msecs_to_jiffies(timeout);

	while (time_before(jiffies, expire)) {
		// 1. Try to process packet from ring buffer
		packet_len = syno_process_packet_from_buffer(g_rx_head, g_rx_len, cmd_out, cmd_out_len, sn, &consumed);

		// 2. Handle consumed data
		if (0 < consumed) {
			g_rx_head = (g_rx_head + consumed) % buf_size;
			g_rx_len -= consumed;
		}

		// 3. Check result
		if (0 < packet_len) {
			ret = packet_len; // Success
			goto END;
		} else if (-EOVERFLOW == packet_len) {
			ret = -EOVERFLOW;
			goto END;
		}

		// 4. Wait for more data
		remain = expire - jiffies;
		if (0 >= (long)remain) {
			break;
		}

		if (0 < consumed && 0 < g_rx_len) {
			continue;
		}

		if (0 == wait_for_completion_timeout(&micropReceived, remain)) {
			ret = -ETIMEDOUT;
			goto END;
		}

		// 5. Read data from kfifo into temp buffer then copy to ring buffer
		free_space = buf_size - g_rx_len;
		if (0 < free_space) {
			if (sizeof(tmp_buf) < free_space) {
				free_space = sizeof(tmp_buf);
			}

			read_len = syno_microp_kfifo_read(tmp_buf, free_space);
			if (0 < read_len) {
				tail = (g_rx_head + g_rx_len) % buf_size;
				chunk_len = buf_size - tail;

				if (read_len <= chunk_len) {
					memcpy(g_rx_buf + tail, tmp_buf, read_len);
				} else {
					memcpy(g_rx_buf + tail, tmp_buf, chunk_len);
					memcpy(g_rx_buf, tmp_buf + chunk_len, read_len - chunk_len);
				}

				g_rx_len += read_len;
				if (micropLogSwitch) {
					printk("syno_ttyS_read: read %d bytes, total %d\n", read_len, g_rx_len);
				}
			}
		} else {
			// Buffer full, discard 1 byte to make progress if stuck
			if (buf_size <= g_rx_len) {
				printk("syno_ttyS_read: buffer full, discarding 1 byte\n");
				g_rx_head = (g_rx_head + 1) % buf_size;
				g_rx_len--;
			}
		}
	}

END:
	return ret;
}

static inline u8 syno_ttyS_update_sn(u8 sn)
{
	return (sn + 1 - 32) % 95 + 32; // ascii 32-126
}

/* send command to microP
 *
 * @param cmd: [IN] the command to send
 * @param retry: [IN] the retry times
 *
 * @return 0: success
 *       -1: failed
 */
static int syno_ttyS_cmd_send(struct command *cmd)
{
	int ret = -1;
	u8 cmd_formatted[SYNO_MICROP_BUF_SIZE] = {'\0'};
	u8 cmd_out[SYNO_MICROP_BUF_SIZE] = {'\0'};
	u8 flag = 0;
	int len = 0;
	int cmd_out_len = 0;
	static u8 sn = 30;

	if (NULL == cmd) {
		printk("%s: parameter error\n", __func__);
		goto END;
	}

	sn = syno_ttyS_update_sn(sn);
	if (0 > (len = syno_up_cmd_assemble(sn, SYNO_UP_CMD_FLAG_SEND, cmd->data, cmd_formatted, sizeof(cmd_formatted)))) {
		printk("%s syno_up_cmd_assemble %s failed\n", __func__, cmd->data);
		goto END;
	}
	reinit_completion(&micropReceived);

	if (0 > syno_ttyS_write(cmd_formatted, len)) {
		printk("%s syno_ttyS_write failed\n", __func__);
		goto END;
	}

	if (0 > (cmd_out_len = syno_ttyS_read(cmd_out, sizeof(cmd_out), cmd->timeout, sn))) {
		printk("%s syno_ttyS_read ack failed err=%d\n", __func__, cmd_out_len);
		ret = cmd_out_len;
		goto END;
	}

	// check flag
	flag = cmd_out[2];
	switch (flag) {
		case SYNO_UP_CMD_FLAG_ACK:
			break;
		case SYNO_UP_CMD_FLAG_ERR:
			goto END;
			break;
		default:
			goto END;
			break;
	}

	if (NULL == cmd->payload || 0 == cmd->payload_size) {
		ret = 0;
		goto END;
	}

	// read command need to reply an ack or err
	memset(cmd_formatted, 0, sizeof(cmd_formatted));

	sn = syno_ttyS_update_sn(sn);
	if (0 > (len = syno_up_cmd_assemble(sn, SYNO_UP_CMD_FLAG_ACK, "", cmd_formatted, sizeof(cmd_formatted)))) {
		printk("%s: read command reply ack failed\n", __func__);
		goto END;
	}
	if (0 > syno_ttyS_write(cmd_formatted, len)) {
		printk("%s: syno_ttyS_write failed\n", __func__);
		goto END;
	}

	if (cmd_out_len - SYNO_UP_CMD_MINIMAL_LEN >= cmd->payload_size) {
		// exceed payload buff size
		printk("%s payload buff size is too small cmd_out_len=%d cmd->payload_size=%d\n", __func__, cmd_out_len, cmd->payload_size);
		goto END;
	}

	if (SYNO_UP_CMD_MINIMAL_LEN < cmd->payload_size) {
		memcpy(cmd->payload, &cmd_out[3], cmd_out_len - SYNO_UP_CMD_MINIMAL_LEN);
	}

	ret = 0;

END:
	return ret;
}

typedef enum {
	UP_CMD_TYPE_READ = 0,
	UP_CMD_TYPE_WRITE,
	UP_CMD_TYPE_INVALID,
} syno_up_cmd_type;

struct syno_up_cmd_trans {
	u8 *old_cmd;
	u8 *new_cmd;
	syno_up_cmd_type type;
};

static struct syno_up_cmd_trans syno_up_cmd_table_v1_to_v2[] = {
	// SW Function Command
	{"*", "EEPROM", UP_CMD_TYPE_READ},
	{"o", "VER", UP_CMD_TYPE_INVALID},
	{"R", "INFO", UP_CMD_TYPE_READ},

	{"1", "OFFSTA", UP_CMD_TYPE_WRITE},
	{"C", "Clear", UP_CMD_TYPE_WRITE},

	// BUZZER Command
	{"M", "BUZmute", UP_CMD_TYPE_WRITE},
	{"2", "BUZshort", UP_CMD_TYPE_WRITE},
	{"3", "BUZlong", UP_CMD_TYPE_WRITE},
	{"X", "BUZkeepRS", UP_CMD_TYPE_WRITE},
	{"x", "BUZkeepDS", UP_CMD_TYPE_WRITE},

	// LED Command
	{"4", "LEDpwrON", UP_CMD_TYPE_WRITE},
	{"5", "LEDpwrBli", UP_CMD_TYPE_WRITE},
	{"6", "LEDpwrOFF", UP_CMD_TYPE_WRITE},
	{"7", "LEDstaOFF", UP_CMD_TYPE_WRITE},
	{"8", "LEDstaGON", UP_CMD_TYPE_WRITE},
	{"9", "LEDstaGBli", UP_CMD_TYPE_WRITE},
	{":", "LEDstaOON", UP_CMD_TYPE_WRITE},
	{";", "LEDstaOBli", UP_CMD_TYPE_WRITE},
	{"LA1", "LEDAlrON", UP_CMD_TYPE_WRITE},
	{"LA3", "LEDAlrOFF", UP_CMD_TYPE_WRITE},
	{"LA2", "LEDAlrBli", UP_CMD_TYPE_WRITE},

	// EEPROM Command
	{"q", "ACRON", UP_CMD_TYPE_WRITE},
	{"p", "ACROFF", UP_CMD_TYPE_WRITE},
	{"Z", "ACONON", UP_CMD_TYPE_WRITE},
	{"Y", "ACONOFF", UP_CMD_TYPE_WRITE},
	{"l", "WOLON", UP_CMD_TYPE_WRITE},
	{"k", "WOLOFF", UP_CMD_TYPE_WRITE},
	{"s", "SONON", UP_CMD_TYPE_WRITE},
	{"r", "SONOFF", UP_CMD_TYPE_WRITE},
	{"u", "FCKRON", UP_CMD_TYPE_WRITE},
	{"t", "FCKROFF", UP_CMD_TYPE_WRITE},
};

int syno_up_cmd_translate(const u8 *cmd, u8 *cmd_out, int cmd_out_len, syno_up_cmd_type *type)
{
	int ret = -1;
	int i = 0;
	bool found = false;
	int num = 0;

	if (NULL == cmd || NULL == cmd_out || 0 >= cmd_out_len || NULL == type) {
		printk("%s: parameter error\n", __func__);
		goto END;
	}

	// Handle Dxxx
	if (1 < strlen(cmd) && 'd' == cmd[0] && '0' <= cmd[1] && '9' >= cmd[1]) {
		if (1 == sscanf((const char *)&cmd[1], "%d", &num) && 0 <= num && 999 >= num) {
			snprintf((char *)cmd_out, cmd_out_len, "DelayACON:%d", num);
			*type = UP_CMD_TYPE_WRITE;
			ret = 0;
			found = true;
			goto END;
		}
	}

	// Handle Vxx
	if (1 < strlen(cmd) && 'V' == cmd[0] && '0' <= cmd[1] && '9' >= cmd[1]) {
		if (1 == sscanf((const char *)&cmd[1], "%d", &num) && 0 <= num && 99 >= num) {
			snprintf((char *)cmd_out, cmd_out_len, "Fpwm:%d", num);
			*type = UP_CMD_TYPE_WRITE;
			ret = 0;
			found = true;
			goto END;
		}
	}

	// lookup command table
	for (i = 0; i < sizeof(syno_up_cmd_table_v1_to_v2) / sizeof(struct syno_up_cmd_trans); i++) {
		if (0 == strcmp(cmd, syno_up_cmd_table_v1_to_v2[i].old_cmd)) {
			if (cmd_out_len <= strlen(syno_up_cmd_table_v1_to_v2[i].new_cmd)) {
				goto END;
			}
			if (UP_CMD_TYPE_INVALID == syno_up_cmd_table_v1_to_v2[i].type) {
				// invalid command for v2
				goto END;
			}
			*type = syno_up_cmd_table_v1_to_v2[i].type;
			snprintf(cmd_out, cmd_out_len, "%s", syno_up_cmd_table_v1_to_v2[i].new_cmd);
			ret = 0;
			found = true;
			goto END;
		}
	}

	if (!found) {
		// v2 new commands
		memcpy(cmd_out, cmd, strlen(cmd));
		ret = 0;
		goto END;
	}

END:
	return ret;
}

static bool is_queue_empty(void) {
	return queue_head == queue_tail;
}

static int process_queue(void *data) {
	int ret = -1;
	struct command *cmd = NULL;
	int retry = 1;
	int err = -1;

	while (1) {
		wait_event_interruptible(queue_wq,
				!is_queue_empty() || kthread_should_stop());

		if (kthread_should_stop()) {
			ret = 0;
			goto END;
		}

		cmd = NULL;
		mutex_lock(&queue_lock);
		if (queue_head != queue_tail) {
			cmd = command_queue[queue_head];
			queue_head = (queue_head + 1) % QUEUE_SIZE;
		}
		mutex_unlock(&queue_lock);

		if (NULL == cmd) {
			continue;
		}

		if (!tty_filp) {
			printk("%s Need open %s before read\n", __func__, SYNO_UART_TTYS_PATH);
			goto END;
		}

		retry = 1;
		do {
			if (0 == (err = syno_ttyS_cmd_send(cmd))) {
				break;
			}
			printk("%s Process cmd %s failed err=%d try=%d\n", __func__, cmd->data, err, retry);
			retry++;
		} while (retry <= cmd->retry);

		complete(&cmd->comp);
		kref_put(&cmd->refcount, syno_cmd_release);
	}
	ret = 0;

END:
	return ret;
}

static int enqueue_command(struct command *cmd) {
	int ret = -1;

	if (NULL == cmd) {
		goto END;
	}
	mutex_lock(&queue_lock);

	if ((queue_tail + 1) % QUEUE_SIZE == queue_head) {
		mutex_unlock(&queue_lock);
		goto END;
	}

	if (cmd->preempt) {
		if (queue_head == 0) {
			queue_head = QUEUE_SIZE - 1;
		} else {
			queue_head = queue_head - 1;
		}
		command_queue[queue_head] = cmd;
	} else {
		command_queue[queue_tail] = cmd;
		queue_tail = (queue_tail + 1) % QUEUE_SIZE;
	}

	mutex_unlock(&queue_lock);
	wake_up_interruptible(&queue_wq);
	ret = 0;

END:
	return ret;
}

int syno_microp_ttyS_write(UART2_BUFFER *buffer)
{
	struct command *cmd = NULL;
	int ret = -1;
	u8 up_cmd[SYNO_MICROP_BUF_SIZE] = {'\0'};
	syno_up_cmd_type type = UP_CMD_TYPE_WRITE;


	if (NULL == buffer || '\0' == buffer->szBuf[0] || 0 >= buffer->size) {
		printk("%s: parameter error\n", __func__);
		goto END;
	}

	/* Auto-open if not already opened */
	if (!syno_microp_v2_is_open()) {
		if (0 != syno_microp_v2_open()) {
			printk("%s: auto-open failed\n", __func__);
			goto END;
		}
	}

	if (0 > syno_up_cmd_translate(buffer->szBuf, up_cmd, sizeof(up_cmd), &type)) {
		printk("%s: syno_up_cmd_translate failed\n", __func__);
		goto END;
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		goto END;
	}

	if (NULL == (cmd->data = kzalloc(strlen(up_cmd) + 1, GFP_KERNEL))) {
		kfree(cmd);
		cmd = NULL;
		goto END;
	}
	memcpy(cmd->data, up_cmd, strlen(up_cmd));
	cmd->data[strlen(up_cmd)] = '\0';
	cmd->payload = NULL;
	cmd->payload_size = 0;
	cmd->timeout = buffer->timeout;
	cmd->retry = buffer->retry;
	cmd->preempt = buffer->preempt;
	init_completion(&cmd->comp);
	kref_init(&cmd->refcount);

	if (0 > enqueue_command(cmd)) {
		printk("%s: enqueue_command failed\n", __func__);
		if (cmd->data) {
			kfree(cmd->data);
		}
		kfree(cmd);
		goto END;
	}
	ret = 0;

END:
	return ret;
}
EXPORT_SYMBOL(syno_microp_ttyS_write);

/**
 * syno_microp_read - Simplified wrapper to send command and read response from microP
 * @command: Command string to send
 * @result: Buffer to store the response
 * @result_len: Size of the result buffer
 *
 * This function wraps UART2_BUFFER and calls syno_microp_ttyS_read().
 * It checks if microP v2 is supported before sending.
 * Uses default timeout (SYNO_UP_CMD_V2_DEFAUT_TIMEOUT), retry (SYNO_UP_CMD_V2_DEFAULT_RETRY),
 * and preempt (false).
 *
 * Return: 0 on success, -ENOTSUPP if v2 not supported, negative error code on failure
 */
int syno_microp_read(const char *command, char *result, int result_len)
{
	UART2_BUFFER Buffer;
	int ret = 0;

	if (NULL == command || NULL == result || 0 >= result_len) {
		goto END;
	}

	/* Check if microP v2 is supported */
	if (1 >= gSynoMicropSeries) {
		ret = -ENOTSUPP;
		goto END;
	}

	memset(&Buffer, 0, sizeof(Buffer));
	strncpy(Buffer.szBuf, command, sizeof(Buffer.szBuf) - 1);
	Buffer.szBuf[sizeof(Buffer.szBuf) - 1] = '\0';
	Buffer.size = result_len;
	Buffer.timeout = SYNO_UP_CMD_V2_DEFAUT_TIMEOUT;
	Buffer.retry = SYNO_UP_CMD_V2_DEFAULT_RETRY;
	Buffer.preempt = false;

	if (0 > (ret = syno_microp_ttyS_read(&Buffer))) {
		goto END;
	}

	/* Copy result back to user buffer */
	strncpy(result, Buffer.szBuf, result_len - 1);
	result[result_len - 1] = '\0';

	ret = 0;

END:
	return ret;
}
EXPORT_SYMBOL(syno_microp_read);

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
int syno_microp_write(const char *command)
{
	int ret = -EINVAL;
	UART2_BUFFER buffer;

	if (NULL == command || '\0' == command[0]) {
		printk("%s: parameter error\n", __func__);
		goto END;
	}

	/* Check if microP v2 is supported */
	if (1 >= gSynoMicropSeries) {
		ret = -ENOTSUPP;
		goto END;
	}

	memset(&buffer, 0, sizeof(buffer));
	strncpy(buffer.szBuf, command, sizeof(buffer.szBuf) - 1);
	buffer.szBuf[sizeof(buffer.szBuf) - 1] = '\0';
	buffer.size = strlen(buffer.szBuf) + 1;
	buffer.timeout = SYNO_UP_CMD_V2_DEFAUT_TIMEOUT;
	buffer.retry = SYNO_UP_CMD_V2_DEFAULT_RETRY;
	buffer.preempt = false;

	ret = syno_microp_ttyS_write(&buffer);

END:
	return ret;
}
EXPORT_SYMBOL(syno_microp_write);

/*
 * Perform a read operation on ttyS
 *
 * [In/Out] szBuf: the buffer to store the data
 * [In] size: the size of the buffer
 * [In] preempt: whether the operation is preempt
 *
 * return 0: success
 * return -1: failed
 */
int syno_microp_ttyS_read(UART2_BUFFER *buffer)
{
	struct command *cmd = NULL;
	u8 up_cmd[SYNO_MICROP_BUF_SIZE] = {'\0'};
	syno_up_cmd_type type = UP_CMD_TYPE_READ;
	int ret = -1;

	if (NULL == buffer || '\0' == buffer->szBuf[0] || 0 >= buffer->size) {
		printk("%s: parameter error\n", __func__);
		goto END;
	}

	/* Auto-open if not already opened */
	if (!syno_microp_v2_is_open()) {
		if (0 != syno_microp_v2_open()) {
			printk("%s: auto-open failed\n", __func__);
			goto END;
		}
	}

	if (0 > syno_up_cmd_translate(buffer->szBuf, up_cmd, sizeof(up_cmd), &type)) {
		printk("%s: syno_up_cmd_translate failed\n", __func__);
		goto END;
	}

	cmd = kzalloc(sizeof(struct command), GFP_KERNEL);
	if (!cmd) {
		goto END;
	}

	if (NULL == (cmd->data = kzalloc(strlen(up_cmd) + 1, GFP_KERNEL))) {
		kfree(cmd);
		cmd = NULL;
		goto END;
	}

	memcpy(cmd->data, up_cmd, strlen(up_cmd));
	cmd->data[strlen(up_cmd)] = '\0';
	if (NULL == (cmd->payload = kzalloc(buffer->size, GFP_KERNEL))) {
		kfree(cmd->data);
		kfree(cmd);
		cmd = NULL;
		goto END;
	}
	cmd->payload_size = buffer->size - 1;
	cmd->timeout = buffer->timeout;
	cmd->retry = buffer->retry;
	cmd->preempt = buffer->preempt;
	init_completion(&cmd->comp);
	kref_init(&cmd->refcount);
	// get refcount for syno_up_queue_thread
	kref_get(&cmd->refcount);

	if (0 > enqueue_command(cmd)) {
		printk("%s: enqueue_command failed\n", __func__);
		kref_put(&cmd->refcount, NULL);
		goto END;
	}

	if (0 == wait_for_completion_timeout(&cmd->comp, msecs_to_jiffies(SYNO_TTY_TIMEOUT_MS))) {
		printk("syno_microp_ttyS_read timeout\n");
		goto END;
	}
	memcpy(buffer->szBuf, cmd->payload, buffer->size);

	ret = 0;

END:
	if (cmd) {
		kref_put(&cmd->refcount, syno_cmd_release);
	}
	return ret;
}
EXPORT_SYMBOL(syno_microp_ttyS_read);

int syno_microp_v2_open(void)
{
	int ret = -1;

	mutex_lock(&open_lock);

	if (0 < open_count) {
		open_count++;
		ret = 0;
		goto END;
	}

	if (tty_filp) {
		printk("%s have been opened\n", SYNO_UART_TTYS_PATH);
		goto END;
	}

	tty_filp = filp_open(SYNO_UART_TTYS_PATH, O_RDWR | O_NOCTTY | O_NONBLOCK , 0);
	if (IS_ERR(tty_filp)) {
		printk("Unable to open %s\n", SYNO_UART_TTYS_PATH);
		tty_filp = NULL;
		goto END;
	}

	if (syno_ttyS_set_termios(tty_filp)) {
		printk("Unable to set termios of %s\n", SYNO_UART_TTYS_PATH);
		filp_close(tty_filp, NULL);
		tty_filp = NULL;
		goto END;
	}

	printk("Open %s success\n", SYNO_UART_TTYS_PATH);

	init_completion(&micropReceived);
	mutex_init(&queue_lock);
	queue_head = 0;
	queue_tail = 0;
	g_rx_len = 0;
	g_rx_head = 0;

	queue_thread = kthread_run(process_queue, NULL, "syno_up_queue_thread");
	if (IS_ERR(queue_thread)) {
		printk("Failed to create kernel thread\n");
		filp_close(tty_filp, NULL);
		tty_filp = NULL;
		goto END;
	}

	open_count = 1;
	ret = 0;

END:
	mutex_unlock(&open_lock);
	return ret;
}
EXPORT_SYMBOL(syno_microp_v2_open);

int syno_microp_v2_is_open(void)
{
	return (NULL != tty_filp) ? 1 : 0;
}
EXPORT_SYMBOL(syno_microp_v2_is_open);

/* External function to cleanup kfifo */
extern void syno_microp_kfifo_cleanup(void);

void syno_microp_v2_close(void)
{
	mutex_lock(&open_lock);

    if (0 >= open_count) {
        printk("%s: open_count is already 0 or negative\n", __func__);
        goto END;
    }

	open_count--;
	if (0 < open_count) {
		goto END;
	}

	/* Cleanup kfifo first */
	syno_microp_kfifo_cleanup();

	kthread_stop(queue_thread);
	mutex_destroy(&queue_lock);

	if (!tty_filp) {
		printk("%s wasn't opened", SYNO_UART_TTYS_PATH);
		goto END;
	}

	filp_close(tty_filp, NULL);
	tty_filp = NULL;

END:
	mutex_unlock(&open_lock);
	return;
}
EXPORT_SYMBOL(syno_microp_v2_close);

void syno_microp_v2_wakeup(void) {
	complete(&micropReceived);
}
EXPORT_SYMBOL(syno_microp_v2_wakeup);

/**
 * syno_microp_v2_set_bypass - Set bypass mode for ttyS1 access
 * @enable: true to enable bypass mode, false to disable
 *
 * When bypass is enabled, syno_microp_irq_flip_push will route data to
 * tty_schedule_flip instead of the kfifo, allowing direct /dev/ttyS1 read.
 * When bypass is disabled, data will route to the microP v2 kfifo.
 *
 * Return: 0 on success, negative error code on failure
 */
int syno_microp_v2_set_bypass(bool enable)
{
	struct tty_struct *tty = NULL;

	if (NULL == tty_filp || NULL == tty_filp->private_data) {
		printk(KERN_ERR "%s: tty not opened\n", __func__);
		return -ENODEV;
	}

	tty = ((struct tty_file_private *)tty_filp->private_data)->tty;
	if (NULL == tty || NULL == tty->port) {
		printk(KERN_ERR "%s: tty or port is NULL\n", __func__);
		return -ENODEV;
	}

	WRITE_ONCE(tty->port->syno_microp_bypass, enable);
	return 0;
}

/**
 * syno_microp_v2_is_bypass - Check if bypass mode is enabled
 *
 * Return: true if bypass enabled, false otherwise
 */
bool syno_microp_v2_is_bypass(void)
{
	struct tty_struct *tty = NULL;
	bool ret = false;

	if (NULL == tty_filp || NULL == tty_filp->private_data) {
		goto END;
	}

	tty = ((struct tty_file_private *)tty_filp->private_data)->tty;
	if (NULL == tty || NULL == tty->port) {
		goto END;
	}
	ret = READ_ONCE(tty->port->syno_microp_bypass);

END:
	return ret;
}

/*
 * Sysfs interface for debug control
 * /sys/kernel/syno_microp/control - write "open" or "close"
 * /sys/kernel/syno_microp/status  - read current status
 * /sys/kernel/syno_microp/bypass  - write "1" to enable, "0" to disable
 */
static struct kobject *syno_microp_kobj;

static ssize_t control_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int cmd;
	int ret = kstrtoint(buf, 10, &cmd);
	if (ret) {
		printk(KERN_ERR "syno_microp_v2: invalid input, use '1' to open or '0' to close\n");
		return -EINVAL;
	}
	if (1 == cmd) {
		printk(KERN_INFO "syno_microp_v2: manual open triggered\n");
		if (0 == syno_microp_v2_open()) {
			printk(KERN_INFO "syno_microp_v2: open success\n");
		} else {
			printk(KERN_ERR "syno_microp_v2: open failed\n");
		}
	} else if (0 == cmd) {
		printk(KERN_INFO "syno_microp_v2: manual close triggered\n");
		syno_microp_v2_close();
		printk(KERN_INFO "syno_microp_v2: close done\n");
	} else {
		printk(KERN_ERR "syno_microp_v2: unknown command, use '1' to open or '0' to close\n");
		return -EINVAL;
	}
	return count;
}

static ssize_t control_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "Write '1' to open or '0' to close to control microP v2\n");
}

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "is_open: %d\nopen_count: %d\ntty_filp: %p\n",
			syno_microp_v2_is_open(), open_count, tty_filp);
}

static ssize_t bypass_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int cmd = 0;
	int ret = -EINVAL;

	if (kstrtoint(buf, 10, &cmd)) {
		printk(KERN_ERR "syno_microp_v2: invalid input, use '1' to enable bypass or '0' to disable\n");
		goto END;
	}

	if (1 == cmd) {
		if (0 != syno_microp_v2_set_bypass(true)) {
			printk(KERN_ERR "syno_microp_v2: set bypass failed\n");
			goto END;
		}
	} else if (0 == cmd) {
		if (0 != syno_microp_v2_set_bypass(false)) {
			printk(KERN_ERR "syno_microp_v2: set bypass failed\n");
			goto END;
		}
	} else {
		printk(KERN_ERR "syno_microp_v2: unknown command, use '1' to enable bypass or '0' to disable\n");
		goto END;
	}

	ret = count;

END:
	return ret;
}

static ssize_t bypass_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "bypass: %d\nWrite '1' to enable bypass or '0' to disable\n",
			syno_microp_v2_is_bypass() ? 1 : 0);
}

static struct kobj_attribute control_attr = __ATTR(control, 0644, control_show, control_store);
static struct kobj_attribute status_attr = __ATTR_RO(status);
static struct kobj_attribute bypass_attr = __ATTR(bypass, 0644, bypass_show, bypass_store);

static struct attribute *syno_microp_attrs[] = {
	&control_attr.attr,
	&status_attr.attr,
	&bypass_attr.attr,
	NULL,
};

static struct attribute_group syno_microp_attr_group = {
	.attrs = syno_microp_attrs,
};

static int __init syno_microp_sysfs_init(void)
{
	int ret;

	syno_microp_kobj = kobject_create_and_add("syno_microp", kernel_kobj);
	if (NULL == syno_microp_kobj) {
		printk(KERN_ERR "syno_microp_v2: failed to create sysfs kobject\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(syno_microp_kobj, &syno_microp_attr_group);
	if (0 != ret) {
		printk(KERN_ERR "syno_microp_v2: failed to create sysfs group\n");
		kobject_put(syno_microp_kobj);
		return ret;
	}

	printk(KERN_INFO "syno_microp_v2: sysfs interface created at /sys/kernel/syno_microp/\n");
	return 0;
}
core_initcall(syno_microp_sysfs_init);

#endif /* CONFIG_SYNO_MICROP_COMMAND_V2 */
