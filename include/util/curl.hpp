#pragma once
#include <string>
#include <cstdint>
#include <functional>

namespace inst::curl {
    using DownloadProgressCallback = std::function<void(std::uint64_t downloaded, std::uint64_t total)>;
    bool downloadFile(const std::string ourUrl, const char *pagefilename, long timeout = 5000, bool writeProgress = false);
    bool downloadFileWithProgress(const std::string ourUrl, const char *pagefilename, long timeout, const DownloadProgressCallback& progressCb);
    bool downloadFileWithAuth(const std::string ourUrl, const char *pagefilename, const std::string& user, const std::string& pass, long timeout = 5000);
    bool downloadImageWithAuth(const std::string ourUrl, const char *pagefilename, const std::string& user, const std::string& pass, long timeout = 5000);
    std::string downloadToBuffer (const std::string ourUrl, int firstRange = -1, int secondRange = -1, long timeout = 5000);
}
