/*

   comedi/drivers/me_daq.c

   Hardware driver for Meilhaus data acquisition cards:

     ME-2000i, ME-2600i, ME-3000vm1

   Copyright (C) 2002 Michael Hillmann <hillmann@syscongroup.de>

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
Driver: me_daq.o
Description: Driver for the Meilhaus PCI data acquisition cards.
Author: Michael Hillmann <hillmann@syscongroup.de>
Devices: [Meilhaus] ME-2600i, ME-2000i (me_daq)
Status: experimental

Supports:

    Analog Output

Configuration options:

    [0] - PCI bus number (optional)
    [1] - PCI slot number (optional)

    If bus/slot is not specified, the first available PCI
    device will be used.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/comedidev.h>

#include "me2600_fw.h"

#define ME_DRIVER_NAME                 "me_daq"

#define ME2000_DEVICE_ID               0x2000
#define ME2600_DEVICE_ID               0x2600

#define PLX_INTCSR                     0x4C      // PLX interrupt status register
#define XILINX_DOWNLOAD_RESET          0x42      // Xilinx registers

#define ME_CONTROL_1                   0x0000    // - | W
#define   INTERRUPT_ENABLE             (1<<15)
#define   COUNTER_B_IRQ                (1<<12)
#define   COUNTER_A_IRQ                (1<<11)
#define   CHANLIST_READY_IRQ           (1<<10)
#define   EXT_IRQ                      (1<<9)
#define   ADFIFO_HALFFULL_IRQ          (1<<8)
#define   SCAN_COUNT_ENABLE            (1<<5)
#define   SIMULTANEOUS_ENABLE          (1<<4)
#define   TRIGGER_FALLING_EDGE         (1<<3)
#define   CONTINUOUS_MODE              (1<<2)
#define   DISABLE_ADC                  (0<<0)
#define   SOFTWARE_TRIGGERED_ADC       (1<<0)
#define   SCAN_TRIGGERED_ADC           (2<<0)
#define   EXT_TRIGGERED_ADC            (3<<0)
#define ME_ADC_START                   0x0000    // R | -
#define ME_CONTROL_2                   0x0002    // - | W
#define   ENABLE_ADFIFO                (1<<10)
#define   ENABLE_CHANLIST              (1<<9)
#define   ENABLE_PORT_B                (1<<7)
#define   ENABLE_PORT_A                (1<<6)
#define   ENABLE_COUNTER_B             (1<<4)
#define   ENABLE_COUNTER_A             (1<<3)
#define   ENABLE_DAC                   (1<<1)
#define   BUFFERED_DAC                 (1<<0)
#define ME_DAC_UPDATE                  0x0002    // R | -
#define ME_STATUS                      0x0004    // R | -
#define   COUNTER_B_IRQ_PENDING        (1<<12)
#define   COUNTER_A_IRQ_PENDING        (1<<11)
#define   CHANLIST_READY_IRQ_PENDING   (1<<10)
#define   EXT_IRQ_PENDING              (1<<9)
#define   ADFIFO_HALFFULL_IRQ_PENDING  (1<<8)
#define   ADFIFO_FULL                  (1<<4)
#define   ADFIFO_HALFFULL              (1<<3)
#define   ADFIFO_EMPTY                 (1<<2)
#define   CHANLIST_FULL                (1<<1)
#define   FST_ACTIVE                   (1<<0)
#define ME_RESET_INTERRUPT             0x0004    // - | W
#define ME_DIO_PORT_A                  0x0006    // R | W
#define ME_DIO_PORT_B                  0x0008    // R | W
#define ME_TIMER_DATA_0                0x000A    // - | W
#define ME_TIMER_DATA_1                0x000C    // - | W
#define ME_TIMER_DATA_2                0x000E    // - | W
#define ME_CHANNEL_LIST                0x0010    // - | W
#define   ADC_UNIPOLAR                 (1<<6)
#define   ADC_GAIN_0                   (0<<4)
#define   ADC_GAIN_1                   (1<<4)
#define   ADC_GAIN_2                   (2<<4)
#define   ADC_GAIN_3                   (3<<4)
#define ME_READ_AD_FIFO                0x0010    // R | -
#define ME_DAC_CONTROL                 0x0012    // - | W
#define   DAC_UNIPOLAR_D               (0<<4)
#define   DAC_BIPOLAR_D                (1<<4)
#define   DAC_UNIPOLAR_C               (0<<5)
#define   DAC_BIPOLAR_C                (1<<5)
#define   DAC_UNIPOLAR_B               (0<<6)
#define   DAC_BIPOLAR_B                (1<<6)
#define   DAC_UNIPOLAR_A               (0<<7)
#define   DAC_BIPOLAR_A                (1<<7)
#define   DAC_GAIN_0_D                 (0<<8)
#define   DAC_GAIN_1_D                 (1<<8)
#define   DAC_GAIN_0_C                 (0<<9)
#define   DAC_GAIN_1_C                 (1<<9)
#define   DAC_GAIN_0_B                 (0<<10)
#define   DAC_GAIN_1_B                 (1<<10)
#define   DAC_GAIN_0_A                 (0<<11)
#define   DAC_GAIN_1_A                 (1<<11)
#define ME_DAC_CONTROL_UPDATE          0x0012    // R | -
#define ME_DAC_DATA_A                  0x0014    // - | W
#define ME_DAC_DATA_B                  0x0016    // - | W
#define ME_DAC_DATA_C                  0x0018    // - | W
#define ME_DAC_DATA_D                  0x001A    // - | W
#define ME_COUNTER_ENDDATA_A           0x001C    // - | W
#define ME_COUNTER_ENDDATA_B           0x001E    // - | W
#define ME_COUNTER_STARTDATA_A         0x0020    // - | W
#define ME_COUNTER_VALUE_A             0x0020    // R | -
#define ME_COUNTER_STARTDATA_B         0x0022    // - | W
#define ME_COUNTER_VALUE_B             0x0022    // R | -

//
// Function prototypes
//

static int me_attach(comedi_device *dev, comedi_devconfig *it);
static int me_detach(comedi_device *dev);

static comedi_lrange me2000_ai_range=
{
  8,
  {
    BIP_RANGE(10),
    BIP_RANGE(5),
    BIP_RANGE(2.5),
    BIP_RANGE(1.25),
    UNI_RANGE(10),
    UNI_RANGE(5),
    UNI_RANGE(2.5),
    UNI_RANGE(1.25)
  }
};

static comedi_lrange me2600_ai_range=
{
  8,
  {
    BIP_RANGE(10),
    BIP_RANGE(5),
    BIP_RANGE(2.5),
    BIP_RANGE(1.25),
    UNI_RANGE(10),
    UNI_RANGE(5),
    UNI_RANGE(2.5),
    UNI_RANGE(1.25)
  }
};

static comedi_lrange me2600_ao_range=
{
  3,
  {
    BIP_RANGE(10),
    BIP_RANGE(5),
    UNI_RANGE(10)
  }
};

static struct pci_device_id me_pci_table[] __devinitdata =
{
  { PCI_VENDOR_ID_MEILHAUS, ME2600_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
  { PCI_VENDOR_ID_MEILHAUS, ME2000_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
  { 0 }
};
MODULE_DEVICE_TABLE(pci, me_pci_table);

//
// Board specification structure
//

typedef struct
{
  char          *name;              // driver name
  int           device_id;
  int           ao_channel_nbr;     // DA config
  int           ao_resolution;
  int           ao_resolution_mask;
  comedi_lrange *ao_range_list;
  int           ai_channel_nbr;     // AD config
  int           ai_resolution;
  int           ai_resolution_mask;
  comedi_lrange *ai_range_list;
  int           dio_channel_nbr;    // DIO config
} me_board_struct;

static me_board_struct me_boards[] =
{
  {                                                     // -- ME-2600i --
    name:                         ME_DRIVER_NAME,
    device_id:                    ME2600_DEVICE_ID,
    ao_channel_nbr:               4,                    // Analog Output
    ao_resolution:                12,
    ao_resolution_mask:           0x0fff,
    ao_range_list:                &me2600_ao_range,
    ai_channel_nbr:               16,                   // Analog Input
    ai_resolution:                12,
    ai_resolution_mask:           0x0fff,
    ai_range_list:                &me2600_ai_range,
    dio_channel_nbr:              32,
  },
  {                                                     // -- ME-2000i --
    name:                         ME_DRIVER_NAME,
    device_id:                    ME2000_DEVICE_ID,
    ao_channel_nbr:               0,                    // Analog Output
    ao_resolution:                0,
    ao_resolution_mask:           0,
    ao_range_list:                0,
    ai_channel_nbr:               16,                   // Analog Input
    ai_resolution:                12,
    ai_resolution_mask:           0x0fff,
    ai_range_list:                &me2000_ai_range,
    dio_channel_nbr:              32,
  }
};

#define me_board_nbr (sizeof(me_boards)/sizeof(me_board_struct))

static comedi_driver me_driver=
{
  driver_name: ME_DRIVER_NAME,
  module:      THIS_MODULE,
  attach:      me_attach,
  detach:      me_detach,
  num_names:   me_board_nbr,
  board_name:  me_boards,
  offset:      sizeof(me_board_struct),
};
COMEDI_INITCLEANUP(me_driver);

//
// Private data structure
//

typedef struct
{
  struct pci_dev* pci_device;
  unsigned int plx_regbase;         // PLX configuration base address
  unsigned int me_regbase;          // Base address of the Meilhaus card
  unsigned int plx_regbase_size;    // Size of PLX configuration space
  unsigned int me_regbase_size;     // Size of Meilhaus space

  unsigned short control_1;         // Mirror of CONTROL_1 register
  unsigned short control_2;         // Mirror of CONTROL_2 register
  unsigned short dac_control;       // Mirror of the DAC_CONTROL register
  int ao_readback[4];               // Mirror of analog output data

} me_private_data_struct;

#define dev_private ((me_private_data_struct *)dev->private)

// ------------------------------------------------------------------
//
// Helpful functions
//
// ------------------------------------------------------------------

static __inline__ void sleep(unsigned sec)
{
  current->state = TASK_INTERRUPTIBLE;
  schedule_timeout(sec*HZ);
}

// ------------------------------------------------------------------
//
// DIGITAL INPUT/OUTPUT SECTION
//
// ------------------------------------------------------------------

static int me_dio_insn_config(comedi_device *dev,
                              comedi_subdevice *s,
                              comedi_insn *insn,
                              lsampl_t *data)
{
  int bits;
  int mask = 1 << CR_CHAN(insn->chanspec);

  /* calculate port */
  if(mask & 0x0000ffff) /* Port A in use */
  {
    bits = 0x0000ffff;

    /* Enable Port A */
    dev_private->control_2 |= ENABLE_PORT_A;
    writew(dev_private->control_2, dev_private->me_regbase + ME_CONTROL_2);
  }
  else                  /* Port B in use */
  {
    bits = 0xffff0000;

    /* Enable Port B */
    dev_private->control_2 |= ENABLE_PORT_B;
    writew(dev_private->control_2, dev_private->me_regbase + ME_CONTROL_2);
  }


  if(data[0])  /* Config port as output */
  {
    s->io_bits |= bits;
  }
  else         /* Config port as input */
  {
    s->io_bits &= ~bits;
  }

  return 1;
}

