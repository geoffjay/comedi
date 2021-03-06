/*
    comedi/drivers/dt2817.c
    Hardware driver for Data Translation DT2817

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998 David A. Schleef <ds@schleef.org>

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

*/
/*
Driver: dt2817
Description: Data Translation DT2817
Author: ds
Status: complete
Devices: [Data Translation] DT2817 (dt2817)

A very simple digital I/O card.  Four banks of 8 lines, each bank
is configurable for input or output.  One wonders why it takes a
50 page manual to describe this thing.

The driver (which, btw, is much less than 50 pages) has 1 subdevice
with 32 channels, configurable in groups of 8.

Configuration options:
  [0] - I/O port base base address
*/

#include <linux/comedidev.h>

#include <linux/ioport.h>

#define DT2817_SIZE 5

#define DT2817_CR 0
#define DT2817_DATA 1

static int dt2817_attach(comedi_device * dev, comedi_devconfig * it);
static int dt2817_detach(comedi_device * dev);
static comedi_driver driver_dt2817 = {
      driver_name:"dt2817",
      module:THIS_MODULE,
      attach:dt2817_attach,
      detach:dt2817_detach,
};

COMEDI_INITCLEANUP(driver_dt2817);

static int dt2817_dio_insn_config(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data)
{
	int mask;
	int chan;
	int oe = 0;

	chan = CR_CHAN(insn->chanspec);
	if (chan < 8) {
		mask = 0xff;
	} else if (chan < 16) {
		mask = 0xff00;
	} else if (chan < 24) {
		mask = 0xff0000;
	} else
		mask = 0xff000000;

	switch (data[0]) {
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~mask;
		break;
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= mask;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & mask) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
	}

	if (s->io_bits & 0x000000ff)
		oe |= 0x1;
	if (s->io_bits & 0x0000ff00)
		oe |= 0x2;
	if (s->io_bits & 0x00ff0000)
		oe |= 0x4;
	if (s->io_bits & 0xff000000)
		oe |= 0x8;

	outb(oe, dev->iobase + DT2817_CR);

	return insn->n;
}

static int dt2817_dio_insn_bits(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data)
{
	unsigned int changed;

	/* It's questionable whether it is more important in
	 * a driver like this to be deterministic or fast. 
	 * We choose fast. */

	if (data[0]) {
		changed = s->state;
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
		changed ^= s->state;
		changed &= s->io_bits;
		if (changed & 0x000000ff)
			outb(s->state & 0xff, dev->iobase + DT2817_DATA + 0);
		if (changed & 0x0000ff00)
			outb((s->state >> 8) & 0xff,
				dev->iobase + DT2817_DATA + 1);
		if (changed & 0x00ff0000)
			outb((s->state >> 16) & 0xff,
				dev->iobase + DT2817_DATA + 2);
		if (changed & 0xff000000)
			outb((s->state >> 24) & 0xff,
				dev->iobase + DT2817_DATA + 3);
	}
	data[1] = inb(dev->iobase + DT2817_DATA + 0);
	data[1] |= (inb(dev->iobase + DT2817_DATA + 1) << 8);
	data[1] |= (inb(dev->iobase + DT2817_DATA + 2) << 16);
	data[1] |= (inb(dev->iobase + DT2817_DATA + 3) << 24);

	return 2;
}

static int dt2817_attach(comedi_device * dev, comedi_devconfig * it)
{
	int ret;
	comedi_subdevice *s;
	unsigned long iobase;

	iobase = it->options[0];
	printk("comedi%d: dt2817: 0x%04lx ", dev->minor, iobase);
	if (!request_region(iobase, DT2817_SIZE, "dt2817")) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;
	dev->board_name = "dt2817";

	if ((ret = alloc_subdevices(dev, 1)) < 0)
		return ret;

	s = dev->subdevices + 0;

	s->n_chan = 32;
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->range_table = &range_digital;
	s->maxdata = 1;
	s->insn_bits = dt2817_dio_insn_bits;
	s->insn_config = dt2817_dio_insn_config;

	s->state = 0;
	outb(0, dev->iobase + DT2817_CR);

	printk("\n");

	return 0;
}

static int dt2817_detach(comedi_device * dev)
{
	printk("comedi%d: dt2817: remove\n", dev->minor);

	if (dev->iobase)
		release_region(dev->iobase, DT2817_SIZE);

	return 0;
}
