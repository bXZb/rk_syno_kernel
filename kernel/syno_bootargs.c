// Copyright (c) 2003-2020 Synology Inc. All rights reserved.
#include <linux/kernel.h>
#include <linux/synolib.h>
#include <linux/string.h>

#ifdef CONFIG_SYNO_MAC_ADDRESS
#include <linux/synolib.h>
#endif /* CONFIG_SYNO_MAC_ADDRESS */

#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
extern int setup_early_printk(char *);
extern char gszSynoTtyS0[50];
extern char gszSynoTtyS1[50];
extern char gszSynoTtyS2[50];
#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */
#ifdef CONFIG_SYNO_INTERNAL_NETIF_NUM
extern long g_internal_netif_num;
#endif /* CONFIG_SYNO_INTERNAL_NETIF_NUM*/

#ifdef CONFIG_SYNO_HW_VERSION
extern char gszSynoHWVersion[];
#endif /* CONFIG_SYNO_HW_VERSION */

#ifdef CONFIG_SYNO_HW_REVISION
extern char gszSynoHWRevision[];
#endif /* CONFIG_SYNO_HW_REVISION */

#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
extern long g_smbus_hdd_powerctl;
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

#ifdef CONFIG_SYNO_MAC_ADDRESS
extern unsigned char grgbLanMac[SYNO_MAC_MAX_NUMBER][16];
extern int giVenderFormatVersion;
extern char gszSkipVenderMacInterfaces[256];
#endif /* CONFIG_SYNO_MAC_ADDRESS */

#ifdef CONFIG_SYNO_SERIAL
extern char gszSerialNum[32];
extern char gszCustomSerialNum[32];
#endif /* CONFIG_SYNO_SERIAL */

#ifdef CONFIG_SYNO_SAS
extern long g_is_sas_model;
#endif /* CONFIG_SYNO_SAS */

#ifdef CONFIG_SYNO_AHCI_SWITCH
extern char g_ahci_switch;
#endif /* CONFIG_SYNO_AHCI_SWITCH */

#ifdef CONFIG_SYNO_PCI_OPTIONAL_SLOT
extern char gszPciAddrList[PCI_ADDR_NUM_MAX][PCI_ADDR_LEN_MAX];
extern int gPciAddrNum;
#endif /* CONFIG_SYNO_PCI_OPTIONAL_SLOT */

#ifdef CONFIG_SYNO_USB_CASTRATED_XHC
extern char gSynoCastratedXhcAddr[CONFIG_SYNO_USB_NUM_CASTRATED_XHC][32];
extern unsigned int gSynoCastratedXhcPortBitmap[CONFIG_SYNO_USB_NUM_CASTRATED_XHC];
#endif /* CONFIG_SYNO_USB_CASTRATED_XHC */

#ifdef CONFIG_SYNO_DISK_POWER_MANAGER
extern int g_syno_dpm_debug_level;
#endif /* CONFIG_SYNO_DISK_POWER_MANAGER */

#ifdef CONFIG_SYNO_SATA_SPINUP_GROUP
extern int giSynoSpinupGroupDebug;
static int __init early_syno_spinup_group_debug(char *p)
{
	giSynoSpinupGroupDebug = simple_strtol(p, NULL, 10);
	printk("SYNO Spinup Group Debug: %d\n", (int)giSynoSpinupGroupDebug);
	return 1;
}
__setup("syno_spinup_group_debug=", early_syno_spinup_group_debug);
#endif /* CONFIG_SYNO_SATA_SPINUP_GROUP */

#ifdef CONFIG_SYNO_MULTIPATH_FEATURE
extern long gIsMultipathModel;
#endif /* CONFIG_SYNO_MULTIPATH_FEATURE */

#ifdef CONFIG_SYNO_TTY_DISABLE
int gSynoTtyS0Enable = 0;
EXPORT_SYMBOL(gSynoTtyS0Enable);
#endif /* CONFIG_SYNO_TTY_DISABLE */

#ifdef CONFIG_SYNO_HW_VERSION
static int __init early_hw_version(char *p)
{
	snprintf(gszSynoHWVersion, 16, "%s", p);

	printk("Synology Hardware Version: %s\n", gszSynoHWVersion);

	return 1;
}
__setup("syno_hw_version=", early_hw_version);
#endif /* CONFIG_SYNO_HW_VERSION */

#ifdef CONFIG_SYNO_HW_REVISION
static int __init early_hw_revision(char *p)
{
       snprintf(gszSynoHWRevision, 4, "%s", p);

       printk("Synology Hardware Revision: %s\n", gszSynoHWRevision);

       return 1;
}
__setup("rev=", early_hw_revision);
#endif /* CONFIG_SYNO_HW_REVISION */

