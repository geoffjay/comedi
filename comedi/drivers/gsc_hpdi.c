/*
    gsc_hpdi.c
    This is a driver for the General Standards Corporation High
    Speed Parallel Digital Interface rs485 boards.

    Author:  Frank Mori Hess <fmhess@users.sourceforge.net>
    Copyright (C) 2003 Coherent Imaging Systems

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>

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

************************************************************************/

/*

Driver: gsc_hpdi.o
Description: Driver for the General Standards Corporation High
    Speed Parallel Digital Interface rs485 boards.
Author: Frank Mori Hess <fmhess@users.sourceforge.net>
Status: in development
Updated: 2003-02-14
Devices: [General Standards Corporation] PCI-HPDI32 (gsc_hpdi),
  PMC-HPDI32

Configuration options:
   [0] - PCI bus of device (optional)
   [1] - PCI slot of device (optional)

There are some additional hpdi models available from GSC for which
support could be added to this driver.

*/

#include <linux/comedidev.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "plx9080.h"
#include "comedi_fc.h"

static int hpdi_attach( comedi_device *dev, comedi_devconfig *it );
static int hpdi_detach( comedi_device *dev );
void abort_dma( comedi_device *dev, unsigned int channel );
static int hpdi_cmd( comedi_device *dev, comedi_subdevice *s );
static int hpdi_cmd_test( comedi_device *dev, comedi_subdevice *s, comedi_cmd *cmd );
static int hpdi_cancel( comedi_device *dev, comedi_subdevice *s );
static void handle_interrupt(int irq, void *d, struct pt_regs *regs);
static int dio_config_block_size( comedi_device *dev, lsampl_t *data );

#undef HPDI_DEBUG	// disable debugging messages
//#define HPDI_DEBUG	// enable debugging code

#ifdef HPDI_DEBUG
#define DEBUG_PRINT(format, args...)  rt_printk(format , ## args )
#else
#define DEBUG_PRINT(format, args...)
#endif

#define TIMER_BASE 50	// 20MHz master clock
#define DMA_BUFFER_SIZE 0x1000
/* maximum number of dma transfers we will chain together into a ring
 * (and the maximum number of dma buffers we maintain) */
#define DMA_RING_COUNT 64

// indices of base address regions
enum base_address_regions
{
	PLX9080_BADDRINDEX = 0,
	HPDI_BADDRINDEX = 2,
};

enum hpdi_registers
{
	FIRMWARE_REV_REG = 0x0,
	BOARD_CONTROL_REG = 0x4,
	BOARD_STATUS_REG = 0x8,
	TX_PROG_ALMOST_REG = 0xc,
	RX_PROG_ALMOST_REG = 0x10,
	FEATURES_REG = 0x14,
	FIFO_REG = 0x18,
	TX_STATUS_COUNT_REG = 0x1c,
	TX_LINE_VALID_COUNT_REG = 0x20,
	TX_LINE_INVALID_COUNT_REG = 0x24,
	RX_STATUS_COUNT_REG = 0x28,
	RX_LINE_COUNT_REG = 0x2c,
	INTERRUPT_CONTROL_REG = 0x30,
	INTERRUPT_STATUS_REG = 0x34,
	TX_CLOCK_DIVIDER_REG = 0x38,
	TX_FIFO_SIZE_REG = 0x40,
	RX_FIFO_SIZE_REG = 0x44,
	TX_FIFO_WORDS_REG = 0x48,
	RX_FIFO_WORDS_REG = 0x4c,
	INTERRUPT_EDGE_LEVEL_REG = 0x50,
	INTERRUPT_POLARITY_REG = 0x54,
};

int command_channel_valid( unsigned int channel )
{
	if( channel == 0 || channel > 6 )
	{
		rt_printk( "gsc_hpdi: bug! invalid cable command channel\n");
		return 0;
	}
	return 1;
}

// bit definitions

enum firmware_revision_bits
{
	FEATURES_REG_PRESENT_BIT = 0x8000,
};
int firmware_revision( uint32_t fwr_bits )
{
	return fwr_bits & 0xff;
}
int pcb_revision( uint32_t fwr_bits )
{
	return ( fwr_bits >> 8 ) & 0xff;
}
int hpdi_subid( uint32_t fwr_bits )
{
	return ( fwr_bits >> 16 ) & 0xff;
}

