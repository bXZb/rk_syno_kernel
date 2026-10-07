// SPDX-License-Identifier: GPL-2.0
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/screen_info.h>
#include <linux/usb/ch9.h>
#include <linux/pci_regs.h>
#include <linux/pci_ids.h>
#include <linux/errno.h>
#include <linux/pgtable.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/fcntl.h>
#include <asm/setup.h>
#include <xen/hvc-console.h>
#include <asm/pci-direct.h>
#include <asm/fixmap.h>
#include <linux/usb/ehci_def.h>
#include <linux/usb/xhci-dbgp.h>
#include <asm/pci_x86.h>
#ifdef CONFIG_SYNO_TTY_DTS_INFO
#include <linux/synolib.h>
#include <linux/syno_fdt.h>
#ifdef CONFIG_SYNO_OOB_SERIAL_OVER_LAN
#include <linux/serial.h>
#endif /* CONFIG_SYNO_OOB_SERIAL_OVER_LAN */
#endif /* CONFIG_SYNO_TTY_DTS_INFO */

/* Simple VGA output */
#define VGABASE		(__ISA_IO_base + 0xb8000)

static int max_ypos = 25, max_xpos = 80;
static int current_ypos = 25, current_xpos;

static void early_vga_write(struct console *con, const char *str, unsigned n)
{
	char c;
	int  i, k, j;

	while ((c = *str++) != '\0' && n-- > 0) {
		if (current_ypos >= max_ypos) {
			/* scroll 1 line up */
			for (k = 1, j = 0; k < max_ypos; k++, j++) {
				for (i = 0; i < max_xpos; i++) {
					writew(readw(VGABASE+2*(max_xpos*k+i)),
					       VGABASE + 2*(max_xpos*j + i));
				}
			}
			for (i = 0; i < max_xpos; i++)
				writew(0x720, VGABASE + 2*(max_xpos*j + i));
			current_ypos = max_ypos-1;
		}
#ifdef CONFIG_KGDB_KDB
		if (c == '\b') {
			if (current_xpos > 0)
				current_xpos--;
		} else if (c == '\r') {
			current_xpos = 0;
		} else
#endif
		if (c == '\n') {
			current_xpos = 0;
			current_ypos++;
		} else if (c != '\r')  {
			writew(((0x7 << 8) | (unsigned short) c),
			       VGABASE + 2*(max_xpos*current_ypos +
						current_xpos++));
			if (current_xpos >= max_xpos) {
				current_xpos = 0;
				current_ypos++;
			}
		}
	}
}

static struct console early_vga_console = {
	.name =		"earlyvga",
	.write =	early_vga_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};

/* Serial functions loosely based on a similar package from Klaus P. Gerlicher */

static unsigned long early_serial_base = 0x3f8;  /* ttyS0 */
#if defined(CONFIG_SYNO_OOB_SERIAL_OVER_LAN) && defined(CONFIG_SYNO_TTY_DTS_INFO)
static unsigned long oob_early_serial_base = 0x3f8;  /* ttyS2 */
#endif /* CONFIG_SYNO_OOB_SERIAL_OVER_LAN && CONFIG_SYNO_TTY_DTS_INFO */

#define XMTRDY          0x20
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
#define TEMT		0x40
#define THRE		XMTRDY
#define BOTH_EMPTY 	(TEMT | THRE)
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */

#define DLAB		0x80

#if defined(CONFIG_SYNO_OOB_SERIAL_OVER_LAN) && defined(CONFIG_SYNO_TTY_DTS_INFO)
#define CTS	     0x10
#endif /* CONFIG_SYNO_OOB_SERIAL_OVER_LAN && CONFIG_SYNO_TTY_DTS_INFO */

#define TXR             0       /*  Transmit register (WRITE) */
#define RXR             0       /*  Receive register  (READ)  */
#define IER             1       /*  Interrupt Enable          */
#define IIR             2       /*  Interrupt ID              */
#define FCR             2       /*  FIFO control              */
#define LCR             3       /*  Line control              */
#define MCR             4       /*  Modem control             */
#define LSR             5       /*  Line Status               */
#define MSR             6       /*  Modem Status              */
#define DLL             0       /*  Divisor Latch Low         */
#define DLH             1       /*  Divisor latch High        */

static unsigned int io_serial_in(unsigned long addr, int offset)
{
	return inb(addr + offset);
}

static void io_serial_out(unsigned long addr, int offset, int value)
{
	outb(value, addr + offset);
}

