#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <curl/curl.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>
#include <switch.h>
#include <zlib.h>

#include "util/save_sync.hpp"

#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/json.hpp"
#include "util/lang.hpp"
#include "util/title_util.hpp"
#include "util/util.hpp"

namespace {
    constexpr const char* kSaveMountName = "svsync";

    std::string FormatResultHex(Result rc)
    {
        char buf[16] = {0};
        std::snprintf(buf, sizeof(buf), "0x%08X", rc);
        return std::string(buf);
    }

    std::string FormatTitleIdHex(std::uint64_t titleId)
    {
        char buf[32] = {0};
        std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(titleId));
        return std::string(buf);
    }

    std::string TrimAscii(const std::string& value)
    {
        const auto start = value.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            return std::string();
        const auto end = value.find_last_not_of(" \t\r\n");
        return value.substr(start, (end - start) + 1);
    }

    bool TryParseTitleIdText(const std::string& value, std::uint64_t& out)
    {
        std::string text = TrimAscii(value);
        if (text.empty())
            return false;
        if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0)
            text = text.substr(2);
        if (text.empty())
            return false;

        bool hasHexLetters = false;
        bool allDigits = true;
        for (unsigned char c : text) {
            if (!std::isxdigit(c))
                return false;
            if (!std::isdigit(c))
                hasHexLetters = true;
            if (!std::isdigit(c))
                allDigits = false;
        }

        if (hasHexLetters || text.size() == 16) {
            char* end = nullptr;
            unsigned long long parsed = std::strtoull(text.c_str(), &end, 16);
            if (end == text.c_str() || (end && *end != '\0'))
                return false;
            out = static_cast<std::uint64_t>(parsed);
            return true;
        }

        if (allDigits) {
            char* end = nullptr;
            unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
            if (end == text.c_str() || (end && *end != '\0'))
                return false;
            out = static_cast<std::uint64_t>(parsed);
            return true;
        }
        return false;
    }

    bool AccountUidEquals(const AccountUid& a, const AccountUid& b)
    {
        return a.uid[0] == b.uid[0] && a.uid[1] == b.uid[1];
    }

    std::string NormalizeShopUrl(std::string url)
    {
        url = TrimAscii(url);
        if (url.empty())
            return url;
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
            url = "http://" + url;
        if (!url.empty() && url.back() == '/')
            url.pop_back();
        return url;
    }

    std::string BuildUploadUrl(const std::string& shopUrl, std::uint64_t titleId)
    {
        const std::string baseUrl = NormalizeShopUrl(shopUrl);
        if (baseUrl.empty())
            return std::string();
        return baseUrl + "/api/saves/upload/" + FormatTitleIdHex(titleId);
    }

    std::string NormalizeSaveIdToken(std::string value)
    {
        value = TrimAscii(value);
        if (value.empty())
            return std::string();
        if (value.size() > 96)
            value.resize(96);

        std::string normalized;
        normalized.reserve(value.size());
        for (unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.')
                normalized.push_back(static_cast<char>(c));
        }
        return normalized;
    }

    std::string BuildDownloadUrl(const std::string& shopUrl, std::uint64_t titleId, const std::string& saveId = std::string())
    {
        const std::string baseUrl = NormalizeShopUrl(shopUrl);
        if (baseUrl.empty())
            return std::string();
        const std::string normalizedSaveId = NormalizeSaveIdToken(saveId);
        if (!normalizedSaveId.empty())
            return baseUrl + "/api/saves/download/" + FormatTitleIdHex(titleId) + "/" + normalizedSaveId + ".zip";
        return baseUrl + "/api/saves/download/" + FormatTitleIdHex(titleId) + ".zip";
    }

    std::string BuildDeleteUrl(const std::string& shopUrl, std::uint64_t titleId, const std::string& saveId = std::string())
    {
        const std::string baseUrl = NormalizeShopUrl(shopUrl);
        if (baseUrl.empty())
            return std::string();
        const std::string normalizedSaveId = NormalizeSaveIdToken(saveId);
        if (!normalizedSaveId.empty())
            return baseUrl + "/api/saves/delete/" + FormatTitleIdHex(titleId) + "/" + normalizedSaveId;
        return baseUrl + "/api/saves/delete/" + FormatTitleIdHex(titleId);
    }

    std::string BuildRemoteListUrl(const std::string& shopUrl)
    {
        const std::string baseUrl = NormalizeShopUrl(shopUrl);
        if (baseUrl.empty())
            return std::string();
        return baseUrl + "/api/saves/list";
    }

    std::string BuildFullUrl(const std::string& baseUrl, const std::string& pathOrUrl)
    {
        if (pathOrUrl.empty())
            return std::string();
        if (pathOrUrl.rfind("http://", 0) == 0 || pathOrUrl.rfind("https://", 0) == 0)
            return pathOrUrl;
        if (baseUrl.empty())
            return pathOrUrl;
        if (pathOrUrl.front() == '/')
            return baseUrl + pathOrUrl;
        return baseUrl + "/" + pathOrUrl;
    }

    bool ResolveRemoteTitleId(const shopInstStuff::ShopItem& item, std::uint64_t& outTitleId)
    {
        if (item.hasTitleId) {
            outTitleId = item.titleId;
            return true;
        }
        if (item.hasAppId && TryParseTitleIdText(item.appId, outTitleId))
            return true;
        return false;
    }

    bool ResolveActiveUser(AccountUid& outUid, std::string& error)
    {
        error.clear();
        Result rc = accountInitialize(AccountServiceType_Application);
        if (R_FAILED(rc)) {
            error = "Failed to initialize account service (" + FormatResultHex(rc) + ").";
            return false;
        }

        AccountUid uid = {};
        if (R_SUCCEEDED(accountGetPreselectedUser(&uid)) && accountUidIsValid(&uid)) {
            outUid = uid;
            accountExit();
            return true;
        }
        if (R_SUCCEEDED(accountGetLastOpenedUser(&uid)) && accountUidIsValid(&uid)) {
            outUid = uid;
            accountExit();
            return true;
        }

        AccountUid users[ACC_USER_LIST_SIZE] = {};
        s32 total = 0;
        if (R_SUCCEEDED(accountListAllUsers(users, ACC_USER_LIST_SIZE, &total)) && total > 0 && accountUidIsValid(&users[0])) {
            outUid = users[0];
            accountExit();
            return true;
        }

        accountExit();
        error = "No valid user account found for save-data access.";
        return false;
    }

    bool EnumerateLocalSaves(const AccountUid& uid, std::unordered_map<std::uint64_t, inst::save_sync::SaveSyncEntry>& entries, std::string& warning)
    {
        warning.clear();
        FsSaveDataInfoReader reader = {};
        Result rc = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
        if (R_FAILED(rc)) {
            warning = "Failed to enumerate local saves (" + FormatResultHex(rc) + ").";
            return false;
        }

        const bool nsReady = R_SUCCEEDED(nsInitialize());
        while (true) {
            FsSaveDataInfo infoBuf[64] = {};
            s64 outCount = 0;
            rc = fsSaveDataInfoReaderRead(&reader, infoBuf, 64, &outCount);
            if (R_FAILED(rc)) {
                warning = "Failed while reading local saves (" + FormatResultHex(rc) + ").";
                break;
            }
            if (outCount <= 0)
                break;

            for (s64 i = 0; i < outCount; i++) {
                const FsSaveDataInfo& info = infoBuf[i];
                if (info.save_data_type != FsSaveDataType_Account)
                    continue;
                if (!accountUidIsValid(&uid) || !AccountUidEquals(info.uid, uid))
                    continue;
                if (info.application_id == 0)
                    continue;

                auto& entry = entries[info.application_id];
                entry.titleId = info.application_id;
                entry.localAvailable = true;
                if (entry.titleName.empty())
                    entry.titleName = tin::util::GetTitleName(info.application_id, NcmContentMetaType_Application);
            }
        }

        if (nsReady)
            nsExit();
        fsSaveDataInfoReaderClose(&reader);
        return true;
    }

    bool CreateZipFromDirectory(const std::filesystem::path& sourceRoot, const std::filesystem::path& zipPath, std::string& error)
    {
        error.clear();
        zipFile zf = zipOpen64(zipPath.string().c_str(), APPEND_STATUS_CREATE);
        if (!zf) {
            error = "Failed to create save archive.";
            return false;
        }

        std::vector<std::filesystem::path> files;
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(sourceRoot, std::filesystem::directory_options::skip_permission_denied, ec);
        std::filesystem::recursive_directory_iterator end;
        while (!ec && it != end) {
            if (it->is_regular_file())
                files.push_back(it->path());
            it.increment(ec);
        }
        if (ec) {
            zipClose(zf, nullptr);
            error = "Failed while collecting save files for archive.";
            return false;
        }

        if (files.empty()) {
            zip_fileinfo zi = {};
            if (zipOpenNewFileInZip64(zf, ".empty", &zi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 1) != ZIP_OK) {
                zipClose(zf, nullptr);
                error = "Failed to build save archive.";
                return false;
            }
            zipCloseFileInZip(zf);
            zipClose(zf, nullptr);
            return true;
        }

        std::vector<char> buffer(0x8000);
        for (const auto& file : files) {
            std::error_code relEc;
            std::string rel = std::filesystem::relative(file, sourceRoot, relEc).generic_string();
            if (relEc || rel.empty())
                rel = file.filename().string();

            zip_fileinfo zi = {};
            if (zipOpenNewFileInZip64(zf, rel.c_str(), &zi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, 1) != ZIP_OK) {
                zipClose(zf, nullptr);
                error = "Failed to open file in save archive.";
                return false;
            }

            std::ifstream in(file, std::ios::binary);
            if (!in.is_open()) {
                zipCloseFileInZip(zf);
                zipClose(zf, nullptr);
                error = "Failed to read local save file for archive.";
                return false;
            }

            while (in.good()) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize read = in.gcount();
                if (read > 0) {
                    if (zipWriteInFileInZip(zf, buffer.data(), static_cast<unsigned int>(read)) != ZIP_OK) {
                        zipCloseFileInZip(zf);
                        zipClose(zf, nullptr);
                        error = "Failed while writing save archive.";
                        return false;
                    }
                }
            }

            if (zipCloseFileInZip(zf) != ZIP_OK) {
                zipClose(zf, nullptr);
                error = "Failed to finalize save archive entry.";
                return false;
            }
        }

        if (zipClose(zf, nullptr) != ZIP_OK) {
            error = "Failed to finalize save archive.";
            return false;
        }
        return true;
    }

    bool IsSafeZipRelativePath(const std::string& rawPath)
    {
        if (rawPath.empty())
            return false;
        if (rawPath.front() == '/' || rawPath.front() == '\\')
            return false;
        if (rawPath.find(':') != std::string::npos)
            return false;

        std::filesystem::path p(rawPath);
        for (const auto& part : p) {
            const std::string token = part.string();
            if (token == "..")
                return false;
        }
        return true;
    }

    bool ExtractZipToMountedSaveWithCommits(const std::filesystem::path& archivePath, const std::string& mountedPath, const char* mountName, std::string& error)
    {
        error.clear();
        unzFile unz = unzOpen64(archivePath.string().c_str());
        if (!unz) {
            error = "Failed to open downloaded save archive.";
            return false;
        }

        int code = unzGoToFirstFile(unz);
        bool sawEntry = false;
        std::vector<char> buffer(0x8000);
        while (code == UNZ_OK) {
            sawEntry = true;
            unz_file_info64 info = {};
            if (unzGetCurrentFileInfo64(unz, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
                unzClose(unz);
                error = "Failed to read save archive metadata.";
                return false;
            }

            std::string relPath;
            if (info.size_filename > 0) {
                std::vector<char> pathBuf(static_cast<size_t>(info.size_filename) + 1, '\0');
                if (unzGetCurrentFileInfo64(unz, &info, pathBuf.data(), static_cast<uLong>(pathBuf.size()), nullptr, 0, nullptr, 0) != UNZ_OK) {
                    unzClose(unz);
                    error = "Failed to read save archive filename.";
                    return false;
                }
                relPath.assign(pathBuf.data());
            }
            std::replace(relPath.begin(), relPath.end(), '\\', '/');

            const bool isDirectory = !relPath.empty() && relPath.back() == '/';
            if (!relPath.empty() && relPath != ".empty") {
                if (!IsSafeZipRelativePath(relPath)) {
                    unzClose(unz);
                    error = "Save archive contains an invalid path entry.";
                    return false;
                }

                const std::filesystem::path outPath = std::filesystem::path(mountedPath) / std::filesystem::path(relPath);
                std::error_code ec;
                if (isDirectory) {
                    std::filesystem::create_directories(outPath, ec);
                    if (ec) {
                        unzClose(unz);
                        error = "Failed to prepare save directories.";
                        return false;
                    }
                } else {
                    std::filesystem::create_directories(outPath.parent_path(), ec);
                    if (ec) {
                        unzClose(unz);
                        error = "Failed to prepare save file directories.";
                        return false;
                    }

                    if (unzOpenCurrentFile(unz) != UNZ_OK) {
                        unzClose(unz);
                        error = "Failed to open file in save archive.";
                        return false;
                    }

                    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                    if (!out.is_open()) {
                        unzCloseCurrentFile(unz);
                        unzClose(unz);
                        error = "Failed to create file in save data.";
                        return false;
                    }

                    while (true) {
                        const int read = unzReadCurrentFile(unz, buffer.data(), static_cast<unsigned int>(buffer.size()));
                        if (read < 0) {
                            out.close();
                            unzCloseCurrentFile(unz);
                            unzClose(unz);
                            error = "Failed while extracting save archive.";
                            return false;
                        }
                        if (read == 0)
                            break;
                        out.write(buffer.data(), read);
                        if (!out.good()) {
                            out.close();
                            unzCloseCurrentFile(unz);
                            unzClose(unz);
                            error = "Failed while writing save data.";
                            return false;
                        }
                    }

                    out.flush();
                    out.close();
                    if (unzCloseCurrentFile(unz) != UNZ_OK) {
                        unzClose(unz);
                        error = "Failed to finalize save file extraction.";
                        return false;
                    }

                    // SaveData writes should be committed frequently to avoid journal pressure.
                    Result commitRc = fsdevCommitDevice(mountName);
                    if (R_FAILED(commitRc)) {
                        unzClose(unz);
                        error = "Failed to commit imported save file (" + FormatResultHex(commitRc) + ").";
                        return false;
                    }
                }
            }

            code = unzGoToNextFile(unz);
        }

        unzClose(unz);
        if (!sawEntry) {
            error = "Downloaded save archive is empty.";
            return false;
        }
        if (code != UNZ_END_OF_LIST_OF_FILE) {
            error = "Failed while reading downloaded save archive.";
            return false;
        }
        return true;
    }

    size_t WriteToString(char* ptr, size_t size, size_t numItems, void* userdata)
    {
        auto* out = reinterpret_cast<std::string*>(userdata);
        const size_t total = size * numItems;
        out->append(ptr, total);
        return total;
    }

    void BuildVersionAndRevision(std::string& outVersion, std::string& outRevision)
    {
        const std::string raw = inst::config::appVersion;
        outVersion = raw.empty() ? "0.0" : raw;
        outRevision = "0";

        const std::size_t firstDot = raw.find('.');
        if (firstDot == std::string::npos)
            return;

        const std::size_t secondDot = raw.find('.', firstDot + 1);
        if (secondDot == std::string::npos) {
            outVersion = raw;
            return;
        }

        outVersion = raw.substr(0, secondDot);
        const std::string revisionToken = raw.substr(secondDot + 1);
        if (revisionToken.empty())
            return;

        std::size_t digitsEnd = 0;
        while (digitsEnd < revisionToken.size()) {
            const char c = revisionToken[digitsEnd];
            if (c < '0' || c > '9')
                break;
            digitsEnd++;
        }
        if (digitsEnd > 0)
            outRevision = revisionToken.substr(0, digitsEnd);
    }

    std::vector<std::string> BuildShopHeaders()
    {
        std::string themeHeader = "Theme: 0000000000000000000000000000000000000000000000000000000000000000";
        std::string versionValue;
        std::string revisionValue;
        BuildVersionAndRevision(versionValue, revisionValue);
        std::string versionHeader = "Version: " + versionValue;
        std::string revisionHeader = "Revision: " + revisionValue;
        std::string languageHeader = "Language: " + Language::GetShopHeaderLanguage();
        return {
            themeHeader,
            "UID: 0000000000000000000000000000000000000000000000000000000000000000",
            versionHeader,
            revisionHeader,
            languageHeader,
            "HAUTH: 0",
            "UAUTH: 0"
        };
    }

    bool HttpGetWithAuth(const std::string& url, const std::string& user, const std::string& pass, long& outCode, std::string& outBody, std::string& error)
    {
        outBody.clear();
        error.clear();
        outCode = 0;

        if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
            error = "Failed to initialize HTTP client.";
            return false;
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            error = "Failed to initialize HTTP request.";
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "tinfoil");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);

        struct curl_slist* headerList = nullptr;
        const auto headers = BuildShopHeaders();
        for (const auto& header : headers)
            headerList = curl_slist_append(headerList, header.c_str());
        if (headerList)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        std::string authValue;
        if (!user.empty() || !pass.empty()) {
            authValue = user + ":" + pass;
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERPWD, authValue.c_str());
        }

        const CURLcode rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &outCode);
        if (headerList)
            curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK) {
            error = "Failed to query remote saves: " + std::string(curl_easy_strerror(rc));
            return false;
        }
        if (outCode < 200 || outCode >= 300) {
            if (outCode == 401 || outCode == 403) {
                error = "Save sync is not permitted for this account (backup access required).";
            } else if (outCode == 404) {
                error = "Server does not provide save sync endpoints.";
            } else {
                std::ostringstream ss;
                ss << "Remote save list request returned HTTP " << outCode << ".";
                error = ss.str();
            }
            return false;
        }
        return true;
    }

    bool HttpDeleteWithAuth(const std::string& url, const std::string& user, const std::string& pass, long& outCode, std::string& outBody, std::string& error)
    {
        outBody.clear();
        error.clear();
        outCode = 0;

        if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
            error = "Failed to initialize HTTP client.";
            return false;
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            error = "Failed to initialize HTTP request.";
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "tinfoil");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 20000L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);

        struct curl_slist* headerList = nullptr;
        const auto headers = BuildShopHeaders();
        for (const auto& header : headers)
            headerList = curl_slist_append(headerList, header.c_str());
        if (headerList)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        std::string authValue;
        if (!user.empty() || !pass.empty()) {
            authValue = user + ":" + pass;
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERPWD, authValue.c_str());
        }

        const CURLcode rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &outCode);
        if (headerList)
            curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK) {
            error = "Failed to delete remote save: " + std::string(curl_easy_strerror(rc));
            return false;
        }
        return true;
    }

    bool TryGetTitleIdFromJson(const nlohmann::json& obj, std::uint64_t& outTitleId)
    {
        static const char* keys[] = {"title_id", "titleId", "app_id", "appId", "id"};
        for (const auto* key : keys) {
            if (!obj.contains(key))
                continue;
            const auto& value = obj[key];
            if (value.is_number_unsigned()) {
                outTitleId = value.get<std::uint64_t>();
                return true;
            }
            if (value.is_number_integer()) {
                const auto parsed = value.get<long long>();
                if (parsed >= 0) {
                    outTitleId = static_cast<std::uint64_t>(parsed);
                    return true;
                }
            }
            if (value.is_string()) {
                if (TryParseTitleIdText(value.get<std::string>(), outTitleId))
                    return true;
            }
        }
        return false;
    }

    bool TryGetStringByKeys(const nlohmann::json& obj, const std::vector<const char*>& keys, std::string& out)
    {
        out.clear();
        for (const auto* key : keys) {
            if (!obj.contains(key))
                continue;
            const auto& value = obj[key];
            if (!value.is_string())
                continue;
            out = TrimAscii(value.get<std::string>());
            if (!out.empty())
                return true;
        }
        return false;
    }

    bool TryGetU64ByKeys(const nlohmann::json& obj, const std::vector<const char*>& keys, std::uint64_t& out)
    {
        out = 0;
        for (const auto* key : keys) {
            if (!obj.contains(key))
                continue;
            const auto& value = obj[key];
            if (value.is_number_unsigned()) {
                out = value.get<std::uint64_t>();
                return true;
            }
            if (value.is_number_integer()) {
                const auto parsed = value.get<long long>();
                if (parsed >= 0) {
                    out = static_cast<std::uint64_t>(parsed);
                    return true;
                }
            }
            if (value.is_string()) {
                const std::string text = TrimAscii(value.get<std::string>());
                if (text.empty())
                    continue;
                bool allDigits = true;
                for (unsigned char c : text) {
                    if (!std::isdigit(c)) {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits) {
                    char* end = nullptr;
                    unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
                    if (end != text.c_str() && end && *end == '\0') {
                        out = static_cast<std::uint64_t>(parsed);
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool UploadZipMultipart(const std::string& url, const std::string& zipPath, const std::string& user, const std::string& pass, std::uint64_t titleId, const std::string& note, std::string& error)
    {
        error.clear();
        if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
            error = "Failed to initialize HTTP client.";
            return false;
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            error = "Failed to initialize HTTP request.";
            return false;
        }

        std::string responseBody;
        const std::string titleIdText = FormatTitleIdHex(titleId);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "tinfoil");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 60000L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);

        struct curl_slist* headerList = nullptr;
        const auto headers = BuildShopHeaders();
        for (const auto& header : headers)
            headerList = curl_slist_append(headerList, header.c_str());
        if (headerList)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        std::string authValue;
        if (!user.empty() || !pass.empty()) {
            authValue = user + ":" + pass;
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERPWD, authValue.c_str());
        }

        curl_mime* mime = curl_mime_init(curl);
        if (!mime) {
            if (headerList)
                curl_slist_free_all(headerList);
            curl_easy_cleanup(curl);
            error = "Failed to prepare upload payload.";
            return false;
        }

        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filedata(part, zipPath.c_str());
        curl_mime_filename(part, (titleIdText + ".zip").c_str());

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "title_id");
        curl_mime_data(part, titleIdText.c_str(), CURL_ZERO_TERMINATED);

        part = curl_mime_addpart(mime);
        curl_mime_name(part, "application_id");
        curl_mime_data(part, titleIdText.c_str(), CURL_ZERO_TERMINATED);

        const std::string trimmedNote = TrimAscii(note);
        if (!trimmedNote.empty()) {
            part = curl_mime_addpart(mime);
            curl_mime_name(part, "note");
            curl_mime_data(part, trimmedNote.c_str(), CURL_ZERO_TERMINATED);
        }

        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

        const CURLcode rc = curl_easy_perform(curl);
        long responseCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        curl_mime_free(mime);
        if (headerList)
            curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK) {
            error = "Upload failed: " + std::string(curl_easy_strerror(rc));
            return false;
        }
        if (responseCode < 200 || responseCode >= 300) {
            if (responseCode == 401 || responseCode == 403) {
                error = "Save upload is not permitted for this account (backup access required).";
            } else if (responseCode == 404) {
                error = "Server does not provide save upload endpoints.";
            } else {
                std::ostringstream ss;
                ss << "Upload failed with HTTP " << responseCode;
                if (!responseBody.empty())
                    ss << ": " << responseBody;
                error = ss.str();
            }
            return false;
        }
        return true;
    }

    bool ClearDirectoryContents(const std::filesystem::path& root, std::string& error)
    {
        error.clear();
        std::error_code ec;
        if (!std::filesystem::exists(root, ec))
            return true;
        for (const auto& entry : std::filesystem::directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                error = "Failed while reading existing save data.";
                return false;
            }
            std::filesystem::remove_all(entry.path(), ec);
            if (ec) {
                error = "Failed while clearing existing save data.";
                return false;
            }
        }
        return true;
    }

    bool MountSaveDataForTitle(const AccountUid& uid, std::uint64_t titleId, bool readOnly, std::string& error)
    {
        error.clear();
        fsdevUnmountDevice(kSaveMountName);
        Result rc = readOnly ? fsdevMountSaveDataReadOnly(kSaveMountName, titleId, uid) : fsdevMountSaveData(kSaveMountName, titleId, uid);
        if (readOnly && R_FAILED(rc))
            rc = fsdevMountSaveData(kSaveMountName, titleId, uid);
        if (R_FAILED(rc)) {
            error = "Failed to mount save data (" + FormatResultHex(rc) + ").";
            return false;
        }
        return true;
    }
}

