#include <algorithm>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <cctype>
#include <cstdlib>
#include <switch.h>
#include "ui/MainApplication.hpp"
#include "ui/shopInstPage.hpp"
#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/lang.hpp"
#include "util/title_util.hpp"
#include "util/util.hpp"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace {
    std::string NormalizeHex(std::string hex)
    {
        std::string out;
        out.reserve(hex.size());
        for (char c : hex) {
            if (std::isxdigit(static_cast<unsigned char>(c)))
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return out;
    }

    bool TryParseHexU64(const std::string& hex, std::uint64_t& out)
    {
        if (hex.empty())
            return false;
        char* end = nullptr;
        unsigned long long parsed = std::strtoull(hex.c_str(), &end, 16);
        if (end == hex.c_str() || (end && *end != '\0'))
            return false;
        out = static_cast<std::uint64_t>(parsed);
        return true;
    }

    bool DeriveBaseTitleId(const shopInstStuff::ShopItem& item, std::uint64_t& out)
    {
        if (item.hasTitleId) {
            out = item.titleId;
            return true;
        }
        if (!item.hasAppId)
            return false;
        std::string appId = NormalizeHex(item.appId);
        if (appId.size() < 16)
            return false;
        std::string baseId;
        if (item.appType == NcmContentMetaType_Patch) {
            baseId = appId.substr(0, appId.size() - 3) + "000";
        } else if (item.appType == NcmContentMetaType_AddOnContent) {
            std::string basePart = appId.substr(0, appId.size() - 3);
            if (basePart.empty())
                return false;
            char* end = nullptr;
            unsigned long long baseValue = std::strtoull(basePart.c_str(), &end, 16);
            if (end == basePart.c_str() || (end && *end != '\0') || baseValue == 0)
                return false;
            baseValue -= 1;
            char buf[17] = {0};
            std::snprintf(buf, sizeof(buf), "%0*llx", (int)basePart.size(), baseValue);
            baseId = std::string(buf) + "000";
        } else {
            baseId = appId;
        }
        return TryParseHexU64(baseId, out);
    }

    bool IsBaseItem(const shopInstStuff::ShopItem& item)
    {
        if (item.appType == NcmContentMetaType_Application)
            return true;
        if (item.hasAppId) {
            std::string appId = NormalizeHex(item.appId);
            return appId.size() >= 3 && appId.rfind("000") == appId.size() - 3;
        }
        if (item.hasTitleId) {
            return (item.titleId & 0xFFF) == 0;
        }
        return false;
    }

    bool TryGetInstalledUpdateVersionNcm(u64 baseTitleId, u32& outVersion)
    {
        outVersion = 0;
        const u64 patchTitleId = baseTitleId ^ 0x800;
        const NcmStorageId storages[] = {NcmStorageId_BuiltInUser, NcmStorageId_SdCard};
        for (auto storage : storages) {
            NcmContentMetaDatabase db;
            if (R_FAILED(ncmOpenContentMetaDatabase(&db, storage)))
                continue;
            NcmContentMetaKey key = {};
            if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, patchTitleId))) {
                if (key.type == NcmContentMetaType_Patch && key.id == patchTitleId) {
                    if (key.version > outVersion)
                        outVersion = key.version;
                }
            }
            ncmContentMetaDatabaseClose(&db);
        }
        return outVersion > 0;
    }
}

namespace inst::ui {
    extern MainApplication *mainApp;

