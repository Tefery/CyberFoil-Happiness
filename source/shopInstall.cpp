#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include "shopInstall.hpp"
#include "install/http_nsp.hpp"
#include "install/http_xci.hpp"
#include "install/install.hpp"
#include "install/install_nsp.hpp"
#include "install/install_xci.hpp"
#include "nx/nca_writer.h"
#include "util/file_util.hpp"
#include "util/title_util.hpp"
#include "ui/MainApplication.hpp"
#include "ui/instPage.hpp"
#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/error.hpp"
#include "util/json.hpp"
#include "util/lang.hpp"
#include "util/network_util.hpp"
#include "util/util.hpp"

namespace inst::ui {
    extern MainApplication *mainApp;
}

namespace {
    size_t WriteToString(char* ptr, size_t size, size_t numItems, void* userdata)
    {
        auto out = reinterpret_cast<std::string*>(userdata);
        out->append(ptr, size * numItems);
        return size * numItems;
    }

    std::string NormalizeShopUrl(std::string url)
    {
        url.erase(0, url.find_first_not_of(" \t\r\n"));
        url.erase(url.find_last_not_of(" \t\r\n") + 1);
        if (url.empty())
            return url;
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
            url = "http://" + url;
        if (!url.empty() && url.back() == '/')
            url.pop_back();
        return url;
    }

    std::string DecodeUrlSegment(const std::string& value)
    {
        CURL* curl = curl_easy_init();
        if (!curl)
            return value;
        int outLength = 0;
        char* decoded = curl_easy_unescape(curl, value.c_str(), value.size(), &outLength);
        std::string result = decoded ? std::string(decoded, outLength) : value;
        if (decoded)
            curl_free(decoded);
        curl_easy_cleanup(curl);
        return result;
    }

    std::vector<std::string> BuildTinfoilHeaders()
    {
        std::string themeHeader = "Theme: CyberFoil/" + inst::config::appVersion;
        return {
            themeHeader,
            "Uid: 0000000000000000",
            "Version: 0.0",
            "Revision: 0",
            "Language: en",
            "Hauth: 0",
            "Uauth: 0"
        };
    }

    bool IsXciExtension(const std::string& name)
    {
        std::string ext = std::filesystem::path(name).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".xci" || ext == ".xcz";
    }

    bool IsXciMagic(const std::string& url)
    {
        try {
            tin::network::HTTPDownload download(url);
            u32 magic = 0;
            download.BufferDataRange(&magic, 0xF000, sizeof(magic), nullptr);
            if (magic == 0x30534648)
                return true;
            magic = 0;
            download.BufferDataRange(&magic, 0x10000, sizeof(magic), nullptr);
            return magic == 0x30534648;
        } catch (...) {
            return false;
        }
    }

    bool ContainsHtml(const std::string& body)
    {
        std::string lower = body;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        return lower.find("<!doctype html") != std::string::npos || lower.find("<html") != std::string::npos;
    }

    bool IsLoginUrl(const char* effectiveUrl)
    {
        if (!effectiveUrl)
            return false;
        std::string url = effectiveUrl;
        return url.find("/login") != std::string::npos;
    }

    std::string BuildFullUrl(const std::string& baseUrl, const std::string& urlPath)
    {
        if (urlPath.rfind("http://", 0) == 0 || urlPath.rfind("https://", 0) == 0)
            return urlPath;
        if (!urlPath.empty() && urlPath[0] == '/')
            return baseUrl + urlPath;
        return baseUrl + "/" + urlPath;
    }

    std::string GetShopIconCachePath(const shopInstStuff::ShopItem& item)
    {
        if (!item.hasIconUrl)
            return "";
        std::string cacheDir = inst::config::appDir + "/shop_icons";
        std::error_code ec;
        if (!std::filesystem::exists(cacheDir, ec)) {
            std::filesystem::create_directory(cacheDir, ec);
            if (ec)
                return "";
        }

        std::string urlPath = item.iconUrl;
        std::string ext = ".jpg";
        auto queryPos = urlPath.find('?');
        std::string cleanPath = queryPos == std::string::npos ? urlPath : urlPath.substr(0, queryPos);
        auto dotPos = cleanPath.find_last_of('.');
        if (dotPos != std::string::npos) {
            std::string suffix = cleanPath.substr(dotPos);
            if (suffix.size() <= 5 && suffix.find('/') == std::string::npos && suffix.find('?') == std::string::npos)
                ext = suffix;
        }

        std::string fileName;
        if (item.hasTitleId)
            fileName = std::to_string(item.titleId);
        else
            fileName = std::to_string(std::hash<std::string>{}(item.iconUrl));
        return cacheDir + "/" + fileName + ext;
    }

