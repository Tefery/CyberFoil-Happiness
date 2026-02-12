#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "shopInstall.hpp"

namespace inst::save_sync {
    struct SaveSyncRemoteVersion {
        std::string saveId;
        std::string note;
        std::string createdAt;
        std::uint64_t createdTs = 0;
        std::string downloadUrl;
        std::uint64_t size = 0;
    };

    struct SaveSyncEntry {
        std::uint64_t titleId = 0;
        std::string titleName;
        bool localAvailable = false;
        bool remoteAvailable = false;
        std::string remoteDownloadUrl;
        std::uint64_t remoteSize = 0;
        std::vector<SaveSyncRemoteVersion> remoteVersions;
    };

    bool FetchRemoteSaveItems(const std::string& shopUrl, const std::string& user, const std::string& pass, std::vector<shopInstStuff::ShopItem>& outItems, std::string& warning);
    bool BuildEntries(const std::vector<shopInstStuff::ShopItem>& remoteItems, std::vector<SaveSyncEntry>& outEntries, std::string& warning);
    bool UploadSaveToServer(const std::string& shopUrl, const std::string& user, const std::string& pass, const SaveSyncEntry& entry, const std::string& note, std::string& error);
    bool DownloadSaveToConsole(const std::string& shopUrl, const std::string& user, const std::string& pass, const SaveSyncEntry& entry, const SaveSyncRemoteVersion* remoteVersion, std::string& error);
    bool DeleteSaveFromServer(const std::string& shopUrl, const std::string& user, const std::string& pass, const SaveSyncEntry& entry, const SaveSyncRemoteVersion* remoteVersion, std::string& error);
}