    shopInstPage::shopInstPage() : Layout::Layout() {
        this->SetBackgroundColor(COLOR("#670000FF"));
        if (std::filesystem::exists(inst::config::appDir + "/background.png")) this->SetBackgroundImage(inst::config::appDir + "/background.png");
        else this->SetBackgroundImage("romfs:/images/background.jpg");
        this->topRect = Rectangle::New(0, 0, 1280, 94, COLOR("#170909FF"));
        this->infoRect = Rectangle::New(0, 95, 1280, 60, COLOR("#17090980"));
        this->botRect = Rectangle::New(0, 660, 1280, 60, COLOR("#17090980"));
        if (inst::config::gayMode) {
            this->titleImage = Image::New(-113, 0, "romfs:/images/logo.png");
            this->appVersionText = TextBlock::New(367, 49, "v" + inst::config::appVersion, 22);
        }
        else {
            this->titleImage = Image::New(0, 0, "romfs:/images/logo.png");
            this->appVersionText = TextBlock::New(480, 49, "v" + inst::config::appVersion, 22);
        }
        this->appVersionText->SetColor(COLOR("#FFFFFFFF"));
        this->pageInfoText = TextBlock::New(10, 109, "", 30);
        this->pageInfoText->SetColor(COLOR("#FFFFFFFF"));
        this->butText = TextBlock::New(10, 678, "", 24);
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->menu = pu::ui::elm::Menu::New(0, 156, 1280, COLOR("#FFFFFF00"), 84, (506 / 84));
        this->menu->SetOnFocusColor(COLOR("#00000033"));
        this->menu->SetScrollbarColor(COLOR("#17090980"));
        this->infoImage = Image::New(453, 292, "romfs:/images/icons/lan-connection-waiting.png");
        this->previewImage = Image::New(900, 230, "romfs:/images/awoos/7d8a05cddfef6da4901b20d2698d5a71.png");
        this->previewImage->SetWidth(320);
        this->previewImage->SetHeight(320);
        this->debugText = TextBlock::New(10, 620, "", 18);
        this->debugText->SetColor(COLOR("#FFFFFFFF"));
        this->debugText->SetVisible(false);
        this->Add(this->topRect);
        this->Add(this->infoRect);
        this->Add(this->botRect);
        this->Add(this->titleImage);
        this->Add(this->appVersionText);
        this->Add(this->butText);
        this->Add(this->pageInfoText);
        this->Add(this->menu);
        this->Add(this->infoImage);
        this->Add(this->previewImage);
        this->Add(this->debugText);
    }

    bool shopInstPage::isAllSection() const {
        if (this->shopSections.empty())
            return false;
        if (this->selectedSectionIndex < 0 || this->selectedSectionIndex >= (int)this->shopSections.size())
            return false;
        return this->shopSections[this->selectedSectionIndex].id == "all";
    }

    const std::vector<shopInstStuff::ShopItem>& shopInstPage::getCurrentItems() const {
        static const std::vector<shopInstStuff::ShopItem> empty;
        if (this->shopSections.empty())
            return empty;
        if (this->selectedSectionIndex < 0 || this->selectedSectionIndex >= (int)this->shopSections.size())
            return empty;
        return this->shopSections[this->selectedSectionIndex].items;
    }

    void shopInstPage::updateSectionText() {
        if (this->shopSections.empty()) {
            this->pageInfoText->SetText("inst.shop.top_info"_lang);
            return;
        }
        const auto& section = this->shopSections[this->selectedSectionIndex];
        std::string label = "inst.shop.top_info"_lang + " " + section.title;
        if (this->isAllSection() && !this->searchQuery.empty()) {
            label += " (" + this->searchQuery + ")";
        }
        this->pageInfoText->SetText(label);
    }

    void shopInstPage::updateButtonsText() {
        if (this->isAllSection())
            this->butText->SetText("inst.shop.buttons_all"_lang);
        else
            this->butText->SetText("inst.shop.buttons"_lang);
    }