    void UpdateInstallIcon(const shopInstStuff::ShopItem& item)
    {
        if (!item.hasIconUrl) {
            inst::ui::instPage::clearInstallIcon();
            return;
        }

        std::string filePath = GetShopIconCachePath(item);
        if (filePath.empty()) {
            inst::ui::instPage::clearInstallIcon();
            return;
        }

        if (!std::filesystem::exists(filePath)) {
            bool ok = inst::curl::downloadImageWithAuth(item.iconUrl, filePath.c_str(), inst::config::shopUser, inst::config::shopPass, 8000);
            if (!ok && std::filesystem::exists(filePath))
                std::filesystem::remove(filePath);
        }

        if (std::filesystem::exists(filePath))
            inst::ui::instPage::setInstallIcon(filePath);
        else
            inst::ui::instPage::clearInstallIcon();
    }

    constexpr int kShopCacheTtlSeconds = 300;

    std::string GetShopCachePath(const std::string& baseUrl)
    {
        std::size_t hash = std::hash<std::string>{}(baseUrl);
        return inst::config::appDir + "/shop_cache_" + std::to_string(hash) + ".json";
    }

    bool LoadShopCache(const std::string& baseUrl, std::string& body, bool& fresh)
    {
        fresh = false;
        std::string path = GetShopCachePath(baseUrl);
        if (!std::filesystem::exists(path))
            return false;

        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        body = ss.str();
        if (body.empty())
            return false;

        auto ftime = std::filesystem::last_write_time(path);
        auto now = std::chrono::system_clock::now();
        auto ftime_sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + now);
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - ftime_sys).count();
        fresh = age >= 0 && age <= kShopCacheTtlSeconds;
        return true;
    }

    void SaveShopCache(const std::string& baseUrl, const std::string& body)
    {
        if (body.empty())
            return;
        std::string path = GetShopCachePath(baseUrl);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out << body;
    }

    std::string GetShopPrefetchMarker(const std::string& baseUrl)
    {
        std::size_t hash = std::hash<std::string>{}(baseUrl);
        return inst::config::appDir + "/shop_icons_prefetch_" + std::to_string(hash) + ".done";
    }

    bool TryParseTitleId(const nlohmann::json& entry, std::uint64_t& out);
    bool TryParseAppVersion(const nlohmann::json& entry, std::uint32_t& out);
    bool TryParseAppType(const nlohmann::json& entry, std::int32_t& out);

    bool TryParseHexU64(const std::string& value, std::uint64_t& out)
    {
        if (value.empty())
            return false;
        char* end = nullptr;
        unsigned long long parsed = std::strtoull(value.c_str(), &end, 16);
        if (end == value.c_str() || (end && *end != '\0'))
            return false;
        out = static_cast<std::uint64_t>(parsed);
        return true;
    }

    bool TryParseTitleId(const nlohmann::json& entry, std::uint64_t& out)
    {
        if (!entry.contains("title_id"))
            return false;
        const auto& value = entry["title_id"];
        if (value.is_number_unsigned()) {
            out = value.get<std::uint64_t>();
            return true;
        }
        if (value.is_string()) {
            std::string text = value.get<std::string>();
            text.erase(0, text.find_first_not_of(" \t\r\n"));
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            return TryParseHexU64(text, out);
        }
        return false;
    }

    bool TryParseAppVersion(const nlohmann::json& entry, std::uint32_t& out)
    {
        if (!entry.contains("app_version"))
            return false;
        const auto& value = entry["app_version"];
        if (value.is_number_unsigned()) {
            out = value.get<std::uint32_t>();
            return true;
        }
        if (value.is_number_integer()) {
            int parsed = value.get<int>();
            if (parsed < 0)
                return false;
            out = static_cast<std::uint32_t>(parsed);
            return true;
        }
        if (value.is_string()) {
            std::string text = value.get<std::string>();
            text.erase(0, text.find_first_not_of(" \t\r\n"));
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            char* end = nullptr;
            unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
            if (end == nullptr || *end != '\0')
                return false;
            out = static_cast<std::uint32_t>(parsed);
            return true;
        }
        return false;
    }

    bool TryParseAppType(const nlohmann::json& entry, std::int32_t& out)
    {
        if (!entry.contains("app_type"))
            return false;
        const auto& value = entry["app_type"];
        if (value.is_number_integer()) {
            out = value.get<std::int32_t>();
            return true;
        }
        if (value.is_number_unsigned()) {
            out = static_cast<std::int32_t>(value.get<std::uint32_t>());
            return true;
        }
        if (value.is_string()) {
            std::string type = value.get<std::string>();
            type.erase(0, type.find_first_not_of(" \t\r\n"));
            type.erase(type.find_last_not_of(" \t\r\n") + 1);
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            if (type == "base") {
                out = NcmContentMetaType_Application;
                return true;
            }
            if (type == "upd" || type == "update" || type == "patch") {
                out = NcmContentMetaType_Patch;
                return true;
            }
            if (type == "dlc" || type == "addon") {
                out = NcmContentMetaType_AddOnContent;
                return true;
            }
        }
        return false;
    }

    std::vector<shopInstStuff::ShopSection> ParseShopSectionsBody(const std::string& body, const std::string& baseUrl, std::string& error)
    {
        std::vector<shopInstStuff::ShopSection> sections;
        try {
            nlohmann::json shop = nlohmann::json::parse(body);
            if (shop.contains("error") && shop["error"].is_string()) {
                error = "Shop login failed. " + shop["error"].get<std::string>();
                return sections;
            }
            if (!shop.contains("sections") || !shop["sections"].is_array()) {
                std::string lower = body;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
                if (lower.find("unauthorized") != std::string::npos || lower.find("login") != std::string::npos) {
                    error = "Shop login failed. Check username/password or enable public shop.";
                } else {
                    error = "Shop response missing sections.";
                }
                return sections;
            }

            for (const auto& section : shop["sections"]) {
                if (!section.contains("items") || !section["items"].is_array())
                    continue;
                shopInstStuff::ShopSection parsed;
                parsed.id = section.value("id", "all");
                parsed.title = section.value("title", "All");
                for (const auto& entry : section["items"]) {
                    if (!entry.contains("url"))
                        continue;
                    std::string url = entry["url"].get<std::string>();
                    std::uint64_t size = 0;
                    if (entry.contains("size") && entry["size"].is_number()) {
                        size = entry["size"].get<std::uint64_t>();
                    }

                    std::string fragment;
                    std::string urlPath = url;
                    auto hashPos = urlPath.find('#');
                    if (hashPos != std::string::npos) {
                        fragment = urlPath.substr(hashPos + 1);
                        urlPath = urlPath.substr(0, hashPos);
                    }

                    std::string fullUrl = BuildFullUrl(baseUrl, urlPath);

                    std::string name;
                    if (entry.contains("name")) {
                        name = entry["name"].get<std::string>();
                    } else if (!fragment.empty()) {
                        name = DecodeUrlSegment(fragment);
                    } else {
                        name = inst::util::formatUrlString(fullUrl);
                    }

                    if (!fullUrl.empty() && !name.empty()) {
                        shopInstStuff::ShopItem item{name, fullUrl, "", "", size};
                        std::uint64_t titleId = 0;
                        std::uint32_t appVersion = 0;
                        std::int32_t appType = -1;
                        if (TryParseTitleId(entry, titleId)) {
                            item.titleId = titleId;
                            item.hasTitleId = true;
                        }
                        if (TryParseAppVersion(entry, appVersion)) {
                            item.appVersion = appVersion;
                            item.hasAppVersion = true;
                        }
                        if (TryParseAppType(entry, appType))
                            item.appType = appType;
                        if (entry.contains("app_id") && entry["app_id"].is_string()) {
                            item.appId = entry["app_id"].get<std::string>();
                            item.hasAppId = !item.appId.empty();
                        }
                        if (entry.contains("icon_url") && entry["icon_url"].is_string()) {
                            std::string iconUrl = entry["icon_url"].get<std::string>();
                            if (!iconUrl.empty()) {
                                item.iconUrl = BuildFullUrl(baseUrl, iconUrl);
                                item.hasIconUrl = true;
                            }
                        } else if (entry.contains("iconUrl") && entry["iconUrl"].is_string()) {
                            std::string iconUrl = entry["iconUrl"].get<std::string>();
                            if (!iconUrl.empty()) {
                                item.iconUrl = BuildFullUrl(baseUrl, iconUrl);
                                item.hasIconUrl = true;
                            }
                        }
                        parsed.items.push_back(item);
                    }
                }

                if (!parsed.items.empty())
                    sections.push_back(parsed);
            }
        }
        catch (...) {
            error = "Invalid shop response.";
            return {};
        }

        return sections;
    }
}