static unsigned int (*serial_in)(unsigned long addr, int offset) = io_serial_in;
static void (*serial_out)(unsigned long addr, int offset, int value) = io_serial_out;

#if defined(CONFIG_SYNO_OOB_SERIAL_OVER_LAN) && defined(CONFIG_SYNO_TTY_DTS_INFO)
static unsigned int (*oob_serial_in)(unsigned long addr, int offset) = io_serial_in;
static void (*oob_serial_out)(unsigned long addr, int offset, int value) = io_serial_out;

static int syno_oob_early_serial_putc(unsigned char ch)
{
	unsigned timeout = 0xffff;
	while (((oob_serial_in(oob_early_serial_base, LSR) & XMTRDY) == 0 && --timeout) || 0 == (oob_serial_in(oob_early_serial_base, MSR) & CTS))
		cpu_relax();
	oob_serial_out(oob_early_serial_base, TXR, ch);
	return timeout ? 0 : -1;
}

static void oob_early_serial_write(struct console *con, const char *s, unsigned n)
{
	while (*s && n-- > 0) {
		if (*s == '\n')
			syno_oob_early_serial_putc('\r');
		syno_oob_early_serial_putc(*s);
		s++;
	}
}

static __init void oob_early_serial_hw_init(unsigned divisor)
{
	unsigned char c;

	oob_serial_out(oob_early_serial_base, LCR, 0x3);	/* 8n1 */
	oob_serial_out(oob_early_serial_base, IER, 0);	/* no interrupt */
	oob_serial_out(oob_early_serial_base, FCR, 0);	/* no fifo */
	oob_serial_out(oob_early_serial_base, MCR, 0x3);	/* DTR + RTS */

	c = oob_serial_in(oob_early_serial_base, LCR);
	oob_serial_out(oob_early_serial_base, LCR, c | DLAB);
	oob_serial_out(oob_early_serial_base, DLL, divisor & 0xff);
	oob_serial_out(oob_early_serial_base, DLH, (divisor >> 8) & 0xff);
	oob_serial_out(oob_early_serial_base, LCR, c & ~DLAB);
}
#endif /* CONFIG_SYNO_OOB_SERIAL_OVER_LAN && CONFIG_SYNO_TTY_DTS_INFO */

static int early_serial_putc(unsigned char ch)
{
	unsigned timeout = 0xffff;
	while ((serial_in(early_serial_base, LSR) & XMTRDY) == 0 && --timeout)
		cpu_relax();
	serial_out(early_serial_base, TXR, ch);
	return timeout ? 0 : -1;
}

static void early_serial_write(struct console *con, const char *s, unsigned n)
{
	while (*s && n-- > 0) {
		if (*s == '\n')
			early_serial_putc('\r');
		early_serial_putc(*s);
		s++;
	}
}

static __init void early_serial_hw_init(unsigned divisor)
{
	unsigned char c;

	serial_out(early_serial_base, LCR, 0x3);	/* 8n1 */
	serial_out(early_serial_base, IER, 0);	/* no interrupt */
	serial_out(early_serial_base, FCR, 0);	/* no fifo */
	serial_out(early_serial_base, MCR, 0x3);	/* DTR + RTS */

	c = serial_in(early_serial_base, LCR);
	serial_out(early_serial_base, LCR, c | DLAB);
	serial_out(early_serial_base, DLL, divisor & 0xff);
	serial_out(early_serial_base, DLH, (divisor >> 8) & 0xff);
	serial_out(early_serial_base, LCR, c & ~DLAB);
}

#define DEFAULT_BAUD 9600

static __init void early_serial_init(char *s)
{
	unsigned divisor;
	unsigned long baud = DEFAULT_BAUD;
	char *e;

	if (*s == ',')
		++s;

	if (*s) {
		unsigned port;
		if (!strncmp(s, "0x", 2)) {
			early_serial_base = simple_strtoul(s, &e, 16);
		} else {
			static const int __initconst bases[] = { 0x3f8, 0x2f8 };

			if (!strncmp(s, "ttyS", 4))
				s += 4;
			port = simple_strtoul(s, &e, 10);
			if (port > 1 || s == e)
				port = 0;
			early_serial_base = bases[port];
		}
		s += strcspn(s, ",");
		if (*s == ',')
			s++;
	}

	if (*s) {
		baud = simple_strtoull(s, &e, 0);

		if (baud == 0 || s == e)
			baud = DEFAULT_BAUD;
	}

	/* Convert from baud to divisor value */
	divisor = 115200 / baud;

	/* These will always be IO based ports */
	serial_in = io_serial_in;
	serial_out = io_serial_out;

	/* Set up the HW */
	early_serial_hw_init(divisor);
}