enum board_control_bits
{
	BOARD_RESET_BIT = 0x1,	/* wait 10usec before accessing fifos */
	TX_FIFO_RESET_BIT = 0x2,
	RX_FIFO_RESET_BIT = 0x4,
	TX_ENABLE_BIT = 0x10,
	RX_ENABLE_BIT = 0x20,
	DEMAND_DMA_DIRECTION_TX_BIT = 0x40, /* for channel 0, channel 1 can only transmit (when present) */
	LINE_VALID_ON_STATUS_VALID_BIT = 0x80,
	START_TX_BIT = 0x10,
	CABLE_THROTTLE_ENABLE_BIT = 0x20,
	TEST_MODE_ENABLE_BIT = 0x80000000,
};
uint32_t command_discrete_output_bits( unsigned int channel, int output, int output_value )
{
	uint32_t bits = 0;

	if( command_channel_valid( channel ) == 0 )
		return 0;
	if( output )
	{
		bits |= 0x1 << ( 16 + channel );
		if( output_value )
		bits |= 0x1 << ( 24 + channel );
	}else
		bits |= 0x1 << ( 24 + channel );

	return bits;
}

enum board_status_bits
{
	COMMAND_LINE_STATUS_MASK = 0x7f,
	TX_IN_PROGRESS_BIT = 0x80,
	TX_NOT_EMPTY_BIT = 0x100,
	TX_NOT_ALMOST_EMPTY_BIT = 0x200,
	TX_NOT_ALMOST_FULL_BIT = 0x400,
	TX_NOT_FULL_BIT = 0x800,
	RX_NOT_EMPTY_BIT = 0x1000,
	RX_NOT_ALMOST_EMPTY_BIT = 0x2000,
	RX_NOT_ALMOST_FULL_BIT = 0x4000,
	RX_NOT_FULL_BIT = 0x8000,
	BOARD_JUMPER0_INSTALLED_BIT = 0x10000,
	BOARD_JUMPER1_INSTALLED_BIT = 0x20000,
	TX_OVERRUN_BIT = 0x2000000,
	RX_UNDERRUN_BIT = 0x4000000,
	RX_OVERRUN_BIT = 0x8000000,
};

uint32_t almost_full_bits( unsigned int num_words )
{
// XXX need to add or subtract one?
	return ( num_words << 16 ) & 0xff0000;
}
uint32_t almost_empty_bits( unsigned int num_words )
{
	return num_words & 0xffff;
}
unsigned int almost_full_num_words( uint32_t bits )
{
// XXX need to add or subtract one?
	return ( bits >> 16 ) & 0xffff;
}
unsigned int almost_empty_num_words( uint32_t bits )
{
	return bits & 0xffff;
}

enum features_bits
{
	FIFO_SIZE_PRESENT_BIT = 0x1,
	FIFO_WORDS_PRESENT_BIT = 0x2,
	LEVEL_EDGE_INTERRUPTS_PRESENT_BIT = 0x4,
	GPIO_SUPPORTED_BIT = 0x8,
	PLX_DMA_CH1_SUPPORTED_BIT = 0x10,
	OVERRUN_UNDERRUN_SUPPORTED_BIT = 0x20,
};

enum interrupt_sources
{
	FRAME_VALID_START_INTR = 0,
	FRAME_VALID_END_INTR = 1,
	TX_FIFO_EMPTY_INTR = 8,
	TX_FIFO_ALMOST_EMPTY_INTR = 9,
	TX_FIFO_ALMOST_FULL_INTR = 10,
	TX_FIFO_FULL_INTR = 11,
	RX_EMPTY_INTR = 12,
	RX_ALMOST_EMPTY_INTR = 13,
	RX_ALMOST_FULL_INTR = 14,
	RX_FULL_INTR = 15,
};
int command_intr_source( unsigned int channel )
{
	if( command_channel_valid( channel ) == 0 )
		channel = 1;
	return channel + 1;
}
uint32_t intr_bit( int interrupt_source )
{
	return 0x1 << interrupt_source;
}

