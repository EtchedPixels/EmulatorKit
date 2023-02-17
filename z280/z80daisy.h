// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
/***************************************************************************

    z80daisy.h

    Z80/180 daisy chaining support functions.

***************************************************************************/

#ifndef Z80DAISY_H
#define Z80DAISY_H

#pragma once


//**************************************************************************
//  CONSTANTS
//**************************************************************************

// these constants are returned from the irq_state function
#define Z80_DAISY_INT 0x01       // interrupt request mask
#define Z80_DAISY_IEO 0x02       // interrupt disable mask (IEO)


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************


// ======================> z80_daisy_config


/*struct z80_daisy_config
{
	const char *    devname;                    // name of the device
};*/



// ======================> device_z80daisy_interface

	// required operation overrides
	typedef int (*z80daisy_irq_state)(const device_t * device);
	typedef int (*z80daisy_irq_ack)(const device_t * device);
	typedef void (*z80daisy_irq_reti)(const device_t * device);

//private:
struct z80daisy_interface {
	const device_t * m_device;	// chained device (eg.85230)
	struct z80daisy_interface *m_daisy_next;    // next device in the chain
	z80daisy_irq_state z80daisy_irq_state_cb;
	z80daisy_irq_ack z80daisy_irq_ack_cb;
	z80daisy_irq_reti z80daisy_irq_reti_cb;
	uint8_t m_last_opcode;
};

/*class device_z80daisy_interface : public device_interface
{
	friend class z80_daisy_chain;

	// interface-level overrides
	virtual void interface_post_start() override;
	virtual void interface_post_reset() override;

public:*/

	// construction/destruction
	struct z80daisy_interface* z80daisy_interface_create(
		const device_t * m_device,   // chained device
		z80daisy_irq_state z80daisy_irq_state_cb,
		z80daisy_irq_ack z80daisy_irq_ack_cb,
		z80daisy_irq_reti z80daisy_irq_reti_cb
	);
	//virtual ~device_z80daisy_interface();

	// instruction decoding
	void z80daisy_decode(struct z80daisy_interface* d, uint8_t opcode);


// ======================> z80_daisy_chain

//private:
struct z80_daisy_chain {
	const device_t * m_device;		// master device (eg. Z80)
	//struct z80daisy_interface **m_daisy_config;
	struct z80daisy_interface *m_chain;     // head of the daisy chain
};

/*class z80_daisy_chain : public device_interface
{
public:*/
	// construction/destruction
	struct z80_daisy_chain* z80_daisy_chain_create(const device_t* device, struct z80daisy_interface *daisy);
	//virtual ~z80_daisy_chain();

	// configuration helpers
	//void z80_daisy_chain_set_config(const z80_daisy_config *config) { m_daisy_config = config; }

	// getters
	int z80_daisy_chain_chain_present(struct z80_daisy_chain* d); //const { return (m_chain != nullptr); }
	char* z80_daisy_chain_show_chain(struct z80_daisy_chain* d, char* s);

/*protected:
	// interface-level overrides
	virtual void interface_post_start() override;
	virtual void interface_post_reset() override;*/
	void z80_daisy_chain_post_reset(struct z80_daisy_chain* d);

	// initialization
	void z80_daisy_chain_init(struct z80_daisy_chain* d, struct z80daisy_interface *daisy /*array of interfaces in order*/);

	// callbacks
	int z80_daisy_chain_update_irq_state(struct z80_daisy_chain* d);
	struct z80daisy_interface *z80_daisy_chain_get_irq_device(struct z80_daisy_chain* d);
	void z80_daisy_chain_call_reti_device(struct z80_daisy_chain* d);


#endif // Z80DAISY_H