#ifdef CONFIG_PCI
static void mem32_serial_out(unsigned long addr, int offset, int value)
{
	u32 __iomem *vaddr = (u32 __iomem *)addr;
	/* shift implied by pointer type */
	writel(value, vaddr + offset);
}

static unsigned int mem32_serial_in(unsigned long addr, int offset)
{
	u32 __iomem *vaddr = (u32 __iomem *)addr;
	/* shift implied by pointer type */
	return readl(vaddr + offset);
}

#if defined(CONFIG_SYNO_OOB_SERIAL_OVER_LAN) && defined(CONFIG_SYNO_TTY_DTS_INFO)
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
static void oob_early_serial_hw_deinit(void)
{
	unsigned long timeout_jiffies = jiffies + msecs_to_jiffies(2000);
	while ((oob_serial_in(oob_early_serial_base, LSR) & BOTH_EMPTY) != BOTH_EMPTY) {
		if (time_after(jiffies, timeout_jiffies)) {
			break;
		}
	}
	oob_serial_out(oob_early_serial_base, IER, 0);	/* no interrupt */
	oob_serial_out(oob_early_serial_base, FCR, 0);	/* no fifo */
}
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */

static struct console oob_early_serial_console = {
	.name =		"earlyser",
	.write =	oob_early_serial_write,
	.flags =	CON_PRINTBUFFER,
	.index =	SYNO_OOB_TTY,
	/* Synology add */
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
	.pcimapaddress = 0,
	.pcimapsize = 0,
	.deinit = oob_early_serial_hw_deinit,
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
};
#endif /* CONFIG_SYNO_OOB_SERIAL_OVER_LAN && CONFIG_SYNO_TTY_DTS_INFO */
 
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
static void early_serial_hw_deinit(void)
{
	unsigned long timeout_jiffies = jiffies + msecs_to_jiffies(2000);
	while ((serial_in(early_serial_base, LSR) & BOTH_EMPTY) != BOTH_EMPTY) {
		if (time_after(jiffies, timeout_jiffies)) {
			break;
		}
	}
	serial_out(early_serial_base, IER, 0);	/* no interrupt */
	serial_out(early_serial_base, FCR, 0);	/* no fifo */
}

static struct console early_serial_console = {
	.name =		"earlyser",
	.write =	early_serial_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
	/* Synology add */
	.pcimapaddress = 0,
	.pcimapsize = 0,
	.deinit = early_serial_hw_deinit,
};

static __init void early_mmio_serial_init(char *s)
{
        unsigned divisor;
        unsigned long addr;
        unsigned long baud = 115200;         /* Default baud 115200 */
        unsigned long base_clock = 1843200;  /* Default clock 1.84M */
        char *e;

        if (*s == ',')
                ++s;

        if (!strncmp(s, "0x", 2)) {
                addr = simple_strtoul(s, &e, 16);
        }

        s = e;

        if (*s == ',')
                ++s;

        baud = simple_strtoul(s, &e, 10);

        s = e;

        if (*s == ',')
                ++s;

        base_clock = simple_strtoul(s, &e, 10);


        early_serial_base = (unsigned long)early_ioremap(addr, 0x10);


        serial_in = mem32_serial_in;
        serial_out = mem32_serial_out;

        early_serial_console.pcimapaddress = (void __iomem *)early_serial_base;
        early_serial_console.pcimapsize = 0x10;

        divisor = (base_clock / 16) / baud;

        early_serial_hw_init(divisor);
}

/*
 * early_pcifull_serial_init()
 *
 */