uint32_t tx_clock_divisor_bits( unsigned int divisor )
{
	return divisor & 0xff;
}

unsigned int fifo_size( uint32_t fifo_size_bits )
{
	return fifo_size_bits & 0xfffff;
}

unsigned int fifo_words( uint32_t fifo_words_bits )
{
	return fifo_words_bits & 0xfffff;
}

uint32_t intr_edge_bit( int interrupt_source )
{
	return 0x1 << interrupt_source;
}

uint32_t intr_active_high_bit( int interrupt_source )
{
	return 0x1 << interrupt_source;
}

typedef struct
{
	char *name;
	int device_id;	// pci device id
	int subdevice_id;	// pci subdevice id
} hpdi_board;

static const hpdi_board hpdi_boards[] =
{
	{
		name: "pci-hpdi32",
		device_id: PCI_DEVICE_ID_PLX_9080,
		subdevice_id: 0x2400,
	},
#if 0
	{
		name: "pxi-hpdi32",
		device_id: 0x9656,
		subdevice_id: 0x2705,
	},
#endif
};

static inline unsigned int num_boards( void )
{
	return sizeof( hpdi_boards ) / sizeof( hpdi_board );
}

static struct pci_device_id hpdi_pci_table[] __devinitdata = {
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9080, PCI_VENDOR_ID_PLX, 0x2400, 0, 0, 0 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, hpdi_pci_table);

static inline hpdi_board* board( const comedi_device *dev )
{
	return ( hpdi_board * ) dev->board_ptr;
}

typedef struct
{
	struct pci_dev *hw_dev;	// pointer to board's pci_dev struct
	// base addresses (physical)
	unsigned long plx9080_phys_iobase;
	unsigned long hpdi_phys_iobase;
	// base addresses (ioremapped)
	unsigned long plx9080_iobase;
	unsigned long hpdi_iobase;
	uint16_t *dio_buffer[ DMA_RING_COUNT ];	// dma buffers
	dma_addr_t dio_buffer_phys_addr[ DMA_RING_COUNT ];	// physical addresses of dma buffers
	struct plx_dma_desc *dma_desc;	// array of dma descriptors read by plx9080, allocated to get proper alignment
	dma_addr_t dma_desc_phys_addr;	// physical address of dma descriptor array
	volatile unsigned int dma_index;	// index of the dma descriptor/buffer that is currently being used
	unsigned int tx_fifo_size;
	unsigned int rx_fifo_size;
	volatile unsigned long dio_count;
	volatile uint32_t bits[ 24 ];	// software copies of values written to hpdi registers
	unsigned dio_config_output : 1;
} hpdi_private;

static inline hpdi_private* priv( comedi_device *dev )
{
	return dev->private;
}

static comedi_driver driver_hpdi =
{
	driver_name:	"gsc_hpdi",
	module:		THIS_MODULE,
	attach:		hpdi_attach,
	detach:		hpdi_detach,
};

COMEDI_INITCLEANUP( driver_hpdi );

static int dio_config_insn( comedi_device *dev,comedi_subdevice *s,
	comedi_insn *insn, lsampl_t *data)
{
	if( insn->n != 1 ) return -EINVAL;

	switch( data[ 0 ] )
	{
		case COMEDI_OUTPUT:
			priv(dev)->dio_config_output = 1;
			return 1;
			break;
		case COMEDI_INPUT:
			priv(dev)->dio_config_output = 0;
			return 1;
			break;
		case INSN_CONFIG_BLOCK_SIZE:
			return dio_config_block_size( dev, data );
			break;
		default:
			break;
	}

	return -EINVAL;
}

static void disable_plx_interrupts( comedi_device *dev )
{
	writel( 0, priv(dev)->plx9080_iobase + PLX_INTRCS_REG );
}

