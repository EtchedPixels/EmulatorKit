#ifndef SN76489SDL_H
#define SN76489SDL_H

struct SN76489SDL;
extern struct SN76489SDL *SN76489SDL_create(void);
extern void SN76489SDL_writeIO(struct SN76489SDL *sn, uint8_t val);
extern void SN76489SDL_destroy(struct SN76489SDL *sn);

#endif
