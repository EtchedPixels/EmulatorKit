/*
 *	Wrapper header.  Keep all the actual detail and defines etc away
 *	from our code and separated.
 */

struct amd9511;

uint8_t amd9511_read(struct amd9511 *am, uint8_t addr);
void amd9511_write(struct amd9511 *am, uint8_t addr, uint8_t val);
struct amd9511 *amd9511_create(void);
void amd9511_free(struct amd9511 *am);
void amd9511_reset(struct amd9511 *am);
void amd9511_trace(struct amd9511 *am, unsigned int trace);
unsigned int amd9511_irq_pending(struct amd9511 *am);
