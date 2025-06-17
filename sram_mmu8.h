struct sram_mmu;

extern void sram_mmu_set_latch(struct sram_mmu *mmu, uint8_t latch);
extern uint8_t *sram_mmu_translate(struct sram_mmu *mmu, uint32_t addr, unsigned int wr,
	unsigned int super, unsigned int silent, unsigned int *berr);
extern struct sram_mmu *sram_mmu_create(void);
extern void sram_mmu_free(struct sram_mmu *mmu);
extern void sram_mmu_trace(struct sram_mmu *mmu, unsigned int trace);
