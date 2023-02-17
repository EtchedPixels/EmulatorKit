// license:BSD-3-Clause
// copyright-holders:Joakim Larsson Edstrom

// Copyright (c) Michal Tomek 2018-2021
// based on Z80SCC

/***************************************************************************

    Z280 UART

****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "z280.h"

#define UARTCR_BC 0xC0
#define UARTCR_P  0x20
#define UARTCR_EO 0x10
#define UARTCR_CS 0x8
#define UARTCR_CR 0x6
#define UARTCR_LB 0x1

#define TCSR_EN   0x80
#define TCSR_IE   0x40
#define TCSR_SB   0x10
#define TCSR_BRK  0x8
#define TCSR_FRC  0x4
#define TCSR_VAL  0x2
#define TCSR_BE   0x1

#define RCSR_EN   0x80
#define RCSR_IE   0x40
#define RCSR_CA   0x10
#define RCSR_FE   0x8
#define RCSR_PE   0x4
#define RCSR_OVE  0x2
#define RCSR_ERR  0x1

#define FUNCNAME __func__

// exports from z280
void set_irq_internal(device_t *device, int irq, int state);
UINT32 get_brg_const_z280(struct z280_device *d);

//**************************************************************************
//  FORWARD DECLARATIONS
//**************************************************************************
#define receive_register_reset(d) d->rx_bits_rem = 0
#define transmit_register_reset(d) d->tx_bits_rem = 0
#define set_tra_rate(d,x) /**/
#define set_rcv_rate(d,x) /**/
#define is_transmit_register_empty(d) (d->tx_bits_rem == 0)
#define transmit_register_setup(d,data) d->tx_data = data; d->tx_bits_rem = d->m_bit_count	/* load character into shift register */
#define transmit_register_get_data_bit(d) 0
#define receive_register_extract(d) /**/
#define get_received_char(d) d->rx_data 

void z280uart_device_start(struct z280uart_device *d);
void z280uart_device_reset(struct z280uart_device *d);
void z280uart_device_tra_callback(struct z280uart_device *d);
void z280uart_device_rcv_callback(struct z280uart_device *d);
void z280uart_device_tra_complete(struct z280uart_device *d);
void z280uart_device_rcv_complete(struct z280uart_device *d);
//void z280uart_device_m_rx_fifo_rp_step(struct z280uart_device *d);
void z280uart_device_receive_data(struct z280uart_device *d, uint8_t data);
uint8_t z280uart_device_data_read(struct z280uart_device *d);
void z280uart_device_data_write(struct z280uart_device *d, uint8_t data);
void z280uart_device_update_serial(struct z280uart_device *d);


struct z280uart_device *z280uart_device_create(void *owner, char *tag, /*UINT32 type, UINT32 clock,*/
	rx_callback_t rx_callback,tx_callback_t tx_callback) {

	struct z280uart_device *d = malloc(sizeof(struct z280uart_device));
	memset(d,0,sizeof(struct z280uart_device));
	d->m_owner = owner;
	//d->m_type = type;
	//d->m_clock = clock;
	d->m_tag = tag;

	//d->m_out_int_cb = out_int_cb;
	d->tx_callback = tx_callback;
	d->rx_callback = rx_callback;

	z280uart_device_start(d);
	z280uart_device_reset(d);

	return d;
}

//-------------------------------------------------
//  start - device startup
//-------------------------------------------------
void z280uart_device_start(struct z280uart_device *d)
{
}

/*
 * Interrupts
*/

//-------------------------------------------------
//  check_interrupts -
//-------------------------------------------------
void z280uart_device_check_txint(struct z280uart_device *d)
{
	int i = (d->m_tcsr & (TCSR_IE | TCSR_BE)) == (TCSR_IE | TCSR_BE);
	LOG("%s %s set UARTTX int:%d\n", FUNCNAME, d->m_tag, i);
	set_irq_internal(d->m_owner, Z280_INT_UARTTX, i);
}

void z280uart_device_check_rxint(struct z280uart_device *d)
{
	int i = (d->m_rcsr & (RCSR_IE | RCSR_CA)) == (RCSR_IE | RCSR_CA);
	LOG("%s %s set UARTRX int:%d\n", FUNCNAME, d->m_tag, i);
	set_irq_internal(d->m_owner, Z280_INT_UARTRX, i);
}


//-------------------------------------------------
//  reset - reset device status
//-------------------------------------------------
void z280uart_device_reset(struct z280uart_device *d)
{
	LOG("%s\n", FUNCNAME);

	// reset registers
	d->m_uartcr = 0xe2;
	d->m_rcsr = 0x80;
	d->m_tcsr = 0x1; /* p.9-20 */

	d->m_clock_divisor = 1;
	d->m_timer = d->m_clock_divisor;

	// stop receiver and transmitter
	d->tx_bits_rem = 0;
	d->rx_bits_rem = 0;

	z280uart_device_update_serial(d);
	// reset interrupts
}

