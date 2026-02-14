#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include "util/config.hpp"
#include "util/json.hpp"

namespace inst::config {
    std::string gAuthKey;
    std::string sigPatchesUrl;
    std::string lastNetUrl;
    std::string offlineDbManifestUrl;
    std::string shopUrl;
    std::string shopUser;
    std::string shopPass;
    std::vector<std::string> updateInfo;
    int languageSetting;
    bool autoUpdate;
    bool deletePrompt;
    bool gayMode;
    bool soundEnabled;
    bool oledMode;
    bool mtpExposeAlbum;
    bool ignoreReqVers;
    bool overClock;
    bool usbAck;
    bool validateNCAs;
    bool shopHideInstalled;
    bool shopHideInstalledSection;
    bool shopStartGridMode;
    bool offlineDbAutoCheckOnStartup;

    namespace {
        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string Trim(const std::string& value)
        {
            if (value.empty())
                return "";
            std::size_t start = value.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                return "";
            std::size_t end = value.find_last_not_of(" \t\r\n");
            return value.substr(start, (end - start) + 1);
        }

        std::string NormalizeProtocol(const std::string& protocol)
        {
            std::string lower = ToLower(Trim(protocol));
            if (lower == "https")
                return "https";
            return "http";
        }

        int DefaultPortForNormalizedProtocol(const std::string& normalizedProtocol)
        {
            return normalizedProtocol == "https" ? 443 : 8465;
        }

        bool EnsureShopsDirectory()
        {
            std::error_code ec;
            if (std::filesystem::exists(inst::config::shopsDir, ec))
                return std::filesystem::is_directory(inst::config::shopsDir, ec);
            return std::filesystem::create_directories(inst::config::shopsDir, ec);
        }

        bool ParseBoolTextValue(const std::string& value, bool& out)
        {
            std::string lower = ToLower(Trim(value));
            if (lower == "true" || lower == "1" || lower == "yes") {
                out = true;
                return true;
            }
            if (lower == "false" || lower == "0" || lower == "no") {
                out = false;
                return true;
            }
            return false;
        }

        bool ParsePortValue(const std::string& value, int& out)
        {
            std::string trimmed = Trim(value);
            if (trimmed.empty())
                return false;
            char* end = nullptr;
            long parsed = std::strtol(trimmed.c_str(), &end, 10);
            if (end == trimmed.c_str() || (end != nullptr && *end != '\0'))
                return false;
            if (parsed <= 0 || parsed > 65535)
                return false;
            out = static_cast<int>(parsed);
            return true;
        }

        std::int64_t GetWriteTimestampSeconds(const std::filesystem::path& path)
        {
            std::error_code ec;
            auto ftime = std::filesystem::last_write_time(path, ec);
            if (ec)
                return 0;
            auto now = std::chrono::system_clock::now();
            auto ftimeSys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + now);
            return std::chrono::duration_cast<std::chrono::seconds>(ftimeSys.time_since_epoch()).count();
        }

        std::string SanitizeTitleStem(const std::string& title)
        {
            std::string stem;
            stem.reserve(title.size());
            for (unsigned char c : title) {
                if (std::isspace(c))
                    continue;
                if (std::isalnum(c) || c == '-' || c == '_')
                    stem.push_back(static_cast<char>(c));
                else
                    stem.push_back('_');
            }
            if (stem.empty())
                stem = "Shop";
            return stem;
        }

        std::string UniqueFileNameForTitle(const std::string& title)
        {
            const std::string base = SanitizeTitleStem(title);
            std::string candidate = base + ".json";
            int suffix = 2;
            while (std::filesystem::exists(inst::config::shopsDir + "/" + candidate)) {
                candidate = base + "_" + std::to_string(suffix) + ".json";
                suffix++;
            }
            return candidate;
        }