namespace shopInstStuff {
    struct FetchResult {
        std::string body;
        long responseCode = 0;
        std::string effectiveUrl;
        std::string contentType;
        std::string error;
    };

    FetchResult FetchShopResponse(const std::string& url, const std::string& user, const std::string& pass)
    {
        FetchResult result;
        CURL* curl = curl_easy_init();
        if (!curl) {
            result.error = "Failed to initialize curl.";
            return result;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "tinfoil");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);

        struct curl_slist* headerList = nullptr;
        const auto headers = BuildTinfoilHeaders();
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

        CURLcode rc = curl_easy_perform(curl);
        long responseCode = 0;
        char* effectiveUrl = nullptr;
        char* contentType = nullptr;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
        if (headerList)
            curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);

        result.responseCode = responseCode;
        result.effectiveUrl = effectiveUrl ? effectiveUrl : "";
        result.contentType = contentType ? contentType : "";

        if (rc != CURLE_OK) {
            result.error = curl_easy_strerror(rc);
        }

        return result;
    }

    bool ValidateShopResponse(const FetchResult& fetch, std::string& error)
    {
        if (!fetch.error.empty()) {
            error = fetch.error;
            return false;
        }
        if (fetch.responseCode == 401 || fetch.responseCode == 403) {
            error = "Shop requires authentication. Check credentials or enable public shop in Ownfoil.";
            return false;
        }
        if (IsLoginUrl(fetch.effectiveUrl.c_str()) || (!fetch.contentType.empty() && fetch.contentType.find("text/html") != std::string::npos) || ContainsHtml(fetch.body)) {
            error = "Ownfoil returned the login page. Check shop URL, username, and password, or enable public shop.";
            return false;
        }
        if (fetch.body.rfind("TINFOIL", 0) == 0) {
            error = "inst.shop.encrypted_unsupported"_lang;
            return false;
        }
        return true;
    }

    std::vector<ShopItem> FetchShop(const std::string& shopUrl, const std::string& user, const std::string& pass, std::string& error)
    {
        std::vector<ShopItem> items;
        error.clear();

        std::string baseUrl = NormalizeShopUrl(shopUrl);
        if (baseUrl.empty()) {
            error = "Shop URL is empty.";
            return items;
        }

        FetchResult fetch = FetchShopResponse(baseUrl, user, pass);
        if (!ValidateShopResponse(fetch, error))
            return items;

        try {
            nlohmann::json shop = nlohmann::json::parse(fetch.body);
            if (shop.contains("error")) {
                error = shop["error"].get<std::string>();
                return items;
            }
            if (!shop.contains("files") || !shop["files"].is_array()) {
                error = "Shop response missing file list.";
                return items;
            }

            for (const auto& entry : shop["files"]) {
                if (!entry.contains("url"))
                    continue;
                std::string url = entry["url"].get<std::string>();
                std::uint64_t size = 0;
                if (entry.contains("size") && entry["size"].is_number()) {
                    size = entry["size"].get<std::uint64_t>();
                }

                std::string fragment;
                std::string urlPath = url;
                auto hashPos = urlPath.find('#');
                if (hashPos != std::string::npos) {
                    fragment = urlPath.substr(hashPos + 1);
                    urlPath = urlPath.substr(0, hashPos);
                }

                std::string fullUrl;
                if (urlPath.rfind("http://", 0) == 0 || urlPath.rfind("https://", 0) == 0)
                    fullUrl = urlPath;
                else if (!urlPath.empty() && urlPath[0] == '/')
                    fullUrl = baseUrl + urlPath;
                else
                    fullUrl = baseUrl + "/" + urlPath;

                std::string name;
                if (!fragment.empty())
                    name = DecodeUrlSegment(fragment);
                else {
                    name = inst::util::formatUrlString(fullUrl);
                }

                if (!fullUrl.empty() && !name.empty()) {
                    items.push_back({name, fullUrl, "", "", size});
                }
            }
        }
        catch (...) {
            error = "Invalid shop response.";
            return {};
        }

        std::sort(items.begin(), items.end(), [](const ShopItem& a, const ShopItem& b) {
            return inst::util::ignoreCaseCompare(a.name, b.name);
        });
        return items;
    }

    std::vector<ShopSection> FetchShopSections(const std::string& shopUrl, const std::string& user, const std::string& pass, std::string& error, bool allowCache)
    {
        std::vector<ShopSection> sections;
        error.clear();

        std::string baseUrl = NormalizeShopUrl(shopUrl);
        if (baseUrl.empty()) {
            error = "Shop URL is empty.";
            return sections;
        }

        if (allowCache) {
            std::string cachedBody;
            bool fresh = false;
            if (LoadShopCache(baseUrl, cachedBody, fresh) && fresh) {
                std::string cacheError;
                sections = ParseShopSectionsBody(cachedBody, baseUrl, cacheError);
                if (!sections.empty()) {
                    error.clear();
                    return sections;
                }
            }
        }

        std::string sectionsUrl = baseUrl + "/api/shop/sections";
        FetchResult fetch = FetchShopResponse(sectionsUrl, user, pass);
        if (fetch.responseCode == 404) {
            std::vector<ShopItem> items = FetchShop(shopUrl, user, pass, error);
            if (!items.empty()) {
                sections.push_back({"all", "All", items});
            }
            return sections;
        }

        if (!ValidateShopResponse(fetch, error)) {
            if (allowCache) {
                std::string cachedBody;
                bool fresh = false;
                if (LoadShopCache(baseUrl, cachedBody, fresh)) {
                    std::string cacheError;
                    sections = ParseShopSectionsBody(cachedBody, baseUrl, cacheError);
                    if (!sections.empty()) {
                        error.clear();
                        return sections;
                    }
                }
            }
            return sections;
        }

        sections = ParseShopSectionsBody(fetch.body, baseUrl, error);
        if (!sections.empty())
            SaveShopCache(baseUrl, fetch.body);
        return sections;
    }

    void ResetShopIconCache(const std::string& shopUrl)
    {
        std::string baseUrl = NormalizeShopUrl(shopUrl);
        if (baseUrl.empty())
            return;
        std::string marker = GetShopPrefetchMarker(baseUrl);
        std::error_code ec;
        if (std::filesystem::exists(marker, ec))
            std::filesystem::remove(marker, ec);
        std::string cacheDir = inst::config::appDir + "/shop_icons";
        if (std::filesystem::exists(cacheDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
                if (entry.is_regular_file())
                    std::filesystem::remove(entry.path(), ec);
            }
        }
    }

    namespace {
        class StreamInstallHelper final : public tin::install::Install {
        public:
            StreamInstallHelper(NcmStorageId dest_storage, bool ignore_req)
                : Install(dest_storage, ignore_req) {}

            void AddContentMeta(const nx::ncm::ContentMeta& meta, const NcmContentInfo& info) {
                m_contentMeta.push_back(meta);
                m_cnmt_infos.push_back(info);
            }

            void CommitLatest() {
                if (m_contentMeta.empty()) return;
                const size_t idx = m_contentMeta.size() - 1;
                tin::data::ByteBuffer install_buf;
                m_contentMeta[idx].GetInstallContentMeta(install_buf, m_cnmt_infos[idx], m_ignoreReqFirmVersion);
                InstallContentMetaRecords(install_buf, idx);
                InstallApplicationRecord(idx);
            }

            void CommitAll() {
                for (size_t i = 0; i < m_contentMeta.size(); i++) {
                    tin::data::ByteBuffer install_buf;
                    m_contentMeta[i].GetInstallContentMeta(install_buf, m_cnmt_infos[i], m_ignoreReqFirmVersion);
                    InstallContentMetaRecords(install_buf, i);
                    InstallApplicationRecord(i);
                }
            }

        private:
            std::vector<NcmContentInfo> m_cnmt_infos;
            std::vector<std::tuple<nx::ncm::ContentMeta, NcmContentInfo>> ReadCNMT() override { return {}; }
            void InstallTicketCert() override {}
            void InstallNCA(const NcmContentId& /*ncaId*/) override {}
        };

        class HttpStreamSource {
        public:
            explicit HttpStreamSource(tin::network::HTTPDownload& download) : m_download(download) {}

            Result Read(void* buf, s64 off, s64 size, u64* bytes_read) {
                if (off < 0 || size <= 0) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
                const size_t req_size = static_cast<size_t>(size);
                const size_t req_off = static_cast<size_t>(off);

                if (!m_cache.empty() && req_off >= m_cache_start && (req_off + req_size) <= m_cache_end) {
                    const size_t rel = req_off - m_cache_start;
                    std::memcpy(buf, m_cache.data() + rel, req_size);
                    *bytes_read = static_cast<u64>(size);
                    return 0;
                }

                const size_t fetch_size = std::max(req_size, kReadAheadSize);
                m_cache.resize(fetch_size);
                m_download.BufferDataRange(m_cache.data(), req_off, fetch_size, nullptr);
                m_cache_start = req_off;
                m_cache_end = req_off + fetch_size;

                std::memcpy(buf, m_cache.data(), req_size);
                *bytes_read = static_cast<u64>(size);
                return 0;
            }

        private:
            static constexpr size_t kReadAheadSize = 16 * 1024 * 1024;
            tin::network::HTTPDownload& m_download;
            std::vector<std::uint8_t> m_cache;
            size_t m_cache_start = 0;
            size_t m_cache_end = 0;
        };

        struct StreamHfs0Header {
            u32 magic;
            u32 total_files;
            u32 string_table_size;
            u32 padding;
        };

        struct StreamHfs0FileTableEntry {
            u64 data_offset;
            u64 data_size;
            u32 name_offset;
            u32 hash_size;
            u64 padding;
            u8 hash[0x20];
        };

        struct StreamHfs0 {
            StreamHfs0Header header{};
            std::vector<StreamHfs0FileTableEntry> file_table{};
            std::vector<std::string> string_table{};
            s64 data_offset{};
        };

        struct StreamCollectionEntry {
            std::string name;
            u64 offset{};
            u64 size{};
        };

        static bool ReadHfs0Partition(HttpStreamSource& source, s64 off, StreamHfs0& out) {
            u64 bytes_read = 0;
            if (R_FAILED(source.Read(&out.header, off, sizeof(out.header), &bytes_read))) return false;
            if (out.header.magic != 0x30534648) return false;
            if (out.header.total_files == 0 || out.header.total_files > 0x4000) return false;
            if (out.header.string_table_size > (256 * 1024)) return false;
            off += bytes_read;

            out.file_table.resize(out.header.total_files);
            const auto file_table_size = static_cast<s64>(out.file_table.size() * sizeof(StreamHfs0FileTableEntry));
            if (file_table_size > (4 * 1024 * 1024)) return false;
            if (R_FAILED(source.Read(out.file_table.data(), off, file_table_size, &bytes_read))) return false;
            off += bytes_read;

            std::vector<char> string_table(out.header.string_table_size);
            if (R_FAILED(source.Read(string_table.data(), off, string_table.size(), &bytes_read))) return false;
            off += bytes_read;

            out.string_table.clear();
            out.string_table.reserve(out.header.total_files);
            for (u32 i = 0; i < out.header.total_files; i++) {
                out.string_table.emplace_back(string_table.data() + out.file_table[i].name_offset);
            }

            out.data_offset = off;
            return true;
        }

        static bool GetXciCollections(HttpStreamSource& source, std::vector<StreamCollectionEntry>& out) {
            StreamHfs0 root{};
            s64 root_offset = 0xF000;
            if (!ReadHfs0Partition(source, root_offset, root)) {
                root_offset = 0x10000;
                if (!ReadHfs0Partition(source, root_offset, root)) {
                    return false;
                }
            }

            for (u32 i = 0; i < root.header.total_files; i++) {
                if (root.string_table[i] != "secure") continue;

                StreamHfs0 secure{};
                const auto secure_offset = root.data_offset + static_cast<s64>(root.file_table[i].data_offset);
                if (!ReadHfs0Partition(source, secure_offset, secure)) return false;

                out.clear();
                out.reserve(secure.header.total_files);
                for (u32 j = 0; j < secure.header.total_files; j++) {
                    StreamCollectionEntry entry;
                    entry.name = secure.string_table[j];
                    entry.offset = static_cast<u64>(secure.data_offset + static_cast<s64>(secure.file_table[j].data_offset));
                    entry.size = secure.file_table[j].data_size;
                    out.emplace_back(std::move(entry));
                }
                return true;
            }

            return false;
        }

        static bool InstallXciHttpStream(const std::string& url, NcmStorageId dest_storage) {
            tin::network::HTTPDownload download(url);
            HttpStreamSource source(download);

            std::vector<StreamCollectionEntry> collections;
            if (!GetXciCollections(source, collections)) return false;

            u64 totalBytes = 0;
            for (const auto& c : collections) {
                totalBytes += c.size;
            }
            u64 processedBytes = 0;
            u64 lastTick = armGetSystemTick();
            u64 lastProcessed = 0;
            const u64 freq = armGetSystemTickFreq();
            inst::ui::instPage::setInstInfoText("inst.info_page.preparing"_lang);
            inst::ui::instPage::setInstBarPerc(0);

            std::sort(collections.begin(), collections.end(), [](const auto& a, const auto& b) {
                return a.offset < b.offset;
            });

            struct EntryState {
                std::string name;
                NcmContentId nca_id{};
                std::uint64_t size = 0;
                std::uint64_t written = 0;
                bool started = false;
                bool complete = false;
                bool is_nca = false;
                bool is_cnmt = false;
                std::shared_ptr<nx::ncm::ContentStorage> storage;
                std::unique_ptr<NcaWriter> nca_writer;
                std::vector<std::uint8_t> ticket_buf;
                std::vector<std::uint8_t> cert_buf;
            };

            auto ensureStarted = [&](EntryState& entry) {
                if (entry.started) return true;
                if (!entry.is_nca) {
                    entry.started = true;
                    return true;
                }
                entry.storage = std::make_shared<nx::ncm::ContentStorage>(dest_storage);
                try { entry.storage->DeletePlaceholder(*(NcmPlaceHolderId*)&entry.nca_id); } catch (...) {}
                entry.nca_writer = std::make_unique<NcaWriter>(entry.nca_id, entry.storage);
                entry.started = true;
                return true;
            };

            StreamInstallHelper helper(dest_storage, inst::config::ignoreReqVers);

            std::unordered_map<std::string, EntryState> entries;
            entries.reserve(collections.size());

            std::vector<std::uint8_t> buf(0x800000);
            for (const auto& collection : collections) {
                EntryState entry;
                entry.name = collection.name;
                entry.size = collection.size;
                entry.is_nca = entry.name.find(".nca") != std::string::npos || entry.name.find(".ncz") != std::string::npos;
                entry.is_cnmt = entry.name.find(".cnmt.nca") != std::string::npos || entry.name.find(".cnmt.ncz") != std::string::npos;
                if (entry.is_nca && entry.name.size() >= 32) {
                    entry.nca_id = tin::util::GetNcaIdFromString(entry.name.substr(0, 32));
                }

                if (!ensureStarted(entry)) return false;

                u64 remaining = collection.size;
                u64 offset = collection.offset;
                while (remaining > 0) {
                    const auto chunk = static_cast<size_t>(std::min<u64>(remaining, buf.size()));
                    u64 bytes_read = 0;
                    if (R_FAILED(source.Read(buf.data(), static_cast<s64>(offset), static_cast<s64>(chunk), &bytes_read))) {
                        return false;
                    }
                    if (bytes_read == 0) return false;

                    if (entry.name.find(".tik") != std::string::npos) {
                        entry.ticket_buf.insert(entry.ticket_buf.end(), buf.data(), buf.data() + bytes_read);
                        entry.written += bytes_read;
                        if (entry.written >= entry.size) entry.complete = true;
                    } else if (entry.name.find(".cert") != std::string::npos) {
                        entry.cert_buf.insert(entry.cert_buf.end(), buf.data(), buf.data() + bytes_read);
                        entry.written += bytes_read;
                        if (entry.written >= entry.size) entry.complete = true;
                    } else if (entry.is_nca && entry.nca_writer) {
                        entry.nca_writer->write(buf.data(), bytes_read);
                        entry.written += bytes_read;
                        if (entry.written >= entry.size) {
                            entry.nca_writer->close();
                            try {
                                entry.storage->Register(*(NcmPlaceHolderId*)&entry.nca_id, entry.nca_id);
                                entry.storage->DeletePlaceholder(*(NcmPlaceHolderId*)&entry.nca_id);
                            } catch (...) {}
                            entry.complete = true;
                            if (entry.is_cnmt) {
                                try {
                                    std::string cnmt_path = entry.storage->GetPath(entry.nca_id);
                                    nx::ncm::ContentMeta meta = tin::util::GetContentMetaFromNCA(cnmt_path);
                                    NcmContentInfo cnmt_info{};
                                    cnmt_info.content_id = entry.nca_id;
                                    ncmU64ToContentInfoSize(entry.size & 0xFFFFFFFFFFFF, &cnmt_info);
                                    cnmt_info.content_type = NcmContentType_Meta;
                                    helper.AddContentMeta(meta, cnmt_info);
                                    helper.CommitLatest();
                                } catch (...) {}
                            }
                        }
                    }

                    offset += bytes_read;
                    remaining -= bytes_read;
                    processedBytes += bytes_read;

                    const u64 now = armGetSystemTick();
                    if (now - lastTick >= (freq / 2)) {
                        double speed = 0.0;
                        if (processedBytes > lastProcessed) {
                            double deltaMb = (processedBytes - lastProcessed) / 1000000.0;
                            double deltaSec = (double)(now - lastTick) / (double)freq;
                            if (deltaSec > 0.0)
                                speed = deltaMb / deltaSec;
                        }
                        lastTick = now;
                        lastProcessed = processedBytes;

                        if (totalBytes > 0) {
                            int progress = (int)((double)processedBytes / (double)totalBytes * 100.0);
                            inst::ui::instPage::setInstBarPerc(progress);
                            std::string speedStr = std::to_string(speed);
                            if (speedStr.size() > 4)
                                speedStr = speedStr.substr(0, speedStr.size() - 4);
                            inst::ui::instPage::setInstInfoText("inst.info_page.downloading"_lang + speedStr + "MB/s");
                        }
                    }
                }

                entries.emplace(entry.name, std::move(entry));
            }

            for (auto& [name, entry] : entries) {
                if (entry.name.find(".tik") != std::string::npos) {
                    const auto base = entry.name.substr(0, entry.name.size() - 4);
                    auto it = entries.find(base + ".cert");
                    if (it != entries.end() && !entry.ticket_buf.empty() && !it->second.cert_buf.empty()) {
                        ASSERT_OK(esImportTicket(entry.ticket_buf.data(), entry.ticket_buf.size(), it->second.cert_buf.data(), it->second.cert_buf.size()),
                            "Failed to import ticket");
                    }
                }
            }

            helper.CommitAll();
            inst::ui::instPage::setInstBarPerc(100);
            return true;
        }
    }

    std::string FetchShopMotd(const std::string& shopUrl, const std::string& user, const std::string& pass)
    {
        std::string baseUrl = NormalizeShopUrl(shopUrl);
        if (baseUrl.empty())
            return "";

        FetchResult fetch = FetchShopResponse(baseUrl, user, pass);
        if (fetch.responseCode == 401 || fetch.responseCode == 403)
            return "";
        if (!fetch.error.empty())
            return "";
        if (fetch.body.rfind("TINFOIL", 0) == 0)
            return "";

        try {
            nlohmann::json shop = nlohmann::json::parse(fetch.body);
            if (shop.contains("success") && shop["success"].is_string())
                return shop["success"].get<std::string>();
        }
        catch (...) {
            return "";
        }

        return "";
    }

    void installTitleShop(const std::vector<ShopItem>& items, int storage, const std::string& sourceLabel)
    {
        inst::util::initInstallServices();
        inst::ui::instPage::loadInstallScreen();
        bool nspInstalled = true;
        NcmStorageId destStorageId = storage ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;

        std::vector<std::string> names;
        names.reserve(items.size());
        for (const auto& item : items)
            names.push_back(inst::util::shortenString(item.name, 38, true));

        std::vector<int> previousClockValues;
        if (inst::config::overClock) {
            previousClockValues.push_back(inst::util::setClockSpeed(0, 1785000000)[0]);
            previousClockValues.push_back(inst::util::setClockSpeed(1, 76800000)[0]);
            previousClockValues.push_back(inst::util::setClockSpeed(2, 1600000000)[0]);
        }

        if (!inst::config::shopUser.empty() || !inst::config::shopPass.empty())
            tin::network::SetBasicAuth(inst::config::shopUser, inst::config::shopPass);
        else
            tin::network::ClearBasicAuth();

        std::string currentName;
        try {
            for (size_t i = 0; i < items.size(); i++) {
                LOG_DEBUG("%s %s\n", "Install request from", items[i].url.c_str());
                currentName = names[i];
                UpdateInstallIcon(items[i]);
                inst::ui::instPage::setTopInstInfoText("inst.info_page.top_info0"_lang + currentName + sourceLabel);
                std::unique_ptr<tin::install::Install> installTask;
                bool isXci = IsXciExtension(items[i].name) || IsXciExtension(items[i].url) || IsXciMagic(items[i].url);
                if (isXci) {
                    inst::ui::instPage::setInstInfoText("inst.info_page.preparing"_lang);
                    if (!InstallXciHttpStream(items[i].url, destStorageId)) {
                        THROW_FORMAT("Failed to install XCI from shop.");
                    }
                    continue;
                } else {
                    auto httpNSP = std::make_shared<tin::install::nsp::HTTPNSP>(items[i].url);
                    installTask = std::make_unique<tin::install::nsp::NSPInstall>(destStorageId, inst::config::ignoreReqVers, httpNSP);
                }

                LOG_DEBUG("%s\n", "Preparing installation");
                inst::ui::instPage::setInstInfoText("inst.info_page.preparing"_lang);
                inst::ui::instPage::setInstBarPerc(0);
                installTask->Prepare();
                installTask->Begin();
            }
        }
        catch (std::exception& e) {
            LOG_DEBUG("Failed to install");
            LOG_DEBUG("%s", e.what());
            fprintf(stdout, "%s", e.what());
            std::string failedName = currentName.empty() ? names.front() : currentName;
            inst::ui::instPage::setInstInfoText("inst.info_page.failed"_lang + failedName);
            inst::ui::instPage::setInstBarPerc(0);
            std::string audioPath = "romfs:/audio/bark.wav";
            if (!inst::config::soundEnabled) audioPath = "";
            if (std::filesystem::exists(inst::config::appDir + "/bark.wav")) audioPath = inst::config::appDir + "/bark.wav";
            std::thread audioThread(inst::util::playAudio, audioPath);
            inst::ui::mainApp->CreateShowDialog("inst.info_page.failed"_lang + failedName + "!", "inst.info_page.failed_desc"_lang + "\n\n" + (std::string)e.what(), {"common.ok"_lang}, true);
            audioThread.join();
            nspInstalled = false;
        }

        tin::network::ClearBasicAuth();

        if (previousClockValues.size() > 0) {
            inst::util::setClockSpeed(0, previousClockValues[0]);
            inst::util::setClockSpeed(1, previousClockValues[1]);
            inst::util::setClockSpeed(2, previousClockValues[2]);
        }

        if (nspInstalled) {
            inst::ui::instPage::setInstInfoText("inst.info_page.complete"_lang);
            inst::ui::instPage::setInstBarPerc(100);
            std::string audioPath = "romfs:/audio/success.wav";
            if (!inst::config::soundEnabled) audioPath = "";
            if (std::filesystem::exists(inst::config::appDir + "/success.wav")) audioPath = inst::config::appDir + "/success.wav";
            std::thread audioThread(inst::util::playAudio, audioPath);
            if (items.size() > 1)
                inst::ui::mainApp->CreateShowDialog(std::to_string(items.size()) + "inst.info_page.desc0"_lang, Language::GetRandomMsg(), {"common.ok"_lang}, true);
            else
                inst::ui::mainApp->CreateShowDialog(names.front() + "inst.info_page.desc1"_lang, Language::GetRandomMsg(), {"common.ok"_lang}, true);
            audioThread.join();
        }

        LOG_DEBUG("Done");
        inst::ui::instPage::loadMainMenu();
        inst::util::deinitInstallServices();
    }
}
