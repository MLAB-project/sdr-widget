/* -*- mode: c++; tab-width: 4; c-basic-offset: 4 -*- */
/* This source file is part of the ATMEL AVR32-SoftwareFramework-AT32UC3-1.5.0 Release */

/*This file is prepared for Doxygen automatic documentation generation.*/
/*! \file ******************************************************************
 *
 * \brief Management of the USB device Audio task.
 *
 * This file manages the USB device Audio task.
 *
 * - Compiler:           IAR EWAVR32 and GNU GCC for AVR32
 * - Supported devices:  All AVR32 devices with a USB module can be used.
 * - AppNote:
 *
 * \author               Atmel Corporation: http://www.atmel.com \n
 *                       Support and FAQ: http://support.atmel.no/
 *
 ***************************************************************************/

/* Copyright (c) 2009 Atmel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an Atmel
 * AVR product.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 *
 * Modified by Alex Lee and SDR-Widget team for the sdr-widget project.
 * See http://code.google.com/p/sdr-widget/
 * Copyright under GNU General Public License v2
 */

//_____  I N C L U D E S ___________________________________________________

#include <stdio.h>
#include "usart.h"     // Shall be included before FreeRTOS header files, since 'inline' is defined to ''; leading to
                       // link errors
#include "conf_usb.h"
#include "features.h"

#if USB_DEVICE_FEATURE == ENABLED

#include "board.h"
#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#endif
#include "pdca.h"
#include "gpio.h"
#include "usb_drv.h"
#include "usb_descriptors.h"
#include "hpsdr_usb_descriptors.h"
#include "usb_standard_request.h"
#include "usb_specific_request.h"
#include "hpsdr_usb_specific_request.h"
#include "device_audio_task.h"
#include "hpsdr_device_audio_task.h"

#if LCD_DISPLAY				// Multi-line LCD display
#include "taskLCD.h"
#endif

//#include "taskEXERCISE.h"
#include "composite_widget.h"
#include "taskAK5394A.h"
#include "hpsdr_taskAK5394A.h"

//_____ M A C R O S ________________________________________________________


//_____ D E F I N I T I O N S ______________________________________________


//_____ D E C L A R A T I O N S ____________________________________________


static U32  index, spk_index;
static U8 /*audio_buffer_out,*/ spk_buffer_in;	// the ID number of the buffer used for sending out
											// to the USB and reading from USB
static U8 rxbuff_out;

U8 command [4][5];
U8 command_out [8];

static U8 ep_audio_in, ep_audio_out, ep_audio_out_fb;

//!
//! @brief This function initializes the hardware/software resources
//! required for device Audio task.
//!
void hpsdr_device_audio_task_init(U8 ep_in, U8 ep_out, U8 ep_out_fb)
{
	index     =0;
	//audio_buffer_out = 0;
	rxbuff_out = 0;
	spk_index = 0;
	spk_buffer_in = 0;
	mute = FALSE;
	spk_mute = FALSE;
	ep_audio_in = ep_in;
	ep_audio_out = ep_out;
	ep_audio_out_fb = ep_out_fb;

	xTaskCreate(hpsdr_device_audio_task,
				configTSK_USB_DAUDIO_NAME,
				configTSK_USB_DAUDIO_STACK_SIZE,
				NULL,
				configTSK_USB_DAUDIO_PRIORITY,
				NULL);


}


//!
//! @brief Entry point of the device Audio task management
//!

