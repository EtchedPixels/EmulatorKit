// license:BSD-3-Clause
// copyright-holders:Joakim Larsson Edstrom 

// Copyright (c) Michal Tomek 2018-2021
// based on Z80SCC 

/***************************************************************************

    Z280 UART

****************************************************************************/

#ifndef __Z280UART_H
#define __Z280UART_H

#pragma once

// ======================> z280uart_device

typedef void (*tx_callback_t)(device_t *device, int channel, UINT8 data);
typedef int (*rx_callback_t)(device_t *device, int channel);

struct z280uart_device {
	char *m_tag;
	//UINT32 m_type;
	//UINT32 m_clock;
	void *m_owner;

	// internal state

	// Register state
	uint8_t m_uartcr;
	uint8_t m_tcsr;
	uint8_t m_rcsr;

	//unsigned int m_brg_const;
	uint16_t m_clock_divisor;
	uint16_t m_timer;
	unsigned int m_brg_rate;

	// receiver state
	//int m_rx_clock;         // receive clock pulse count
	UINT8 rx_bits_rem;
	UINT8 rx_data;
	UINT8 m_rdr;

	//int m_rxd; 	

	// transmitter state
	//int m_tx_clock;             // transmit clock pulse count
	UINT8 tx_bits_rem;
	UINT8 tx_data;
	UINT8 m_tdr;

	UINT8 m_bit_count;

	//int m_txd;
	
	// byte rx/tx callbacks
	// Note: bit rx/tx callbacks are not implemented
	tx_callback_t tx_callback;
	rx_callback_t rx_callback;
    
	// interrupt line callback
	//devcb_write_line    m_out_int_cb;

};

struct z280uart_device *z280uart_device_create(void *owner, char *tag, /*UINT32 type, UINT32 clock,*/
	rx_callback_t rx_callback,tx_callback_t tx_callback);
void z280uart_device_reset(struct z280uart_device *device);
void z280uart_device_timer(struct z280uart_device *device /*, emu_timer *timer, device_timer_id id, int param, void *ptr*/);
uint8_t z280uart_device_register_read(struct z280uart_device *device, uint8_t reg);
void z280uart_device_register_write(struct z280uart_device *device, uint8_t reg, uint8_t data);

#endif // __Z280ASCI_H