namespace inst::save_sync {
    bool FetchRemoteSaveItems(const std::string& shopUrl, const std::string& user, const std::string& pass, std::vector<shopInstStuff::ShopItem>& outItems, std::string& warning)
    {
        outItems.clear();
        warning.clear();

        const std::string baseUrl = NormalizeShopUrl(shopUrl);
        const std::string listUrl = BuildRemoteListUrl(shopUrl);
        if (listUrl.empty())
            return true;

        long responseCode = 0;
        std::string body;
        if (!HttpGetWithAuth(listUrl, user, pass, responseCode, body, warning))
            return false;

        try {
            nlohmann::json root = nlohmann::json::parse(body);
            const nlohmann::json* saves = nullptr;
            if (root.is_array())
                saves = &root;
            else if (root.is_object() && root.contains("saves") && root["saves"].is_array())
                saves = &root["saves"];
            if (!saves)
                return true;

            for (const auto& save : *saves) {
                if (!save.is_object())
                    continue;

                std::uint64_t titleId = 0;
                if (!TryGetTitleIdFromJson(save, titleId))
                    continue;

                std::string name;
                if (save.contains("name") && save["name"].is_string())
                    name = save["name"].get<std::string>();
                if (name.empty())
                    name = tin::util::GetTitleName(titleId, NcmContentMetaType_Application);
                if (name.empty())
                    name = FormatTitleIdHex(titleId);

                std::string saveId;
                if (TryGetStringByKeys(save, {"save_id", "saveId", "version_id", "versionId"}, saveId))
                    saveId = NormalizeSaveIdToken(saveId);

                std::string note;
                TryGetStringByKeys(save, {"note", "save_note", "saveNote"}, note);

                std::string createdAt;
                TryGetStringByKeys(save, {"created_at", "createdAt", "date", "timestamp"}, createdAt);

                std::uint64_t createdTs = 0;
                TryGetU64ByKeys(save, {"created_ts", "createdTs", "timestamp_unix", "timestampUnix"}, createdTs);

                std::string downloadUrl;
                if (save.contains("download_url") && save["download_url"].is_string())
                    downloadUrl = save["download_url"].get<std::string>();
                else if (save.contains("downloadUrl") && save["downloadUrl"].is_string())
                    downloadUrl = save["downloadUrl"].get<std::string>();
                else if (save.contains("url") && save["url"].is_string())
                    downloadUrl = save["url"].get<std::string>();
                downloadUrl = BuildFullUrl(baseUrl, downloadUrl);
                if (downloadUrl.empty())
                    downloadUrl = BuildDownloadUrl(shopUrl, titleId, saveId);

                std::uint64_t size = 0;
                TryGetU64ByKeys(save, {"size", "archive_size", "archiveSize"}, size);

                shopInstStuff::ShopItem item;
                item.name = name;
                item.url = downloadUrl;
                item.size = size;
                item.titleId = titleId;
                item.hasTitleId = true;
                item.saveId = saveId;
                item.saveNote = note;
                item.saveCreatedAt = createdAt;
                item.saveCreatedTs = createdTs;
                outItems.push_back(std::move(item));
            }
        } catch (...) {
            warning = "Failed to parse remote save list.";
            return false;
        }

        return true;
    }

