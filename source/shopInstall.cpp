#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include "shopInstall.hpp"
#include "install/http_nsp.hpp"
#include "install/http_xci.hpp"
#include "install/install.hpp"
#include "install/install_nsp.hpp"
#include "install/install_xci.hpp"
#include "ui/MainApplication.hpp"
#include "ui/instPage.hpp"
#include "util/config.hpp"
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
        return {
            "Theme: Awoo-Installer",
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
            if (!shop.contains("sections") || !shop["sections"].is_array()) {
                error = "Shop response missing sections.";
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
            error = "Encrypted shop responses are not supported. Disable Encrypt shop in Ownfoil settings.";
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
                inst::ui::instPage::setTopInstInfoText("inst.info_page.top_info0"_lang + currentName + sourceLabel);
                std::unique_ptr<tin::install::Install> installTask;

                if (IsXciExtension(items[i].name)) {
                    auto httpXCI = std::make_shared<tin::install::xci::HTTPXCI>(items[i].url);
                    installTask = std::make_unique<tin::install::xci::XCIInstallTask>(destStorageId, inst::config::ignoreReqVers, httpXCI);
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