    void shopInstPage::buildInstalledSection() {
        std::vector<shopInstStuff::ShopItem> installedItems;
        Result rc = nsInitialize();
        if (R_FAILED(rc))
            return;
        rc = ncmInitialize();
        if (R_FAILED(rc)) {
            nsExit();
            return;
        }

        const s32 chunk = 64;
        s32 offset = 0;
        while (true) {
            NsApplicationRecord records[chunk];
            s32 outCount = 0;
            rc = nsListApplicationRecord(records, chunk, offset, &outCount);
            if (R_FAILED(rc) || outCount <= 0)
                break;

            for (s32 i = 0; i < outCount; i++) {
                const u64 baseId = records[i].application_id;
                shopInstStuff::ShopItem baseItem;
                baseItem.name = tin::util::GetTitleName(baseId, NcmContentMetaType_Application);
                baseItem.url = "";
                baseItem.size = 0;
                baseItem.titleId = baseId;
                baseItem.hasTitleId = true;
                baseItem.appType = NcmContentMetaType_Application;
                installedItems.push_back(baseItem);

                s32 metaCount = 0;
                if (R_SUCCEEDED(nsCountApplicationContentMeta(baseId, &metaCount)) && metaCount > 0) {
                    std::vector<NsApplicationContentMetaStatus> list(metaCount);
                    s32 metaOut = 0;
                    if (R_SUCCEEDED(nsListApplicationContentMetaStatus(baseId, 0, list.data(), metaCount, &metaOut)) && metaOut > 0) {
                        for (s32 j = 0; j < metaOut; j++) {
                            if (list[j].meta_type != NcmContentMetaType_Patch && list[j].meta_type != NcmContentMetaType_AddOnContent)
                                continue;
                            shopInstStuff::ShopItem item;
                            item.titleId = list[j].application_id;
                            item.hasTitleId = true;
                            item.appVersion = list[j].version;
                            item.hasAppVersion = true;
                            item.appType = list[j].meta_type;
                            item.name = tin::util::GetTitleName(item.titleId, static_cast<NcmContentMetaType>(item.appType));
                            item.url = "";
                            item.size = 0;
                            installedItems.push_back(item);
                        }
                    }
                }
            }
            offset += outCount;
        }

        nsExit();

        if (installedItems.empty())
            return;

        std::sort(installedItems.begin(), installedItems.end(), [](const auto& a, const auto& b) {
            return inst::util::ignoreCaseCompare(a.name, b.name);
        });

        shopInstStuff::ShopSection installedSection;
        installedSection.id = "installed";
        installedSection.title = "Installed";
        installedSection.items = std::move(installedItems);
        this->shopSections.insert(this->shopSections.begin(), std::move(installedSection));
    }

    void shopInstPage::cacheAvailableUpdates() {
        this->availableUpdates.clear();
        for (const auto& section : this->shopSections) {
            if (section.id == "updates") {
                this->availableUpdates = section.items;
                break;
            }
        }
    }

    void shopInstPage::filterOwnedSections() {
        if (this->shopSections.empty())
            return;

        Result rc = nsInitialize();
        if (R_FAILED(rc))
            return;

        std::unordered_map<std::uint64_t, std::uint32_t> installedUpdateVersion;
        std::unordered_map<std::uint64_t, bool> baseInstalled;

        const s32 chunk = 64;
        s32 offset = 0;
        while (true) {
            NsApplicationRecord records[chunk];
            s32 outCount = 0;
            if (R_FAILED(nsListApplicationRecord(records, chunk, offset, &outCount)) || outCount <= 0)
                break;
            for (s32 i = 0; i < outCount; i++)
                baseInstalled[records[i].application_id] = true;
            offset += outCount;
        }

        auto isBaseInstalled = [&](const shopInstStuff::ShopItem& item, std::uint32_t& outVersion) {
            std::uint64_t baseTitleId = 0;
            if (!DeriveBaseTitleId(item, baseTitleId))
                return false;
            auto baseIt = baseInstalled.find(baseTitleId);
            if (baseIt != baseInstalled.end()) {
                auto verIt = installedUpdateVersion.find(baseTitleId);
                if (verIt != installedUpdateVersion.end()) {
                    outVersion = verIt->second;
                    return baseIt->second;
                }
                if (!baseIt->second)
                    return false;
            }
            bool installed = true;
            if (baseIt == baseInstalled.end())
                installed = tin::util::IsTitleInstalled(baseTitleId);
            if (installed) {
                tin::util::GetInstalledUpdateVersion(baseTitleId, outVersion);
                if (outVersion == 0)
                    TryGetInstalledUpdateVersionNcm(baseTitleId, outVersion);
            }
            baseInstalled[baseTitleId] = installed;
            installedUpdateVersion[baseTitleId] = outVersion;
            return installed;
        };

        for (auto& section : this->shopSections) {
            if (section.items.empty())
                continue;
            if (section.id != "updates" && section.id != "dlc")
                continue;

            std::vector<shopInstStuff::ShopItem> filtered;
            filtered.reserve(section.items.size());
            for (const auto& item : section.items) {
                std::uint32_t installedVersion = 0;
                if (!isBaseInstalled(item, installedVersion))
                    continue;
                if (section.id == "updates" || item.appType == NcmContentMetaType_Patch) {
                    if (!item.hasAppVersion)
                        continue;
                    if (item.appVersion > installedVersion)
                        filtered.push_back(item);
                } else {
                    filtered.push_back(item);
                }
            }
            section.items = std::move(filtered);
        }

        ncmExit();
        nsExit();
    }