void z280uart_device_timer(struct z280uart_device *d /*, emu_timer *timer, device_timer_id id, int param, void *ptr*/)
{
	//LOG("%s timer: %d\n",d->m_tag,d->m_timer);
	if (!--d->m_timer) {
		z280uart_device_rcv_callback(d);
		z280uart_device_tra_callback(d);
		d->m_timer = d->m_clock_divisor;
	}
}


//-------------------------------------------------
//  tra_callback -
//-------------------------------------------------
void z280uart_device_tra_callback(struct z280uart_device *d)
{
	if (!(d->m_tcsr & TCSR_EN))
	{
		LOG("%s \"%s\" transmit mark 1\n", FUNCNAME, d->m_tag);
		// transmit mark
		//out_txd_cb(1);
	}
	else if (d->m_tcsr & TCSR_BRK)
	{
		LOG("%s \"%s\" send break 0\n", FUNCNAME, d->m_tag);
		// transmit break
		//out_txd_cb(0);
	}
	else if (!is_transmit_register_empty(d))
	{
//		int db = transmit_register_get_data_bit(d); // unimplemented

		if (d->m_tcsr & TCSR_FRC)
		{
			if (d->m_tcsr & TCSR_VAL)
			{
				LOG("%s \"%s\" transmit force 1\n", FUNCNAME, d->m_tag/*, db*/);
			}
			else
			{
				LOG("%s \"%s\" transmit force 0\n", FUNCNAME, d->m_tag/*, db*/);
			}
		}
		else
		{
			LOG("%s \"%s\" transmit data bit\n", FUNCNAME, d->m_tag/*, db*/);
			// transmit data
			//out_txd_cb(db);
		}
		if (!--d->tx_bits_rem)
			z280uart_device_tra_complete(d);
	}
	/*else
	{
		LOG("%s \"%s\" Failed to transmit\n", FUNCNAME, d->m_tag);
	}*/
}

//-------------------------------------------------
//  tra_complete -
//-------------------------------------------------
void z280uart_device_tra_complete(struct z280uart_device *d)
{
    // transmit callback
	UINT8 tdata = (d->m_tcsr & TCSR_FRC)?((d->m_tcsr & TCSR_VAL)?0xff:0):d->tx_data;
	if (d->tx_callback)
		d->tx_callback(d,0,tdata);

	if (!(d->m_tcsr & TCSR_BE))
	{
		LOG("%s \"%s\" done sending, loading data from TDR:%02x '%c'\n", FUNCNAME, d->m_tag, 
			   d->m_tdr, isprint(d->m_tdr) ? d->m_tdr : ' ');
		transmit_register_setup(d,d->m_tdr); // Reload the shift register
		d->m_tcsr |= TCSR_BE; // Now here is room in the TDR again

		// assert interrupt
		z280uart_device_check_txint(d);
	}
	else
	{
		LOG("%s \"%s\" done sending\n", FUNCNAME, d->m_tag);
		// INT is already asserted
	}

}


//-------------------------------------------------
//  rcv_callback -
//-------------------------------------------------
void z280uart_device_rcv_callback(struct z280uart_device *d)
{
	int c = -1;
	if (d->m_rcsr & RCSR_EN)
	{
		if (d->rx_bits_rem > 0) {
			LOG("%s \"%s\" receive data bit\n", FUNCNAME, d->m_tag);
			//receive_register_update_bit(d->m_rxd);
			if (!--d->rx_bits_rem) {
				z280uart_device_rcv_complete(d);
			}
		} else {
			if (d->rx_callback)
				c = d->rx_callback(d,0);
			if (c!=-1) {
				d->rx_data = c;
				d->rx_bits_rem = d->m_bit_count;
			}
		}
	}
	/*else
	{
		LOG("%s \"%s\" Channel %d Received Data Bit but receiver is disabled\n", FUNCNAME, d->m_tag, d->m_index);
	}*/
}


//-------------------------------------------------
//  rcv_complete -
//-------------------------------------------------
void z280uart_device_rcv_complete(struct z280uart_device *d)
{
	uint8_t data;

	receive_register_extract(d);
	data = get_received_char(d);
	LOG("%s \"%s\" Received Data %c\n", FUNCNAME, d->m_tag, data);
	z280uart_device_receive_data(d,data);
}


//-------------------------------------------------
//  get_clock_mode - get clock divisor
//-------------------------------------------------
int z280uart_device_get_clock_mode(struct z280uart_device *d)
{
	switch (d->m_uartcr & UARTCR_CR)
	{
		case 0<<1:
			return 1;
		case 1<<1:
			return 16;
		case 2<<1:
			return 32;
		case 3<<1:
			return 64;
	}
	return 0;
}

