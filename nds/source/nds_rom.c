#include "nds_rom.h"

#include <fat.h>
#include <stdio.h>

static uint32_t RomCrc32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

NdsRomResult NdsRomLoad(const char *path, uint8_t *destination,
                        size_t capacity, uint32_t *out_crc32) {
  if (out_crc32) *out_crc32 = 0;
  if (!path || !destination || capacity < NDS_SMW_ROM_SIZE)
    return NDS_ROM_SIZE_MISMATCH;
  if (!fatInitDefault()) return NDS_ROM_FAT_INIT_FAILED;

  FILE *file = fopen(path, "rb");
  if (!file) return NDS_ROM_OPEN_FAILED;

  NdsRomResult result = NDS_ROM_OK;
  if (fseek(file, 0, SEEK_END) != 0) {
    result = NDS_ROM_READ_FAILED;
  } else {
    const long length = ftell(file);
    if (length < 0) {
      result = NDS_ROM_READ_FAILED;
    } else if ((unsigned long)length != NDS_SMW_ROM_SIZE) {
      result = NDS_ROM_SIZE_MISMATCH;
    } else if (fseek(file, 0, SEEK_SET) != 0 ||
               fread(destination, 1, NDS_SMW_ROM_SIZE, file) !=
                   NDS_SMW_ROM_SIZE) {
      result = NDS_ROM_READ_FAILED;
    }
  }
  fclose(file);

  if (result != NDS_ROM_OK) return result;
  const uint32_t crc32 = RomCrc32(destination, NDS_SMW_ROM_SIZE);
  if (out_crc32) *out_crc32 = crc32;
  return crc32 == NDS_SMW_ROM_CRC32 ? NDS_ROM_OK : NDS_ROM_WRONG_REVISION;
}

const char *NdsRomResultText(NdsRomResult result) {
  switch (result) {
    case NDS_ROM_OK: return "verified USA ROM";
    case NDS_ROM_FAT_INIT_FAILED: return "SD card could not be mounted";
    case NDS_ROM_OPEN_FAILED: return "smw.sfc was not found";
    case NDS_ROM_SIZE_MISMATCH: return "expected an unheadered 512 KiB ROM";
    case NDS_ROM_READ_FAILED: return "could not read ROM file";
    case NDS_ROM_WRONG_REVISION: return "ROM revision did not match";
    default: return "unknown ROM error";
  }
}