//
// Digital instant input/outputs
//

static int me_dio_insn_bits(comedi_device *dev,
                            comedi_subdevice *s,
                            comedi_insn *insn,
                            lsampl_t *data)
{
  unsigned int mask = data[0];
  s->state &= ~mask;
  s->state |= (mask & data[1]);

  mask &= s->io_bits;
  if(mask & 0x0000ffff) /* Port A */
  {
    writew((s->state & 0xffff), dev_private->me_regbase + ME_DIO_PORT_A);
  }
  else
  {
    data[1] &= ~0x0000ffff;
    data[1] |= readw(dev_private->me_regbase + ME_DIO_PORT_A);
  }

  if(mask & 0xffff0000) /* Port B */
  {
    writew(((s->state >> 16) & 0xffff), dev_private->me_regbase + ME_DIO_PORT_B);
  }
  else
  {
    data[1] &= ~0xffff0000;
    data[1] |= readw(dev_private->me_regbase + ME_DIO_PORT_B) << 16;
  }

  return 2;
}

// ------------------------------------------------------------------
//
// ANALOG INPUT SECTION
//
// ------------------------------------------------------------------

//
// Analog instant input
//
static int me_ai_insn_read(comedi_device *dev,
                           comedi_subdevice *subdevice,
                           comedi_insn *insn,
                           lsampl_t *data)
{
  unsigned short value;
  int chan = CR_CHAN((&insn->chanspec)[0]);
  int rang = CR_RANGE((&insn->chanspec)[0]);
  int aref = CR_AREF((&insn->chanspec)[0]);
  int i;

  /* stop any running conversion */
  dev_private->control_1 &= 0xFFFC;
  writew(dev_private->control_1, dev_private->me_regbase + ME_CONTROL_1);

  /* clear chanlist and ad fifo */
  dev_private->control_2 &= ~(ENABLE_ADFIFO | ENABLE_CHANLIST);
  writew(dev_private->control_2, dev_private->me_regbase + ME_CONTROL_2);

  /* reset any pending interrupt */
  writew(0x00, dev_private->me_regbase + ME_RESET_INTERRUPT);

  /* enable the chanlist and ADC fifo */
  dev_private->control_2 |= (ENABLE_ADFIFO | ENABLE_CHANLIST);
  writew(dev_private->control_2, dev_private->me_regbase + ME_CONTROL_2);

  /* write to channel list fifo */
  value  = chan & 0x0f;                      // b3:b0 are the channel number
  value |= (rang & 0x03) << 4;               // b5:b4 are the channel gain
  value |= (rang & 0x04) << 4;               // b6 channel polarity
  value |= ((aref & AREF_DIFF) ? 0x80 : 0);  // b7 single or differential
  writew(value & 0xff, dev_private->me_regbase + ME_CHANNEL_LIST);

  /* set ADC mode to software trigger */
  dev_private->control_1 |= SOFTWARE_TRIGGERED_ADC;
  writew(dev_private->control_1, dev_private->me_regbase + ME_CONTROL_1);

  /* start conversion by reading from ADC_START */
  readw(dev_private->me_regbase + ME_ADC_START);

  /* wait for ADC fifo not empty flag */
  for(i = 100000; i > 0; i--)
  {
    if(!(readw(dev_private->me_regbase + ME_STATUS) & 0x0004))
    {
      break;
    }
  }

  /* get value from ADC fifo*/
  if(i)
  {
    data[0] = (readw(dev_private->me_regbase + ME_READ_AD_FIFO) ^ 0x800) & 0x0FFF;
  }
  else
  {
    printk("comedi%d: Cannot get single value\n", dev->minor);
    return -EIO;
  }

  /* stop any running conversion */
  dev_private->control_1 &= 0xFFFC;
  writew(dev_private->control_1, dev_private->me_regbase + ME_CONTROL_1);

  return 1;
}


