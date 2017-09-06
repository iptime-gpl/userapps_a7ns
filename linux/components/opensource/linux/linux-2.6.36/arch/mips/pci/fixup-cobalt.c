/*
 * Cobalt Qube/Raq PCI support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2002, 2003 by Ralf Baechle
 * Copyright (C) 2001, 2002, 2003 by Liam Davies (ldavies@agile.tv)
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/pci.h>
#include <asm/io.h>
#include <asm/gt64120.h>

#include <cobalt.h>
#include <irq.h>

/*
 * PCI slot numbers
 */
#define COBALT_PCICONF_CPU	0x06
#define COBALT_PCICONF_ETH0	0x07
#define COBALT_PCICONF_RAQSCSI	0x08
#define COBALT_PCICONF_VIA	0x09
#define COBALT_PCICONF_PCISLOT	0x0A
#define COBALT_PCICONF_ETH1	0x0C

/*
 * The Cobalt board ID information.  The boards have an ID number wired
 * into the VIA that is available in the high nibble of register 94.
 */
#define VIA_COBALT_BRD_ID_REG  0x94
#define VIA_COBALT_BRD_REG_to_ID(reg)	((unsigned char)(reg) >> 4)

static void qube_raq_galileo_early_fixup(struct pci_dev *dev)
{
	if (dev->devfn == PCI_DEVFN(0, 0) &&
		(dev->class >> 8) == PCI_CLASS_MEMORY_OTHER) {

		dev->class = (PCI_CLASS_BRIDGE_HOST << 8) | (dev->class & 0xff);

		printk(KERN_INFO "Galileo: fixed bridge class\n");
	}
}

DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_MARVELL, PCI_DEVICE_ID_MARVELL_GT64111,
	 qube_raq_galileo_early_fixup);

static void __devinit cobalt_legacy_ide_resource_fixup(struct pci_dev *dev,
						       struct resource *res)
{
	struct pci_controller *hose = (struct pci_controller *)dev->sysdata;
	unsigned long offset = hose->io_offset;
	struct resource orig = *res;

	if (!(res->flags & IORESOURCE_IO) ||
	    !(res->flags & IORESOURCE_PCI_FIXED))
		return;

	res->start -= offset;
	res->end -= offset;
	dev_printk(KERN_DEBUG, &dev->dev, "converted legacy %pR to bus %pR\n",
		   &orig, res);
}

static void __devinit cobalt_legacy_ide_fixup(struct pci_dev *dev)
{
	u32 class;
	u8 progif;

	/*
	 * If the IDE controller is in legacy mode, pci_setup_device() fills in
	 * the resources with the legacy addresses that normally appear on the
	 * PCI bus, just as if we had read them from a BAR.
	 *
	 * However, with the GT-64111, those legacy addresses, e.g., 0x1f0,
	 * will never appear on the PCI bus because it converts memory accesses
	 * in the PCI I/O region (which is never at address zero) into I/O port
	 * accesses with no address translation.
	 *
	 * For example, if GT_DEF_PCI0_IO_BASE is 0x10000000, a load or store
	 * to physical address 0x100001f0 will become a PCI access to I/O port
	 * 0x100001f0.  There's no way to generate an access to I/O port 0x1f0,
	 * but the VT82C586 IDE controller does respond at 0x100001f0 because
	 * it only decodes the low 24 bits of the address.
	 *
	 * When this quirk runs, the pci_dev resources should contain bus
	 * addresses, not Linux I/O port numbers, so convert legacy addresses
	 * like 0x1f0 to bus addresses like 0x100001f0.  Later, we'll convert
	 * them back with pcibios_fixup_bus() or pcibios_bus_to_resource().
	 */
	class = dev->class >> 8;
	if (class != PCI_CLASS_STORAGE_IDE)
		return;

	pci_read_config_byte(dev, PCI_CLASS_PROG, &progif);
	if ((progif & 1) == 0) {
		cobalt_legacy_ide_resource_fixup(dev, &dev->resource[0]);
		cobalt_legacy_ide_resource_fixup(dev, &dev->resource[1]);
	}
	if ((progif & 4) == 0) {
		cobalt_legacy_ide_resource_fixup(dev, &dev->resource[2]);
		cobalt_legacy_ide_resource_fixup(dev, &dev->resource[3]);
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_1,
	  cobalt_legacy_ide_fixup);

static void qube_raq_via_bmIDE_fixup(struct pci_dev *dev)
{
	unsigned short cfgword;
	unsigned char lt;

	/* Enable Bus Mastering and fast back to back. */
	pci_read_config_word(dev, PCI_COMMAND, &cfgword);
	cfgword |= (PCI_COMMAND_FAST_BACK | PCI_COMMAND_MASTER);
	pci_write_config_word(dev, PCI_COMMAND, cfgword);

	/* Enable both ide interfaces. ROM only enables primary one.  */
	pci_write_config_byte(dev, 0x40, 0xb);

	/* Set latency timer to reasonable value. */
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lt);
	if (lt < 64)
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 8);
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_1,
	 qube_raq_via_bmIDE_fixup);