// initialize plx9080 chip
static void init_plx9080(comedi_device *dev)
{
	uint32_t bits;
	unsigned long plx_iobase = priv(dev)->plx9080_iobase;

	// plx9080 dump
	DEBUG_PRINT(" plx interrupt status 0x%x\n", readl(plx_iobase + PLX_INTRCS_REG));
	DEBUG_PRINT(" plx id bits 0x%x\n", readl(plx_iobase + PLX_ID_REG));
	DEBUG_PRINT(" plx control reg 0x%x\n", readl(priv(dev)->plx9080_iobase + PLX_CONTROL_REG));

	DEBUG_PRINT(" plx revision 0x%x\n", readl(plx_iobase + PLX_REVISION_REG));
	DEBUG_PRINT(" plx dma channel 0 mode 0x%x\n", readl(plx_iobase + PLX_DMA0_MODE_REG));
	DEBUG_PRINT(" plx dma channel 1 mode 0x%x\n", readl(plx_iobase + PLX_DMA1_MODE_REG));
	DEBUG_PRINT(" plx dma channel 0 pci address 0x%x\n", readl(plx_iobase + PLX_DMA0_PCI_ADDRESS_REG));
	DEBUG_PRINT(" plx dma channel 0 local address 0x%x\n", readl(plx_iobase + PLX_DMA0_LOCAL_ADDRESS_REG));
	DEBUG_PRINT(" plx dma channel 0 transfer size 0x%x\n", readl(plx_iobase + PLX_DMA0_TRANSFER_SIZE_REG));
	DEBUG_PRINT(" plx dma channel 0 descriptor 0x%x\n", readl(plx_iobase + PLX_DMA0_DESCRIPTOR_REG));
	DEBUG_PRINT(" plx dma channel 0 command status 0x%x\n", readb(plx_iobase + PLX_DMA0_CS_REG));
	DEBUG_PRINT(" plx dma channel 0 threshold 0x%x\n", readl(plx_iobase + PLX_DMA0_THRESHOLD_REG));

	disable_plx_interrupts( dev );

	abort_dma(dev, 0);
	abort_dma(dev, 1);

	// configure dma0 mode
	bits = 0;
	// enable ready input
	bits |= PLX_DMA_EN_READYIN_BIT;
	// enable dma chaining
	bits |= PLX_EN_CHAIN_BIT;
	// enable interrupt on dma done (probably don't need this, since chain never finishes)
	bits |= PLX_EN_DMA_DONE_INTR_BIT;
	// don't increment local address during transfers (we are transferring from a fixed fifo register)
	bits |= PLX_LOCAL_ADDR_CONST_BIT;
	// route dma interrupt to pci bus
	bits |= PLX_DMA_INTR_PCI_BIT;
	// enable demand mode
	bits |= PLX_DEMAND_MODE_BIT;
	// enable local burst mode
	bits |= PLX_DMA_LOCAL_BURST_EN_BIT;
	bits |= PLX_LOCAL_BUS_32_WIDE_BITS;
	writel(bits, plx_iobase + PLX_DMA0_MODE_REG);
}

/* Allocate and initialize the subdevice structures.
 */
static int setup_subdevices(comedi_device *dev)
{
	comedi_subdevice *s;

	if( alloc_subdevices( dev, 1 ) < 0 )
		return -ENOMEM;

	s = dev->subdevices + 0;
	/* analog input subdevice */
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITEABLE | SDF_LSAMPL;
	s->n_chan = 32;
	s->len_chanlist = 32;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_config = dio_config_insn;
	s->do_cmd = hpdi_cmd;
	s->do_cmdtest = hpdi_cmd_test;
	s->cancel = hpdi_cancel;

	return 0;
}

static int init_hpdi( comedi_device *dev )
{
	uint32_t plx_intcsr_bits;

	writel( BOARD_RESET_BIT, priv(dev)->hpdi_iobase + BOARD_CONTROL_REG );
	udelay( 10 );

	priv(dev)->tx_fifo_size = fifo_size( readl( priv(dev)->hpdi_iobase +
		TX_FIFO_SIZE_REG ) );
	priv(dev)->rx_fifo_size = fifo_size( readl( priv(dev)->hpdi_iobase +
		RX_FIFO_SIZE_REG ) );

	// enable interrupts
	plx_intcsr_bits = ICS_AERR | ICS_PERR | ICS_PIE | ICS_PLIE | ICS_PAIE | ICS_LIE | ICS_DMA0_E;
	writel( plx_intcsr_bits, priv(dev)->plx9080_iobase + PLX_INTRCS_REG );

	return 0;
}

