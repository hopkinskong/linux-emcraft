/*
    comedi/drivers/das08_cs.c
    DAS08 driver

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>
    Copyright (C) 2001,2002,2003 Frank Mori Hess <fmhess@users.sourceforge.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*****************************************************************

*/
/*
Driver: das08_cs
Description: DAS-08 PCMCIA boards
Author: Warren Jasper, ds, Frank Hess
Devices: [ComputerBoards] PCM-DAS08 (pcm-das08)
Status: works

This is the PCMCIA-specific support split off from the
das08 driver.

Options (for pcm-das08):
        NONE

Command support does not exist, but could be added for this board.
*/

#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/pci.h>

#include "das08.h"

/* pcmcia includes */
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

static struct pcmcia_device *cur_dev = NULL;

#define thisboard ((const struct das08_board_struct *)dev->board_ptr)

static int das08_cs_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it);

static struct comedi_driver driver_das08_cs = {
	.driver_name = "das08_cs",
	.module = THIS_MODULE,
	.attach = das08_cs_attach,
	.detach = das08_common_detach,
	.board_name = &das08_cs_boards[0].name,
	.num_names = ARRAY_SIZE(das08_cs_boards),
	.offset = sizeof(struct das08_board_struct),
};

static int das08_cs_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	int ret;
	unsigned long iobase;
	struct pcmcia_device *link = cur_dev;	/*  XXX hack */

	ret = alloc_private(dev, sizeof(struct das08_private_struct));
	if (ret < 0)
		return ret;

	printk("comedi%d: das08_cs: ", dev->minor);
	/*  deal with a pci board */

	if (thisboard->bustype == pcmcia) {
		if (link == NULL) {
			printk(" no pcmcia cards found\n");
			return -EIO;
		}
		iobase = link->io.BasePort1;
	} else {
		printk(" bug! board does not have PCMCIA bustype\n");
		return -EINVAL;
	}

	printk("\n");

	return das08_common_attach(dev, iobase);
}

/*======================================================================

    The following pcmcia code for the pcm-das08 is adapted from the
    dummy_cs.c driver of the Linux PCMCIA Card Services package.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

======================================================================*/

static void das08_pcmcia_config(struct pcmcia_device *link);
static void das08_pcmcia_release(struct pcmcia_device *link);
static int das08_pcmcia_suspend(struct pcmcia_device *p_dev);
static int das08_pcmcia_resume(struct pcmcia_device *p_dev);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static int das08_pcmcia_attach(struct pcmcia_device *);
static void das08_pcmcia_detach(struct pcmcia_device *);

/*
   You'll also need to prototype all the functions that will actually
   be used to talk to your device.  See 'memory_cs' for a good example
   of a fully self-sufficient driver; the other drivers rely more or
   less on other parts of the kernel.
*/

/*
   The dev_info variable is the "key" that is used to match up this
   device driver with appropriate cards, through the card configuration
   database.
*/

static const dev_info_t dev_info = "pcm-das08";

struct local_info_t {
	struct pcmcia_device *link;
	dev_node_t node;
	int stop;
	struct bus_operations *bus;
};

/*======================================================================

    das08_pcmcia_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.

======================================================================*/

static int das08_pcmcia_attach(struct pcmcia_device *link)
{
	struct local_info_t *local;

	dev_dbg(&link->dev, "das08_pcmcia_attach()\n");

	/* Allocate space for private device-specific data */
	local = kzalloc(sizeof(struct local_info_t), GFP_KERNEL);
	if (!local)
		return -ENOMEM;
	local->link = link;
	link->priv = local;

	/* Interrupt setup */
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
	link->irq.Handler = NULL;

	/*
	   General socket configuration defaults can go here.  In this
	   client, we assume very little, and rely on the CIS for almost
	   everything.  In most clients, many details (i.e., number, sizes,
	   and attributes of IO windows) are fixed by the nature of the
	   device, and can be hard-wired here.
	 */
	link->conf.Attributes = 0;
	link->conf.IntType = INT_MEMORY_AND_IO;

	cur_dev = link;

	das08_pcmcia_config(link);

	return 0;
}				/* das08_pcmcia_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void das08_pcmcia_detach(struct pcmcia_device *link)
{

	dev_dbg(&link->dev, "das08_pcmcia_detach\n");

	if (link->dev_node) {
		((struct local_info_t *)link->priv)->stop = 1;
		das08_pcmcia_release(link);
	}

	/* This points to the parent struct local_info_t struct */
	if (link->priv)
		kfree(link->priv);

}				/* das08_pcmcia_detach */