static void qube_raq_galileo_fixup(struct pci_dev *dev)
{
	if (dev->devfn != PCI_DEVFN(0, 0))
		return;

	/* Fix PCI latency-timer and cache-line-size values in Galileo
	 * host bridge.
	 */
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 8);

	/*
	 * The code described by the comment below has been removed
	 * as it causes bus mastering by the Ethernet controllers
	 * to break under any kind of network load. We always set
	 * the retry timeouts to their maximum.
	 *
	 * --x--x--x--x--x--x--x--x--x--x--x--x--x--x--x--x--x--x--x--x--
	 *
	 * On all machines prior to Q2, we had the STOP line disconnected
	 * from Galileo to VIA on PCI.  The new Galileo does not function
	 * correctly unless we have it connected.
	 *
	 * Therefore we must set the disconnect/retry cycle values to
	 * something sensible when using the new Galileo.
	 */

 	printk(KERN_INFO "Galileo: revision %u\n", dev->revision);

	{
		signed int timeo;
		timeo = GT_READ(GT_PCI0_TOR_OFS);
		/* Old Galileo, assumes PCI STOP line to VIA is disconnected. */
		GT_WRITE(GT_PCI0_TOR_OFS,
			(0xff << 16) |		/* retry count */
			(0xff << 8) |		/* timeout 1   */
			0xff);			/* timeout 0   */

		/* enable PCI retry exceeded interrupt */
		GT_WRITE(GT_INTRMASK_OFS, GT_INTR_RETRYCTR0_MSK | GT_READ(GT_INTRMASK_OFS));
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL, PCI_DEVICE_ID_MARVELL_GT64111,
	 qube_raq_galileo_fixup);

int cobalt_board_id;

static void qube_raq_via_board_id_fixup(struct pci_dev *dev)
{
	u8 id;
	int retval;

	retval = pci_read_config_byte(dev, VIA_COBALT_BRD_ID_REG, &id);
	if (retval) {
		panic("Cannot read board ID");
		return;
	}

	cobalt_board_id = VIA_COBALT_BRD_REG_to_ID(id);

	printk(KERN_INFO "Cobalt board ID: %d\n", cobalt_board_id);
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_0,
	 qube_raq_via_board_id_fixup);

static char irq_tab_qube1[] __initdata = {
  [COBALT_PCICONF_CPU]     = 0,
  [COBALT_PCICONF_ETH0]    = QUBE1_ETH0_IRQ,
  [COBALT_PCICONF_RAQSCSI] = SCSI_IRQ,
  [COBALT_PCICONF_VIA]     = 0,
  [COBALT_PCICONF_PCISLOT] = PCISLOT_IRQ,
  [COBALT_PCICONF_ETH1]    = 0
};

static char irq_tab_cobalt[] __initdata = {
  [COBALT_PCICONF_CPU]     = 0,
  [COBALT_PCICONF_ETH0]    = ETH0_IRQ,
  [COBALT_PCICONF_RAQSCSI] = SCSI_IRQ,
  [COBALT_PCICONF_VIA]     = 0,
  [COBALT_PCICONF_PCISLOT] = PCISLOT_IRQ,
  [COBALT_PCICONF_ETH1]    = ETH1_IRQ
};

static char irq_tab_raq2[] __initdata = {
  [COBALT_PCICONF_CPU]     = 0,
  [COBALT_PCICONF_ETH0]    = ETH0_IRQ,
  [COBALT_PCICONF_RAQSCSI] = RAQ2_SCSI_IRQ,
  [COBALT_PCICONF_VIA]     = 0,
  [COBALT_PCICONF_PCISLOT] = PCISLOT_IRQ,
  [COBALT_PCICONF_ETH1]    = ETH1_IRQ
};

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (cobalt_board_id <= COBALT_BRD_ID_QUBE1)
		return irq_tab_qube1[slot];

	if (cobalt_board_id == COBALT_BRD_ID_RAQ2)
		return irq_tab_raq2[slot];

	return irq_tab_cobalt[slot];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
