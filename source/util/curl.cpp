#include <curl/curl.h>
#include <string>
#include <sstream>
#include <iostream>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include "util/curl.hpp"
#include "util/config.hpp"
#include "util/error.hpp"
#include "ui/instPage.hpp"

static size_t writeDataFile(void *ptr, size_t size, size_t nmemb, void *stream) {
  size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
  return written;
}

static bool isLikelyImageFile(const char *path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    unsigned char buf[12] = {};
    in.read(reinterpret_cast<char *>(buf), sizeof(buf));
    std::streamsize read = in.gcount();
    if (read >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF)
        return true;
    if (read >= 8 &&
        buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47 &&
        buf[4] == 0x0D && buf[5] == 0x0A && buf[6] == 0x1A && buf[7] == 0x0A)
        return true;
    if (read >= 12 &&
        buf[0] == 'R' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == 'F' &&
        buf[8] == 'W' && buf[9] == 'E' && buf[10] == 'B' && buf[11] == 'P')
        return true;
    return false;
}

static std::string getUserAgent() {
    return "CyberFoil/" + inst::config::appVersion;
}

size_t writeDataBuffer(char *ptr, size_t size, size_t nmemb, void *userdata) {
    std::ostringstream *stream = (std::ostringstream*)userdata;
    size_t count = size * nmemb;
    stream->write(ptr, count);
    return count;
}

int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (ultotal) {
        int uploadProgress = (int)(((double)ulnow / (double)ultotal) * 100.0);
        inst::ui::instPage::setInstBarPerc(uploadProgress);
    } else if (dltotal) {
        int downloadProgress = (int)(((double)dlnow / (double)dltotal) * 100.0);
        inst::ui::instPage::setInstBarPerc(downloadProgress);
    }
    return 0;
}

struct DownloadProgressContext {
    const inst::curl::DownloadProgressCallback* cb = nullptr;
    curl_off_t lastNow = -1;
    curl_off_t lastTotal = -1;
};

int progress_callback_file(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* ctx = static_cast<DownloadProgressContext*>(clientp);
    if (ctx == nullptr || ctx->cb == nullptr || !(*ctx->cb)) {
        return 0;
    }

    if (ctx->lastNow == dlnow && ctx->lastTotal == dltotal) {
        return 0;
    }
    ctx->lastNow = dlnow;
    ctx->lastTotal = dltotal;

    const std::uint64_t now = (dlnow > 0) ? static_cast<std::uint64_t>(dlnow) : 0;
    const std::uint64_t total = (dltotal > 0) ? static_cast<std::uint64_t>(dltotal) : 0;
    (*ctx->cb)(now, total);
    return 0;
}

static constexpr long kDefaultConnectTimeoutMs = 15000;
static constexpr long kLowSpeedLimitBytesPerSec = 1;
static constexpr long kLowSpeedTimeSeconds = 45;

static bool ensureCurlGlobalInit() {
    static std::once_flag initFlag;
    static bool initOk = false;
    std::call_once(initFlag, []() {
        initOk = (curl_global_init(CURL_GLOBAL_ALL) == CURLE_OK);
    });
    return initOk;
}

static void removeFileIfExistsNoThrow(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void applyCommonCurlOptions(CURL *curl_handle, const std::string& url, long timeout, bool writeProgress) {
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    std::string userAgent = getUserAgent();
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_NOSIGNAL, 1L);

    if (writeProgress) {
        curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
    } else {
        curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);
    }

    if (timeout > 0) {
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT_MS, timeout);
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT_MS, timeout);
    } else {
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT_MS, kDefaultConnectTimeoutMs);
        curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, kLowSpeedLimitBytesPerSec);
        curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, kLowSpeedTimeSeconds);
    }
}