// ------------------------------------------------------------------
//
// HARDWARE TRIGGERED ANALOG INPUT SECTION
//
// ------------------------------------------------------------------

//
// Cancel analog input autoscan
//
static int me_ai_cancel(comedi_device *dev,
                        comedi_subdevice *s)
{
  /* disable interrupts */

  /* stop any running conversion */
  dev_private->control_1 &= 0xFFFC;
  writew(dev_private->control_1, dev_private->me_regbase + ME_CONTROL_1);

  return 0;
}

//
// Test analog input command
//
static int me_ai_do_cmd_test(comedi_device *dev,
                             comedi_subdevice *s,
                             comedi_cmd *cmd)
{
  return 0;
}

//
// Analog input command
//
static int me_ai_do_cmd(comedi_device *dev,
                        comedi_subdevice *subdevice)
{
  return 0;
}

// ------------------------------------------------------------------
//
// ANALOG OUTPUT SECTION
//
// ------------------------------------------------------------------

//
// Analog instant output
//
static int me_ao_insn_write(comedi_device *dev,
                            comedi_subdevice *s,
                            comedi_insn *insn,
                            lsampl_t *data)
{
  int chan;
  int rang;
  int i;

  /* Enable all DAC */
  dev_private->control_2 |= ENABLE_DAC;
  writew(dev_private->control_2, dev_private->me_regbase + ME_CONTROL_2);

  /* and set DAC to "buffered" mode */
  dev_private->control_2 |= BUFFERED_DAC;
  writew(dev_private->control_2, dev_private->me_regbase + ME_CONTROL_2);

  /* Set dac-control register */
  for (i=0; i < insn->n; i++)
  {
    chan = CR_CHAN((&insn->chanspec)[i]);
    rang = CR_RANGE((&insn->chanspec)[i]);

    dev_private->dac_control &= ~(0x0880 >> chan); /* clear bits for this channel */
    if (rang == 0)
      dev_private->dac_control |= ((DAC_BIPOLAR_A | DAC_GAIN_1_A) >> chan);
    else if (rang == 1)
      dev_private->dac_control |= ((DAC_BIPOLAR_A | DAC_GAIN_0_A) >> chan);
  }
  writew(dev_private->dac_control, dev_private->me_regbase + ME_DAC_CONTROL);

  /* Update dac-control register */
  readw(dev_private->me_regbase + ME_DAC_CONTROL_UPDATE);

  /* Set data register */
  for (i=0; i < insn->n; i++)
  {
    chan = CR_CHAN((&insn->chanspec)[i]);
    writew((data[0] & s->maxdata), dev_private->me_regbase + ME_DAC_DATA_A + (chan<<1));
    dev_private->ao_readback[chan] = (data[0] & s->maxdata);
  }

  /* Update dac with data registers */
  readw(dev_private->me_regbase + ME_DAC_UPDATE);

  return i;
}

