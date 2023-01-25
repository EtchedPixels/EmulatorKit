struct sasi_bus;


struct sasi_bus *sasi_bus_create(void);
void sasi_bus_free(struct sasi_bus *bus);
void sasi_bus_reset(struct sasi_bus *bus);

void sasi_disk_attach(struct sasi_bus *bus, unsigned int lun, const char *path, unsigned int sectorsize);


void sasi_write_data(struct sasi_bus *bus, uint8_t data);
void sasi_set_data(struct sasi_bus *bus, uint8_t data);
uint8_t sasi_read_data(struct sasi_bus *bus);
void sasi_bus_control(struct sasi_bus *bus, unsigned val);
unsigned sasi_bus_state(struct sasi_bus *bus);

#define SCSI_ATN	0x100

#define SASI_BSY	0x80	/* The ordering here is arbitrary */
#define SASI_SEL	0x40	/* as there is no formal ordering */
#define SASI_CD		0x20
#define SASI_IO		0x10
#define SASI_MSG	0x08
#define SASI_REQ	0x04	/* REQ/ACK is down to the caller */
#define SASI_ACK	0x02
#define SASI_RST	0x01
