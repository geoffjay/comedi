/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module. 
        
        ADDI-DATA GmbH 
        Dieselstrasse 3 
        D-77833 Ottersweier 
        Tel: +19(0)7223/9493-0 
        Fax: +49(0)7223/9493-92 
        http://www.addi-data-com 
        info@addi-data.com 

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You shoud also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/
/*
    
  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstraße 3       D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-----------------------------------------------------------------------+
  | Project     : API APCI1710    | Compiler : gcc                        |
  | Module name : 82X54.C         | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date     :  02/12/2002                |
  +-----------------------------------------------------------------------+
  | Description :   APCI-1710 82X54 timer module                          |
  |                                                                       |
  |                                                                       |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  | 29/06/98 | S. Weber  | Digital input / output implementation          |
  |----------|-----------|------------------------------------------------|
  | 08/05/00 | Guinot C  | - 0400/0228 All Function in RING 0             |
  |          |           |   available                                    |
  +-----------------------------------------------------------------------+
  | 27.10.03 | J. Krauth |  Add the possibility to use a 40 Mhz quartz    |
  |          |           |                                                |
  +-----------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
|                               Included files                               |
+----------------------------------------------------------------------------+
*/

#include "APCI1710_82x54.h"

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_InitTimer                         |
|                               (BYTE_   b_BoardHandle,                      |
|                                BYTE_   b_ModulNbr,                         |
|                                BYTE_   b_TimerNbr,                         |
|                                BYTE_   b_TimerMode,                        |
|                                ULONG_ ul_ReloadValue,                      |
|                                BYTE_   b_InputClockSelection,              |
|                                BYTE_   b_InputClockLevel,                  |
|                                BYTE_   b_OutputLevel,                      |
|                                BYTE_   b_HardwareGateLevel)
INT i_InsnConfig_InitTimer(comedi_device *dev,comedi_subdevice *s,
	comedi_insn *insn,lsampl_t *data)