//
// Analog output readback
//
static int me_ao_insn_read(comedi_device * dev,
                           comedi_subdevice *s,
                           comedi_insn *insn,
                           lsampl_t *data)
{
  int i;

  for (i=0; i < insn->n; i++)
  {
    data[i] = dev_private->ao_readback[CR_CHAN((&insn->chanspec)[i])];
  }

  return 1;
}

// ------------------------------------------------------------------
//
// INITIALISATION SECTION
//
// ------------------------------------------------------------------

//
// Xilinx firmware download for card: ME-2600i
//

static int me2600_xilinx_download(comedi_device *dev)
{
  unsigned int value;
  unsigned int file_length;
  unsigned int i;

  /* disable irq's on PLX */
  writel(0x00, dev_private->plx_regbase + PLX_INTCSR);

  /* First, make a dummy read to reset xilinx */
  value = readw(dev_private->me_regbase + XILINX_DOWNLOAD_RESET);

  /* Wait until reset is over */
  sleep(1);

  /* Write a dummy value to Xilinx */
  writeb(0x00, dev_private->me_regbase + 0x0);
  sleep(1);

  /*
   * Format of the firmware
   * Build longs from the byte-wise coded header
   * Byte 1-3:   length of the array
   * Byte 4-7:   version
   * Byte 8-11:  date
   * Byte 12-15: reserved
   */
  file_length =
    (((unsigned int)me2600_firmware[0] & 0xff)<<24) +
    (((unsigned int)me2600_firmware[1] & 0xff)<<16) +
    (((unsigned int)me2600_firmware[2] & 0xff)<< 8) +
    ((unsigned int)me2600_firmware[3] & 0xff);

  /*
   * Loop for writing firmware byte by byte to xilinx
   * Firmware data start at offfset 16
   */
  for(i = 0; i < file_length; i++)
  {
    writeb((me2600_firmware[16+i] & 0xff), dev_private->me_regbase + 0x0);
  }

  /* Write 5 dummy values to xilinx */
  for(i = 0; i < 5; i++)
  {
    writeb(0x00, dev_private->me_regbase + 0x0);
  }

  /* Test if there was an error during download -> INTB was thrown */
  value = readl(dev_private->plx_regbase + PLX_INTCSR);
  if(value & 0x20)
  {
    /* Disable interrupt */
    writel(0x00, dev_private->plx_regbase + PLX_INTCSR);
    printk("comedi%d: Xilinx download failed\n", dev->minor);
    return -EIO;
  }

  /* Wait until the Xilinx is ready for real work */
  sleep(1);

  /* Enable PLX-Interrupts */
  writel(0x43, dev_private->plx_regbase + PLX_INTCSR);

  return 0;
}

