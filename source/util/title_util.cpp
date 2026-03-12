/*
Copyright (c) 2017-2018 Adubbz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "util/title_util.hpp"

#include <machine/endian.h>
#include <vector>
#include <switch.h>
#include "util/error.hpp"
#include "util/offline_title_db.hpp"
#include "util/debug.h"

namespace tin::util
{
    u64 GetRightsIdTid(FsRightsId rightsId)
    {
        return __bswap64(*(u64 *)rightsId.c);
    }

    u64 GetRightsIdKeyGen(FsRightsId rightsId)
    {
        return __bswap64(*(u64 *)(rightsId.c + 8));
    }

    std::string GetNcaIdString(const NcmContentId& ncaId)
    {
        char ncaIdStr[FS_MAX_PATH] = {0};
        u64 ncaIdLower = __bswap64(*(u64 *)ncaId.c);
        u64 ncaIdUpper = __bswap64(*(u64 *)(ncaId.c + 0x8));
        snprintf(ncaIdStr, FS_MAX_PATH, "%016lx%016lx", ncaIdLower, ncaIdUpper);
        return std::string(ncaIdStr);
    }

    NcmContentId GetNcaIdFromString(std::string ncaIdStr)
    {
        NcmContentId ncaId = {0};
        char lowerU64[17] = {0};
        char upperU64[17] = {0};
        memcpy(lowerU64, ncaIdStr.c_str(), 16);
        memcpy(upperU64, ncaIdStr.c_str() + 16, 16);

        *(u64 *)ncaId.c = __bswap64(strtoul(lowerU64, NULL, 16));
        *(u64 *)(ncaId.c + 8) = __bswap64(strtoul(upperU64, NULL, 16));

        return ncaId;
    }

    u64 GetBaseTitleId(u64 titleId, NcmContentMetaType contentMetaType)
    {
        switch (contentMetaType)
        {
            case NcmContentMetaType_Patch:
                return titleId ^ 0x800;

            case NcmContentMetaType_AddOnContent:
                return (titleId ^ 0x1000) & ~0xFFF;

            default:
                return titleId;
        }
    }

    std::string GetBaseTitleName(u64 baseTitleId)
    {
        LOG_DEBUG("[GetBaseTitleName] titleId=0x%016lx\n", baseTitleId);

        auto getOfflineName = [baseTitleId]() -> std::string {
            inst::offline::TitleMetadata meta;
            if (inst::offline::TryGetMetadata(baseTitleId, meta) && !meta.name.empty()) {
                LOG_DEBUG("[GetBaseTitleName] 0x%016lx offline DB hit: \"%s\"\n", baseTitleId, meta.name.c_str());
                return meta.name;
            }
            LOG_DEBUG("[GetBaseTitleName] 0x%016lx offline DB miss, returning Unknown\n", baseTitleId);
            return "Unknown";
        };

        Result rc = 0;
        NsApplicationControlData appControlData;
        size_t sizeRead;

        if (R_FAILED(rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, baseTitleId, &appControlData, sizeof(NsApplicationControlData), &sizeRead)))
        {
            LOG_DEBUG("[GetBaseTitleName] 0x%016lx nsGetApplicationControlData failed: 0x%08x -> offline DB\n", baseTitleId, rc);
            return getOfflineName();
        }

        if (sizeRead < sizeof(appControlData.nacp))
        {
            LOG_DEBUG("[GetBaseTitleName] 0x%016lx NACP size too small (%zu < %zu) -> offline DB\n", baseTitleId, sizeRead, sizeof(appControlData.nacp));
            return getOfflineName();
        }

        NacpLanguageEntry *languageEntry;

        if (R_FAILED(rc = nacpGetLanguageEntry(&appControlData.nacp, &languageEntry)))
        {
            LOG_DEBUG("[GetBaseTitleName] 0x%016lx nacpGetLanguageEntry failed: 0x%08x -> offline DB\n", baseTitleId, rc);
            return getOfflineName();
        }

        if (languageEntry == NULL)
        {
            LOG_DEBUG("[GetBaseTitleName] 0x%016lx language entry is null -> offline DB\n", baseTitleId);
            return getOfflineName();
        }

        if (languageEntry->name[0] != '\0') {
            LOG_DEBUG("[GetBaseTitleName] 0x%016lx NACP name: \"%s\" (bytes: %02x %02x %02x %02x)\n",
                baseTitleId, languageEntry->name,
                (unsigned char)languageEntry->name[0], (unsigned char)languageEntry->name[1],
                (unsigned char)languageEntry->name[2], (unsigned char)languageEntry->name[3]);
            return languageEntry->name;
        }

        LOG_DEBUG("[GetBaseTitleName] 0x%016lx NACP name empty -> offline DB\n", baseTitleId);
        return getOfflineName();
    }

    std::string GetTitleName(u64 titleId, NcmContentMetaType contentMetaType)
    {
        u64 baseTitleId = GetBaseTitleId(titleId, contentMetaType);
        std::string titleName = GetBaseTitleName(baseTitleId);

        switch (contentMetaType)
        {
            case NcmContentMetaType_Patch:
                titleName += " (Update)";
                break;

            case NcmContentMetaType_AddOnContent:
                titleName += " (DLC)";
                break;

            default:
                break;
        }

        return titleName;
    }

    bool IsTitleInstalled(u64 titleId)
    {
        NsApplicationControlData appControlData;
        u64 sizeRead = 0;
        Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, titleId, &appControlData, sizeof(NsApplicationControlData), &sizeRead);
        return R_SUCCEEDED(rc) && sizeRead >= sizeof(appControlData.nacp);
    }

    bool GetInstalledUpdateVersion(u64 baseTitleId, u32& outVersion)
    {
        outVersion = 0;
        s32 count = 0;
        Result rc = nsCountApplicationContentMeta(baseTitleId, &count);
        if (R_FAILED(rc) || count <= 0)
            return false;

        std::vector<NsApplicationContentMetaStatus> list(count);
        s32 outCount = 0;
        rc = nsListApplicationContentMetaStatus(baseTitleId, 0, list.data(), count, &outCount);
        if (R_FAILED(rc) || outCount <= 0)
            return false;

        for (s32 i = 0; i < outCount; i++) {
            if (list[i].meta_type == NcmContentMetaType_Patch) {
                if (list[i].version > outVersion)
                    outVersion = list[i].version;
            }
        }

        return true;
    }
}