#ifdef  CONFIG_SYNO_INTERNAL_NETIF_NUM
static int __init early_internal_netif_num(char *p)
{
	g_internal_netif_num = simple_strtol(p, NULL, 10);

	if ( g_internal_netif_num >= 0 ) {
		printk("Internal netif num: %d\n", (int)g_internal_netif_num);
	}

	return 1;
}
__setup("netif_num=", early_internal_netif_num);
#endif /* CONFIG_SYNO_INTERNAL_NETIF_NUM */

#ifdef CONFIG_SYNO_MAC_ADDRESS
static int __init early_mac1(char *p)
{
	snprintf(grgbLanMac[0], sizeof(grgbLanMac[0]), "%s", p);

	printk("Mac1: %s\n", grgbLanMac[0]);

	return 1;
}
__setup("mac1=", early_mac1);

static int __init early_mac2(char *p)
{
	snprintf(grgbLanMac[1], sizeof(grgbLanMac[1]), "%s", p);

	printk("Mac2: %s\n", grgbLanMac[1]);

	return 1;
}
__setup("mac2=", early_mac2);

static int __init early_mac3(char *p)
{
	snprintf(grgbLanMac[2], sizeof(grgbLanMac[2]), "%s", p);

	printk("Mac3: %s\n", grgbLanMac[2]);

	return 1;
}
__setup("mac3=", early_mac3);

static int __init early_mac4(char *p)
{
	snprintf(grgbLanMac[3], sizeof(grgbLanMac[3]), "%s", p);

	printk("Mac4: %s\n", grgbLanMac[3]);

	return 1;
}
__setup("mac4=", early_mac4);

static int __init early_macs(char *p)
{
	int iMacCount = 0;
	char *pBegin = p;
	char *pEnd = strstr(pBegin, ",");

	while (NULL != pEnd && SYNO_MAC_MAX_NUMBER > iMacCount) {
		*pEnd = '\0';
		snprintf(grgbLanMac[iMacCount], sizeof(grgbLanMac[iMacCount]), "%s", pBegin);
		pBegin = pEnd + 1;
		pEnd = strstr(pBegin, ",");
		iMacCount++;
	}

	if ('\0' != *pBegin && SYNO_MAC_MAX_NUMBER > iMacCount) {
		snprintf(grgbLanMac[iMacCount], sizeof(grgbLanMac[iMacCount]), "%s", pBegin);
	}

	return 1;
}
__setup("macs=", early_macs);

static int __init early_vender_format_version(char *p)
{
	giVenderFormatVersion = simple_strtol(p, NULL, 10);

	printk("Vender format version: %d\n", giVenderFormatVersion);

	return 1;
}
__setup("vender_format_version=", early_vender_format_version);

static int __init early_skip_vender_mac_interfaces(char *p)
{
	snprintf(gszSkipVenderMacInterfaces, sizeof(gszSkipVenderMacInterfaces), "%s", p);

	printk("Skip vender mac interfaces: %s\n", gszSkipVenderMacInterfaces);

	return 1;
}
__setup("skip_vender_mac_interfaces=", early_skip_vender_mac_interfaces);
#endif /* CONFIG_SYNO_MAC_ADDRESS */

#ifdef CONFIG_SYNO_SERIAL
static int __init early_sn(char *p)
{
	snprintf(gszSerialNum, sizeof(gszSerialNum), "%s", p);
	printk("Serial Number: %s\n", gszSerialNum);
	return 1;
}
__setup("sn=", early_sn);

static int __init early_custom_sn(char *p)
{
	snprintf(gszCustomSerialNum, sizeof(gszCustomSerialNum), "%s", p);
	printk("Custom Serial Number: %s\n", gszCustomSerialNum);
	return 1;
}
__setup("custom_sn=", early_custom_sn);
#endif /* CONFIG_SYNO_SERIAL */

#ifdef CONFIG_SYNO_SAS
static int __init early_SASmodel(char *p)
{
	g_is_sas_model = simple_strtol(p, NULL, 10);

	if (1 == g_is_sas_model) {
		printk("SAS model: %d\n", (int)g_is_sas_model);
	}

	return 1;
}
__setup("SASmodel=", early_SASmodel);
#endif /* CONFIG_SYNO_SAS */

#ifdef CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS
static int __init early_syno_set_ttyS0(char *p)
{
	snprintf(gszSynoTtyS0, strlen(p) + 1, "%s", p);
	setup_early_printk(p);
	return 1;
}
__setup("syno_ttyS0=", early_syno_set_ttyS0);

static int __init early_syno_set_ttyS1(char *p)
{
	snprintf(gszSynoTtyS1, strlen(p) + 1, "%s", p);
	return 1;
}
__setup("syno_ttyS1=", early_syno_set_ttyS1);

static int __init early_syno_set_ttyS2(char *p)
{
	snprintf(gszSynoTtyS2, strlen(p) + 1, "%s", p);
	return 1;
}
__setup("syno_ttyS2=", early_syno_set_ttyS2);