static int das08_pcmcia_config_loop(struct pcmcia_device *p_dev,
				cistpl_cftable_entry_t *cfg,
				cistpl_cftable_entry_t *dflt,
				unsigned int vcc,
				void *priv_data)
{
	if (cfg->index == 0)
		return -ENODEV;

	/* Do we need to allocate an interrupt? */
	if (cfg->irq.IRQInfo1 || dflt->irq.IRQInfo1)
		p_dev->conf.Attributes |= CONF_ENABLE_IRQ;

	/* IO window settings */
	p_dev->io.NumPorts1 = p_dev->io.NumPorts2 = 0;
	if ((cfg->io.nwin > 0) || (dflt->io.nwin > 0)) {
		cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt->io;
		p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
		if (!(io->flags & CISTPL_IO_8BIT))
			p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
		if (!(io->flags & CISTPL_IO_16BIT))
			p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
		p_dev->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
		p_dev->io.BasePort1 = io->win[0].base;
		p_dev->io.NumPorts1 = io->win[0].len;
		if (io->nwin > 1) {
			p_dev->io.Attributes2 = p_dev->io.Attributes1;
			p_dev->io.BasePort2 = io->win[1].base;
			p_dev->io.NumPorts2 = io->win[1].len;
		}
		/* This reserves IO space but doesn't actually enable it */
		return pcmcia_request_io(p_dev, &p_dev->io);
	}
	return 0;
}


/*======================================================================

    das08_pcmcia_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

======================================================================*/

static void das08_pcmcia_config(struct pcmcia_device *link)
{
	struct local_info_t *dev = link->priv;
	int ret;

	dev_dbg(&link->dev, "das08_pcmcia_config\n");

	ret = pcmcia_loop_config(link, das08_pcmcia_config_loop, NULL);
	if (ret) {
		dev_warn(&link->dev, "no configuration found\n");
		goto failed;
	}

	if (link->conf.Attributes & CONF_ENABLE_IRQ) {
		ret = pcmcia_request_irq(link, &link->irq);
		if (ret)
			goto failed;
	}

	/*
	   This actually configures the PCMCIA socket -- setting up
	   the I/O windows and the interrupt mapping, and putting the
	   card and host interface into "Memory and IO" mode.
	 */
	ret = pcmcia_request_configuration(link, &link->conf);
	if (ret)
		goto failed;

	/*
	   At this point, the dev_node_t structure(s) need to be
	   initialized and arranged in a linked list at link->dev.
	 */
	sprintf(dev->node.dev_name, "pcm-das08");
	dev->node.major = dev->node.minor = 0;
	link->dev_node = &dev->node;

	/* Finally, report what we've done */
	printk(KERN_INFO "%s: index 0x%02x",
	       dev->node.dev_name, link->conf.ConfigIndex);
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk(", irq %u", link->irq.AssignedIRQ);
	if (link->io.NumPorts1)
		printk(", io 0x%04x-0x%04x", link->io.BasePort1,
		       link->io.BasePort1 + link->io.NumPorts1 - 1);
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2,
		       link->io.BasePort2 + link->io.NumPorts2 - 1);
	printk("\n");

	return;

failed:
	das08_pcmcia_release(link);

}				/* das08_pcmcia_config */

/*======================================================================

    After a card is removed, das08_pcmcia_release() will unregister the
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.

======================================================================*/

static void das08_pcmcia_release(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "das08_pcmcia_release\n");
	pcmcia_disable_device(link);
}				/* das08_pcmcia_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.

    When a CARD_REMOVAL event is received, we immediately set a
    private flag to block future accesses to this device.  All the
    functions that actually access the device should check this flag
    to make sure the card is still present.

======================================================================*/

static int das08_pcmcia_suspend(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;
	/* Mark the device as stopped, to block IO until later */
	local->stop = 1;

	return 0;
}				/* das08_pcmcia_suspend */

static int das08_pcmcia_resume(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	local->stop = 0;
	return 0;
}				/* das08_pcmcia_resume */

/*====================================================================*/

static struct pcmcia_device_id das08_cs_id_table[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01c5, 0x4001),
	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, das08_cs_id_table);

struct pcmcia_driver das08_cs_driver = {
	.probe = das08_pcmcia_attach,
	.remove = das08_pcmcia_detach,
	.suspend = das08_pcmcia_suspend,
	.resume = das08_pcmcia_resume,
	.id_table = das08_cs_id_table,
	.owner = THIS_MODULE,
	.drv = {
		.name = dev_info,
		},
};

static int __init init_das08_pcmcia_cs(void)
{
	pcmcia_register_driver(&das08_cs_driver);
	return 0;
}

static void __exit exit_das08_pcmcia_cs(void)
{
	pr_debug("das08_pcmcia_cs: unloading\n");
	pcmcia_unregister_driver(&das08_cs_driver);
}

static int __init das08_cs_init_module(void)
{
	int ret;

	ret = init_das08_pcmcia_cs();
	if (ret < 0)
		return ret;

	return comedi_driver_register(&driver_das08_cs);
}

static void __exit das08_cs_exit_module(void)
{
	exit_das08_pcmcia_cs();
	comedi_driver_unregister(&driver_das08_cs);
}

MODULE_LICENSE("GPL");
module_init(das08_cs_init_module);
module_exit(das08_cs_exit_module);