namespace inst::curl {
    bool downloadFile (const std::string ourUrl, const char *pagefilename, long timeout, bool writeProgress) {
        if (!ensureCurlGlobalInit()) {
            LOG_DEBUG("curl global init failed\n");
            return false;
        }

        CURL *curl_handle = curl_easy_init();
        if (curl_handle == nullptr) {
            LOG_DEBUG("curl_easy_init failed\n");
            return false;
        }

        applyCommonCurlOptions(curl_handle, ourUrl, timeout, writeProgress);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeDataFile);
        curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);

        FILE *pagefile = fopen(pagefilename, "wb");
        if (pagefile == nullptr) {
            LOG_DEBUG("Failed to open download output file: %s\n", pagefilename);
            curl_easy_cleanup(curl_handle);
            return false;
        }

        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, pagefile);
        const CURLcode result = curl_easy_perform(curl_handle);
        long responseCode = 0;
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &responseCode);

        fclose(pagefile);
        curl_easy_cleanup(curl_handle);

        const bool ok = (result == CURLE_OK) && (responseCode >= 200 && responseCode < 300);
        if (ok)
            return true;

        removeFileIfExistsNoThrow(pagefilename);

        LOG_DEBUG("downloadFile failed rc=%s http=%ld url=%s\n", curl_easy_strerror(result), responseCode, ourUrl.c_str());
        return false;
    }

    bool downloadFileWithProgress(const std::string ourUrl, const char *pagefilename, long timeout, const DownloadProgressCallback& progressCb) {
        if (!ensureCurlGlobalInit()) {
            LOG_DEBUG("curl global init failed\n");
            return false;
        }

        CURL *curl_handle = curl_easy_init();
        if (curl_handle == nullptr) {
            LOG_DEBUG("curl_easy_init failed\n");
            return false;
        }

        applyCommonCurlOptions(curl_handle, ourUrl, timeout, false);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeDataFile);
        curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);

        DownloadProgressContext progressCtx{};
        progressCtx.cb = &progressCb;
        progressCtx.lastNow = -1;
        progressCtx.lastTotal = -1;
        curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, progress_callback_file);
        curl_easy_setopt(curl_handle, CURLOPT_XFERINFODATA, &progressCtx);

        FILE *pagefile = fopen(pagefilename, "wb");
        if (pagefile == nullptr) {
            LOG_DEBUG("Failed to open download output file: %s\n", pagefilename);
            curl_easy_cleanup(curl_handle);
            return false;
        }

        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, pagefile);
        const CURLcode result = curl_easy_perform(curl_handle);
        long responseCode = 0;
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &responseCode);

        fclose(pagefile);
        curl_easy_cleanup(curl_handle);

        const bool ok = (result == CURLE_OK) && (responseCode >= 200 && responseCode < 300);
        if (ok) {
            if (progressCb) {
                progressCb(progressCtx.lastNow > 0 ? static_cast<std::uint64_t>(progressCtx.lastNow) : 0,
                           progressCtx.lastTotal > 0 ? static_cast<std::uint64_t>(progressCtx.lastTotal) : 0);
            }
            return true;
        }

        removeFileIfExistsNoThrow(pagefilename);

        LOG_DEBUG("downloadFileWithProgress failed rc=%s http=%ld url=%s\n", curl_easy_strerror(result), responseCode, ourUrl.c_str());
        return false;
    }

    bool downloadFileWithAuth(const std::string ourUrl, const char *pagefilename, const std::string& user, const std::string& pass, long timeout) {
        if (!ensureCurlGlobalInit()) {
            LOG_DEBUG("curl global init failed\n");
            return false;
        }

        CURL *curl_handle = curl_easy_init();
        if (curl_handle == nullptr) {
            LOG_DEBUG("curl_easy_init failed\n");
            return false;
        }

        applyCommonCurlOptions(curl_handle, ourUrl, timeout, false);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeDataFile);
        curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);

        if (!user.empty() || !pass.empty()) {
            std::string authValue = user + ":" + pass;
            curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl_handle, CURLOPT_USERPWD, authValue.c_str());
        }

        FILE *pagefile = fopen(pagefilename, "wb");
        if (pagefile == nullptr) {
            LOG_DEBUG("Failed to open download output file: %s\n", pagefilename);
            curl_easy_cleanup(curl_handle);
            return false;
        }

        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, pagefile);
        const CURLcode result = curl_easy_perform(curl_handle);
        long responseCode = 0;
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &responseCode);

        fclose(pagefile);
        curl_easy_cleanup(curl_handle);

        const bool ok = (result == CURLE_OK) && (responseCode >= 200 && responseCode < 300);
        if (ok)
            return true;

        removeFileIfExistsNoThrow(pagefilename);

        LOG_DEBUG("downloadFileWithAuth failed rc=%s http=%ld url=%s\n", curl_easy_strerror(result), responseCode, ourUrl.c_str());
        return false;
    }

    bool downloadImageWithAuth(const std::string ourUrl, const char *pagefilename, const std::string& user, const std::string& pass, long timeout) {
        if (!ensureCurlGlobalInit()) {
            LOG_DEBUG("curl global init failed\n");
            return false;
        }

        CURL *curl_handle = curl_easy_init();
        if (curl_handle == nullptr) {
            LOG_DEBUG("curl_easy_init failed\n");
            return false;
        }

        applyCommonCurlOptions(curl_handle, ourUrl, timeout, false);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeDataFile);
        curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);

        FILE *pagefile = fopen(pagefilename, "wb");
        if (pagefile == nullptr) {
            LOG_DEBUG("Failed to open image output file: %s\n", pagefilename);
            curl_easy_cleanup(curl_handle);
            return false;
        }

        long responseCode = 0;
        char* contentType = nullptr;

        if (!user.empty() || !pass.empty()) {
            std::string authValue = user + ":" + pass;
            curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl_handle, CURLOPT_USERPWD, authValue.c_str());
        }

        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, pagefile);
        const CURLcode result = curl_easy_perform(curl_handle);
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &responseCode);
        curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &contentType);

        fclose(pagefile);
        curl_easy_cleanup(curl_handle);

        bool ok = (result == CURLE_OK) && (responseCode >= 200 && responseCode < 300);
        if (ok) {
            bool typeOk = (contentType != nullptr) && (std::strncmp(contentType, "image/", 6) == 0);
            if (!typeOk)
                typeOk = isLikelyImageFile(pagefilename);
            ok = typeOk;
        }
        if (!ok)
            removeFileIfExistsNoThrow(pagefilename);
        if (!ok)
            LOG_DEBUG(curl_easy_strerror(result));
        return ok;
    }

    std::string downloadToBuffer (const std::string ourUrl, int firstRange, int secondRange, long timeout) {
        if (!ensureCurlGlobalInit()) {
            LOG_DEBUG("curl global init failed\n");
            return "";
        }

        CURL *curl_handle = curl_easy_init();
        if (curl_handle == nullptr) {
            LOG_DEBUG("curl_easy_init failed\n");
            return "";
        }

        std::ostringstream stream;
        applyCommonCurlOptions(curl_handle, ourUrl, timeout, false);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeDataBuffer);
        std::string ourRange;
        if (firstRange && secondRange) {
            ourRange = std::to_string(firstRange) + "-" + std::to_string(secondRange);
            curl_easy_setopt(curl_handle, CURLOPT_RANGE, ourRange.c_str());
        }
        
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &stream);
        const CURLcode result = curl_easy_perform(curl_handle);
        long responseCode = 0;
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &responseCode);
        
        curl_easy_cleanup(curl_handle);

        if ((result == CURLE_OK) && (responseCode >= 200 && responseCode < 300))
            return stream.str();

        LOG_DEBUG("downloadToBuffer failed rc=%s http=%ld url=%s\n", curl_easy_strerror(result), responseCode, ourUrl.c_str());
        return "";
    }
}