static __init void early_pcifull_serial_init(char *s)
{
	unsigned divisor;
	unsigned long baud = DEFAULT_BAUD;
	u8 bus, slot, func;
	u8 htype, secondbus;
#ifdef CONFIG_SYNO_EARLY_PRINTK_PCI_64
	u32 classcode;
	u64 bar0;
#else
	u32 classcode, bar0;
#endif
	u16 cmdreg;
	char *e;

	/*
	 * First, part the param to get the BDF values
	 */
	if (*s == ',')
		++s;

	if (*s == 0)
		return;

	bus = (u8)simple_strtoul(s, &e, 16);
	s = e;
	if (*s != ':')
		return;
	++s;
	slot = (u8)simple_strtoul(s, &e, 16);
	s = e;
	if (*s != '.')
		return;
	++s;
	func = (u8)simple_strtoul(s, &e, 16);
	s = e;

	htype = read_pci_config_byte(bus, slot, func, PCI_HEADER_TYPE);
	while((htype & 0x7F) == PCI_HEADER_TYPE_BRIDGE ||
		  (htype & 0x7F) == PCI_HEADER_TYPE_CARDBUS ){

		secondbus = read_pci_config_byte(bus, slot, func, PCI_SECONDARY_BUS);
		if(secondbus == 0xFF)
			return;
		bus = secondbus;

		if (*s != ',')
			return;
		++s;

		slot = (u8)simple_strtoul(s, &e, 16);
		s = e;
		if (*s != '.')
			return;
		++s;

		func = (u8)simple_strtoul(s, &e, 16);
		s = e;

		htype = read_pci_config_byte(bus, slot, func, PCI_HEADER_TYPE);
	}

	if ((htype & 0x7F) != PCI_HEADER_TYPE_NORMAL)
		return ;

	/* A baud might be following */
	if (*s == ',')
		s++;

	/*
	 * Second, find the device from the BDF
	 */
	cmdreg = read_pci_config(bus, slot, func, PCI_COMMAND);
	classcode = read_pci_config(bus, slot, func, PCI_CLASS_REVISION);
	bar0 = read_pci_config(bus, slot, func, PCI_BASE_ADDRESS_0);

	/*
	 * Determine if it is IO or memory mapped
	 */
	if (bar0 & 0x01) {
		/* it is IO mapped */
		serial_in = io_serial_in;
		serial_out = io_serial_out;
		early_serial_base = bar0&0xfffffffc;
		write_pci_config(bus, slot, func, PCI_COMMAND,
						cmdreg|PCI_COMMAND_IO);
	} else {
		/* It is memory mapped - assume 32-bit alignment */
		serial_in = mem32_serial_in;
		serial_out = mem32_serial_out;
		/* WARNING! assuming the address is always in the first 4G */
#ifdef CONFIG_SYNO_EARLY_PRINTK_PCI_64
		/*
		* Verify support 64 bit BAR
		*/
		if (bar0 & 0x4) {
			bar0 |= ((u64)read_pci_config(bus, slot, func, PCI_BASE_ADDRESS_1)) << 32;
		}
		early_serial_base =
			(unsigned long)early_ioremap(bar0 & 0xfffffffffffffff0, 0x10);
#else
		early_serial_base =
			(unsigned long)early_ioremap(bar0 & 0xfffffff0, 0x10);
#endif
		early_serial_console.pcimapaddress = (void __iomem *)early_serial_base;
		/* base on pci spec with serial console */
		early_serial_console.pcimapsize = 0x10;
		write_pci_config(bus, slot, func, PCI_COMMAND,
						cmdreg|PCI_COMMAND_MEMORY);
	}

	/*
	 * Lastly, initalize the hardware
	 */
	if (*s) {
		if (strcmp(s, "nocfg") == 0)
			/* Sometimes, we want to leave the UART alone
			 * and assume the BIOS has set it up correctly.
			 * "nocfg" tells us this is the case, and we
			 * should do no more setup.
			 */
			return;
		if (kstrtoul(s, 0, &baud) < 0 || baud == 0)
			baud = DEFAULT_BAUD;
	}

	/* Convert from baud to divisor value */
	divisor = 115200 / baud;

	/* Set up the HW */
	early_serial_hw_init(divisor);
}
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */

/*
 * early_pci_serial_init()
 *
 * This function is invoked when the early_printk param starts with "pciserial"
 * The rest of the param should be "[force],B:D.F,baud", where B, D & F describe
 * the location of a PCI device that must be a UART device. "force" is optional
 * and overrides the use of an UART device with a wrong PCI class code.
 */
