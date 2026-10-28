#ifndef SMW_NDS_ROM_H
#define SMW_NDS_ROM_H

#include <stddef.h>
#include <stdint.h>

#define NDS_SMW_ROM_SIZE 0x80000u
#define NDS_SMW_ROM_CRC32 0xB19ED489u

typedef enum NdsRomResult {
  NDS_ROM_OK = 0,
  NDS_ROM_FAT_INIT_FAILED,
  NDS_ROM_OPEN_FAILED,
  NDS_ROM_SIZE_MISMATCH,
  NDS_ROM_READ_FAILED,
  NDS_ROM_WRONG_REVISION,
} NdsRomResult;

NdsRomResult NdsRomLoad(const char *path, uint8_t *destination,
                        size_t capacity, uint32_t *out_crc32);
const char *NdsRomResultText(NdsRomResult result);

#endif
