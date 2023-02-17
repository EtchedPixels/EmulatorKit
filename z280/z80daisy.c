// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
/***************************************************************************

    z80daisy.c

    Z80/180 daisy chaining support functions.

***************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "z80common.h"
#include "z80daisy.h"

//#define VERBOSE 0

void z80daisy_interface_post_start(struct z80daisy_interface *d);
void z80_daisy_chain_post_start(struct z80_daisy_chain* d);

//**************************************************************************
//  DEVICE Z80 DAISY INTERFACE
//**************************************************************************

//-------------------------------------------------
//  device_z80daisy_interface - constructor
//-------------------------------------------------

struct z80daisy_interface* z80daisy_interface_create(
		const device_t * device,
		z80daisy_irq_state z80daisy_irq_state_cb,
		z80daisy_irq_ack z80daisy_irq_ack_cb,
		z80daisy_irq_reti z80daisy_irq_reti_cb
)
//: device_interface(device, "z80daisy"),
{
		struct z80daisy_interface *d = malloc(sizeof(struct z80daisy_interface));
		d->m_device = device;
		d->z80daisy_irq_state_cb=z80daisy_irq_state_cb;
		d->z80daisy_irq_ack_cb=z80daisy_irq_ack_cb;
		d->z80daisy_irq_reti_cb=z80daisy_irq_reti_cb;
		d->m_daisy_next = NULL;
		d->m_last_opcode = 0;
		//z80daisy_interface_post_start(d);
		return d;
}


//-------------------------------------------------
//  ~device_z80daisy_interface - destructor
//-------------------------------------------------

/*device_z80daisy_interface::~device_z80daisy_interface()
{
}


//-------------------------------------------------
//  interface_post_start - work to be done after
//  actually starting a device
//-------------------------------------------------

void z80daisy_interface_post_start(struct z80daisy_interface *d)
{
	device().save_item(NAME(m_last_opcode));
}*/


//-------------------------------------------------
//  interface_post_reset - work to be done after a
//  device is reset
//-------------------------------------------------

void z80daisy_interface_post_reset(struct z80daisy_interface *d)
{
	d->m_last_opcode = 0;
}


//-------------------------------------------------
//  z80daisy_decode - handle state machine that
//  decodes the RETI instruction from M1 fetches
//-------------------------------------------------

void z80daisy_decode(struct z80daisy_interface *d, uint8_t opcode)
{
	switch (d->m_last_opcode)
	{
	case 0xed:
		// ED 4D = RETI
		if (opcode == 0x4d)
			d->z80daisy_irq_reti_cb(d);

		d->m_last_opcode = 0;
		break;

	case 0xcb:
	case 0xdd:
	case 0xfd:
		// CB xx, DD xx, FD xx are just two-byte opcodes
		d->m_last_opcode = 0;
		break;

	default:
		// TODO: ED affects IEO
		d->m_last_opcode = opcode;
		break;
	}
}


//**************************************************************************
//  Z80 DAISY CHAIN
//**************************************************************************

//-------------------------------------------------
//  z80_daisy_chain - constructor
//-------------------------------------------------

struct z80_daisy_chain* z80_daisy_chain_create(const device_t* device, struct z80daisy_interface *daisy)
//	: device_interface(device, "z80daisychain"),
{
		struct z80_daisy_chain *d = malloc(sizeof(struct z80_daisy_chain));
		d->m_device = device;
		//d->m_daisy_config = NULL;
		d->m_chain = NULL;
		//z80_daisy_chain_post_start(d);
		z80_daisy_chain_init(d, daisy);
		return d;
}


//-------------------------------------------------
//  z80_daisy_chain - destructor
//-------------------------------------------------
/*z80_daisy_chain::~z80_daisy_chain()
{
}


//-------------------------------------------------
//  interface_post_start - work to be done after
//  actually starting a device
//-------------------------------------------------

void z80_daisy_chain_post_start(struct z80_daisy_chain* d)
{
	if (d->m_daisy_config != NULL)
		z80_daisy_chain_init(d);
}*/