void hpsdr_device_audio_task(void *pvParameters)
{
	static U32  time=0;
	static Bool startup=TRUE;
	int i, j = 0;
	int overflow_flag = 0;
	U16 num_samples, gap = 0;
	U16 prevgap = 0;

	U8 sample_MSB;
	U8 sample_SB;
	U8 sample_LSB;

	const U8 EP_IQ_IN = ep_audio_in;
	const U8 EP_IQ_OUT = ep_audio_out;
	const U8 IN_LEFT = FEATURE_IN_NORMAL ? 0 : 1;
	const U8 IN_RIGHT = FEATURE_IN_NORMAL ? 1 : 0;

	for (i=0; i < 5; i++){
		for (j=0; j < 4; j++) command[j][i] = 0;
	}


	portTickType xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();

	while (TRUE) {
		vTaskDelayUntil(&xLastWakeTime, HPSDR_configTSK_USB_DAUDIO_PERIOD);

		int rxbuff_in = (rxbuff_next-1+RXBUFF_NO_OF_CHUNKS) % RXBUFF_NO_OF_CHUNKS;

		// First, check the device enumeration state
		if (!Is_device_enumerated()) { time=0; startup=TRUE; continue; };

		if( startup ) {

			time+=HPSDR_configTSK_USB_DAUDIO_PERIOD;
#define STARTUP_LED_DELAY  4000
			if ( time<= 1*STARTUP_LED_DELAY ) {
				LED_On( LED0 );
				pdca_disable_interrupt_reload_counter_zero(PDCA_CHANNEL_SSC_RX);
				pdca_disable(PDCA_CHANNEL_SSC_RX);
			} else if( time== 2*STARTUP_LED_DELAY ) LED_On( LED1 );
			else if( time== 3*STARTUP_LED_DELAY ) LED_On( LED2 );
			else if( time== 4*STARTUP_LED_DELAY ) LED_On( LED3 );
			else if( time== 5*STARTUP_LED_DELAY ) {
				LED_Off( LED0 );
			} else if( time== 6*STARTUP_LED_DELAY ) LED_Off( LED1 );
			else if( time== 7*STARTUP_LED_DELAY ) LED_Off( LED2 );
			else if( time== 8*STARTUP_LED_DELAY ) LED_Off( LED3 );
			else if( time >= 9*STARTUP_LED_DELAY ) {
				startup=FALSE;
				rxbuff_out = rxbuff_in;
				index = 0;
				gap = 0;
				overflow_flag = 0;
				freq_changed = 1;						// force a freq change reset
			}
		}

		/*
		// recover from stalls
		while (Is_usb_endpoint_stall_requested(EP_IQ_IN))
		{
		if (Is_usb_setup_received()) usb_process_request();
		}

		while (Is_usb_endpoint_stall_requested(EP_IQ_OUT))
		{
		if (Is_usb_setup_received()) usb_process_request();
		}
		*/

		//  Fill frames of 512 bytes and send to host

		num_samples = 63;	// (512 bytes - 8 bytes (sync+command)) / 8 (6 bytes I/Q + 2 bytes Mic)

		//  wait till there are enough samples in the audio buffer
		// AK data is being filled into ~audio_buffer_in, ie if audio_buffer_in is 0
		// buffer 0 is set in the reload register of the pdca
		// So the actual loading is occuring in buffer 1
		// USB data is being taken from audio_buffer_out

		// find out the current status of PDCA transfer
		// gap is how far the audio_buffer_out is from overlapping audio_buffer_in

		int chunksgap = (rxbuff_in-rxbuff_out+RXBUFF_NO_OF_CHUNKS) % RXBUFF_NO_OF_CHUNKS;

		/* prevgap will be the last cycle's gap. if samples were transferred last cycle,
		   the gap was subtracted */
		prevgap = gap;

		/* calculate gap with new info on DMA position */
		gap = chunksgap*RXBUFF_CHUNK_SIZE - index;

		/* did gap suddenly decrease? must be an overflow */
		if (gap < prevgap)
			overflow_flag = 1;

		if (!startup && (Is_usb_in_ready(EP_IQ_IN)) && (gap > (num_samples * 2))) {
			gap -= num_samples * 2;

			Usb_reset_endpoint_fifo_access(EP_IQ_IN);

			// fill the 1st 8 bytes with SYNC and CONTROL as in HPSDR protocol
			for (i=0; i < 3; i++) Usb_write_endpoint_data(EP_IQ_IN, 8, 0x7f);
			//for (i=0; i < 5; i++) Usb_write_endpoint_data(EP_IQ_IN, 8, command[j][i]);
			Usb_write_endpoint_data(EP_IQ_IN, 8, overflow_flag << 7);
			overflow_flag = 0;
			for (i=0; i<4; i++)
				Usb_write_endpoint_data(EP_IQ_IN, 8, 0);

			j++;
			if (j > 3) j = 0;

			for( i=0 ; i < num_samples ; i++ ) {
				// Fill endpoint with samples
				if(!mute) {
					U32 *buff = (U32 *) rx_buffers[rxbuff_out];

					sample_LSB = buff[index+IN_LEFT];
					sample_SB = buff[index+IN_LEFT] >> 8;
					sample_MSB = buff[index+IN_LEFT] >> 16;

					Usb_write_endpoint_data(EP_IQ_IN, 8, sample_MSB);
					Usb_write_endpoint_data(EP_IQ_IN, 8, sample_SB);
					Usb_write_endpoint_data(EP_IQ_IN, 8, sample_LSB);

					sample_LSB = buff[index+IN_RIGHT];
					sample_SB = buff[index+IN_RIGHT] >> 8;
					sample_MSB = buff[index+IN_RIGHT] >> 16;

					Usb_write_endpoint_data(EP_IQ_IN, 8, sample_MSB);
					Usb_write_endpoint_data(EP_IQ_IN, 8, sample_SB);
					Usb_write_endpoint_data(EP_IQ_IN, 8, sample_LSB);

					index += 2;
					if (index >= RXBUFF_CHUNK_SIZE) {
						index=0;
						rxbuff_out++;
						if (rxbuff_out >= RXBUFF_NO_OF_CHUNKS) rxbuff_out = 0;
					}
				} else {
					Usb_write_endpoint_data(EP_IQ_IN, 8, 0x00);
					Usb_write_endpoint_data(EP_IQ_IN, 8, 0x00);
					Usb_write_endpoint_data(EP_IQ_IN, 8, 0x00);
					Usb_write_endpoint_data(EP_IQ_IN, 8, 0x00);
					Usb_write_endpoint_data(EP_IQ_IN, 8, 0x00);
					Usb_write_endpoint_data(EP_IQ_IN, 8, 0x00);

				}
				// 2 bytes of Mic input data
				Usb_write_endpoint_data(EP_IQ_IN, 8, 0x00);
				Usb_write_endpoint_data(EP_IQ_IN, 8, 0x00);

			}
			Usb_ack_in_ready_send(EP_IQ_IN);		// send the current bank
		}	// end if in ready

		if (Is_usb_out_received(EP_IQ_OUT) && (Usb_byte_count(EP_IQ_OUT) >= 8)) {
			Usb_reset_endpoint_fifo_access(EP_IQ_OUT);

			// read the first 8 bytes of command for now
			for (i=0; i < 8; i++) command_out[i] = Usb_read_endpoint_data(EP_IQ_OUT, 8);

			if ( (command_out[0] & 0xfe) == 0) {
				switch (command_out[1] & 0x03) {
				case 0:
					if (current_freq.frequency != 48000) {
						current_freq.frequency = 48000;
						freq_changed = TRUE;
					}
					break;
				case 1:
					if (current_freq.frequency != 96000) {
						current_freq.frequency = 96000;
						freq_changed = TRUE;
					}
					break;
				case 2:
					if (current_freq.frequency != 192000) {
						current_freq.frequency = 192000;
						freq_changed = TRUE;
					}
					break;
				default:
					break;
				}
			}

			Usb_ack_out_received_free(EP_IQ_OUT);
		}	// end if out received


	} // end while vTask

}

#endif  // USB_DEVICE_FEATURE == ENABLED