        bool ParseShopUrl(const std::string& rawUrl, std::string& protocol, std::string& host, int& port)
        {
            std::string url = Trim(rawUrl);
            if (url.empty())
                return false;

            protocol = "http";
            if (url.rfind("https://", 0) == 0) {
                protocol = "https";
                url = url.substr(8);
            } else if (url.rfind("http://", 0) == 0) {
                protocol = "http";
                url = url.substr(7);
            }

            std::size_t slashPos = url.find('/');
            if (slashPos != std::string::npos)
                url = url.substr(0, slashPos);

            std::size_t atPos = url.rfind('@');
            if (atPos != std::string::npos)
                url = url.substr(atPos + 1);

            if (url.empty())
                return false;

            host.clear();
            port = DefaultPortForNormalizedProtocol(protocol);
            if (!url.empty() && url.front() == '[') {
                std::size_t closingBracket = url.find(']');
                if (closingBracket == std::string::npos)
                    return false;
                host = url.substr(0, closingBracket + 1);
                if (closingBracket + 1 < url.size() && url[closingBracket + 1] == ':') {
                    int parsedPort = 0;
                    if (!ParsePortValue(url.substr(closingBracket + 2), parsedPort))
                        return false;
                    port = parsedPort;
                }
            } else {
                std::size_t colonPos = url.rfind(':');
                if (colonPos != std::string::npos && url.find(':') == colonPos) {
                    int parsedPort = 0;
                    if (!ParsePortValue(url.substr(colonPos + 1), parsedPort))
                        return false;
                    port = parsedPort;
                    host = url.substr(0, colonPos);
                } else {
                    host = url;
                }
            }

            host = Trim(host);
            if (host.empty())
                return false;
            return true;
        }

        bool ParseShopFile(const std::filesystem::path& path, inst::config::ShopProfile& outShop)
        {
            std::ifstream file(path);
            if (!file)
                return false;

            nlohmann::json root;
            try {
                file >> root;
            }
            catch (...) {
                return false;
            }

            if (!root.is_object())
                return false;

            const nlohmann::json* shopNode = &root;
            if (root.contains("shop")) {
                if (!root["shop"].is_object())
                    return false;
                shopNode = &root["shop"];
            }

            inst::config::ShopProfile parsed;
            parsed.protocol = "http";
            parsed.port = DefaultPortForNormalizedProtocol(parsed.protocol);
            parsed.favourite = false;
            parsed.protocol = NormalizeProtocol(shopNode->value("protocol", "http"));
            parsed.port = DefaultPortForNormalizedProtocol(parsed.protocol);
            parsed.host = shopNode->value("host", "");
            parsed.username = shopNode->value("username", "");
            parsed.password = shopNode->value("password", "");
            parsed.title = shopNode->value("title", "");

            if (shopNode->contains("port")) {
                const auto& portValue = (*shopNode)["port"];
                int parsedPort = 0;
                if (portValue.is_number_integer()) {
                    parsedPort = portValue.get<int>();
                    if (parsedPort > 0 && parsedPort <= 65535)
                        parsed.port = parsedPort;
                } else if (portValue.is_number_unsigned()) {
                    std::uint32_t raw = portValue.get<std::uint32_t>();
                    if (raw > 0 && raw <= 65535)
                        parsed.port = static_cast<int>(raw);
                } else if (portValue.is_string()) {
                    if (ParsePortValue(portValue.get<std::string>(), parsedPort))
                        parsed.port = parsedPort;
                }
            }

            const char* favouriteKeys[] = {"favourite", "favorite"};
            for (const char* key : favouriteKeys) {
                if (!shopNode->contains(key))
                    continue;
                const auto& favouriteValue = (*shopNode)[key];
                bool parsedFavourite = false;
                if (favouriteValue.is_boolean()) {
                    parsed.favourite = favouriteValue.get<bool>();
                } else if (favouriteValue.is_number_integer()) {
                    parsed.favourite = favouriteValue.get<int>() != 0;
                } else if (favouriteValue.is_number_unsigned()) {
                    parsed.favourite = favouriteValue.get<unsigned int>() != 0;
                } else if (favouriteValue.is_string() &&
                           ParseBoolTextValue(favouriteValue.get<std::string>(), parsedFavourite)) {
                    parsed.favourite = parsedFavourite;
                }
                break;
            }

            parsed.protocol = NormalizeProtocol(parsed.protocol);
            parsed.host = Trim(parsed.host);
            parsed.title = Trim(parsed.title);
            if (parsed.port <= 0 || parsed.port > 65535)
                parsed.port = DefaultPortForNormalizedProtocol(parsed.protocol);
            if (parsed.host.empty() || parsed.title.empty())
                return false;

            parsed.fileName = path.filename().string();
            parsed.updatedAt = GetWriteTimestampSeconds(path);
            outShop = parsed;
            return true;
        }

