#pragma once
// FatFs is the real library from libDaisy/Middlewares, backed by the RAM disk
// in ramdisk.cpp instead of the SD card.
#include "ff.h"
#include "diskio.h"

namespace daisy
{
class FatFSInterface
{
  public:
    struct Config
    {
        enum Media : uint8_t
        {
            MEDIA_SD  = 0x01,
            MEDIA_USB = 0x02,
        };
        uint8_t media;
    };
    enum Result { OK, ERR_TOO_MANY_VOLUMES, ERR_NO_MEDIA_SELECTED, ERR_GENERIC };
    Result      Init(const Config&) { return OK; }
    Result      Init(uint8_t) { return OK; }
    FATFS&      GetSDFileSystem();
    const char* GetSDPath() { return "0:/"; }
};
} // namespace daisy
