/*
 *	Reverse implementation of a PS/2 keyboard (ie from the keyboard
 *	end)
 */

#ifdef PS2_INTERNAL

#define PS2_BUFSIZ 16

struct ps2 {
	unsigned int clock_in;
	unsigned int clock_out;
	unsigned int data_in;
	unsigned int data_out;

	int (*state)(struct ps2 *ps2, int);
	unsigned int step;
	unsigned int count;
	unsigned int wait;

	unsigned int rxstate;

	uint8_t last_sent;
	uint16_t receive;
	uint16_t send;

	unsigned int busy;
	uint8_t rbuffer[PS2_BUFSIZ];
	unsigned int rbufptr;
	uint8_t buffer[PS2_BUFSIZ];
	unsigned int bufptr;
	unsigned int replymode;
	unsigned int disabled;

	unsigned int divider;

	unsigned int trace;
	uint32_t window;
};

#endif

struct ps2 *ps2_create(unsigned int divider);
void ps2_free(struct ps2 *ps2);
void ps2_set_lines(struct ps2 *ps2, unsigned int clock, unsigned int data);
unsigned int ps2_get_clock(struct ps2 *ps2);
unsigned int ps2_get_data(struct ps2 *ps2);
void ps2_event(struct ps2 *ps2, unsigned int clocks);
void ps2_trace(struct ps2 *ps2, int onoff);
void ps2_queue_byte(struct ps2 *ps2, uint8_t byte);

void ps2_add_events(struct ps2 *ps2, uint32_t window);