        void SortShopProfiles(std::vector<inst::config::ShopProfile>& shops)
        {
            std::sort(shops.begin(), shops.end(), [](const auto& a, const auto& b) {
                if (a.favourite != b.favourite)
                    return a.favourite > b.favourite;

                if (a.favourite && b.favourite) {
                    std::string aTitle = ToLower(a.title);
                    std::string bTitle = ToLower(b.title);
                    if (aTitle != bTitle)
                        return aTitle < bTitle;
                    return a.fileName < b.fileName;
                }

                if (a.updatedAt != b.updatedAt)
                    return a.updatedAt > b.updatedAt;
                std::string aTitle = ToLower(a.title);
                std::string bTitle = ToLower(b.title);
                if (aTitle != bTitle)
                    return aTitle < bTitle;
                return a.fileName < b.fileName;
            });
        }

        void TryMigrateLegacyShopToJson()
        {
            if (inst::config::shopUrl.empty())
                return;

            std::vector<inst::config::ShopProfile> shops = inst::config::LoadShops();
            for (const auto& shop : shops) {
                if (inst::config::BuildShopUrl(shop) == inst::config::shopUrl &&
                    shop.username == inst::config::shopUser &&
                    shop.password == inst::config::shopPass) {
                    return;
                }
            }

            std::string protocol;
            std::string host;
            int port = DefaultPortForProtocol("http");
            if (!ParseShopUrl(inst::config::shopUrl, protocol, host, port))
                return;

            inst::config::ShopProfile migrated;
            migrated.protocol = protocol;
            migrated.host = host;
            migrated.port = port;
            migrated.username = inst::config::shopUser;
            migrated.password = inst::config::shopPass;
            migrated.title = host;
            migrated.favourite = false;

            std::string ignored;
            inst::config::SaveShop(migrated, &ignored);
        }
    }

    int DefaultPortForProtocol(const std::string& protocol)
    {
        return DefaultPortForNormalizedProtocol(NormalizeProtocol(protocol));
    }

    std::string BuildShopUrl(const ShopProfile& shop)
    {
        std::string host = Trim(shop.host);
        if (host.empty())
            return "";

        std::string protocol = NormalizeProtocol(shop.protocol);
        int port = shop.port;
        if (port <= 0 || port > 65535)
            port = DefaultPortForNormalizedProtocol(protocol);

        return protocol + "://" + host + ":" + std::to_string(port);
    }

    std::vector<ShopProfile> LoadShops()
    {
        std::vector<ShopProfile> shops;
        if (!EnsureShopsDirectory())
            return shops;

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(inst::config::shopsDir, ec)) {
            if (ec)
                break;
            if (!entry.is_regular_file())
                continue;

            ShopProfile parsed;
            if (ParseShopFile(entry.path(), parsed))
                shops.push_back(std::move(parsed));
        }

        SortShopProfiles(shops);
        return shops;
    }

    bool SaveShop(const ShopProfile& shop, std::string* error)
    {
        auto fail = [error](const std::string& message) {
            if (error != nullptr)
                *error = message;
            return false;
        };

        if (!EnsureShopsDirectory())
            return fail("Failed to create shops directory.");

        ShopProfile normalized = shop;
        normalized.protocol = NormalizeProtocol(normalized.protocol);
        normalized.host = Trim(normalized.host);
        normalized.title = Trim(normalized.title);
        if (normalized.port <= 0 || normalized.port > 65535)
            normalized.port = DefaultPortForNormalizedProtocol(normalized.protocol);

        if (normalized.host.empty())
            return fail("Host is required.");
        if (normalized.title.empty())
            return fail("Title is required.");

        std::string fileName = std::filesystem::path(normalized.fileName).filename().string();
        if (fileName.empty()) {
            fileName = UniqueFileNameForTitle(normalized.title);
        } else if (std::filesystem::path(fileName).extension().empty()) {
            fileName += ".json";
        }

        std::filesystem::path shopPath = std::filesystem::path(inst::config::shopsDir) / fileName;
        std::ofstream file(shopPath, std::ios::out | std::ios::trunc);
        if (!file)
            return fail("Failed to open shop file for writing.");

        nlohmann::json j = {
            {"shop", {
                {"protocol", normalized.protocol},
                {"host", normalized.host},
                {"port", normalized.port},
                {"username", normalized.username},
                {"password", normalized.password},
                {"title", normalized.title},
                {"favourite", normalized.favourite}
            }}
        };

        file << std::setw(4) << j << std::endl;

        if (!file.good())
            return fail("Failed to write shop file.");

        return true;
    }

    bool DeleteShop(const std::string& fileName)
    {
        std::string sanitized = std::filesystem::path(fileName).filename().string();
        if (sanitized.empty())
            return false;
        std::filesystem::path shopPath = std::filesystem::path(inst::config::shopsDir) / sanitized;
        std::error_code ec;
        return std::filesystem::remove(shopPath, ec);
    }

    bool SetActiveShop(const ShopProfile& shop, bool writeConfig)
    {
        std::string url = BuildShopUrl(shop);
        if (url.empty())
            return false;
        inst::config::shopUrl = url;
        inst::config::shopUser = shop.username;
        inst::config::shopPass = shop.password;
        if (writeConfig)
            inst::config::setConfig();
        return true;
    }

    void setConfig() {
        nlohmann::json j = {
            {"autoUpdate", autoUpdate},
            {"deletePrompt", deletePrompt},
            {"gAuthKey", gAuthKey},
            {"gayMode", gayMode},
            {"soundEnabled", soundEnabled},
            {"oledMode", oledMode},
            {"mtpExposeAlbum", mtpExposeAlbum},
            {"ignoreReqVers", ignoreReqVers},
            {"languageSetting", languageSetting},
            {"overClock", overClock},
            {"sigPatchesUrl", sigPatchesUrl},
            {"usbAck", usbAck},
            {"validateNCAs", validateNCAs},
            {"lastNetUrl", lastNetUrl},
            {"offlineDbManifestUrl", offlineDbManifestUrl},
            {"shopUrl", shopUrl},
            {"shopUser", shopUser},
            {"shopPass", shopPass},
            {"shopHideInstalled", shopHideInstalled},
            {"shopHideInstalledSection", shopHideInstalledSection},
            {"shopStartGridMode", shopStartGridMode},
            {"offlineDbAutoCheckOnStartup", offlineDbAutoCheckOnStartup},
            {"shopRememberSelection", false},
            {"shopSelection", nlohmann::json::array()}
        };
        std::ofstream file(inst::config::configPath);
        file << std::setw(4) << j << std::endl;
    }

    void parseConfig() {
        gAuthKey = {0x41,0x49,0x7a,0x61,0x53,0x79,0x42,0x4d,0x71,0x76,0x34,0x64,0x58,0x6e,0x54,0x4a,0x4f,0x47,0x51,0x74,0x5a,0x5a,0x53,0x33,0x43,0x42,0x6a,0x76,0x66,0x37,0x34,0x38,0x51,0x76,0x78,0x53,0x7a,0x46,0x30};
        sigPatchesUrl = "https://sigmapatches.coomer.party/sigpatches.zip";
        languageSetting = 99;
        autoUpdate = true;
        deletePrompt = true;
        gayMode = false;
        soundEnabled = true;
        oledMode = false;
        mtpExposeAlbum = false;
        ignoreReqVers = true;
        overClock = true;
        usbAck = false;
        validateNCAs = true;
        lastNetUrl = "https://";
        offlineDbManifestUrl = "https://github.com/luketanti/CyberFoil-DB/releases/latest/download/offline_db_manifest.json";
        shopUrl.clear();
        shopUser.clear();
        shopPass.clear();
        shopHideInstalled = false;
        shopHideInstalledSection = false;
        shopStartGridMode = false;
        offlineDbAutoCheckOnStartup = true;

        try {
            std::ifstream file(inst::config::configPath);
            nlohmann::json j;
            file >> j;
            if (j.contains("autoUpdate")) autoUpdate = j["autoUpdate"].get<bool>();
            if (j.contains("deletePrompt")) deletePrompt = j["deletePrompt"].get<bool>();
            if (j.contains("gAuthKey")) gAuthKey = j["gAuthKey"].get<std::string>();
            if (j.contains("gayMode")) gayMode = j["gayMode"].get<bool>();
            if (j.contains("soundEnabled")) soundEnabled = j["soundEnabled"].get<bool>();
            if (j.contains("oledMode")) oledMode = j["oledMode"].get<bool>();
            if (j.contains("mtpExposeAlbum")) mtpExposeAlbum = j["mtpExposeAlbum"].get<bool>();
            if (j.contains("ignoreReqVers")) ignoreReqVers = j["ignoreReqVers"].get<bool>();
            if (j.contains("languageSetting")) languageSetting = j["languageSetting"].get<int>();
            if (j.contains("overClock")) overClock = j["overClock"].get<bool>();
            if (j.contains("sigPatchesUrl")) sigPatchesUrl = j["sigPatchesUrl"].get<std::string>();
            if (j.contains("usbAck")) usbAck = j["usbAck"].get<bool>();
            if (j.contains("validateNCAs")) validateNCAs = j["validateNCAs"].get<bool>();
            if (j.contains("lastNetUrl")) lastNetUrl = j["lastNetUrl"].get<std::string>();
            if (j.contains("offlineDbManifestUrl")) offlineDbManifestUrl = j["offlineDbManifestUrl"].get<std::string>();
            if (j.contains("shopUrl")) shopUrl = j["shopUrl"].get<std::string>();
            if (j.contains("shopUser")) shopUser = j["shopUser"].get<std::string>();
            if (j.contains("shopPass")) shopPass = j["shopPass"].get<std::string>();
            if (j.contains("shopHideInstalled")) shopHideInstalled = j["shopHideInstalled"].get<bool>();
            if (j.contains("shopHideInstalledSection")) shopHideInstalledSection = j["shopHideInstalledSection"].get<bool>();
            if (j.contains("shopStartGridMode")) shopStartGridMode = j["shopStartGridMode"].get<bool>();
            if (j.contains("offlineDbAutoCheckOnStartup")) offlineDbAutoCheckOnStartup = j["offlineDbAutoCheckOnStartup"].get<bool>();
        }
        catch (...) {
            // If loading values from the config fails, we just load the defaults and overwrite the old config
            setConfig();
        }
        if (sigPatchesUrl == "https://github.com/Huntereb/Awoo-Installer/releases/download/SignaturePatches/patches.zip")
            sigPatchesUrl = "https://sigmapatches.coomer.party/sigpatches.zip";

        EnsureShopsDirectory();
        TryMigrateLegacyShopToJson();
    }
}