void z80_daisy_chain_init(struct z80_daisy_chain* d, struct z80daisy_interface *daisy /*array of interfaces in order*/)
{
	assert(daisy != NULL);

	// create a linked list of devices
	struct z80daisy_interface **tailptr = &d->m_chain;
	for ( ; daisy->m_device != NULL; daisy++)
	{
		// find the device
		/*device_t *target = device().subdevice(daisy->devname);
		if (target == NULL)
		{
			target = device().siblingdevice(daisy->devname);
			if (target == NULL)
				logerror("Unable to locate device '%s'\n", daisy->devname);
		}

		// make sure it has an interface
		struct z80daisy_interface *intf;
		if (!target->interface(intf))
			logerror("Device '%s' does not implement the z80daisy interface!\n", daisy->devname);*/

		// splice it out of the list if it was previously added
		struct z80daisy_interface **oldtailptr = tailptr;
		while (*oldtailptr != NULL)
		{
			if (*oldtailptr == daisy)
				*oldtailptr = (*oldtailptr)->m_daisy_next;
			else
				oldtailptr = &(*oldtailptr)->m_daisy_next;
		}

		// add the interface to the list
		daisy->m_daisy_next = *tailptr;
		*tailptr = daisy;
		tailptr = &(*tailptr)->m_daisy_next;
	}

	char s[256];
	z80_daisy_chain_show_chain(d,s);
	logerror("Daisy chain = %s\n", s);
}


//-------------------------------------------------
//  interface_post_reset - work to be done after a
//  device is reset
//-------------------------------------------------

void z80_daisy_chain_post_reset(struct z80_daisy_chain* d)
{
	// loop over all chained devices and call their reset function
	struct z80daisy_interface *intf;
	for (intf = d->m_chain; intf != NULL; intf = intf->m_daisy_next)
		//intf->device().reset()
		z80daisy_interface_post_reset(intf);
}


//-------------------------------------------------
//  update_irq_state - update the IRQ state and
//  return assert/clear based on the state
//-------------------------------------------------

int z80_daisy_chain_update_irq_state(struct z80_daisy_chain* d)
{
	// loop over all devices; dev[0] is highest priority
	struct z80daisy_interface *intf;
	for (intf = d->m_chain; intf != NULL; intf = intf->m_daisy_next)
	{
		// if this device is asserting the INT line, that's the one we want
		int state = intf->z80daisy_irq_state_cb(intf);
		if (state & Z80_DAISY_INT)
			return ASSERT_LINE;

		// if this device is asserting the IEO line, it blocks everyone else
		if (state & Z80_DAISY_IEO)
			return CLEAR_LINE;
	}
	return CLEAR_LINE;
}


//-------------------------------------------------
//  daisy_get_irq_device - return the device
//  in the chain that requested the interrupt
//-------------------------------------------------

struct z80daisy_interface *z80_daisy_chain_get_irq_device(struct z80_daisy_chain* d)
{
	// loop over all devices; dev[0] is the highest priority
	struct z80daisy_interface *intf;
	for (intf = d->m_chain; intf != NULL; intf = intf->m_daisy_next)
	{
		// if this device is asserting the INT line, that's the one we want
		int state = intf->z80daisy_irq_state_cb(intf);
		if (state & Z80_DAISY_INT)
			return intf;
	}

	if (VERBOSE & z80_daisy_chain_chain_present(d))
		logerror("Interrupt from outside Z80 daisy chain\n");
	return NULL;
}


//-------------------------------------------------
//  call_reti_device - signal a RETI operator to
//  the chain
//-------------------------------------------------

void z80_daisy_chain_call_reti_device(struct z80_daisy_chain* d)
{
	// loop over all devices; dev[0] is the highest priority
	struct z80daisy_interface *intf;
	for (intf = d->m_chain; intf != NULL; intf = intf->m_daisy_next)
	{
		// if this device is asserting the IEO line, that's the one we want
		int state = intf->z80daisy_irq_state_cb(intf);
		if (state & Z80_DAISY_IEO)
		{
			z80daisy_decode(intf,0xed);
			z80daisy_decode(intf,0x4d);
			return;
		}
	}
}


/*void z80_daisy_chain_set_config(struct z80_daisy_chain* d, const z80_daisy_config *config); 
{ 
	d->m_daisy_config = config; 
}*/

int z80_daisy_chain_chain_present(struct z80_daisy_chain* d) //const 
{ 
	return (d->m_chain != NULL); 
}

//-------------------------------------------------
//  daisy_show_chain - list devices in the chain
//  in string format (for debugging purposes)
//-------------------------------------------------

struct device_template {
	char *m_tag;
};

char * z80_daisy_chain_show_chain(struct z80_daisy_chain* d, char* s)
{
	//std::ostringstream result;

	// loop over all devices
	struct z80daisy_interface *intf;
	for (intf = d->m_chain; intf != NULL; intf = intf->m_daisy_next)
	{
		if (intf != d->m_chain)
			strcat(s, " -> ");
		strcat(s, ((struct device_template*)intf->m_device)->m_tag);
	}

	return s;
}