#endif /* CONFIG_SYNO_TTY_FIX_TTYS_FUNCTIONS */

#ifdef CONFIG_SYNO_PCI_OPTIONAL_SLOT
static int __init early_opt_pci_slot(char *p)
{
	int index = 0;
	char *ptr = p;
	gPciAddrNum = 0;
	while(ptr && *ptr){
		if (',' ==  *ptr) {
			index = 0;
			gPciAddrNum ++;
			if (PCI_ADDR_NUM_MAX <= gPciAddrNum){
				goto FMT_ERR;
			}
		} else {
			if (PCI_ADDR_LEN_MAX <= index) {
				goto FMT_ERR;
			}
			gszPciAddrList[gPciAddrNum][index] = *ptr;
			index++;
		}
		ptr++;
	}
	gPciAddrNum ++;
	printk(KERN_ERR "Syno Bootargs : opt_pci_slot initialized\n");
	return 0;
FMT_ERR:
	gPciAddrNum = 0;
	printk(KERN_ERR "SYNO: opt_pci_slot format error, ignore.\n" );
	return 0;
}
__setup("opt_pci_slot=", early_opt_pci_slot);
#endif /* CONFIG_SYNO_PCI_OPTIONAL_SLOT */

#ifdef CONFIG_SYNO_SATA_PWR_CTRL_SMBUS
static int __init early_smbus_hdd_powerctl(char *p)
{
	g_smbus_hdd_powerctl = simple_strtol(p, NULL, 10);

	if ( g_smbus_hdd_powerctl > 0 ) {
		printk("Support SMBus HDD Dynamic Power Control.\n");
	}

	return 1;
}
__setup("SMBusHddDynamicPower=", early_smbus_hdd_powerctl);
#endif /* CONFIG_SYNO_SATA_PWR_CTRL_SMBUS */

#ifdef CONFIG_SYNO_USB_CASTRATED_XHC
static int __init early_castrated_xhc(char *p)
{
	int iCount = 0;
	char *pBegin = p;
	char *pEnd = strstr(pBegin, ",");
	char *pPortSep = NULL;

	while (iCount < CONFIG_SYNO_USB_NUM_CASTRATED_XHC) {
		if(NULL != pEnd)
			*pEnd = '\0';
		pPortSep = strstr(pBegin, "@");
		if (pPortSep == NULL) {
			printk("Castrated xHC - Parameter format not correct\n");
			break;
		}
		*pPortSep = '\0';
		snprintf(gSynoCastratedXhcAddr[iCount],
				sizeof(gSynoCastratedXhcAddr[iCount]), "%s", pBegin);
		gSynoCastratedXhcPortBitmap[iCount] = simple_strtoul(pPortSep + 1, NULL,
				16);
		if (NULL == pEnd)
			break;
		pBegin = pEnd + 1;
		pEnd = strstr(pBegin, ",");
		iCount++;
	}

	return 1;
}
__setup("syno_castrated_xhc=", early_castrated_xhc);
#endif /* CONFIG_SYNO_USB_CASTRATED_XHC */

#ifdef  CONFIG_SYNO_AHCI_SWITCH
static int __init early_ahci_switch(char *p)
{
	g_ahci_switch = p[0];
	if ('0' == g_ahci_switch) {
		printk("AHCI: 0\n");
	} else {
		printk("AHCI: 1\n");
	}

	return 1;
}
__setup("ahci=", early_ahci_switch);
#endif /* CONFIG_SYNO_AHCI_SWITCH */

#ifdef CONFIG_SYNO_DISK_POWER_MANAGER
static int __init early_syno_dpm_level(char *p)
{
	g_syno_dpm_debug_level = simple_strtol(p, NULL, 10);
	printk("Syno Dpm Log Level = %d\n", g_syno_dpm_debug_level);
	return 1;
}
__setup("syno_dpm_level=", early_syno_dpm_level);
#endif /* CONFIG_SYNO_DISK_POWER_MANAGER */

#ifdef CONFIG_SYNO_MULTIPATH_FEATURE
static int __init early_syno_multipath_model(char *p)
{
	gIsMultipathModel = simple_strtol(p, NULL, 10);

	printk("Is Multipath Model: %s\n", (int)gIsMultipathModel ? "Yes" : "No");
	return 1;
}
__setup("multipath_model=", early_syno_multipath_model);
#endif /* CONFIG_SYNO_MULTIPATH_FEATURE */

#ifdef CONFIG_SYNO_TTY_DISABLE
static int __init early_ttyS0_enable(char *p)
{
	gSynoTtyS0Enable = 1;
	printk("ttyS0: enabled via boot arg\n");
	return 0;
}
early_param("ttyS0_enable", early_ttyS0_enable);
#endif /* CONFIG_SYNO_TTY_DISABLE */
