
struct serial_device {
	const char *name;
	void *private;
	uint8_t (*get)(struct serial_device *d);
	void (*put)(struct serial_device *d, uint8_t ch);
	unsigned (*ready)(struct serial_device *d);
};
