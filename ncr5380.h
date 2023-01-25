
struct ncr5380;
uint8_t ncr5380_read(struct ncr5380 *ncr, unsigned reg);
uint8_t ncr5380_write(struct ncr5380 *ncr, unsigned reg, uint8_t val);
void ncr5380_activity(struct ncr5380 *ncr);
struct ncr5380 *ncr5380_create(struct sasi_bus *sasi);
void ncr5380_free(struct ncr5380 *ncr);
void ncr5380_trace(struct ncr5380 *ncr, unsigned trace);