static int hpdi_attach(comedi_device *dev, comedi_devconfig *it)
{
	struct pci_dev* pcidev;
	int i;
	int retval;

	printk( "comedi%d: gsc_hpdi\n", dev->minor );

	if( alloc_private( dev, sizeof( hpdi_private ) ) < 0 )
		return -ENOMEM;

	pcidev = NULL;
	for( i = 0; i < num_boards() && dev->board_ptr == NULL; i++ )
	{
		do
		{
			pcidev = pci_find_subsys( PCI_VENDOR_ID_PLX, hpdi_boards[ i ].device_id,
				PCI_VENDOR_ID_PLX, hpdi_boards[ i ].subdevice_id, pcidev );
			// was a particular bus/slot requested?
			if( it->options[0] || it->options[1] )
			{
				// are we on the wrong bus/slot?
				if( pcidev->bus->number != it->options[0] ||
					PCI_SLOT( pcidev->devfn ) != it->options[1] )
					continue;
			}
			if( pcidev )
			{
				dev->board_ptr = hpdi_boards + i;
				break;
			}
		}while( pcidev != NULL );
	}
	if( i == num_boards() )
	{
		printk("gsc_hpdi: no hpdi card found\n");
		return -EIO;
	}

	printk("gsc_hpdi: found %s on bus %i, slot %i\n", board( dev )->name,
		pcidev->bus->number, PCI_SLOT(pcidev->devfn));

	if( pci_enable_device( pcidev ) )
		return -EIO;
	pci_set_master( pcidev );

	priv(dev)->hw_dev = pcidev;

	//Initialize dev->board_name
	dev->board_name = board(dev)->name;

	if( pci_request_regions( pcidev, driver_hpdi.driver_name ) )
	{
		/* Couldn't allocate io space */
		printk(KERN_WARNING " failed to allocate io memory\n");
		return -EIO;
	}

	priv(dev)->plx9080_phys_iobase = pci_resource_start(pcidev, PLX9080_BADDRINDEX);
	priv(dev)->hpdi_phys_iobase = pci_resource_start(pcidev, HPDI_BADDRINDEX);

	// remap, won't work with 2.0 kernels but who cares
	priv(dev)->plx9080_iobase = (unsigned long) ioremap( priv(dev)->plx9080_phys_iobase,
		pci_resource_len( pcidev, PLX9080_BADDRINDEX ) );
	priv(dev)->hpdi_iobase = (unsigned long) ioremap( priv(dev)->hpdi_phys_iobase,
		pci_resource_len( pcidev, HPDI_BADDRINDEX ) );

	DEBUG_PRINT(" plx9080 remapped to 0x%lx\n", priv(dev)->plx9080_iobase);
	DEBUG_PRINT(" hpdi remapped to 0x%lx\n", priv(dev)->hpdi_iobase);

	// get irq
	if( comedi_request_irq( pcidev->irq, handle_interrupt, SA_SHIRQ, driver_hpdi.driver_name,
		 dev ) )
	{
		printk( " unable to allocate irq %d\n", pcidev->irq );
		return -EINVAL;
	}
	dev->irq = pcidev->irq;

	printk(" irq %i\n", dev->irq);

	init_plx9080(dev);

	// alocate pci dma buffers
	for( i = 0; i < DMA_RING_COUNT; i++ )
	{
		priv(dev)->dio_buffer[ i ] = pci_alloc_consistent( priv(dev)->hw_dev,
			DMA_BUFFER_SIZE, &priv(dev)->dio_buffer_phys_addr[ i ] );
	}
	// allocate dma descriptors
	priv(dev)->dma_desc = pci_alloc_consistent( priv(dev)->hw_dev,
		sizeof( struct plx_dma_desc ) * DMA_RING_COUNT,
		&priv(dev)->dma_desc_phys_addr);
	// initialize dma descriptors
	for( i = 0; i < DMA_RING_COUNT; i++ )
	{
		priv(dev)->dma_desc[ i ].pci_start_addr = priv(dev)->dio_buffer_phys_addr[ i ];
		priv(dev)->dma_desc[ i ].local_start_addr = FIFO_REG;
		priv(dev)->dma_desc[ i ].transfer_size = DMA_BUFFER_SIZE;
		priv(dev)->dma_desc[ i ].next = ( priv(dev)->dma_desc_phys_addr +
			( ( i + 1 ) % ( DMA_RING_COUNT ) ) * sizeof( priv(dev)->dma_desc[ 0 ] ) ) |
			PLX_DESC_IN_PCI_BIT | PLX_INTR_TERM_COUNT | PLX_XFER_LOCAL_TO_PCI;
	}

	retval = setup_subdevices( dev );
	if( retval < 0 )
		return retval;

	return init_hpdi( dev );
}