|	
+----------------------------------------------------------------------------+
| Task              : Configure the Timer (b_TimerNbr) operating mode        |
|                     (b_TimerMode) from selected module (b_ModulNbr).       |
|                     You must calling this function be for you call any     |
|                     other function witch access of the timer.              |
|                                                                            |
|                                                                            |
|                       Timer mode description table                         |
|                                                                            |
|+--------+-----------------------------+--------------+--------------------+|
||Selected+      Mode description       +u_ReloadValue | Hardware gate input||
||  mode  |                             |  description |      action        ||
|+--------+-----------------------------+--------------+--------------------+|
||        |Mode 0 is typically used     |              |                    ||
||        |for event counting. After    |              |                    ||
||        |the initialisation, OUT      |              |                    ||
||        |is initially low, and        |              |                    ||
||   0    |will remain low until the    |Start counting|   Hardware gate    ||
||        |counter reaches zero.        |   value      |                    ||
||        |OUT then goes high and       |              |                    ||
||        |remains high until a new     |              |                    ||
||        |count is written. See        |              |                    ||
||        |"i_APCI1710_WriteTimerValue" |              |                    ||
||        |function.                    |              |                    ||
|+--------+-----------------------------+--------------+--------------------+|
||        |Mode 1 is similar to mode 0  |              |                    ||
||        |except for the gate input    |              |                    ||
||   1    |action. The gate input is not|Start counting|  Hardware trigger  ||
||        |used for enabled or disabled |   value      |                    ||
||        |the timer.                   |              |                    ||
||        |The gate input is used for   |              |                    ||
||        |triggered the timer.         |              |                    ||
|+--------+-----------------------------+--------------+--------------------+|
||        |This mode functions like a   |              |                    ||
||        |divide-by-ul_ReloadValue     |              |                    ||
||        |counter. It is typically used|              |                    ||
||        |to generate a real time clock|              |                    ||
||        |interrupt. OUT will initially|              |                    ||
||   2    |be high after the            |   Division   |  Hardware gate     ||
||        |initialisation. When the     |    factor    |                    ||
||        |initial count has decremented|              |                    ||
||        |to 1, OUT goes low for one   |              |                    ||
||        |CLK pule. OUT then goes high |              |                    ||
||        |again, the counter reloads   |              |                    ||
||        |the initial count            |              |                    ||
||        |(ul_ReloadValue) and the     |              |                    ||
||        |process is repeated.         |              |                    ||
||        |This action can generated a  |              |                    ||
||        |interrupt. See function      |              |                    ||
||        |"i_APCI1710_SetBoardInt-     |              |                    ||
||        |RoutineX"                    |              |                    ||
||        |and "i_APCI1710_EnableTimer" |              |                    ||
|+--------+-----------------------------+--------------+--------------------+|
||        |Mode 3 is typically used for |              |                    ||
||        |baud rate generation. This   |              |                    ||
||        |mode is similar to mode 2    |              |                    ||
||        |except for the duty cycle of |              |                    ||
||   3    |OUT. OUT will initially be   |  Division    |   Hardware gate    ||
||        |high after the initialisation|   factor     |                    ||
||        |When half the initial count  |              |                    ||
||        |(ul_ReloadValue) has expired,|              |                    ||
||        |OUT goes low for the         |              |                    ||
||        |remainder of the count. The  |              |                    ||
||        |mode is periodic; the        |              |                    ||
||        |sequence above is repeated   |              |                    ||
||        |indefinitely.                |              |                    ||
|+--------+-----------------------------+--------------+--------------------+|
||        |OUT will be initially high   |              |                    ||
||        |after the initialisation.    |              |                    ||
||        |When the initial count       |              |                    ||
||   4    |expires OUT will go low for  |Start counting|  Hardware gate     ||
||        |one CLK pulse and then go    |    value     |                    ||
||        |high again.                  |              |                    ||
||        |The counting sequences is    |              |                    ||
||        |triggered by writing a new   |              |                    ||
||        |value. See                   |              |                    ||
||        |"i_APCI1710_WriteTimerValue" |              |                    ||
||        |function. If a new count is  |              |                    ||
||        |written during counting,     |              |                    ||
||        |it will be loaded on the     |              |                    ||
||        |next CLK pulse               |              |                    ||
|+--------+-----------------------------+--------------+--------------------+|
||        |Mode 5 is similar to mode 4  |              |                    ||
||        |except for the gate input    |              |                    ||
||        |action. The gate input is not|              |                    ||
||   5    |used for enabled or disabled |Start counting|  Hardware trigger  ||
||        |the timer. The gate input is |    value     |                    ||
||        |used for triggered the timer.|              |                    ||
|+--------+-----------------------------+--------------+--------------------+|
|                                                                            |
|                                                                            |
|                                                                            |
|                      Input clock selection table                           |
|                                                                            |
|  +--------------------------------+------------------------------------+   |
|  |       b_InputClockSelection    |           Description              |   |
|  |           parameter            |                                    |   |
|  +--------------------------------+------------------------------------+   |
|  |    APCI1710_PCI_BUS_CLOCK      | For the timer input clock, the PCI |   |
|  |                                | bus clock / 4 is used. This PCI bus|   |
|  |                                | clock can be 30MHz or 33MHz. For   |   |
|  |                                | Timer 0 only this selection are    |   |
|  |                                | available.                         |   |
|  +--------------------------------+------------------------------------+   |
|  | APCI1710_ FRONT_CONNECTOR_INPUT| Of the front connector you have the|   |
|  |                                | possibility to inject a input clock|   |
|  |                                | for Timer 1 or Timer 2. The source |   |
|  |                                | from this clock can eat the output |   |
|  |                                | clock from Timer 0 or any other    |   |
|  |                                | clock source.                      |   |
|  +--------------------------------+------------------------------------+   |
|                                                                            |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_   b_BoardHandle        : Handle of board         |
|                                                    APCI-1710               |
|                     BYTE_   b_ModulNbr           : Module number to        |
|                                                    configure (0 to 3)      |
|                     BYTE_   b_TimerNbr           : Timer number to         |
|                                                    configure (0 to 2)      |
|                     BYTE_   b_TimerMode          : Timer mode selection    |
|                                                    (0 to 5)                |
|                                                    0: Interrupt on terminal|
|                                                       count                |
|                                                    1: Hardware             |
|                                                       retriggerable one-   |
|                                                       shot                 |
|                                                    2: Rate generator       |
|                                                    3: Square wave mode     |
|                                                    4: Software triggered   |
|                                                       strobe               |
|                                                    5: Hardware triggered   |
|                                                       strobe               |
|                                                       See timer mode       |
|                                                       description table.   |
|                     ULONG_ ul_ReloadValue         : Start counting value   |
|                                                     or division factor     |
|                                                     See timer mode         |
|                                                     description table.     |
|                     BYTE_   b_InputClockSelection : Selection from input   |
|                                                     timer clock.           |
|                                                     See input clock        |
|                                                     selection table.       |
|                     BYTE_   b_InputClockLevel     : Selection from input   |
|                                                     clock level.           |
|                                                     0 : Low active         |
|                                                         (Input inverted)   |
|                                                     1 : High active        |
|                     BYTE_   b_OutputLevel,        : Selection from output  |
|                                                     clock level.           |
|                                                     0 : Low active         |
|                                                     1 : High active        |
|                                                         (Output inverted)  |
|                     BYTE_   b_HardwareGateLevel   : Selection from         |
|                                                     hardware gate level.   |
|                                                     0 : Low active         |
|                                                         (Input inverted)   |
|                                                     1 : High active        |
|                                                     If you will not used   |
|                                                     the hardware gate set  |
|                                                     this value to 0.       
|b_ModulNbr        = (BYTE) CR_AREF(insn->chanspec);
	b_TimerNbr		  = (BYTE) CR_CHAN(insn->chanspec);
	b_TimerMode		  = (BYTE) data[0];
	ul_ReloadValue	  = (ULONG) data[1];
	b_InputClockSelection	=(BYTE) data[2];
	b_InputClockLevel		=(BYTE) data[3];
	b_OutputLevel			=(BYTE) data[4];
	b_HardwareGateLevel		=(BYTE) data[5];
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: Module selection wrong                              |
|                    -3: Timer selection wrong                               |
|                    -4: The module is not a TIMER module                    |
|                    -5: Timer mode selection is wrong                       |
|                    -6: Input timer clock selection is wrong                |
|                    -7: Selection from input clock level is wrong           |
|                    -8: Selection from output clock level is wrong          |
|                    -9: Selection from hardware gate level is wrong         |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnConfigInitTimer(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data)
{

	INT i_ReturnValue = 0;
	BYTE b_ModulNbr;
	BYTE b_TimerNbr;
	BYTE b_TimerMode;
	ULONG ul_ReloadValue;
	BYTE b_InputClockSelection;
	BYTE b_InputClockLevel;
	BYTE b_OutputLevel;
	BYTE b_HardwareGateLevel;

	//BEGIN JK 27.10.2003 : Add the possibility to use a 40 Mhz quartz
	DWORD dw_Test = 0;
	//END JK 27.10.2003 : Add the possibility to use a 40 Mhz quartz

	i_ReturnValue = insn->n;
	b_ModulNbr = (BYTE) CR_AREF(insn->chanspec);
	b_TimerNbr = (BYTE) CR_CHAN(insn->chanspec);
	b_TimerMode = (BYTE) data[0];
	ul_ReloadValue = (ULONG) data[1];
	b_InputClockSelection = (BYTE) data[2];
	b_InputClockLevel = (BYTE) data[3];
	b_OutputLevel = (BYTE) data[4];
	b_HardwareGateLevel = (BYTE) data[5];

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if 82X54 timer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_82X54_TIMER) {
	      /*************************/
			/* Test the timer number */
	      /*************************/

			if (b_TimerNbr <= 2) {
		 /***********************/
				/* Test the timer mode */
		 /***********************/

				if (b_TimerMode <= 5) {
					//BEGIN JK 27.10.2003 : Add the possibility to use a 40 Mhz quartz
		    /*********************************/
					/* Test te imput clock selection */
		    /*********************************/
					/*
					   if (((b_TimerNbr == 0) && (b_InputClockSelection == 0)) ||
					   ((b_TimerNbr != 0) && ((b_InputClockSelection == 0) || (b_InputClockSelection == 1))))
					 */

					if (((b_TimerNbr == 0)
							&&
							(b_InputClockSelection
								==
								APCI1710_PCI_BUS_CLOCK))
						|| ((b_TimerNbr == 0)
							&&
							(b_InputClockSelection
								==
								APCI1710_10MHZ))
						|| ((b_TimerNbr != 0)
							&&
							((b_InputClockSelection
									==
									APCI1710_PCI_BUS_CLOCK)
								||
								(b_InputClockSelection
									==
									APCI1710_FRONT_CONNECTOR_INPUT)
								||
								(b_InputClockSelection
									==
									APCI1710_10MHZ))))
						//END JK 27.10.2003 : Add the possibility to use a 40 Mhz quartz
					{
						//BEGIN JK 27.10.2003 : Add the possibility to use a 40 Mhz quartz
						if (((b_InputClockSelection ==
									APCI1710_10MHZ)
								&& ((devpriv->s_BoardInfos.dw_MolduleConfiguration[b_ModulNbr] & 0x0000FFFFUL) >= 0x3131)) || (b_InputClockSelection != APCI1710_10MHZ)) {
							//END JK 27.10.2003 : Add the possibility to use a 40 Mhz quartz
			  /****************************************/
							/* Test the input clock level selection */
			  /****************************************/

							if ((b_InputClockLevel
									== 0)
								||
								(b_InputClockLevel
									== 1)) {
			     /*****************************************/
								/* Test the output clock level selection */
			     /*****************************************/

								if ((b_OutputLevel == 0) || (b_OutputLevel == 1)) {
				/******************************************/
									/* Test the hardware gate level selection */
				/******************************************/

									if ((b_HardwareGateLevel == 0) || (b_HardwareGateLevel == 1)) {
										//BEGIN JK 27.10.03 : Add the possibility to use a 40 Mhz quartz
				   /*****************************************************/
										/* Test if version > 1.1 and clock selection = 10MHz */
				   /*****************************************************/

										if ((b_InputClockSelection == APCI1710_10MHZ) && ((devpriv->s_BoardInfos.dw_MolduleConfiguration[b_ModulNbr] & 0x0000FFFFUL) > 0x3131)) {
				      /*********************************/
											/* Test if 40MHz quartz on board */
				      /*********************************/

											dw_Test = inl(devpriv->s_BoardInfos.ui_Address + (16 + (b_TimerNbr * 4) + (64 * b_ModulNbr)));

											dw_Test = (dw_Test >> 16) & 1;
										} else {
											dw_Test = 1;
										}

				   /************************/
										/* Test if detection OK */
				   /************************/

										if (dw_Test == 1) {
											//END JK 27.10.03 : Add the possibility to use a 40 Mhz quartz
				      /*********************/
											/* Initialisation OK */
				      /*********************/

											devpriv->
												s_ModuleInfo
												[b_ModulNbr].
												s_82X54ModuleInfo.
												s_82X54TimerInfo
												[b_TimerNbr].
												b_82X54Init
												=
												1;

				      /**********************************/
											/* Save the input clock selection */
				      /**********************************/
											devpriv->
												s_ModuleInfo
												[b_ModulNbr].
												s_82X54ModuleInfo.
												s_82X54TimerInfo
												[b_TimerNbr].
												b_InputClockSelection
												=
												b_InputClockSelection;

				      /******************************/
											/* Save the input clock level */
				      /******************************/
											devpriv->
												s_ModuleInfo
												[b_ModulNbr].
												s_82X54ModuleInfo.
												s_82X54TimerInfo
												[b_TimerNbr].
												b_InputClockLevel
												=
												~b_InputClockLevel
												&
												1;

				      /*************************/
											/* Save the output level */
				      /*************************/

											devpriv->
												s_ModuleInfo
												[b_ModulNbr].
												s_82X54ModuleInfo.
												s_82X54TimerInfo
												[b_TimerNbr].
												b_OutputLevel
												=
												~b_OutputLevel
												&
												1;

				      /***********************/
											/* Save the gate level */
				      /***********************/

											devpriv->
												s_ModuleInfo
												[b_ModulNbr].
												s_82X54ModuleInfo.
												s_82X54TimerInfo
												[b_TimerNbr].
												b_HardwareGateLevel
												=
												b_HardwareGateLevel;

				      /****************************************************/
											/* Set the configuration word and disable the timer */
				      /****************************************************/
											//BEGIN JK 27.10.03 : Add the possibility to use a 40 Mhz quartz
											/*
											   devpriv->s_ModuleInfo [b_ModulNbr].
											   s_82X54ModuleInfo.
											   s_82X54TimerInfo  [b_TimerNbr].
											   dw_ConfigurationWord = (DWORD) (((b_HardwareGateLevel         << 0) & 0x1) |
											   ((b_InputClockLevel           << 1) & 0x2) |
											   (((~b_OutputLevel       & 1)  << 2) & 0x4) |
											   ((b_InputClockSelection       << 4) & 0x10));
											 */
				      /**************************/
											/* Test if 10MHz selected */
				      /**************************/

											if (b_InputClockSelection == APCI1710_10MHZ) {
												b_InputClockSelection
													=
													2;
											}

											devpriv->
												s_ModuleInfo
												[b_ModulNbr].
												s_82X54ModuleInfo.
												s_82X54TimerInfo
												[b_TimerNbr].
												dw_ConfigurationWord
												=
												(DWORD)
												(
												((b_HardwareGateLevel << 0) & 0x1) | ((b_InputClockLevel << 1) & 0x2) | (((~b_OutputLevel & 1) << 2) & 0x4) | ((b_InputClockSelection << 4) & 0x30));
											//END JK 27.10.03 : Add the possibility to use a 40 Mhz quartz                            
											outl(devpriv->s_ModuleInfo[b_ModulNbr].s_82X54ModuleInfo.s_82X54TimerInfo[b_TimerNbr].dw_ConfigurationWord, devpriv->s_BoardInfos.ui_Address + 32 + (b_TimerNbr * 4) + (64 * b_ModulNbr));

				      /******************************/
											/* Initialise the 82X54 Timer */
				      /******************************/

											outl((DWORD) b_TimerMode, devpriv->s_BoardInfos.ui_Address + 16 + (b_TimerNbr * 4) + (64 * b_ModulNbr));

				      /**************************/
											/* Write the reload value */
				      /**************************/

											outl(ul_ReloadValue, devpriv->s_BoardInfos.ui_Address + 0 + (b_TimerNbr * 4) + (64 * b_ModulNbr));
											//BEGIN JK 27.10.03 : Add the possibility to use a 40 Mhz quartz
										}	// if (dw_Test == 1)
										else {
				      /****************************************/
											/* Input timer clock selection is wrong */
				      /****************************************/

											i_ReturnValue
												=
												-6;
										}	// if (dw_Test == 1)
										//END JK 27.10.03 : Add the possibility to use a 40 Mhz quartz
									}	// if ((b_HardwareGateLevel == 0) || (b_HardwareGateLevel == 1))
									else {
				/***********************************************/
										/* Selection from hardware gate level is wrong */
				/***********************************************/

										DPRINTK("Selection from hardware gate level is wrong\n");
										i_ReturnValue
											=
											-9;
									}	// if ((b_HardwareGateLevel == 0) || (b_HardwareGateLevel == 1))
								}	// if ((b_OutputLevel == 0) || (b_OutputLevel == 1))
								else {
			     /**********************************************/
									/* Selection from output clock level is wrong */
			     /**********************************************/

									DPRINTK("Selection from output clock level is wrong\n");
									i_ReturnValue
										=
										-8;
								}	// if ((b_OutputLevel == 0) || (b_OutputLevel == 1))
							}	// if ((b_InputClockLevel == 0) || (b_InputClockLevel == 1))
							else {
			  /*********************************************/
								/* Selection from input clock level is wrong */
			  /*********************************************/

								DPRINTK("Selection from input clock level is wrong\n");
								i_ReturnValue =
									-7;
							}	// if ((b_InputClockLevel == 0) || (b_InputClockLevel == 1))
						} else {
		       /****************************************/
							/* Input timer clock selection is wrong */
		       /****************************************/

							DPRINTK("Input timer clock selection is wrong\n");
							i_ReturnValue = -6;
						}
					} else {
		    /****************************************/
						/* Input timer clock selection is wrong */
		    /****************************************/

						DPRINTK("Input timer clock selection is wrong\n");
						i_ReturnValue = -6;
					}
				}	// if ((b_TimerMode >= 0) && (b_TimerMode <= 5))
				else {
		 /*********************************/
					/* Timer mode selection is wrong */
		 /*********************************/

					DPRINTK("Timer mode selection is wrong\n");
					i_ReturnValue = -5;
				}	// if ((b_TimerMode >= 0) && (b_TimerMode <= 5))
			}	// if ((b_TimerNbr >= 0) && (b_TimerNbr <= 2))
			else {
	      /*************************/
				/* Timer selection wrong */
	      /*************************/

				DPRINTK("Timer selection wrong\n");
				i_ReturnValue = -3;
			}	// if ((b_TimerNbr >= 0) && (b_TimerNbr <= 2))
		} else {
	   /************************************/
			/* The module is not a TIMER module */
	   /************************************/

			DPRINTK("The module is not a TIMER module\n");
			i_ReturnValue = -4;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/

		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_EnableTimer                       |
|                               (BYTE_ b_BoardHandle,                        |
|                                BYTE_ b_ModulNbr,                           |
|                                BYTE_ b_TimerNbr,                           |
|                                BYTE_ b_InterruptEnable)    
INT i_APCI1710_InsnWriteEnableDisableTimer(comedi_device *dev,comedi_subdevice *s,
	comedi_insn *insn,lsampl_t *data)                |
+----------------------------------------------------------------------------+
| Task              : Enable OR Disable the Timer (b_TimerNbr) from selected module     |
|                     (b_ModulNbr). You must calling the                     |
|                     "i_APCI1710_InitTimer" function be for you call this   |
|                     function. If you enable the timer interrupt, the timer |
|                     generate a interrupt after the timer value reach       |
|                     the zero. See function "i_APCI1710_SetBoardIntRoutineX"|
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_   b_BoardHandle     : Handle of board            |
|                                                 APCI-1710                  |
|                     BYTE_   b_ModulNbr        : Selected module number     |
|                                                 (0 to 3)                   |
|                     BYTE_   b_TimerNbr        : Timer number to enable     |
|                                                 (0 to 2)                   |
|                     BYTE_   b_InterruptEnable : Enable or disable the      |
|                                                 timer interrupt.           |
|                                                 APCI1710_ENABLE :          |
|                                                 Enable the timer interrupt |
|                                                 APCI1710_DISABLE :         |
|                                                 Disable the timer interrupt|
i_ReturnValue=insn->n;
	b_ModulNbr        = (BYTE) CR_AREF(insn->chanspec);
	b_TimerNbr		  = (BYTE) CR_CHAN(insn->chanspec);
	b_ActionType      = (BYTE) data[0]; // enable disable 
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: Module selection wrong                              |
|                    -3: Timer selection wrong                               |
|                    -4: The module is not a TIMER module                    |
|                    -5: Timer not initialised see function                  |
|                        "i_APCI1710_InitTimer"                              |
|                    -6: Interrupt parameter is wrong                        |
|                    -7: Interrupt function not initialised.                 |
|                        See function "i_APCI1710_SetBoardIntRoutineX"       |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnWriteEnableDisableTimer(comedi_device * dev,
	comedi_subdevice * s, comedi_insn * insn, lsampl_t * data)
{
	INT i_ReturnValue = 0;
	DWORD dw_DummyRead;
	BYTE b_ModulNbr;
	BYTE b_TimerNbr;
	BYTE b_ActionType;
	BYTE b_InterruptEnable;

	i_ReturnValue = insn->n;
	b_ModulNbr = (BYTE) CR_AREF(insn->chanspec);
	b_TimerNbr = (BYTE) CR_CHAN(insn->chanspec);
	b_ActionType = (BYTE) data[0];	// enable disable 
	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if 82X54 timer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_82X54_TIMER) {
	      /*************************/
			/* Test the timer number */
	      /*************************/

			if (b_TimerNbr <= 2) {
		 /*****************************/
				/* Test if timer initialised */
		 /*****************************/

				if (devpriv->s_ModuleInfo[b_ModulNbr].
					s_82X54ModuleInfo.
					s_82X54TimerInfo[b_TimerNbr].
					b_82X54Init == 1) {

					switch (b_ActionType) {
					case APCI1710_ENABLE:

						b_InterruptEnable =
							(BYTE) data[1];
		    /********************************/
						/* Test the interrupt selection */
		    /********************************/

						if ((b_InterruptEnable ==
								APCI1710_ENABLE)
							|| (b_InterruptEnable ==
								APCI1710_DISABLE))
						{
							if (b_InterruptEnable ==
								APCI1710_ENABLE)
							{

								dw_DummyRead =
									inl
									(devpriv->
									s_BoardInfos.
									ui_Address
									+ 12 +
									(b_TimerNbr
										*
										4)
									+
									(64 * b_ModulNbr));

			     /************************/
								/* Enable the interrupt */
			     /************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									s_82X54TimerInfo
									[b_TimerNbr].
									dw_ConfigurationWord
									=
									devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									s_82X54TimerInfo
									[b_TimerNbr].
									dw_ConfigurationWord
									| 0x8;

								outl(devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									s_82X54TimerInfo
									[b_TimerNbr].
									dw_ConfigurationWord,
									devpriv->
									s_BoardInfos.
									ui_Address
									+ 32 +
									(b_TimerNbr
										*
										4)
									+
									(64 * b_ModulNbr));
								devpriv->tsk_Current = current;	// Save the current process task structure

							}	// if (b_InterruptEnable == APCI1710_ENABLE)
							else {
			  /*************************/
								/* Disable the interrupt */
			  /*************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									s_82X54TimerInfo
									[b_TimerNbr].
									dw_ConfigurationWord
									=
									devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									s_82X54TimerInfo
									[b_TimerNbr].
									dw_ConfigurationWord
									& 0xF7;

								outl(devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									s_82X54TimerInfo
									[b_TimerNbr].
									dw_ConfigurationWord,
									devpriv->
									s_BoardInfos.
									ui_Address
									+ 32 +
									(b_TimerNbr
										*
										4)
									+
									(64 * b_ModulNbr));

			  /***************************/
								/* Save the interrupt flag */
			  /***************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									b_InterruptMask
									=
									devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									b_InterruptMask
									& (0xFF
									-
									(1 << b_TimerNbr));
							}	// if (b_InterruptEnable == APCI1710_ENABLE)

		       /***********************/
							/* Test if error occur */
		       /***********************/

							if (i_ReturnValue >= 0) {
			  /***************************/
								/* Save the interrupt flag */
			  /***************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									b_InterruptMask
									=
									devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									b_InterruptMask
									| ((1 & b_InterruptEnable) << b_TimerNbr);

			  /********************/
								/* Enable the timer */
			  /********************/

								outl(1, devpriv->s_BoardInfos.ui_Address + 44 + (b_TimerNbr * 4) + (64 * b_ModulNbr));
							}
						} else {
		       /********************************/
							/* Interrupt parameter is wrong */
		       /********************************/

							DPRINTK("\n");
							i_ReturnValue = -6;
						}
						break;
					case APCI1710_DISABLE:
			/***************************/
						/* Test the interrupt flag */
		    /***************************/

						if (((devpriv->s_ModuleInfo
									[b_ModulNbr].
									s_82X54ModuleInfo.
									b_InterruptMask
									>>
									b_TimerNbr)
								& 1) == 1) {
		       /*************************/
							/* Disable the interrupt */
		       /*************************/

							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_82X54ModuleInfo.
								s_82X54TimerInfo
								[b_TimerNbr].
								dw_ConfigurationWord
								=
								devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_82X54ModuleInfo.
								s_82X54TimerInfo
								[b_TimerNbr].
								dw_ConfigurationWord
								& 0xF7;

							outl(devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_82X54ModuleInfo.
								s_82X54TimerInfo
								[b_TimerNbr].
								dw_ConfigurationWord,
								devpriv->
								s_BoardInfos.
								ui_Address +
								32 +
								(b_TimerNbr *
									4) +
								(64 * b_ModulNbr));

		       /***************************/
							/* Save the interrupt flag */
		       /***************************/

							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_82X54ModuleInfo.
								b_InterruptMask
								=
								devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_82X54ModuleInfo.
								b_InterruptMask
								& (0xFF -
								(1 << b_TimerNbr));
						}

		    /*********************/
						/* Disable the timer */
		    /*********************/

						outl(0, devpriv->s_BoardInfos.
							ui_Address + 44 +
							(b_TimerNbr * 4) +
							(64 * b_ModulNbr));
						break;
					}	// Switch end
				} else {
		    /**************************************/
					/* Timer not initialised see function */
		    /**************************************/

					DPRINTK("Timer not initialised see function\n");
					i_ReturnValue = -5;
				}
			} else {
		 /*************************/
				/* Timer selection wrong */
		 /*************************/

				DPRINTK("Timer selection wrong\n");
				i_ReturnValue = -3;
			}	// if ((b_TimerNbr >= 0) && (b_TimerNbr <= 2))
		} else {
	      /************************************/
			/* The module is not a TIMER module */
	      /************************************/

			DPRINTK("The module is not a TIMER module\n");
			i_ReturnValue = -4;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/

		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_ReadAllTimerValue                 |
|                                       (BYTE_     b_BoardHandle,            |
|                                        BYTE_     b_ModulNbr,               |
|                                        PULONG_ pul_TimerValueArray)
INT i_APCI1710_InsnReadAllTimerValue(comedi_device *dev,comedi_subdevice *s,
	comedi_insn *insn,lsampl_t *data)        |
+----------------------------------------------------------------------------+
| Task              : Return the all timer values from selected timer        |
|                     module (b_ModulNbr).                                   |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_   b_BoardHandle     : Handle of board            |
|                                                 APCI-1710                  |
|                     BYTE_   b_ModulNbr        : Selected module number     |
|                                                 (0 to 3)                   |
+----------------------------------------------------------------------------+
| Output Parameters : PULONG_ pul_TimerValueArray : Timer value array.       |
|                           Element 0 contain the timer 0 value.             |
|                           Element 1 contain the timer 1 value.             |
|                           Element 2 contain the timer 2 value.             |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: Module selection wrong                              |
|                    -3: The module is not a TIMER module                    |
|                    -4: Timer 0 not initialised see function                |
|                        "i_APCI1710_InitTimer"                              |
|                    -5: Timer 1 not initialised see function                |
|                        "i_APCI1710_InitTimer"                              |
|                    -6: Timer 2 not initialised see function                |
|                        "i_APCI1710_InitTimer"                              |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnReadAllTimerValue(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data)
{
	INT i_ReturnValue = 0;
	BYTE b_ModulNbr, b_ReadType;
	PULONG pul_TimerValueArray;

	b_ModulNbr = CR_AREF(insn->chanspec);
	b_ReadType = CR_CHAN(insn->chanspec);
	pul_TimerValueArray = (PULONG) data;
	i_ReturnValue = insn->n;

	switch (b_ReadType) {
	case APCI1710_TIMER_READINTERRUPT:

		data[0] = devpriv->s_InterruptParameters.
			s_FIFOInterruptParameters[devpriv->
			s_InterruptParameters.ui_Read].b_OldModuleMask;
		data[1] = devpriv->s_InterruptParameters.
			s_FIFOInterruptParameters[devpriv->
			s_InterruptParameters.ui_Read].ul_OldInterruptMask;
		data[2] = devpriv->s_InterruptParameters.
			s_FIFOInterruptParameters[devpriv->
			s_InterruptParameters.ui_Read].ul_OldCounterLatchValue;

			     /**************************/
		/* Increment the read FIFO */
			     /***************************/

		devpriv->
			s_InterruptParameters.
			ui_Read = (devpriv->
			s_InterruptParameters.
			ui_Read + 1) % APCI1710_SAVE_INTERRUPT;

		break;

	case APCI1710_TIMER_READALLTIMER:
	/**************************/
		/* Test the module number */
	/**************************/

		if (b_ModulNbr < 4) {
	   /***********************/
			/* Test if 82X54 timer */
	   /***********************/

			if ((devpriv->s_BoardInfos.
					dw_MolduleConfiguration[b_ModulNbr] &
					0xFFFF0000UL) == APCI1710_82X54_TIMER) {
	      /********************************/
				/* Test if timer 0 iniutialised */
	      /********************************/

				if (devpriv->s_ModuleInfo[b_ModulNbr].
					s_82X54ModuleInfo.
					s_82X54TimerInfo[0].b_82X54Init == 1) {
		 /********************************/
					/* Test if timer 1 iniutialised */
		 /********************************/

					if (devpriv->s_ModuleInfo[b_ModulNbr].
						s_82X54ModuleInfo.
						s_82X54TimerInfo[1].
						b_82X54Init == 1) {
		    /********************************/
						/* Test if timer 2 iniutialised */
		    /********************************/

						if (devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_82X54ModuleInfo.
							s_82X54TimerInfo[2].
							b_82X54Init == 1) {
		       /*********************/
							/* Latch all counter */
		       /*********************/

							outl(0x17,
								devpriv->
								s_BoardInfos.
								ui_Address +
								12 +
								(64 * b_ModulNbr));

		       /**************************/
							/* Read the timer 0 value */
		       /**************************/

							pul_TimerValueArray[0] =
								inl(devpriv->
								s_BoardInfos.
								ui_Address + 0 +
								(64 * b_ModulNbr));

		       /**************************/
							/* Read the timer 1 value */
		       /**************************/

							pul_TimerValueArray[1] =
								inl(devpriv->
								s_BoardInfos.
								ui_Address + 4 +
								(64 * b_ModulNbr));

		       /**************************/
							/* Read the timer 2 value */
		       /**************************/

							pul_TimerValueArray[2] =
								inl(devpriv->
								s_BoardInfos.
								ui_Address + 8 +
								(64 * b_ModulNbr));
						} else {
		       /****************************************/
							/* Timer 2 not initialised see function */
		       /****************************************/

							DPRINTK("Timer 2 not initialised see function\n");
							i_ReturnValue = -6;
						}
					} else {
		    /****************************************/
						/* Timer 1 not initialised see function */
		    /****************************************/

						DPRINTK("Timer 1 not initialised see function\n");
						i_ReturnValue = -5;
					}
				} else {
		 /****************************************/
					/* Timer 0 not initialised see function */
		 /****************************************/

					DPRINTK("Timer 0 not initialised see function\n");
					i_ReturnValue = -4;
				}
			} else {
	      /************************************/
				/* The module is not a TIMER module */
	      /************************************/

				DPRINTK("The module is not a TIMER module\n");
				i_ReturnValue = -3;
			}
		} else {
	   /***********************/
			/* Module number error */
	   /***********************/

			DPRINTK("Module number error\n");
			i_ReturnValue = -2;
		}

	}			// End of Switch
	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     :INT i_APCI1710_InsnBitsTimer(comedi_device *dev,
comedi_subdevice *s,comedi_insn *insn,lsampl_t *data)                   |
+----------------------------------------------------------------------------+
| Task              : Read write functions for Timer                                          |
+----------------------------------------------------------------------------+
| Input Parameters  : 
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnBitsTimer(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data)
{
	BYTE b_BitsType;
	INT i_ReturnValue = 0;
	b_BitsType = data[0];

	printk("\n82X54");

	switch (b_BitsType) {
	case APCI1710_TIMER_READVALUE:
		i_ReturnValue = i_APCI1710_ReadTimerValue(dev,
			(BYTE) CR_AREF(insn->chanspec),
			(BYTE) CR_CHAN(insn->chanspec), (PULONG) & data[0]);
		break;

	case APCI1710_TIMER_GETOUTPUTLEVEL:
		i_ReturnValue = i_APCI1710_GetTimerOutputLevel(dev,
			(BYTE) CR_AREF(insn->chanspec),
			(BYTE) CR_CHAN(insn->chanspec), (PBYTE) & data[0]);
		break;

	case APCI1710_TIMER_GETPROGRESSSTATUS:
		i_ReturnValue = i_APCI1710_GetTimerProgressStatus(dev,
			(BYTE) CR_AREF(insn->chanspec),
			(BYTE) CR_CHAN(insn->chanspec), (PBYTE) & data[0]);
		break;

	case APCI1710_TIMER_WRITEVALUE:
		i_ReturnValue = i_APCI1710_WriteTimerValue(dev,
			(BYTE) CR_AREF(insn->chanspec),
			(BYTE) CR_CHAN(insn->chanspec), (ULONG) data[1]);

		break;

	default:
		printk("Bits Config Parameter Wrong\n");
		i_ReturnValue = -1;
	}

	if (i_ReturnValue >= 0)
		i_ReturnValue = insn->n;
	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_ReadTimerValue                    |
|                                       (BYTE_     b_BoardHandle,            |
|                                        BYTE_     b_ModulNbr,               |
|                                        BYTE_     b_TimerNbr,               |
|                                        PULONG_ pul_TimerValue)             |
+----------------------------------------------------------------------------+
| Task              : Return the timer value from selected digital timer     |
|                     (b_TimerNbr) from selected timer  module (b_ModulNbr). |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_   b_BoardHandle     : Handle of board            |
|                                                 APCI-1710                  |
|                     BYTE_   b_ModulNbr        : Selected module number     |
|                                                 (0 to 3)                   |
|                     BYTE_   b_TimerNbr        : Timer number to read       |
|                                                 (0 to 2)                   |
+----------------------------------------------------------------------------+
| Output Parameters : PULONG_ pul_TimerValue    : Timer value                |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: Module selection wrong                              |
|                    -3: Timer selection wrong                               |
|                    -4: The module is not a TIMER module                    |
|                    -5: Timer not initialised see function                  |
|                        "i_APCI1710_InitTimer"                              |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_ReadTimerValue(comedi_device * dev,
	BYTE b_ModulNbr, BYTE b_TimerNbr, PULONG pul_TimerValue)
{
	INT i_ReturnValue = 0;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if 82X54 timer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_82X54_TIMER) {
	      /*************************/
			/* Test the timer number */
	      /*************************/

			if (b_TimerNbr <= 2) {
		 /*****************************/
				/* Test if timer initialised */
		 /*****************************/

				if (devpriv->
					s_ModuleInfo[b_ModulNbr].
					s_82X54ModuleInfo.
					s_82X54TimerInfo[b_TimerNbr].
					b_82X54Init == 1) {
		    /*************************/
					/* Latch the timer value */
		    /*************************/

					outl((2 << b_TimerNbr) | 0xD0,
						devpriv->s_BoardInfos.
						ui_Address + 12 +
						(64 * b_ModulNbr));

		    /**************************/
					/* Read the counter value */
		    /**************************/

					*pul_TimerValue =
						inl(devpriv->s_BoardInfos.
						ui_Address + (b_TimerNbr * 4) +
						(64 * b_ModulNbr));
				} else {
		    /**************************************/
					/* Timer not initialised see function */
		    /**************************************/
					DPRINTK("Timer not initialised see function\n");
					i_ReturnValue = -5;
				}
			} else {
		 /*************************/
				/* Timer selection wrong */
		 /*************************/
				DPRINTK("Timer selection wrong\n");
				i_ReturnValue = -3;
			}	// if ((b_TimerNbr >= 0) && (b_TimerNbr <= 2))
		} else {
	      /************************************/
			/* The module is not a TIMER module */
	      /************************************/
			DPRINTK("The module is not a TIMER module\n");
			i_ReturnValue = -4;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/
		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

	/*
	   +----------------------------------------------------------------------------+
	   | Function Name     : _INT_     i_APCI1710_GetTimerOutputLevel               |
	   |                                       (BYTE_     b_BoardHandle,            |
	   |                                        BYTE_     b_ModulNbr,               |
	   |                                        BYTE_     b_TimerNbr,               |
	   |                                        PBYTE_   pb_OutputLevel)            |
	   +----------------------------------------------------------------------------+
	   | Task              : Return the output signal level (pb_OutputLevel) from   |
	   |                     selected digital timer (b_TimerNbr) from selected timer|
	   |                     module (b_ModulNbr).                                   |
	   +----------------------------------------------------------------------------+
	   | Input Parameters  : BYTE_   b_BoardHandle     : Handle of board            |
	   |                                                 APCI-1710                  |
	   |                     BYTE_   b_ModulNbr        : Selected module number     |
	   |                                                 (0 to 3)                   |
	   |                     BYTE_   b_TimerNbr        : Timer number to test       |
	   |                                                 (0 to 2)                   |
	   +----------------------------------------------------------------------------+
	   | Output Parameters : PBYTE_ pb_OutputLevel     : Output signal level        |
	   |                                                 0 : The output is low      |
	   |                                                 1 : The output is high     |
	   +----------------------------------------------------------------------------+
	   | Return Value      : 0: No error                                            |
	   |                    -1: The handle parameter of the board is wrong          |
	   |                    -2: Module selection wrong                              |
	   |                    -3: Timer selection wrong                               |
	   |                    -4: The module is not a TIMER module                    |
	   |                    -5: Timer not initialised see function                  |
	   |                        "i_APCI1710_InitTimer"                              |
	   +----------------------------------------------------------------------------+
	 */

INT i_APCI1710_GetTimerOutputLevel(comedi_device * dev,
	BYTE b_ModulNbr, BYTE b_TimerNbr, PBYTE pb_OutputLevel)
{
	INT i_ReturnValue = 0;
	DWORD dw_TimerStatus;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if 82X54 timer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_82X54_TIMER) {
	      /*************************/
			/* Test the timer number */
	      /*************************/

			if (b_TimerNbr <= 2) {
		 /*****************************/
				/* Test if timer initialised */
		 /*****************************/

				if (devpriv->
					s_ModuleInfo[b_ModulNbr].
					s_82X54ModuleInfo.
					s_82X54TimerInfo[b_TimerNbr].
					b_82X54Init == 1) {
		    /*************************/
					/* Latch the timer value */
		    /*************************/

					outl((2 << b_TimerNbr) | 0xE0,
						devpriv->s_BoardInfos.
						ui_Address + 12 +
						(64 * b_ModulNbr));

		    /*************************/
					/* Read the timer status */
		    /*************************/

					dw_TimerStatus =
						inl(devpriv->s_BoardInfos.
						ui_Address + 16 +
						(b_TimerNbr * 4) +
						(64 * b_ModulNbr));

					*pb_OutputLevel =
						(BYTE) (((dw_TimerStatus >> 7) &
							1) ^ devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_82X54ModuleInfo.
						s_82X54TimerInfo[b_TimerNbr].
						b_OutputLevel);
				} else {
		    /**************************************/
					/* Timer not initialised see function */
		    /**************************************/
					DPRINTK("Timer not initialised see function\n");
					i_ReturnValue = -5;
				}
			} else {
		 /*************************/
				/* Timer selection wrong */
		 /*************************/
				DPRINTK("Timer selection wrong\n");
				i_ReturnValue = -3;
			}	// if ((b_TimerNbr >= 0) && (b_TimerNbr <= 2))
		} else {
	      /************************************/
			/* The module is not a TIMER module */
	      /************************************/
			DPRINTK("The module is not a TIMER module\n");
			i_ReturnValue = -4;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/
		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_GetTimerProgressStatus            |
|                                       (BYTE_     b_BoardHandle,            |
|                                        BYTE_     b_ModulNbr,               |
|                                        BYTE_     b_TimerNbr,               |
|                                        PBYTE_   pb_TimerStatus)            |
+----------------------------------------------------------------------------+
| Task              : Return the progress status (pb_TimerStatus) from       |
|                     selected digital timer (b_TimerNbr) from selected timer|
|                     module (b_ModulNbr).                                   |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_   b_BoardHandle     : Handle of board            |
|                                                 APCI-1710                  |
|                     BYTE_   b_ModulNbr        : Selected module number     |
|                                                 (0 to 3)                   |
|                     BYTE_   b_TimerNbr        : Timer number to test       |
|                                                 (0 to 2)                   |
+----------------------------------------------------------------------------+
| Output Parameters : PBYTE_ pb_TimerStatus     : Output signal level        |
|                                                 0 : Timer not in progress  |
|                                                 1 : Timer in progress      |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: Module selection wrong                              |
|                    -3: Timer selection wrong                               |
|                    -4: The module is not a TIMER module                    |
|                    -5: Timer not initialised see function                  |
|                        "i_APCI1710_InitTimer"                              |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_GetTimerProgressStatus(comedi_device * dev,
	BYTE b_ModulNbr, BYTE b_TimerNbr, PBYTE pb_TimerStatus)
{
	INT i_ReturnValue = 0;
	DWORD dw_TimerStatus;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if 82X54 timer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_82X54_TIMER) {
	      /*************************/
			/* Test the timer number */
	      /*************************/

			if (b_TimerNbr <= 2) {
		 /*****************************/
				/* Test if timer initialised */
		 /*****************************/

				if (devpriv->
					s_ModuleInfo[b_ModulNbr].
					s_82X54ModuleInfo.
					s_82X54TimerInfo[b_TimerNbr].
					b_82X54Init == 1) {
		    /*************************/
					/* Latch the timer value */
		    /*************************/

					outl((2 << b_TimerNbr) | 0xE0,
						devpriv->s_BoardInfos.
						ui_Address + 12 +
						(64 * b_ModulNbr));

		    /*************************/
					/* Read the timer status */
		    /*************************/

					dw_TimerStatus =
						inl(devpriv->s_BoardInfos.
						ui_Address + 16 +
						(b_TimerNbr * 4) +
						(64 * b_ModulNbr));

					*pb_TimerStatus =
						(BYTE) ((dw_TimerStatus) >> 8) &
						1;
					printk("ProgressStatus : %d",
						*pb_TimerStatus);
				} else {
		    /**************************************/
					/* Timer not initialised see function */
		    /**************************************/

					i_ReturnValue = -5;
				}
			} else {
		 /*************************/
				/* Timer selection wrong */
		 /*************************/

				i_ReturnValue = -3;
			}	// if ((b_TimerNbr >= 0) && (b_TimerNbr <= 2))
		} else {
	      /************************************/
			/* The module is not a TIMER module */
	      /************************************/

			i_ReturnValue = -4;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/

		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_WriteTimerValue                   |
|                                       (BYTE_   b_BoardHandle,              |
|                                        BYTE_   b_ModulNbr,                 |
|                                        BYTE_   b_TimerNbr,                 |
|                                        ULONG_ ul_WriteValue)               |
+----------------------------------------------------------------------------+
| Task              : Write the value (ul_WriteValue) into the selected timer|
|                     (b_TimerNbr) from selected timer module (b_ModulNbr).  |
|                     The action in depend of the time mode selection.       |
|                     See timer mode description table.                      |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_   b_BoardHandle     : Handle of board            |
|                                                 APCI-1710                  |
|                     BYTE_   b_ModulNbr        : Selected module number     |
|                                                 (0 to 3)                   |
|                     BYTE_   b_TimerNbr        : Timer number to write      |
|                                                 (0 to 2)                   |
|                     ULONG_ ul_WriteValue      : Value to write             |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: Module selection wrong                              |
|                    -3: Timer selection wrong                               |
|                    -4: The module is not a TIMER module                    |
|                    -5: Timer not initialised see function                  |
|                        "i_APCI1710_InitTimer"                              |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_WriteTimerValue(comedi_device * dev,
	BYTE b_ModulNbr, BYTE b_TimerNbr, ULONG ul_WriteValue)
{
	INT i_ReturnValue = 0;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if 82X54 timer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_82X54_TIMER) {
	      /*************************/
			/* Test the timer number */
	      /*************************/

			if (b_TimerNbr <= 2) {
		 /*****************************/
				/* Test if timer initialised */
		 /*****************************/

				if (devpriv->
					s_ModuleInfo[b_ModulNbr].
					s_82X54ModuleInfo.
					s_82X54TimerInfo[b_TimerNbr].
					b_82X54Init == 1) {
		    /*******************/
					/* Write the value */
		    /*******************/

					outl(ul_WriteValue,
						devpriv->s_BoardInfos.
						ui_Address + (b_TimerNbr * 4) +
						(64 * b_ModulNbr));
				} else {
		    /**************************************/
					/* Timer not initialised see function */
		    /**************************************/
					DPRINTK("Timer not initialised see function\n");
					i_ReturnValue = -5;
				}
			} else {
		 /*************************/
				/* Timer selection wrong */
		 /*************************/
				DPRINTK("Timer selection wrong\n");
				i_ReturnValue = -3;
			}	// if ((b_TimerNbr >= 0) && (b_TimerNbr <= 2))
		} else {
	      /************************************/
			/* The module is not a TIMER module */
	      /************************************/
			DPRINTK("The module is not a TIMER module\n");
			i_ReturnValue = -4;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/
		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}
