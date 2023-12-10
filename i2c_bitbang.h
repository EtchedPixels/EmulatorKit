/* i2c_bitbang - interface */

typedef enum {
	START = 1,
	READ,
	WRITE,
	END
} I2C_OP;

struct i2c_bus *i2c_create(void);
void i2c_free(struct i2c_bus * i2c);
void i2c_reset(struct i2c_bus * i2c);
void i2c_write(struct i2c_bus * i2c, uint8_t val);
uint8_t i2c_read(struct i2c_bus * i2c);
void i2c_trace(struct i2c_bus * i2c, int onoff);

void i2c_register(struct i2c_bus * i2c, void *client, uint8_t id, uint8_t(*fn) (void *client, I2C_OP op, uint8_t data));