static int hpdi_detach(comedi_device *dev)
{
	unsigned int i;

	printk( "comedi%d: gsc_hpdi: remove\n", dev->minor );

	if( dev->irq )
		comedi_free_irq( dev->irq, dev );
	if( priv(dev) )
	{
		if( priv(dev)->hw_dev )
		{
			if(priv(dev)->plx9080_iobase)
			{
				disable_plx_interrupts( dev );
				iounmap( (void*) priv(dev)->plx9080_iobase );
			}
			if( priv(dev)->hpdi_iobase )
				iounmap((void*)priv(dev)->hpdi_iobase);
			if( priv(dev)->plx9080_phys_iobase ||
				priv(dev)->hpdi_phys_iobase )
				pci_release_regions( priv(dev)->hw_dev );
			// free pci dma buffers
			for( i = 0; i < DMA_RING_COUNT; i++ )
			{
				if( priv(dev)->dio_buffer[ i ] )
					pci_free_consistent( priv(dev)->hw_dev, DMA_BUFFER_SIZE,
						priv(dev)->dio_buffer[ i ], priv(dev)->dio_buffer_phys_addr[ i ] );
			}
			// free dma descriptors
			if( priv(dev)->dma_desc )
				pci_free_consistent( priv(dev)->hw_dev, sizeof( struct plx_dma_desc ) *
				DMA_RING_COUNT, priv(dev)->dma_desc, priv(dev)->dma_desc_phys_addr );
			pci_disable_device( priv(dev)->hw_dev );
		}
	}

	return 0;
}

static int dio_config_block_size( comedi_device *dev, lsampl_t *data )
{
	unsigned int block_size, requested_block_size;

	requested_block_size = data[ 1 ];

	block_size = requested_block_size;

	data[ 1 ] = block_size;

	return 2;
}