static __init void early_pci_serial_init(char *s)
{
	unsigned divisor;
	unsigned long baud = DEFAULT_BAUD;
	u8 bus, slot, func;
#ifdef CONFIG_SYNO_EARLY_PRINTK_PCI_64
	u32 classcode;
	u64 bar0;
#else
	u32 classcode, bar0;
#endif
	u16 cmdreg;
	char *e;
	int force = 0;

	if (*s == ',')
		++s;

	if (*s == 0)
		return;

	/* Force the use of an UART device with wrong class code */
	if (!strncmp(s, "force,", 6)) {
		force = 1;
		s += 6;
	}

	/*
	 * Part the param to get the BDF values
	 */
	bus = (u8)simple_strtoul(s, &e, 16);
	s = e;
	if (*s != ':')
		return;
	++s;
	slot = (u8)simple_strtoul(s, &e, 16);
	s = e;
	if (*s != '.')
		return;
	++s;
	func = (u8)simple_strtoul(s, &e, 16);
	s = e;

	/* A baud might be following */
	if (*s == ',')
		s++;

	/*
	 * Find the device from the BDF
	 */
	cmdreg = read_pci_config(bus, slot, func, PCI_COMMAND);
	classcode = read_pci_config(bus, slot, func, PCI_CLASS_REVISION);
	bar0 = read_pci_config(bus, slot, func, PCI_BASE_ADDRESS_0);

	/*
	 * Verify it is a UART type device
	 */
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
	force = 1;
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */

	if (((classcode >> 16 != PCI_CLASS_COMMUNICATION_MODEM) &&
	     (classcode >> 16 != PCI_CLASS_COMMUNICATION_SERIAL)) ||
	   (((classcode >> 8) & 0xff) != 0x02)) /* 16550 I/F at BAR0 */ {
		if (!force)
			return;
	}

	/*
	 * Determine if it is IO or memory mapped
	 */
	if (bar0 & 0x01) {
		/* it is IO mapped */
		serial_in = io_serial_in;
		serial_out = io_serial_out;
		early_serial_base = bar0&0xfffffffc;
		write_pci_config(bus, slot, func, PCI_COMMAND,
						cmdreg|PCI_COMMAND_IO);
	} else {
		/* It is memory mapped - assume 32-bit alignment */
		serial_in = mem32_serial_in;
		serial_out = mem32_serial_out;
		/* WARNING! assuming the address is always in the first 4G */
#ifdef CONFIG_SYNO_EARLY_PRINTK_PCI_64
		/*
		* Verify support 64 bit BAR
		*/
		if (bar0 & 0x4) {
			bar0 |= ((u64)read_pci_config(bus, slot, func, PCI_BASE_ADDRESS_1)) << 32;
		}
		early_serial_base =
			(unsigned long)early_ioremap(bar0 & 0xfffffffffffffff0, 0x10);
#else
		early_serial_base =
			(unsigned long)early_ioremap(bar0 & 0xfffffff0, 0x10);
#endif
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
		early_serial_console.pcimapaddress = (void __iomem *)early_serial_base;
		/* base on pci spec with serial console */
		early_serial_console.pcimapsize = 0x10;
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
		write_pci_config(bus, slot, func, PCI_COMMAND,
						cmdreg|PCI_COMMAND_MEMORY);
	}


	/*
	 * Initialize the hardware
	 */
	if (*s) {
		if (strcmp(s, "nocfg") == 0)
			/* Sometimes, we want to leave the UART alone
			 * and assume the BIOS has set it up correctly.
			 * "nocfg" tells us this is the case, and we
			 * should do no more setup.
			 */
			return;
		if (kstrtoul(s, 0, &baud) < 0 || baud == 0)
			baud = DEFAULT_BAUD;
	}

	/* Convert from baud to divisor value */
	divisor = 115200 / baud;

	/* Set up the HW */
	early_serial_hw_init(divisor);
}

