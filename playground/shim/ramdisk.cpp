// RAM disk behind FatFs: stands in for the hardware's microSD card.
// Sectors are allocated in 64 KB chunks on first write, so a large card
// costs only as much memory as the files put on it.
#include <cstdlib>
#include <cstring>
#include "fatfs.h"

namespace
{
constexpr DWORD kSectorSize      = 512;
constexpr DWORD kSectorsPerChunk = 128; // 64 KB
constexpr DWORD kSectorCount     = 2u * 1024u * 1024u; // 1 GB card
constexpr DWORD kChunkCount      = kSectorCount / kSectorsPerChunk;

BYTE* chunks[kChunkCount];
FATFS fs;

BYTE* Sector(DWORD s, bool alloc)
{
    BYTE*& c = chunks[s / kSectorsPerChunk];
    if(!c && alloc)
        c = static_cast<BYTE*>(calloc(kSectorsPerChunk, kSectorSize));
    return c ? c + (s % kSectorsPerChunk) * kSectorSize : nullptr;
}
} // namespace

FATFS& daisy::FatFSInterface::GetSDFileSystem() { return fs; }

extern "C" {
DSTATUS disk_initialize(BYTE) { return 0; }
DSTATUS disk_status(BYTE) { return 0; }

DRESULT disk_read(BYTE, BYTE* buff, DWORD sector, UINT count)
{
    for(UINT i = 0; i < count; i++, buff += kSectorSize)
    {
        const BYTE* src = Sector(sector + i, false);
        if(src)
            memcpy(buff, src, kSectorSize);
        else
            memset(buff, 0, kSectorSize);
    }
    return RES_OK;
}

DRESULT disk_write(BYTE, const BYTE* buff, DWORD sector, UINT count)
{
    for(UINT i = 0; i < count; i++, buff += kSectorSize)
    {
        BYTE* dst = Sector(sector + i, true);
        if(!dst)
            return RES_ERROR;
        memcpy(dst, buff, kSectorSize);
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE, BYTE cmd, void* buff)
{
    switch(cmd)
    {
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_COUNT: *static_cast<DWORD*>(buff) = kSectorCount; return RES_OK;
        case GET_SECTOR_SIZE: *static_cast<WORD*>(buff) = kSectorSize; return RES_OK;
        case GET_BLOCK_SIZE: *static_cast<DWORD*>(buff) = 1; return RES_OK;
        default: return RES_PARERR;
    }
}

DWORD get_fattime(void) { return ((2024u - 1980u) << 25) | (1u << 21) | (1u << 16); }

// Formats and mounts the card. Called once by each engine's init.
int sdcard_format_and_mount()
{
    static BYTE work[4096];
    if(f_mkfs("0:/", FM_FAT32, 0, work, sizeof(work)) != FR_OK)
        return -1;
    return f_mount(&fs, "0:/", 1) == FR_OK ? 0 : -2;
}
}

// ---- Host file API: the page copies sample files onto the card with these.
namespace
{
FIL  host_file;
char host_path[256];
BYTE host_chunk[65536];
} // namespace

extern "C" {
__attribute__((visibility("default"))) char* sd_path() { return host_path; }
__attribute__((visibility("default"))) BYTE* sd_chunk() { return host_chunk; }
__attribute__((visibility("default"))) int   sd_chunk_size() { return sizeof(host_chunk); }

__attribute__((visibility("default"))) int sd_open_write()
{
    return f_open(&host_file, host_path, FA_CREATE_ALWAYS | FA_WRITE);
}
__attribute__((visibility("default"))) int sd_write(UINT len)
{
    UINT bw = 0;
    FRESULT r = f_write(&host_file, host_chunk, len, &bw);
    return r == FR_OK && bw == len ? 0 : -1;
}
__attribute__((visibility("default"))) int sd_close() { return f_close(&host_file); }
__attribute__((visibility("default"))) int sd_mkdir() { return f_mkdir(host_path); }
}