const char *z280uart_stop_bits_tostring(enum stop_bits_t stop_bits)
{
	switch (stop_bits)
	{
	case STOP_BITS_1:
		return "1";

	case STOP_BITS_2:
		return "2";

	default:
		return "UNKNOWN";
	}
}

void z280uart_set_data_frame(struct z280uart_device *d, int data_bit_count, enum parity_t parity, enum stop_bits_t stop_bits)
{
	//LOG("Start bits: %d; Data bits: %d; Parity: %s; Stop bits: %s\n", start_bit_count, data_bit_count, parity_tostring(parity), z280uart_stop_bits_tostring(stop_bits));

	int stop_bit_count;

	switch (stop_bits)
	{
	case STOP_BITS_1:
	default:
		stop_bit_count = 1;
		break;

	case STOP_BITS_2:
		stop_bit_count = 2;
		break;
	}

	//m_df_parity = parity;
	//m_df_start_bit_count = start_bit_count;

	d->m_bit_count = 1 + data_bit_count + stop_bit_count + (parity != PARITY_NONE?1:0);

} 

//-------------------------------------------------
//  get_stop_bits - get number of stop bits
//-------------------------------------------------
enum stop_bits_t z280uart_device_get_stop_bits(struct z280uart_device *d)
{
	switch (d->m_tcsr & TCSR_SB)
	{
	default:
	case 0: return STOP_BITS_1;
	case 1: return STOP_BITS_2;
	}
}


//-------------------------------------------------
//  get_rx_word_length - get receive word length
//-------------------------------------------------
int z280uart_device_get_rx_word_length(struct z280uart_device *d)
{
	int bits;

	switch (d->m_uartcr & UARTCR_BC)
	{
	case 0<<6:  bits = 5;   break;
	case 1<<6:  bits = 6;   break;
	case 2<<6:  bits = 7;   break;
	case 3<<6:  bits = 8;   break;
	}

	return bits;
}

//-------------------------------------------------
//  register_read - read an UART register
//-------------------------------------------------
uint8_t z280uart_device_register_read(struct z280uart_device *d, uint8_t reg)
{
	//if (reg > 1)
	LOG("%s %02x\n", FUNCNAME, reg);
	uint8_t data = 0;
	uint8_t err;

	switch (reg)
	{
		case Z280_UARTCR:
			data = d->m_uartcr;
			break;
		case Z280_TCSR:
			data = d->m_tcsr;
			break;
		case Z280_RCSR:
			// calculate the error bit upon read
			err = ((d->m_rcsr & RCSR_PE)?1:0)|((d->m_rcsr & RCSR_FE)?1:0)|((d->m_rcsr & RCSR_OVE)?1:0);
			data = d->m_rcsr|err;
			break;
		case Z280_TDR:
			data = d->m_tdr; //???
			break;
		case Z280_RDR:
			data = z280uart_device_data_read(d);
			break;
	default:
		LOG("%s: %s : Unsupported UART register:%02x\n", d->m_tag, FUNCNAME, reg);
	}
	return data;
}

//-------------------------------------------------
//  register_write - write an UART register
//-------------------------------------------------
void z280uart_device_register_write(struct z280uart_device *d, uint8_t reg, uint8_t data)
{
	UINT8 old;
	switch (reg)
	{
		case Z280_UARTCR:
			d->m_uartcr = data;
			z280uart_device_update_serial(d);
			break;
		case Z280_TCSR:
			old = d->m_tcsr;
		    // writing to BE has no effect
			d->m_tcsr = (d->m_tcsr & TCSR_BE) | (data & ~(0x20|TCSR_BE));
			if ((old & (TCSR_EN | TCSR_SB)) ^ (d->m_tcsr & (TCSR_EN | TCSR_SB)))
				z280uart_device_update_serial(d);
			z280uart_device_check_txint(d);
			break;
		case Z280_RCSR:
			old = d->m_rcsr;
			// writing to FE has no effect? (it doesn't have a latch)
			// writing to err has no effect
			// writing to CA has no effect
 			d->m_rcsr = (d->m_rcsr & (RCSR_CA|RCSR_FE)) | (data & ~(0x20|RCSR_CA|RCSR_FE|RCSR_ERR));
			if ((old & RCSR_EN) ^ (d->m_tcsr & RCSR_EN))
				z280uart_device_update_serial(d);
			z280uart_device_check_rxint(d);
			break;
		case Z280_TDR:
			z280uart_device_data_write(d,data);
			break;
		case Z280_RDR:
			break;
	default:
		LOG("%s: %s : Unsupported UART register:%02x\n", d->m_tag, FUNCNAME, reg);
	}
}