#if defined(CONFIG_SYNO_OOB_SERIAL_OVER_LAN) && defined(CONFIG_SYNO_TTY_DTS_INFO)
static __init void oob_early_pci_serial_init(char *s)
{
	unsigned divisor;
	unsigned long baud = DEFAULT_BAUD;
	u8 bus, slot, func;
	u32 classcode, bar0;
	u16 cmdreg;
	char *e;
	int force = 0;

	if (*s == ',')
		++s;

	if (*s == 0)
		return;

	/* Force the use of an UART device with wrong class code */
	if (!strncmp(s, "force,", 6)) {
		force = 1;
		s += 6;
	}

	/*
	 * Part the param to get the BDF values
	 */
	bus = (u8)simple_strtoul(s, &e, 16);
	s = e;
	if (*s != ':')
		return;
	++s;
	slot = (u8)simple_strtoul(s, &e, 16);
	s = e;
	if (*s != '.')
		return;
	++s;
	func = (u8)simple_strtoul(s, &e, 16);
	s = e;

	/* A baud might be following */
	if (*s == ',')
		s++;

	/*
	 * Find the device from the BDF
	 */
	cmdreg = read_pci_config(bus, slot, func, PCI_COMMAND);
	classcode = read_pci_config(bus, slot, func, PCI_CLASS_REVISION);
	bar0 = read_pci_config(bus, slot, func, PCI_BASE_ADDRESS_0);

	/*
	 * Verify it is a UART type device
	 */
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
	force = 1;
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */

	if (((classcode >> 16 != PCI_CLASS_COMMUNICATION_MODEM) &&
	     (classcode >> 16 != PCI_CLASS_COMMUNICATION_SERIAL)) ||
	   (((classcode >> 8) & 0xff) != 0x02)) /* 16550 I/F at BAR0 */ {
		if (!force)
			return;
	}

	/*
	 * Determine if it is IO or memory mapped
	 */
	if (bar0 & 0x01) {
		/* it is IO mapped */
		oob_serial_in = io_serial_in;
		oob_serial_out = io_serial_out;
		oob_early_serial_base = bar0&0xfffffffc;
		write_pci_config(bus, slot, func, PCI_COMMAND,
						cmdreg|PCI_COMMAND_IO);
	} else {
		/* It is memory mapped - assume 32-bit alignment */
		oob_serial_in = mem32_serial_in;
		oob_serial_out = mem32_serial_out;
		/* WARNING! assuming the address is always in the first 4G */
		oob_early_serial_base =
			(unsigned long)early_ioremap(bar0 & 0xfffffff0, 0x10);
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
		oob_early_serial_console.pcimapaddress = (void __iomem *)oob_early_serial_base;
		/* base on pci spec with serial console */
		oob_early_serial_console.pcimapsize = 0x10;
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
		write_pci_config(bus, slot, func, PCI_COMMAND,
						cmdreg|PCI_COMMAND_MEMORY);
	}

	/*
	 * Initialize the hardware
	 */
	if (*s) {
		if (strcmp(s, "nocfg") == 0)
			/* Sometimes, we want to leave the UART alone
			 * and assume the BIOS has set it up correctly.
			 * "nocfg" tells us this is the case, and we
			 * should do no more setup.
			 */
			return;
		if (kstrtoul(s, 0, &baud) < 0 || baud == 0)
			baud = DEFAULT_BAUD;
	}

	/* Convert from baud to divisor value */
	divisor = 115200 / baud;

	/* Set up the HW */
	oob_early_serial_hw_init(divisor);
}
#endif /* CONFIG_SYNO_OOB_SERIAL_OVER_LAN && CONFIG_SYNO_TTY_DTS_INFO */
#endif

#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
	/* Move to upper */
