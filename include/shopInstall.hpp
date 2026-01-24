#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace shopInstStuff {
    struct ShopItem {
        std::string name;
        std::string url;
        std::string iconUrl;
        std::string appId;
        std::uint64_t size;
        std::uint64_t titleId = 0;
        std::uint32_t appVersion = 0;
        std::int32_t appType = -1;
        bool hasTitleId = false;
        bool hasAppVersion = false;
        bool hasIconUrl = false;
        bool hasAppId = false;
    };

    struct ShopSection {
        std::string id;
        std::string title;
        std::vector<ShopItem> items;
    };

    std::vector<ShopItem> FetchShop(const std::string& shopUrl, const std::string& user, const std::string& pass, std::string& error);
    std::vector<ShopSection> FetchShopSections(const std::string& shopUrl, const std::string& user, const std::string& pass, std::string& error, bool allowCache = true);
    std::string FetchShopMotd(const std::string& shopUrl, const std::string& user, const std::string& pass);
    void installTitleShop(const std::vector<ShopItem>& items, int storage, const std::string& sourceLabel);
}
