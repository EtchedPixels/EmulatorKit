#define BYTE_SWAP

//#define SWAP16 __builtin_bswap16
//#define SWAP32 __byte_swap_long_variable(x)
#define SWAP32(x) \
        ((((x) & 0xff000000) >> 24) | \
         (((x) & 0x00ff0000) >>  8) | \
         (((x) & 0x0000ff00) <<  8) | \
         (((x) & 0x000000ff) << 24))

typedef struct
   {
      uint8_t DoNotUse_u8_3;
      uint8_t DoNotUse_u8_2;
      uint8_t DoNotUse_u8_1;
      uint8_t u8;
   } use_byte_unsig;

typedef struct
   {
      int8_t DoNotUse_s8_3;
      int8_t DoNotUse_s8_2;
      int8_t DoNotUse_s8_1;
      int8_t s8;
   } use_byte_sig;

typedef struct
   {
      uint16_t DoNotUse_u16_1;
      uint16_t u16;
   } use_word_unsig;

typedef struct
   {
      int16_t DoNotUse_s16_1;
      int16_t s16;
   } use_word_sig;

typedef union
{
   uint32_t     u32;
   int32_t s32;

   use_byte_unsig usebyteu;   
   use_byte_sig usebytes;
   use_word_unsig usewordu;
   use_word_sig usewords;

} MultiReg;