#else /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
static struct console early_serial_console = {
	.name =		"earlyser",
	.write =	early_serial_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */

static void early_console_register(struct console *con, int keep_early)
{
#ifdef CONFIG_SYNO_TTY_DTS_INFO
	static int early_con_set = 0;

	if (early_con_set) {
#else
	if (con->index != -1) {
#endif
		printk(KERN_CRIT "ERROR: earlyprintk= %s already used\n",
		       con->name);
		return;
	}
	early_console = con;
	if (keep_early)
		early_console->flags &= ~CON_BOOT;
	else
		early_console->flags |= CON_BOOT;
	register_console(early_console);
#ifdef CONFIG_SYNO_TTY_DTS_INFO
	early_con_set = 1;
#endif
}

#if defined(CONFIG_SYNO_OOB_SERIAL_OVER_LAN) && defined(CONFIG_SYNO_TTY_DTS_INFO)
static void oob_early_console_register(struct console *con, int keep_early)
{
	static int oob_early_con_set = 0;

	if (oob_early_con_set) {
		printk(KERN_CRIT "ERROR: earlyprintk= %s already used\n",
		       con->name);
		return;
	}
	oob_early_console = con;
	if (keep_early)
		oob_early_console->flags &= ~CON_BOOT;
	else
		oob_early_console->flags |= CON_BOOT;
	register_console(oob_early_console);
	oob_early_con_set = 1;
}

static int __init setup_oob_early_printk(char *buf)
{
	int keep;

	if (!buf)
		return 0;

	if (oob_early_console)
		return 0;

	keep = (strstr(buf, "keep") != NULL);

	while (*buf != '\0') {
		if (!strncmp(buf, "pciserial", 9)) {
			oob_early_pci_serial_init(buf + 9);
			oob_early_console_register(&oob_early_serial_console, keep);
			buf += 9; /* Keep from match the above "serial" */
		}
		buf++;
	}
	return 0;

}
#endif /* CONFIG_SYNO_OOB_SERIAL_OVER_LAN && CONFIG_SYNO_TTY_DTS_INFO */

#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
int __init setup_early_printk(char *buf)
#else /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
static int __init setup_early_printk(char *buf)
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
{
	int keep;

	if (!buf)
		return 0;

	if (early_console)
		return 0;

	keep = (strstr(buf, "keep") != NULL);

	while (*buf != '\0') {
		if (!strncmp(buf, "serial", 6)) {
			buf += 6;
			early_serial_init(buf);
			early_console_register(&early_serial_console, keep);
			if (!strncmp(buf, ",ttyS", 5))
				buf += 5;
		}
		if (!strncmp(buf, "ttyS", 4)) {
			early_serial_init(buf + 4);
			early_console_register(&early_serial_console, keep);
		}
#ifdef CONFIG_PCI
		if (!strncmp(buf, "pciserial", 9)) {
			early_pci_serial_init(buf + 9);
			early_console_register(&early_serial_console, keep);
			buf += 9; /* Keep from match the above "serial" */
		}
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
		if (!strncmp(buf, "pcifull", 7)) {
			early_pcifull_serial_init(buf + 7);
			early_console_register(&early_serial_console, keep);
			buf += 7; /* Keep from match the above "serial" */
		}
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
#endif
#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
		if (!strncmp(buf, "mmio", 4)) {
			early_mmio_serial_init(buf + 4);
			early_console_register(&early_serial_console, keep);
			buf += 4; /* Keep from match the above "serial" */
		}
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
		if (!strncmp(buf, "vga", 3) &&
		    boot_params.screen_info.orig_video_isVGA == 1) {
			max_xpos = boot_params.screen_info.orig_video_cols;
			max_ypos = boot_params.screen_info.orig_video_lines;
			current_ypos = boot_params.screen_info.orig_y;
			early_console_register(&early_vga_console, keep);
		}
#ifdef CONFIG_EARLY_PRINTK_DBGP
		if (!strncmp(buf, "dbgp", 4) && !early_dbgp_init(buf + 4))
			early_console_register(&early_dbgp_console, keep);
#endif
#ifdef CONFIG_HVC_XEN
		if (!strncmp(buf, "xen", 3))
			early_console_register(&xenboot_console, keep);
#endif
#ifdef CONFIG_EARLY_PRINTK_USB_XDBC
		if (!strncmp(buf, "xdbc", 4))
			early_xdbc_parse_parameter(buf + 4);
#endif

		buf++;
	}
	return 0;
}

#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
EXPORT_SYMBOL(setup_early_printk);
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
early_param("earlyprintk", setup_early_printk);

#ifdef CONFIG_SYNO_TTY_DTS_INFO

struct pci_dev_loc {
	u8 bus;
	u8 dev;
	u8 func;
};

static int early_syno_pcieloc_get_by_dts_pattern(const struct device_node *pDeviceNode, struct pci_dev_loc *pdevloc)
{
	int iRet = -1;
	char *szPath = NULL;
	int offset = 0;
	u8 tmpDev = 0;
	u8 tmpFunc = 0;
#ifdef CONFIG_SYNO_PCI_DOMAIN_PATH
	int domain = 0;
#endif /* CONFIG_SYNO_PCI_DOMAIN_PATH */

	if (!pDeviceNode || !pdevloc) {
		printk("Invalid parameter\n");
		goto END;
	}

	szPath = (char *)of_get_property(pDeviceNode, DT_PCIE_ROOT, NULL);

#ifdef CONFIG_SYNO_PCI_DOMAIN_PATH
	iRet = sscanf(szPath, "%04x:%02hhx:%02hhx.%hhx%n", &domain, &pdevloc->bus, &pdevloc->dev, &pdevloc->func, &offset);
	
	if (4 != iRet) {
#else /* CONFIG_SYNO_PCI_DOMAIN_PATH */
	iRet = sscanf(szPath, "%02hhx:%02hhx.%hhx%n", &pdevloc->bus, &pdevloc->dev, &pdevloc->func, &offset);

	if (3 != iRet) {
#endif /* CONFIG_SYNO_PCI_DOMAIN_PATH */
		iRet = -1;
		goto END;
	}

	szPath += offset;

	while (2 == sscanf(szPath, ",%02hhx.%hhx%n", &tmpDev, &tmpFunc, &offset)) {
		pdevloc->bus = read_pci_config_byte(pdevloc->bus, pdevloc->dev, pdevloc->func, PCI_SECONDARY_BUS);
		pdevloc->dev = tmpDev;
		pdevloc->func = tmpFunc;

		szPath += offset;
	}
	iRet = 0;
END:
	return iRet;
}

#ifdef CONFIG_SYNO_TTY_DISABLE
extern int gSynoTtyS0Enable;
#endif /* CONFIG_SYNO_TTY_DISABLE */

void __init syno_setup_early_printk(void)
{
	char buf[128] = {0};
	int err = -1;
	const char *addr_type = NULL;
	u32 base_addr = 0;
	u32 speed = 0;
	u32 clock = 0;
	struct device_node *pSlotNode = NULL;
	struct pci_dev_loc pdevloc = {0};
	int index = -1;
	int prefer_index = -1;
#ifdef CONFIG_SYNO_TTY_DISABLE
	int ttys0_disable = 0;
#endif /* CONFIG_SYNO_TTY_DISABLE */

	if (!of_root) {
		printk("failed to get device tree\n");
		goto END;
	}

#ifdef CONFIG_SYNO_TTY_DISABLE
	// ttyS0 is disabled by default on models with ttyS0_disable_check in DTS
	if (of_property_read_bool(of_root, DT_TTY_DISABLE_CHECK) && !gSynoTtyS0Enable) {
		ttys0_disable = 1;
	}
#endif /* CONFIG_SYNO_TTY_DISABLE */

	// looking for early printk setting and get str
	for_each_child_of_node(of_root, pSlotNode) {
		// get tty node with property earlyprintk
		if (!pSlotNode->full_name || 1 != sscanf(pSlotNode->full_name, DT_TTY_NODE"@%d", &index)) {
			continue;
		}
#ifdef CONFIG_SYNO_TTY_DISABLE
		if (0 == index && ttys0_disable) {
			continue;
		}
#endif /* CONFIG_SYNO_TTY_DISABLE */
		if (!of_property_read_bool(pSlotNode, DT_TTY_PREFERRED_CON) && !of_property_read_bool(pSlotNode, DT_TTY_ENABLE_CON)) {
			continue;
		}

		if (of_property_read_bool(pSlotNode, DT_TTY_PREFERRED_CON)) {
			prefer_index = index;
		}

		err = of_property_read_string(pSlotNode, DT_TTY_ADDR_TYPE, &addr_type);
		if (err < 0) {
			of_node_put(pSlotNode);
			goto END;
		}
		
		err = of_property_read_u32(pSlotNode, DT_TTY_SPEED, &speed);
		if (err < 0) {
			of_node_put(pSlotNode);
			goto END;
		}
		
		if (!strcmp(addr_type, DT_TTY_TYPE_PCIE) || !strcmp(addr_type, DT_TTY_TYPE_DEV_NAME)) {
			// found any match serial pci device
			if (0 > early_syno_pcieloc_get_by_dts_pattern(pSlotNode, &pdevloc)) {
				printk("failed to parse pci device location\n");
				goto END;
			}
			snprintf(buf, sizeof(buf),"pciserial,0x%x:0x%x.0x%x,%u", pdevloc.bus, pdevloc.dev, pdevloc.func, speed);
		} else if (!strcmp(addr_type, DT_TTY_TYPE_MMIO)) {
			err = of_property_read_u32(pSlotNode, DT_TTY_BASE, &base_addr);
			if (err < 0) {
				of_node_put(pSlotNode);
				goto END;
			}

			err = of_property_read_u32(pSlotNode, DT_TTY_CLOCK, &clock);
			if (err < 0) {
				of_node_put(pSlotNode);
				goto END;
			}

			snprintf(buf, sizeof(buf),"%s,0x%x,%u,%u", addr_type, base_addr, speed, clock);
		}

#ifdef CONFIG_SYNO_OOB_SERIAL_OVER_LAN
		if (SYNO_OOB_TTY == index){
			setup_oob_early_printk(buf);
		} else
#endif /* CONFIG_SYNO_OOB_SERIAL_OVER_LAN */
		{
			early_serial_console.index = index;
			setup_early_printk(buf);
		}
	}

	if (prefer_index >= 0)
		add_preferred_console("ttyS", prefer_index, NULL);
END:
	if (pSlotNode) {
		of_node_put(pSlotNode);
	}

	return;
}
EXPORT_SYMBOL(syno_setup_early_printk);
#endif /* CONFIG_SYNO_TTY_DTS_INFO */