//
// Reset device
//

static int me_reset(comedi_device *dev)
{
  /* Reset board */
  writew(0x00, dev_private->me_regbase + ME_CONTROL_1);
  writew(0x00, dev_private->me_regbase + ME_CONTROL_2);
  writew(0x00, dev_private->me_regbase + ME_RESET_INTERRUPT);
  writew(0x00, dev_private->me_regbase + ME_DAC_CONTROL);

  /* Save values in the board context */
  dev_private->dac_control = 0;
  dev_private->control_1 = 0;
  dev_private->control_2 = 0;

  return 0;
}

//
// Attach
//
//  - Register PCI device
//  - Declare device driver capability
//

static int me_attach(comedi_device *dev,comedi_devconfig *it)
{
  struct pci_dev* pci_device;
  comedi_subdevice *subdevice;
  me_board_struct* board;
  unsigned int plx_regbase_tmp;
  unsigned int plx_regbase_size_tmp;
  unsigned int me_regbase_tmp;
  unsigned int me_regbase_size_tmp;
  unsigned int swap_regbase_tmp;
  unsigned int swap_regbase_size_tmp;
  unsigned int regbase_tmp;
  int result, error, i;

//
// Probe the device to determine what device in the series it is.
//
  pci_for_each_dev(pci_device)
  {
    if(pci_device->vendor == PCI_VENDOR_ID_MEILHAUS)
    {
      for(i = 0; i < me_board_nbr; i++)
      {
        if(me_boards[i].device_id == pci_device->device)
        {
          // was a particular bus/slot requested?
          if((it->options[0] != 0) || (it->options[1] != 0))
          {
            // are we on the wrong bus/slot?
            if(pci_device->bus->number != it->options[0] ||
               PCI_SLOT(pci_device->devfn) != it->options[1])
            {
              continue;
            }
          }

          dev->board_ptr = me_boards + i;
          board = (me_board_struct *) dev->board_ptr;
          goto found;
        }
      }
    }
  }

  printk("comedi%d: no supported board found! (req. bus/slot : %d/%d)\n",
         dev->minor,it->options[0], it->options[1]);
  return -EIO;

found:

  printk("comedi%d: found %s at PCI bus %d, slot %d\n",
         dev->minor, me_boards[i].name,
         pci_device->bus->number, PCI_SLOT(pci_device->devfn));

  // Allocate private memory

  if(alloc_private(dev,sizeof(me_private_data_struct)) < 0)
    return -ENOMEM;

  // Set data in device structure

  dev->board_name = board->name;
  dev_private->pci_device = pci_device;

  // Read PLX register base address [PCI_BASE_ADDRESS #0].

  plx_regbase_tmp = pci_resource_start(pci_device, 0);
  plx_regbase_size_tmp = pci_resource_end(pci_device, 0) - plx_regbase_tmp + 1;

  if(plx_regbase_tmp & PCI_BASE_ADDRESS_SPACE)
  {
    printk("comedi%d: PLX space is not MEM\n", dev->minor);
    return -EIO;
  }

  // Read Swap base address [PCI_BASE_ADDRESS #5].

  swap_regbase_tmp = pci_resource_start(pci_device, 5);
  swap_regbase_size_tmp = pci_resource_end(pci_device, 5) - swap_regbase_tmp + 1;

  if(!swap_regbase_tmp)
  {
    printk("comedi%d: Swap not present\n", dev->minor);
  }

  /*----------------------------------------------------- Workaround start ---*/
  if(plx_regbase_tmp & 0x0080)
  {
    printk("comedi%d: PLX-Bug detected\n", dev->minor);

    if(swap_regbase_tmp)
    {
      regbase_tmp = plx_regbase_tmp;
      plx_regbase_tmp = swap_regbase_tmp;
      swap_regbase_tmp = regbase_tmp;

      result = pci_write_config_dword(pci_device, PCI_BASE_ADDRESS_0, plx_regbase_tmp);
      if(result != PCIBIOS_SUCCESSFUL)
        return -EIO;

      result = pci_write_config_dword(pci_device, PCI_BASE_ADDRESS_5, swap_regbase_tmp);
      if(result != PCIBIOS_SUCCESSFUL)
        return -EIO;
    }
    else
    {
      plx_regbase_tmp -= 0x80;
      result = pci_write_config_dword(pci_device, PCI_BASE_ADDRESS_0, plx_regbase_tmp);
      if(result != PCIBIOS_SUCCESSFUL)
        return -EIO;
    }
  }
  /*----------------------------------------------------- Workaround end -----*/

  plx_regbase_tmp &= PCI_BASE_ADDRESS_MEM_MASK;
  dev_private->plx_regbase_size = plx_regbase_size_tmp;
  dev_private->plx_regbase = (unsigned int) ioremap(plx_regbase_tmp, plx_regbase_size_tmp);

  // Read Meilhaus register base address [PCI_BASE_ADDRESS #2].

  me_regbase_tmp = pci_resource_start(pci_device, 2);
  me_regbase_size_tmp = pci_resource_end(pci_device, 2) - me_regbase_tmp + 1;

  if(me_regbase_tmp & PCI_BASE_ADDRESS_SPACE)
  {
    printk("comedi%d: Meilhaus space is not MEM\n", dev->minor);
    return -EIO;
  }

  me_regbase_tmp &= PCI_BASE_ADDRESS_MEM_MASK;
  dev_private->me_regbase_size = me_regbase_size_tmp;
  dev_private->me_regbase = (unsigned int) ioremap(me_regbase_tmp, me_regbase_size_tmp);

  // Download firmware and reset card
  if(board->device_id == ME2600_DEVICE_ID)
  {
    me2600_xilinx_download(dev);
  }

  me_reset(dev);

  // device driver capabilities

  dev->n_subdevices = 3;
  if((error = alloc_subdevices(dev)) < 0)
    return error;

  subdevice = dev->subdevices + 0;
  subdevice->type         = COMEDI_SUBD_AI;
  subdevice->subdev_flags = SDF_READABLE | SDF_COMMON;
  subdevice->n_chan       = board->ai_channel_nbr;
  subdevice->maxdata      = board->ai_resolution_mask;
  subdevice->len_chanlist = board->ai_channel_nbr;
  subdevice->range_table  = board->ai_range_list;
  subdevice->cancel       = me_ai_cancel;
  subdevice->insn_read    = me_ai_insn_read;
  subdevice->do_cmdtest   = me_ai_do_cmd_test;
  subdevice->do_cmd       = me_ai_do_cmd;

  subdevice = dev->subdevices + 1;
  subdevice->type         = COMEDI_SUBD_AO;
  subdevice->subdev_flags = SDF_WRITEABLE | SDF_COMMON;
  subdevice->n_chan       = board->ao_channel_nbr;
  subdevice->maxdata      = board->ao_resolution_mask;
  subdevice->len_chanlist = board->ao_channel_nbr;
  subdevice->range_table  = board->ao_range_list;
  subdevice->insn_read    = me_ao_insn_read;
  subdevice->insn_write   = me_ao_insn_write;

  subdevice = dev->subdevices + 2;
  subdevice->type         = COMEDI_SUBD_DIO;
  subdevice->subdev_flags = SDF_READABLE | SDF_WRITEABLE;
  subdevice->n_chan       = board->dio_channel_nbr;
  subdevice->maxdata      = 1;
  subdevice->len_chanlist = board->dio_channel_nbr;
  subdevice->range_table  = &range_digital;
  subdevice->insn_bits    = me_dio_insn_bits;
  subdevice->insn_config  = me_dio_insn_config;
  subdevice->io_bits      = 0;

  printk("comedi%d: " ME_DRIVER_NAME " attached.\n", dev->minor);
  return 0;
}


//
// Detach
//

static int me_detach(comedi_device *dev)
{
  me_reset(dev);

  return 0;
}