    void shopInstPage::updatePreview() {
        if (this->visibleItems.empty()) {
            this->previewImage->SetVisible(false);
            this->previewKey.clear();
            return;
        }

        int selectedIndex = this->menu->GetSelectedIndex();
        if (selectedIndex < 0 || selectedIndex >= (int)this->visibleItems.size())
            return;
        const auto& item = this->visibleItems[selectedIndex];

        std::string key;
        if (item.url.empty()) {
            key = "installed:" + std::to_string(item.titleId);
        } else if (item.hasIconUrl) {
            key = item.iconUrl;
        } else {
            key = item.url;
        }

        if (key == this->previewKey)
            return;
        this->previewKey = key;

        auto applyPreviewLayout = [&]() {
            this->previewImage->SetX(900);
            this->previewImage->SetY(230);
            this->previewImage->SetWidth(320);
            this->previewImage->SetHeight(320);
        };

        if (item.url.empty()) {
            Result rc = nsInitialize();
            if (R_SUCCEEDED(rc)) {
                u64 baseId = tin::util::GetBaseTitleId(item.titleId, static_cast<NcmContentMetaType>(item.appType));
                NsApplicationControlData appControlData;
                u64 sizeRead = 0;
                if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, baseId, &appControlData, sizeof(NsApplicationControlData), &sizeRead))) {
                    u64 iconSize = 0;
                    if (sizeRead > sizeof(appControlData.nacp))
                        iconSize = sizeRead - sizeof(appControlData.nacp);
                    if (iconSize > 0) {
                        this->previewImage->SetJpegImage(appControlData.icon, iconSize);
                        applyPreviewLayout();
                        this->previewImage->SetVisible(true);
                        nsExit();
                        return;
                    }
                }
                nsExit();
            }
            this->previewImage->SetImage("romfs:/images/awoos/7d8a05cddfef6da4901b20d2698d5a71.png");
            applyPreviewLayout();
            this->previewImage->SetVisible(true);
            return;
        }

        if (item.hasIconUrl) {
            std::string cacheDir = inst::config::appDir + "/shop_icons";
            if (!std::filesystem::exists(cacheDir))
                std::filesystem::create_directory(cacheDir);

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
            std::string filePath = cacheDir + "/" + fileName + ext;

            if (!std::filesystem::exists(filePath)) {
                bool ok = inst::curl::downloadImageWithAuth(item.iconUrl, filePath.c_str(), inst::config::shopUser, inst::config::shopPass, 8000);
                if (!ok) {
                    if (std::filesystem::exists(filePath))
                        std::filesystem::remove(filePath);
                }
            }

            if (std::filesystem::exists(filePath)) {
                this->previewImage->SetImage(filePath);
                applyPreviewLayout();
                this->previewImage->SetVisible(true);
                return;
            }
        }

        this->previewImage->SetImage("romfs:/images/awoos/7d8a05cddfef6da4901b20d2698d5a71.png");
        applyPreviewLayout();
        this->previewImage->SetVisible(true);
    }

    void shopInstPage::updateDebug() {
        if (!this->debugVisible) {
            this->debugText->SetVisible(false);
            return;
        }
        if (this->visibleItems.empty()) {
            std::string text = "debug: no items";
            if (!this->shopSections.empty() && this->selectedSectionIndex >= 0 && this->selectedSectionIndex < (int)this->shopSections.size()) {
                const auto& section = this->shopSections[this->selectedSectionIndex];
                text += " section=" + section.id;
                if (section.id == "updates") {
                    text += " pre=" + std::to_string(this->availableUpdates.size());
                    text += " post=" + std::to_string(section.items.size());
                }
            }
            this->debugText->SetText(text);
            this->debugText->SetVisible(true);
            return;
        }

        int selectedIndex = this->menu->GetSelectedIndex();
        if (selectedIndex < 0 || selectedIndex >= (int)this->visibleItems.size())
            return;
        const auto& item = this->visibleItems[selectedIndex];

        std::uint64_t baseTitleId = 0;
        bool hasBase = DeriveBaseTitleId(item, baseTitleId);
        bool installed = false;
        std::uint32_t installedVersion = 0;

        if (hasBase) {
            if (R_SUCCEEDED(nsInitialize()) && R_SUCCEEDED(ncmInitialize())) {
                installed = tin::util::IsTitleInstalled(baseTitleId);
                if (installed) {
                    tin::util::GetInstalledUpdateVersion(baseTitleId, installedVersion);
                    if (installedVersion == 0)
                        TryGetInstalledUpdateVersionNcm(baseTitleId, installedVersion);
                }
                ncmExit();
                nsExit();
            }
        }

        char baseBuf[32] = {0};
        if (hasBase)
            std::snprintf(baseBuf, sizeof(baseBuf), "%016lx", baseTitleId);
        else
            std::snprintf(baseBuf, sizeof(baseBuf), "unknown");

        std::string text = "debug: base=" + std::string(baseBuf);
        text += " installed=" + std::string(installed ? "1" : "0");
        text += " inst_ver=" + std::to_string(installedVersion);
        text += " avail_ver=" + (item.hasAppVersion ? std::to_string(item.appVersion) : std::string("n/a"));
        text += " type=" + std::to_string(item.appType);
        text += " has_appv=" + std::string(item.hasAppVersion ? "1" : "0");
        text += " has_tid=" + std::string(item.hasTitleId ? "1" : "0");
        text += " has_appid=" + std::string(item.hasAppId ? "1" : "0");
        if (item.hasAppId)
            text += " app_id=" + item.appId;
        this->debugText->SetText(text);
        this->debugText->SetVisible(true);
    }

    void shopInstPage::drawMenuItems(bool clearItems) {
        if (clearItems) this->selectedItems.clear();
        this->menu->ClearItems();
        this->visibleItems.clear();
        const auto& items = this->getCurrentItems();
        if (this->isAllSection() && !this->searchQuery.empty()) {
            for (const auto& item : items) {
                std::string name = item.name;
                std::string query = this->searchQuery;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                std::transform(query.begin(), query.end(), query.begin(), ::tolower);
                if (name.find(query) != std::string::npos)
                    this->visibleItems.push_back(item);
            }
        } else {
            this->visibleItems = items;
        }

        for (const auto& item : this->visibleItems) {
            std::string itm = inst::util::shortenString(item.name, 56, true);
            auto entry = pu::ui::elm::MenuItem::New(itm);
            entry->SetColor(COLOR("#FFFFFFFF"));
            entry->SetIcon("romfs:/images/icons/checkbox-blank-outline.png");
            for (const auto& selected : this->selectedItems) {
                if (selected.url == item.url) {
                    entry->SetIcon("romfs:/images/icons/check-box-outline.png");
                    break;
                }
            }
            this->menu->AddItem(entry);
        }
    }

    void shopInstPage::selectTitle(int selectedIndex) {
        if (selectedIndex < 0 || selectedIndex >= (int)this->visibleItems.size())
            return;
        const auto& item = this->visibleItems[selectedIndex];
        if (item.url.empty())
            return;
        auto selected = std::find_if(this->selectedItems.begin(), this->selectedItems.end(), [&](const auto& entry) {
            return entry.url == item.url;
        });
        if (selected != this->selectedItems.end())
            this->selectedItems.erase(selected);
        else
            this->selectedItems.push_back(item);
        this->updateRememberedSelection();
        this->drawMenuItems(false);
    }

    void shopInstPage::updateRememberedSelection() {
        if (!inst::config::shopRememberSelection)
            return;
        inst::config::shopSelection.clear();
        inst::config::shopSelection.reserve(this->selectedItems.size());
        for (const auto& item : this->selectedItems)
            inst::config::shopSelection.push_back(item.url);
        inst::config::setConfig();
    }

    void shopInstPage::startShop() {
        this->butText->SetText("inst.shop.buttons_loading"_lang);
        this->menu->SetVisible(false);
        this->menu->ClearItems();
        this->infoImage->SetVisible(true);
        this->previewImage->SetVisible(false);
        this->pageInfoText->SetText("inst.shop.loading"_lang);
        mainApp->LoadLayout(mainApp->shopinstPage);
        mainApp->CallForRender();

        std::string shopUrl = inst::config::shopUrl;
        if (shopUrl.empty()) {
            shopUrl = inst::util::softwareKeyboard("options.shop.url_hint"_lang, "http://", 200);
            if (shopUrl.empty()) {
                mainApp->LoadLayout(mainApp->mainPage);
                return;
            }
            inst::config::shopUrl = shopUrl;
            inst::config::setConfig();
        }

        std::string error;
        this->shopSections = shopInstStuff::FetchShopSections(shopUrl, inst::config::shopUser, inst::config::shopPass, error);
        if (!error.empty()) {
            mainApp->CreateShowDialog("inst.shop.failed"_lang, error, {"common.ok"_lang}, true);
            mainApp->LoadLayout(mainApp->mainPage);
            return;
        }
        if (this->shopSections.empty()) {
            mainApp->CreateShowDialog("inst.shop.empty"_lang, "", {"common.ok"_lang}, true);
            mainApp->LoadLayout(mainApp->mainPage);
            return;
        }

        this->buildInstalledSection();
        this->cacheAvailableUpdates();
        this->filterOwnedSections();

        this->selectedSectionIndex = 0;
        this->updateSectionText();
        this->updateButtonsText();
        this->selectedItems.clear();
        if (inst::config::shopRememberSelection && !inst::config::shopSelection.empty()) {
            for (const auto& url : inst::config::shopSelection) {
                for (const auto& section : this->shopSections) {
                    auto match = std::find_if(section.items.begin(), section.items.end(), [&](const auto& item) {
                        return item.url == url;
                    });
                    if (match != section.items.end()) {
                        this->selectedItems.push_back(*match);
                        break;
                    }
                }
            }
        }
        this->drawMenuItems(false);
        this->menu->SetSelectedIndex(0);
        this->infoImage->SetVisible(false);
        this->menu->SetVisible(true);
        this->updatePreview();
    }

    void shopInstPage::startInstall() {
        if (!this->selectedItems.empty()) {
            std::vector<shopInstStuff::ShopItem> updatesToAdd;
            std::unordered_map<std::uint64_t, shopInstStuff::ShopItem> latestUpdates;
            for (const auto& update : this->availableUpdates) {
                if (update.appType != NcmContentMetaType_Patch || !update.hasAppVersion)
                    continue;
                std::uint64_t baseTitleId = 0;
                if (!DeriveBaseTitleId(update, baseTitleId))
                    continue;
                auto it = latestUpdates.find(baseTitleId);
                if (it == latestUpdates.end() || update.appVersion > it->second.appVersion)
                    latestUpdates[baseTitleId] = update;
            }

            for (const auto& item : this->selectedItems) {
                if (!IsBaseItem(item))
                    continue;
                std::uint64_t baseTitleId = 0;
                if (!DeriveBaseTitleId(item, baseTitleId))
                    continue;
                auto updateIt = latestUpdates.find(baseTitleId);
                if (updateIt == latestUpdates.end())
                    continue;
                bool alreadySelected = std::any_of(this->selectedItems.begin(), this->selectedItems.end(), [&](const auto& entry) {
                    return entry.url == updateIt->second.url;
                });
                if (!alreadySelected && !updateIt->second.url.empty())
                    updatesToAdd.push_back(updateIt->second);
            }

            if (!updatesToAdd.empty()) {
                int res = mainApp->CreateShowDialog("inst.shop.update_prompt_title"_lang,
                    "inst.shop.update_prompt_desc"_lang + std::to_string(updatesToAdd.size()),
                    {"common.yes"_lang, "common.no"_lang}, false);
                if (res == 0) {
                    for (const auto& update : updatesToAdd)
                        this->selectedItems.push_back(update);
                }
            }
        }

        int dialogResult = -1;
        if (this->selectedItems.size() == 1) {
            std::string name = inst::util::shortenString(this->selectedItems[0].name, 32, true);
            dialogResult = mainApp->CreateShowDialog("inst.target.desc0"_lang + name + "inst.target.desc1"_lang, "common.cancel_desc"_lang, {"inst.target.opt0"_lang, "inst.target.opt1"_lang}, false);
        } else {
            dialogResult = mainApp->CreateShowDialog("inst.target.desc00"_lang + std::to_string(this->selectedItems.size()) + "inst.target.desc01"_lang, "common.cancel_desc"_lang, {"inst.target.opt0"_lang, "inst.target.opt1"_lang}, false);
        }
        if (dialogResult == -1)
            return;

        this->updateRememberedSelection();
        shopInstStuff::installTitleShop(this->selectedItems, dialogResult, "inst.shop.source_string"_lang);
    }

    void shopInstPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos) {
        if (Down & HidNpadButton_B) {
            this->updateRememberedSelection();
            mainApp->LoadLayout(mainApp->mainPage);
        }
        if ((Down & HidNpadButton_A) || (Up & TouchPseudoKey)) {
            this->selectTitle(this->menu->GetSelectedIndex());
            if (this->menu->GetItems().size() == 1 && this->selectedItems.size() == 1) {
                this->startInstall();
            }
        }
        if (Down & HidNpadButton_L) {
            if (this->shopSections.size() > 1) {
                this->selectedSectionIndex = (this->selectedSectionIndex - 1 + (int)this->shopSections.size()) % (int)this->shopSections.size();
                this->searchQuery.clear();
                this->updateSectionText();
                this->updateButtonsText();
                this->drawMenuItems(false);
            }
        }
        if (Down & HidNpadButton_R) {
            if (this->shopSections.size() > 1) {
                this->selectedSectionIndex = (this->selectedSectionIndex + 1) % (int)this->shopSections.size();
                this->searchQuery.clear();
                this->updateSectionText();
                this->updateButtonsText();
                this->drawMenuItems(false);
            }
        }
        if (Down & HidNpadButton_ZR) {
            if (this->isAllSection()) {
                std::string query = inst::util::softwareKeyboard("inst.shop.search_hint"_lang, this->searchQuery, 60);
                this->searchQuery = query;
                this->updateSectionText();
                this->drawMenuItems(false);
            }
        }
        if (Down & HidNpadButton_ZL) {
            this->debugVisible = !this->debugVisible;
            this->updateDebug();
        }
        if (Down & HidNpadButton_Y) {
            if (this->selectedItems.size() == this->menu->GetItems().size()) {
                this->drawMenuItems(true);
            } else {
                for (long unsigned int i = 0; i < this->menu->GetItems().size(); i++) {
                    if (this->menu->GetItems()[i]->GetIcon() == "romfs:/images/icons/check-box-outline.png") continue;
                    this->selectTitle(i);
                }
                this->drawMenuItems(false);
            }
        }
        if (Down & HidNpadButton_X) {
            this->startShop();
        }
        if (Down & HidNpadButton_Plus) {
            if (this->selectedItems.empty()) {
                this->selectTitle(this->menu->GetSelectedIndex());
            }
            if (!this->selectedItems.empty()) this->startInstall();
        }
        this->updatePreview();
        this->updateDebug();
    }
}
