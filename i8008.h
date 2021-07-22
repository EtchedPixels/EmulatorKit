struct i8008;

extern struct i8008 *i8008_create(void);
extern void i8008_free(struct i8008 *cpu);
extern void i8008_reset(struct i8008 *cpu);
extern void i8008_trace(struct i8008 *cpu, unsigned int onoff);
extern void i8008_dump(struct i8008 *cpu);
extern uint16_t i8008_pc(struct i8008 *cpu);
extern int i8008_stuff(struct i8008 *cpu, uint8_t *bytes, int len);
extern int i8008_halted(struct i8008 *cpu);
extern void i8008_resume(struct i8008 *cpu);
extern void i8008_breakpoint(struct i8008 *cpu, uint16_t addr);
extern void i8008_singlestep(struct i8008 *cpu, unsigned int onoff);
extern void i8008_halt(struct i8008 *cpu, unsigned int onoff);
extern unsigned int i8008_execute(struct i8008 *cpu, unsigned int tstates);
extern unsigned int i8008_get_cycles(struct i8008 *cpu);

/* Platform provided */

extern uint8_t mem_read(struct i8008 *cpu, uint16_t addr, unsigned int trace);
extern void mem_write(struct i8008 *cpu, uint16_t addr, uint8_t val);
extern uint8_t io_read(struct i8008 *cpu, uint8_t addr);
extern void io_write(struct i8008 *cpu, uint8_t addr, uint8_t val);