static int di_cmd_test(comedi_device *dev,comedi_subdevice *s, comedi_cmd *cmd)
{
	int err = 0;
	int tmp;
	int i;

	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if( !cmd->start_src || tmp != cmd->start_src ) err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_EXT;
	if( !cmd->scan_begin_src || tmp != cmd->scan_begin_src ) err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_NOW;
	if( !cmd->convert_src || tmp != cmd->convert_src ) err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if( !cmd->scan_end_src || tmp != cmd->scan_end_src ) err++;

	tmp=cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if( !cmd->stop_src || tmp != cmd->stop_src ) err++;

	if(err) return 1;

	/* step 2: make sure trigger sources are unique and mutually compatible */

	// uniqueness check
	if(cmd->stop_src != TRIG_COUNT &&
		cmd->stop_src != TRIG_NONE ) err++;

	if(err) return 2;

	/* step 3: make sure arguments are trivially compatible */

	if( !cmd->chanlist_len )
	{
		cmd->chanlist_len = 32;
		err++;
	}
	if( cmd->scan_end_arg != cmd->chanlist_len )
	{
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	switch(cmd->stop_src)
	{
		case TRIG_COUNT:
			if(!cmd->stop_arg)
			{
				cmd->stop_arg = 1;
				err++;
			}
			break;
		case TRIG_NONE:
			if(cmd->stop_arg != 0)
			{
				cmd->stop_arg = 0;
				err++;
			}
			break;
		default:
			break;
	}

	if(err) return 3;

	/* step 4: fix up any arguments */

	if(err) return 4;

	if( cmd->chanlist )
	{
		for( i = 1; i < cmd->chanlist_len; i++ )
		{
			if( CR_CHAN( cmd->chanlist[ i ] ) != i )
			{
				// XXX could support 8 channels or 16 channels
				comedi_error( dev, "chanlist must be channels 0 to 31 in order" );
				err++;
				break;
			}
		}
	}

	if(err) return 5;

	return 0;
}

static int hpdi_cmd_test( comedi_device *dev, comedi_subdevice *s, comedi_cmd *cmd )
{
	if( priv(dev)->dio_config_output )
	{
		return -EINVAL;
	}else
		return di_cmd_test( dev, s, cmd );
}

static inline void hpdi_writel( comedi_device *dev, uint32_t bits, unsigned int offset )
{
	writel( bits | priv(dev)->bits[ offset / sizeof( uint32_t ) ],
		priv(dev)->hpdi_iobase + offset );
}

static int di_cmd(comedi_device *dev,comedi_subdevice *s)
{
	uint32_t bits;
	unsigned long flags;

	hpdi_writel( dev, RX_FIFO_RESET_BIT, BOARD_CONTROL_REG );

	priv(dev)->dma_index = 0;

	abort_dma(dev, 1);

	// give location of first dma descriptor
	bits = priv(dev)->dma_desc_phys_addr | PLX_DESC_IN_PCI_BIT | PLX_INTR_TERM_COUNT | PLX_XFER_LOCAL_TO_PCI;
	writel( bits, priv(dev)->plx9080_iobase + PLX_DMA0_DESCRIPTOR_REG );

	// spinlock for plx dma control/status reg
	comedi_spin_lock_irqsave( &dev->spinlock, flags );
	// enable dma transfer
	writeb(PLX_DMA_EN_BIT | PLX_DMA_START_BIT | PLX_CLEAR_DMA_INTR_BIT, priv(dev)->plx9080_iobase + PLX_DMA0_CS_REG);
	comedi_spin_unlock_irqrestore( &dev->spinlock, flags );

	hpdi_writel( dev, RX_ENABLE_BIT, BOARD_CONTROL_REG );

	return 0;
}

static int hpdi_cmd( comedi_device *dev, comedi_subdevice *s )
{
	if( priv(dev)->dio_config_output )
	{
		return -EINVAL;
	}else
		return di_cmd( dev, s );
}

static void drain_dma_buffers(comedi_device *dev, unsigned int channel)
{
	comedi_async *async = dev->read_subdev->async;
	uint32_t next_transfer_addr;
	int j;
	int num_samples = 0;
	unsigned long pci_addr_reg;

	if(channel)
		pci_addr_reg = priv(dev)->plx9080_iobase + PLX_DMA1_PCI_ADDRESS_REG;
	else
		pci_addr_reg = priv(dev)->plx9080_iobase + PLX_DMA0_PCI_ADDRESS_REG;

	// loop until we have read all the full buffers
	j = 0;
	for(next_transfer_addr = readl(pci_addr_reg);
		(next_transfer_addr < priv(dev)->dio_buffer_phys_addr[priv(dev)->dma_index] ||
		next_transfer_addr >= priv(dev)->dio_buffer_phys_addr[priv(dev)->dma_index] + DMA_BUFFER_SIZE) &&
		j < DMA_RING_COUNT;
		j++ )
	{
		// transfer data from dma buffer to comedi buffer
		num_samples = DMA_BUFFER_SIZE / sizeof( uint32_t );
		if( async->cmd.stop_src == TRIG_COUNT )
		{
			if(num_samples > priv(dev)->dio_count)
				num_samples = priv(dev)->dio_count;
			priv(dev)->dio_count -= num_samples;
		}
		cfc_write_array_to_buffer( dev->read_subdev,
			priv(dev)->dio_buffer[ priv(dev)->dma_index ], num_samples * sizeof( sampl_t ) );
		priv(dev)->dma_index = (priv(dev)->dma_index + 1) % DMA_RING_COUNT;

		DEBUG_PRINT("next buffer addr 0x%lx\n", (unsigned long) priv(dev)->ai_buffer_phys_addr[priv(dev)->dma_index]);
		DEBUG_PRINT("pci addr reg 0x%x\n", next_transfer_addr);
	}
	// XXX check for buffer overrun somehow
}

static void handle_interrupt(int irq, void *d, struct pt_regs *regs)
{
	comedi_device *dev = d;
	comedi_subdevice *s = dev->read_subdev;
	comedi_async *async = s->async;
	uint32_t hpdi_intr_status, hpdi_board_status;
	uint32_t plx_status;
	uint32_t plx_bits;
	uint8_t dma0_status, dma1_status;
	unsigned long flags;

	plx_status = readl( priv(dev)->plx9080_iobase + PLX_INTRCS_REG );
	hpdi_intr_status = readl( priv(dev)->hpdi_iobase + INTERRUPT_STATUS_REG );
	hpdi_board_status = readl( priv(dev)->hpdi_iobase + BOARD_STATUS_REG );

	DEBUG_PRINT("hpdi: intr status 0x%x, ", hpdi_intr_status);
	DEBUG_PRINT("board status 0x%x, ", hpdi_board_status);
	DEBUG_PRINT("plx status 0x%x\n", plx_status);

	async->events = 0;

	// spin lock makes sure noone else changes plx dma control reg
	comedi_spin_lock_irqsave( &dev->spinlock, flags );
	dma0_status = readb(priv(dev)->plx9080_iobase + PLX_DMA0_CS_REG);
	if(plx_status & ICS_DMA0_A)
	{	// dma chan 0 interrupt
		writeb((dma0_status & PLX_DMA_EN_BIT) | PLX_CLEAR_DMA_INTR_BIT, priv(dev)->plx9080_iobase + PLX_DMA0_CS_REG);

		DEBUG_PRINT("dma0 status 0x%x\n", dma0_status);
		if(dma0_status & PLX_DMA_EN_BIT)
		{
			drain_dma_buffers(dev, 0);
		}
		DEBUG_PRINT(" cleared dma ch0 interrupt\n");
	}
	comedi_spin_unlock_irqrestore( &dev->spinlock, flags );

	// spin lock makes sure noone else changes plx dma control reg
	comedi_spin_lock_irqsave( &dev->spinlock, flags );
	dma1_status = readb(priv(dev)->plx9080_iobase + PLX_DMA1_CS_REG);
	if(plx_status & ICS_DMA1_A)	// XXX
	{	// dma chan 1 interrupt
		writeb((dma1_status & PLX_DMA_EN_BIT) | PLX_CLEAR_DMA_INTR_BIT, priv(dev)->plx9080_iobase + PLX_DMA1_CS_REG);
		DEBUG_PRINT("dma1 status 0x%x\n", dma1_status);

		DEBUG_PRINT(" cleared dma ch1 interrupt\n");
	}
	comedi_spin_unlock_irqrestore( &dev->spinlock, flags );

	// clear possible plx9080 interrupt sources
	if(plx_status & ICS_LDIA)
	{ // clear local doorbell interrupt
		plx_bits = readl(priv(dev)->plx9080_iobase + PLX_DBR_OUT_REG);
		writel(plx_bits, priv(dev)->plx9080_iobase + PLX_DBR_OUT_REG);
		DEBUG_PRINT(" cleared local doorbell bits 0x%x\n", plx_bits);
	}

	cfc_handle_events( dev, s );

	DEBUG_PRINT("exiting handler\n");

	return;
}

void abort_dma( comedi_device *dev, unsigned int channel )
{
	unsigned long flags;

	// spinlock for plx dma control/status reg
	comedi_spin_lock_irqsave( &dev->spinlock, flags );

	plx9080_abort_dma( priv( dev )->plx9080_iobase, channel );

	comedi_spin_unlock_irqrestore( &dev->spinlock, flags );
}

static int hpdi_cancel( comedi_device *dev, comedi_subdevice *s )
{
	hpdi_writel( dev, 0, BOARD_CONTROL_REG );

	abort_dma(dev, 1);

	return 0;
}