//-------------------------------------------------
//  data_read - read data register
//-------------------------------------------------		   
uint8_t z280uart_device_data_read(struct z280uart_device *d)
{
	uint8_t data = 0;

	if (d->m_rcsr & RCSR_CA)
	{

		// load data
		data = d->m_rdr;
		d->m_rcsr &= ~RCSR_CA;
		LOG("%s \"%s\": Data Register Read: '%c' %02x\n", FUNCNAME, d->m_tag, isprint(data) ? data : ' ', data);

		z280uart_device_check_rxint(d);
	}
	else
	{
		LOG("data_read: Attempt to read out character from empty RDR\n");
	}

	return data;
}

//-------------------------------------------------
//  data_write - write data register
//-------------------------------------------------
void z280uart_device_data_write(struct z280uart_device *d, uint8_t data)
{
	LOG("%s \"%s\": Data Register Write: %02x '%c'\n", FUNCNAME, d->m_tag, data, isprint(data) ? data : ' ');

	if ( !(d->m_tcsr & TCSR_BE) )
	{
		LOG("- TDR is full, discarding data\n");
	}
	else // ..there is still room
	{
		LOG("- TDR has room, loading data and clearing BE bit\n");
		d->m_tdr = data;
		d->m_tcsr &= ~TCSR_BE;

		// clear interrupt
		z280uart_device_check_txint(d);

	}

	/* Transmitter enabled?  */
	if (d->m_tcsr & TCSR_EN)
	{
		LOG("- TX is enabled\n");
		if (is_transmit_register_empty(d)) // Is the shift register loaded?
		{
			LOG("- Setting up transmitter\n");
			transmit_register_setup(d,d->m_tdr); // Load the shift register, reload is done in tra_complete()
			LOG("- TX shift register loaded\n");
			d->m_tcsr |= TCSR_BE; // And there is a slot in the TDR available

			// assert interrupt
			z280uart_device_check_txint(d);
		}
		else
		{
			LOG("- Transmitter not empty\n");
		}
	}
	else
	{
		LOG("- Transmitter disabled\n");
	}
}


//-------------------------------------------------
//  receive_data - put received data word into RDR
//-------------------------------------------------
void z280uart_device_receive_data(struct z280uart_device *d, uint8_t data)
{
	LOG("\"%s\": Received Data Byte '%c'/%02x put into RDR\n", d->m_tag, isprint(data) ? data : ' ', data);

	if (d->m_rcsr & RCSR_CA)
	{
		// receive overrun error detected
		d->m_rcsr |= RCSR_OVE;

		LOG("Receive_data() Overrun Error\n");
	}

	d->m_rdr = data;
	d->m_rcsr |= RCSR_CA;

	z280uart_device_check_rxint(d);
}
 

//-------------------------------------------------
// get_brg_rate
//-------------------------------------------------
unsigned int z280uart_device_get_brg_rate(struct z280uart_device *d)
{
	unsigned int rate;
	struct z280_device *cpu = d->m_owner;

	if (d->m_uartcr & UARTCR_CS) // we use the CT1 as baudrate source
	{
		rate = (cpu->m_clock>>2) / get_brg_const_z280(cpu);
		LOG("   - Source clk rate = int CT1 (%d)\n", rate);
	}
	else // we use the external clock (CTIN1 pin)
	{
		//unsigned int source = (d->m_index == 0) ? d->m_rxca : d->m_rxcb;
		//rate = source / d->m_brg_const;
		rate = ((struct z280_device *)d->m_owner)->m_ctin1;
		LOG("   - Source clk rate = ext CTIN1 (%d)\n", rate);
	}

	return (rate / d->m_clock_divisor);
}

//-------------------------------------------------
//  z280uart_device_update_serial -
//-------------------------------------------------
void z280uart_device_update_serial(struct z280uart_device *d)
{
	int data_bit_count = z280uart_device_get_rx_word_length(d);
	enum stop_bits_t stop_bits = z280uart_device_get_stop_bits(d);
	enum parity_t parity;

	if (d->m_uartcr & UARTCR_P)
	{
		if (d->m_uartcr & UARTCR_EO)
			parity = PARITY_EVEN;
		else
			parity = PARITY_ODD;
	}
	else
	{
		parity = PARITY_NONE;
	}

	LOG("%s \"%s\" setting data frame %d+%d%c%d\n", FUNCNAME, d->m_tag, 1,
		 data_bit_count, parity == PARITY_NONE ? 'N' : parity == PARITY_EVEN ? 'E' : 'O', (stop_bits + 1) / 2);

	z280uart_set_data_frame(d, data_bit_count, parity, stop_bits);

	d->m_clock_divisor = z280uart_device_get_clock_mode(d);

	//d->m_brg_const = z280uart_device_get_brg_const(d);
	d->m_brg_rate = z280uart_device_get_brg_rate(d);

	LOG("   - BRG rate %d\n", d->m_brg_rate);
	set_rcv_rate(d,d->m_brg_rate);
}