    bool BuildEntries(const std::vector<shopInstStuff::ShopItem>& remoteItems, std::vector<SaveSyncEntry>& outEntries, std::string& warning)
    {
        warning.clear();
        outEntries.clear();

        std::unordered_map<std::uint64_t, SaveSyncEntry> entriesByTitleId;

        AccountUid uid = {};
        std::string userError;
        if (ResolveActiveUser(uid, userError)) {
            std::string localWarning;
            EnumerateLocalSaves(uid, entriesByTitleId, localWarning);
            if (!localWarning.empty())
                warning = localWarning;
        } else {
            warning = userError;
        }

        for (const auto& item : remoteItems) {
            std::uint64_t titleId = 0;
            if (!ResolveRemoteTitleId(item, titleId))
                continue;

            auto& entry = entriesByTitleId[titleId];
            entry.titleId = titleId;
            if (!item.name.empty())
                entry.titleName = item.name;

            SaveSyncRemoteVersion version;
            version.saveId = NormalizeSaveIdToken(item.saveId);
            version.note = TrimAscii(item.saveNote);
            version.createdAt = TrimAscii(item.saveCreatedAt);
            version.createdTs = item.saveCreatedTs;
            version.size = item.size;
            version.downloadUrl = item.url;

            bool duplicate = false;
            for (const auto& existing : entry.remoteVersions) {
                if (existing.saveId == version.saveId &&
                    existing.downloadUrl == version.downloadUrl &&
                    existing.createdTs == version.createdTs &&
                    existing.size == version.size) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                entry.remoteVersions.push_back(std::move(version));
        }

        for (auto& it : entriesByTitleId) {
            auto& entry = it.second;
            std::sort(entry.remoteVersions.begin(), entry.remoteVersions.end(), [](const auto& a, const auto& b) {
                if (a.createdTs != b.createdTs)
                    return a.createdTs > b.createdTs;
                if (a.createdAt != b.createdAt)
                    return a.createdAt > b.createdAt;
                return a.saveId > b.saveId;
            });
            entry.remoteAvailable = !entry.remoteVersions.empty();
            if (entry.remoteAvailable) {
                if (entry.remoteDownloadUrl.empty())
                    entry.remoteDownloadUrl = entry.remoteVersions.front().downloadUrl;
                if (entry.remoteSize == 0)
                    entry.remoteSize = entry.remoteVersions.front().size;
            }

            if (entry.titleName.empty())
                entry.titleName = tin::util::GetTitleName(entry.titleId, NcmContentMetaType_Application);
            if (entry.titleName.empty())
                entry.titleName = FormatTitleIdHex(entry.titleId);
            outEntries.push_back(entry);
        }

        std::sort(outEntries.begin(), outEntries.end(), [](const auto& a, const auto& b) {
            return inst::util::ignoreCaseCompare(a.titleName, b.titleName);
        });

        return true;
    }

    bool UploadSaveToServer(const std::string& shopUrl, const std::string& user, const std::string& pass, const SaveSyncEntry& entry, const std::string& note, std::string& error)
    {
        error.clear();
        if (entry.titleId == 0) {
            error = "Invalid title ID for save upload.";
            return false;
        }
        if (!entry.localAvailable) {
            error = "No local save data exists for this title and user. Launch the game once to create save data, then retry.";
            return false;
        }

        AccountUid uid = {};
        if (!ResolveActiveUser(uid, error))
            return false;

        const std::string tempRoot = inst::config::appDir + "/save_sync_tmp";
        const std::filesystem::path archivePath = std::filesystem::path(tempRoot) / (FormatTitleIdHex(entry.titleId) + ".zip");
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
        std::filesystem::create_directories(tempRoot, ec);

        const std::string mountedPath = std::string(kSaveMountName) + ":/";

        if (!MountSaveDataForTitle(uid, entry.titleId, true, error))
            return false;

        if (!CreateZipFromDirectory(mountedPath, archivePath, error)) {
            fsdevUnmountDevice(kSaveMountName);
            return false;
        }

        fsdevUnmountDevice(kSaveMountName);

        const std::string uploadUrl = BuildUploadUrl(shopUrl, entry.titleId);
        if (uploadUrl.empty()) {
            error = "Shop URL is not configured.";
            return false;
        }

        if (!UploadZipMultipart(uploadUrl, archivePath.string(), user, pass, entry.titleId, note, error))
            return false;

        std::filesystem::remove_all(tempRoot, ec);
        return true;
    }

    bool DownloadSaveToConsole(const std::string& shopUrl, const std::string& user, const std::string& pass, const SaveSyncEntry& entry, const SaveSyncRemoteVersion* remoteVersion, std::string& error)
    {
        error.clear();
        if (entry.titleId == 0) {
            error = "Invalid title ID for save download.";
            return false;
        }
        if (!entry.localAvailable) {
            error = "No local save container exists for this title and user. Launch the game once to create save data, then retry download.";
            return false;
        }

        const SaveSyncRemoteVersion* selectedRemoteVersion = remoteVersion;
        if (!selectedRemoteVersion && !entry.remoteVersions.empty())
            selectedRemoteVersion = &entry.remoteVersions.front();

        std::string selectedSaveId;
        std::string downloadUrl = entry.remoteDownloadUrl;
        if (selectedRemoteVersion) {
            if (!selectedRemoteVersion->saveId.empty())
                selectedSaveId = selectedRemoteVersion->saveId;
            if (!selectedRemoteVersion->downloadUrl.empty())
                downloadUrl = selectedRemoteVersion->downloadUrl;
        }
        if (downloadUrl.empty())
            downloadUrl = BuildDownloadUrl(shopUrl, entry.titleId, selectedSaveId);
        if (downloadUrl.empty())
            downloadUrl = BuildDownloadUrl(shopUrl, entry.titleId);
        if (downloadUrl.empty()) {
            error = "No download URL available for this save.";
            return false;
        }

        const std::string tempRoot = inst::config::appDir + "/save_sync_tmp";
        const std::filesystem::path archivePath = std::filesystem::path(tempRoot) / (FormatTitleIdHex(entry.titleId) + ".zip");
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
        std::filesystem::create_directories(tempRoot, ec);

        if (!inst::curl::downloadFileWithAuth(downloadUrl, archivePath.string().c_str(), user, pass, 60000)) {
            error = "Failed to download save archive from server.";
            return false;
        }

        AccountUid uid = {};
        if (!ResolveActiveUser(uid, error))
            return false;

        if (!MountSaveDataForTitle(uid, entry.titleId, false, error))
            return false;

        const std::string mountedPath = std::string(kSaveMountName) + ":/";
        if (!ClearDirectoryContents(mountedPath, error)) {
            fsdevUnmountDevice(kSaveMountName);
            return false;
        }
        Result clearCommitRc = fsdevCommitDevice(kSaveMountName);
        if (R_FAILED(clearCommitRc)) {
            fsdevUnmountDevice(kSaveMountName);
            error = "Failed to commit cleared save data (" + FormatResultHex(clearCommitRc) + ").";
            return false;
        }

        if (!ExtractZipToMountedSaveWithCommits(archivePath, mountedPath, kSaveMountName, error)) {
            fsdevUnmountDevice(kSaveMountName);
            return false;
        }

        Result rc = fsdevCommitDevice(kSaveMountName);
        fsdevUnmountDevice(kSaveMountName);
        if (R_FAILED(rc)) {
            error = "Failed to commit imported save data (" + FormatResultHex(rc) + ").";
            return false;
        }

        std::filesystem::remove_all(tempRoot, ec);
        return true;
    }

    bool DeleteSaveFromServer(const std::string& shopUrl, const std::string& user, const std::string& pass, const SaveSyncEntry& entry, const SaveSyncRemoteVersion* remoteVersion, std::string& error)
    {
        error.clear();
        if (entry.titleId == 0) {
            error = "Invalid title ID for save delete.";
            return false;
        }
        if (!entry.remoteAvailable && entry.remoteVersions.empty()) {
            error = "No remote save is available for this title.";
            return false;
        }

        const SaveSyncRemoteVersion* selectedRemoteVersion = remoteVersion;
        if (!selectedRemoteVersion && !entry.remoteVersions.empty())
            selectedRemoteVersion = &entry.remoteVersions.front();

        std::string selectedSaveId;
        if (selectedRemoteVersion && !selectedRemoteVersion->saveId.empty())
            selectedSaveId = selectedRemoteVersion->saveId;

        std::string deleteUrl = BuildDeleteUrl(shopUrl, entry.titleId, selectedSaveId);
        if (deleteUrl.empty())
            deleteUrl = BuildDeleteUrl(shopUrl, entry.titleId);
        if (deleteUrl.empty()) {
            error = "No delete URL available for this save.";
            return false;
        }

        long responseCode = 0;
        std::string responseBody;
        if (!HttpDeleteWithAuth(deleteUrl, user, pass, responseCode, responseBody, error))
            return false;

        if (responseCode < 200 || responseCode >= 300) {
            if (responseCode == 401 || responseCode == 403) {
                error = "Save delete is not permitted for this account (backup access required).";
            } else if (responseCode == 404) {
                error = "Remote save backup was not found.";
            } else {
                std::ostringstream ss;
                ss << "Save delete failed with HTTP " << responseCode;
                if (!responseBody.empty())
                    ss << ": " << responseBody;
                error = ss.str();
            }
            return false;
        }

        return true;
    }
}
