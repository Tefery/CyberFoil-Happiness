#include <algorithm>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <switch.h>
#include "ui/MainApplication.hpp"
#include "ui/shopInstPage.hpp"
#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/lang.hpp"
#include "util/title_util.hpp"
#include "util/util.hpp"
#include "ui/bottomHint.hpp"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace {
    constexpr int kGridCols = 10;
    constexpr int kGridRows = 4;
    constexpr int kGridTileWidth = 120;
    constexpr int kGridTileHeight = 120;
    constexpr int kGridGap = 6;
    constexpr int kGridWidth = (kGridCols * kGridTileWidth) + ((kGridCols - 1) * kGridGap);
    constexpr int kGridStartX = (1280 - kGridWidth) / 2;
    constexpr int kGridStartY = 140;
    constexpr int kGridItemsPerPage = kGridCols * kGridRows;

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

    std::uint32_t DecodeUtf8CodePoint(const std::string& text, std::size_t& i)
    {
        const unsigned char c0 = static_cast<unsigned char>(text[i]);
        if (c0 < 0x80) {
            i += 1;
            return c0;
        }
        if ((c0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
            const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            if ((c1 & 0xC0) == 0x80) {
                const std::uint32_t cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
                if (cp >= 0x80) {
                    i += 2;
                    return cp;
                }
            }
        } else if ((c0 & 0xF0) == 0xE0 && i + 2 < text.size()) {
            const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
                const std::uint32_t cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
                if (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                    i += 3;
                    return cp;
                }
            }
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < text.size()) {
            const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            const unsigned char c3 = static_cast<unsigned char>(text[i + 3]);
            if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                const std::uint32_t cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                if (cp >= 0x10000 && cp <= 0x10FFFF) {
                    i += 4;
                    return cp;
                }
            }
        }
        i += 1;
        return c0;
    }

    void AppendUtf8(std::string& out, std::uint32_t cp)
    {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool IsCombiningMark(std::uint32_t cp)
    {
        return (cp >= 0x0300 && cp <= 0x036F)
            || (cp >= 0x1AB0 && cp <= 0x1AFF)
            || (cp >= 0x1DC0 && cp <= 0x1DFF)
            || (cp >= 0x20D0 && cp <= 0x20FF)
            || (cp >= 0xFE20 && cp <= 0xFE2F);
    }

    char FoldLatinDiacritic(std::uint32_t cp)
    {
        switch (cp) {
            case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
            case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
            case 0x0100: case 0x0101: case 0x0102: case 0x0103: case 0x0104: case 0x0105:
                return 'a';
            case 0x00C7: case 0x00E7: case 0x0106: case 0x0107: case 0x0108: case 0x0109:
            case 0x010A: case 0x010B: case 0x010C: case 0x010D:
                return 'c';
            case 0x00D0: case 0x00F0: case 0x010E: case 0x010F: case 0x0110: case 0x0111:
                return 'd';
            case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: case 0x00E8: case 0x00E9:
            case 0x00EA: case 0x00EB: case 0x0112: case 0x0113: case 0x0114: case 0x0115:
            case 0x0116: case 0x0117: case 0x0118: case 0x0119: case 0x011A: case 0x011B:
                return 'e';
            case 0x011C: case 0x011D: case 0x011E: case 0x011F: case 0x0120: case 0x0121:
            case 0x0122: case 0x0123:
                return 'g';
            case 0x0124: case 0x0125: case 0x0126: case 0x0127:
                return 'h';
            case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: case 0x00EC: case 0x00ED:
            case 0x00EE: case 0x00EF: case 0x0128: case 0x0129: case 0x012A: case 0x012B:
            case 0x012C: case 0x012D: case 0x012E: case 0x012F: case 0x0130: case 0x0131:
                return 'i';
            case 0x0134: case 0x0135:
                return 'j';
            case 0x0136: case 0x0137: case 0x0138:
                return 'k';
            case 0x0139: case 0x013A: case 0x013B: case 0x013C: case 0x013D: case 0x013E:
            case 0x013F: case 0x0140: case 0x0141: case 0x0142:
                return 'l';
            case 0x00D1: case 0x00F1: case 0x0143: case 0x0144: case 0x0145: case 0x0146:
            case 0x0147: case 0x0148:
                return 'n';
            case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
            case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
            case 0x014C: case 0x014D: case 0x014E: case 0x014F: case 0x0150: case 0x0151:
                return 'o';
            case 0x0154: case 0x0155: case 0x0156: case 0x0157: case 0x0158: case 0x0159:
                return 'r';
            case 0x015A: case 0x015B: case 0x015C: case 0x015D: case 0x015E: case 0x015F:
            case 0x0160: case 0x0161:
                return 's';
            case 0x0162: case 0x0163: case 0x0164: case 0x0165: case 0x0166: case 0x0167:
                return 't';
            case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: case 0x00F9: case 0x00FA:
            case 0x00FB: case 0x00FC: case 0x0168: case 0x0169: case 0x016A: case 0x016B:
            case 0x016C: case 0x016D: case 0x016E: case 0x016F: case 0x0170: case 0x0171:
            case 0x0172: case 0x0173:
                return 'u';
            case 0x00DD: case 0x00FD: case 0x00FF: case 0x0176: case 0x0177: case 0x0178:
                return 'y';
            case 0x0179: case 0x017A: case 0x017B: case 0x017C: case 0x017D: case 0x017E:
                return 'z';
            default:
                return 0;
        }
    }

    std::string NormalizeSearchKey(const std::string& text)
    {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size();) {
            const std::uint32_t cp = DecodeUtf8CodePoint(text, i);
            if (cp < 0x80) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(cp))));
                continue;
            }
            if (IsCombiningMark(cp))
                continue;

            const char folded = FoldLatinDiacritic(cp);
            if (folded != 0) {
                out.push_back(folded);
                continue;
            }
            if (cp == 0x00DF) {
                out += "ss";
                continue;
            }
            if (cp == 0x00C6 || cp == 0x00E6) {
                out += "ae";
                continue;
            }
            if (cp == 0x0152 || cp == 0x0153) {
                out += "oe";
                continue;
            }
            if (cp == 0x00DE || cp == 0x00FE) {
                out += "th";
                continue;
            }

            AppendUtf8(out, cp);
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

    bool IsBaseTitleCurrentlyInstalled(u64 baseTitleId)
    {
        s32 metaCount = 0;
        if (R_FAILED(nsCountApplicationContentMeta(baseTitleId, &metaCount)) || metaCount <= 0)
            return false;
        return tin::util::IsTitleInstalled(baseTitleId);
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

    void CenterTextX(const TextBlock::Ref& text, int containerWidth = 1280)
    {
        int textX = (containerWidth - text->GetTextWidth()) / 2;
        if (textX < 0)
            textX = 0;
        text->SetX(textX);
    }

    std::string FormatGridSizeSuffix(std::uint64_t bytes)
    {
        if (bytes == 0)
            return std::string();
        const double kb = 1024.0;
        const double mb = kb * 1024.0;
        const double gb = mb * 1024.0;
        char buf[32] = {0};
        if (bytes >= static_cast<std::uint64_t>(gb))
            std::snprintf(buf, sizeof(buf), "%.1f GB", bytes / gb);
        else
            std::snprintf(buf, sizeof(buf), "%.0f MB", bytes / mb);
        return " [" + std::string(buf) + "]";
    }

    std::string BuildGridTitleWithSize(const shopInstStuff::ShopItem& item)
    {
        const std::string suffix = FormatGridSizeSuffix(item.size);
        int nameLimit = 70;
        if (!suffix.empty()) {
            const int suffixChars = static_cast<int>(suffix.size()) + 1;
            if (nameLimit > suffixChars)
                nameLimit -= suffixChars;
        }
        if (nameLimit < 8)
            nameLimit = 8;
        return inst::util::shortenString(item.name, nameLimit, true) + suffix;
    }
}

namespace inst::ui {
    extern MainApplication *mainApp;

    shopInstPage::shopInstPage() : Layout::Layout() {
        if (inst::config::oledMode) {
            this->SetBackgroundColor(COLOR("#000000FF"));
        } else {
            this->SetBackgroundColor(COLOR("#670000FF"));
            if (std::filesystem::exists(inst::config::appDir + "/background.png")) this->SetBackgroundImage(inst::config::appDir + "/background.png");
            else this->SetBackgroundImage("romfs:/images/background.jpg");
        }
        const auto topColor = inst::config::oledMode ? COLOR("#000000FF") : COLOR("#170909FF");
        const auto infoColor = inst::config::oledMode ? COLOR("#000000FF") : COLOR("#17090980");
        const auto botColor = inst::config::oledMode ? COLOR("#000000FF") : COLOR("#17090980");
        this->topRect = Rectangle::New(0, 0, 1280, 94, topColor);
        this->infoRect = Rectangle::New(0, 95, 1280, 60, infoColor);
        this->botRect = Rectangle::New(0, 660, 1280, 60, botColor);
        if (inst::config::gayMode) {
            this->titleImage = Image::New(-113, 0, "romfs:/images/logo.png");
            this->appVersionText = TextBlock::New(367, 49, "v" + inst::config::appVersion, 22);
        }
        else {
            this->titleImage = Image::New(0, 0, "romfs:/images/logo.png");
            this->appVersionText = TextBlock::New(480, 49, "v" + inst::config::appVersion, 22);
        }
        this->appVersionText->SetColor(COLOR("#FFFFFFFF"));
        this->timeText = TextBlock::New(0, 18, "--:--", 22);
        this->timeText->SetColor(COLOR("#FFFFFFFF"));
        this->ipText = TextBlock::New(0, 26, "IP: --", 16);
        this->ipText->SetColor(COLOR("#FFFFFFFF"));
        this->sysLabelText = TextBlock::New(0, 6, "System Memory", 16);
        this->sysLabelText->SetColor(COLOR("#FFFFFFFF"));
        this->sysFreeText = TextBlock::New(0, 42, "Free --", 16);
        this->sysFreeText->SetColor(COLOR("#FFFFFFFF"));
        this->sdLabelText = TextBlock::New(0, 6, "microSD Card", 16);
        this->sdLabelText->SetColor(COLOR("#FFFFFFFF"));
        this->sdFreeText = TextBlock::New(0, 42, "Free --", 16);
        this->sdFreeText->SetColor(COLOR("#FFFFFFFF"));
        this->sysBarBack = Rectangle::New(0, 30, 180, 6, COLOR("#FFFFFF33"));
        this->sysBarFill = Rectangle::New(0, 30, 0, 6, COLOR("#FF4D4DFF"));
        this->sdBarBack = Rectangle::New(0, 30, 180, 6, COLOR("#FFFFFF33"));
        this->sdBarFill = Rectangle::New(0, 30, 0, 6, COLOR("#FF4D4DFF"));
        this->netIndicator = Rectangle::New(0, 0, 6, 6, COLOR("#FF3B30FF"), 3);
        this->wifiBar1 = Rectangle::New(0, 0, 4, 4, COLOR("#FFFFFF55"));
        this->wifiBar2 = Rectangle::New(0, 0, 4, 7, COLOR("#FFFFFF55"));
        this->wifiBar3 = Rectangle::New(0, 0, 4, 10, COLOR("#FFFFFF55"));
        this->batteryOutline = Rectangle::New(0, 0, 24, 12, COLOR("#FFFFFF66"));
        this->batteryFill = Rectangle::New(0, 0, 0, 10, COLOR("#4CD964FF"));
        this->batteryCap = Rectangle::New(0, 0, 3, 6, COLOR("#FFFFFF66"));
        this->pageInfoText = TextBlock::New(10, 101, "", 34);
        this->pageInfoText->SetColor(COLOR("#FFFFFFFF"));
        this->butText = TextBlock::New(10, 678, "", 20);
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->setButtonsText("inst.shop.buttons_loading"_lang);
        this->menu = pu::ui::elm::Menu::New(0, 156, 1280, COLOR("#FFFFFF00"), 28, 18, 18);
        if (inst::config::oledMode) {
            this->menu->SetOnFocusColor(COLOR("#FFFFFF33"));
            this->menu->SetScrollbarColor(COLOR("#FFFFFF66"));
        } else {
            this->menu->SetOnFocusColor(COLOR("#00000033"));
            this->menu->SetScrollbarColor(COLOR("#17090980"));
        }
        this->infoImage = Image::New(453, 292, "romfs:/images/icons/eshop-connection-waiting.png");
        this->previewImage = Image::New(900, 230, "romfs:/images/icons/title-placeholder.png");
        this->previewImage->SetWidth(320);
        this->previewImage->SetHeight(320);
        auto highlightColor = inst::config::oledMode ? COLOR("#FFFFFF66") : COLOR("#FFFFFF33");
        this->gridHighlight = Rectangle::New(0, 0, kGridTileWidth + 8, kGridTileHeight + 8, highlightColor);
        this->gridHighlight->SetVisible(false);
        this->gridImages.reserve(kGridItemsPerPage);
        for (int i = 0; i < kGridItemsPerPage; i++) {
            auto img = Image::New(0, 0, "romfs:/images/icons/title-placeholder.png");
            img->SetWidth(kGridTileWidth);
            img->SetHeight(kGridTileHeight);
            img->SetVisible(false);
            this->gridImages.push_back(img);
        }
        auto selectedColor = COLOR("#34C75966");
        this->shopGridSelectHighlights.reserve(kGridItemsPerPage);
        for (int i = 0; i < kGridItemsPerPage; i++) {
            auto highlight = Rectangle::New(0, 0, kGridTileWidth + 8, kGridTileHeight + 8, selectedColor);
            highlight->SetVisible(false);
            this->shopGridSelectHighlights.push_back(highlight);
        }
        this->shopGridSelectIcons.reserve(kGridItemsPerPage);
        for (int i = 0; i < kGridItemsPerPage; i++) {
            auto icon = Image::New(0, 0, "romfs:/images/icons/title_selected.png");
            icon->SetWidth(120);
            icon->SetHeight(120);
            icon->SetVisible(false);
            this->shopGridSelectIcons.push_back(icon);
        }
        this->gridTitleText = TextBlock::New(10, 649, "", 18);
        this->gridTitleText->SetColor(COLOR("#FFFFFFFF"));
        this->gridTitleText->SetVisible(false);
        this->imageLoadingText = TextBlock::New(0, 118, "Fetching images...", 18);
        this->imageLoadingText->SetColor(COLOR("#FFFFFFFF"));
        this->imageLoadingText->SetVisible(false);
        this->debugText = TextBlock::New(10, 620, "", 18);
        this->debugText->SetColor(COLOR("#FFFFFFFF"));
        this->debugText->SetVisible(false);
        this->emptySectionText = TextBlock::New(0, 350, "", 28);
        this->emptySectionText->SetColor(COLOR("#FFFFFFFF"));
        this->emptySectionText->SetVisible(false);
        this->Add(this->topRect);
        this->Add(this->infoRect);
        this->Add(this->botRect);
        this->Add(this->titleImage);
        this->Add(this->appVersionText);
        this->Add(this->sysBarBack);
        this->Add(this->sysBarFill);
        this->Add(this->sdBarBack);
        this->Add(this->sdBarFill);
        this->Add(this->sysLabelText);
        this->Add(this->sysFreeText);
        this->Add(this->sdLabelText);
        this->Add(this->sdFreeText);
        this->Add(this->netIndicator);
        this->Add(this->wifiBar1);
        this->Add(this->wifiBar2);
        this->Add(this->wifiBar3);
        this->Add(this->batteryOutline);
        this->Add(this->batteryFill);
        this->Add(this->batteryCap);
        this->Add(this->timeText);
        this->Add(this->ipText);
        this->Add(this->butText);
        this->Add(this->pageInfoText);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
        this->Add(this->menu);
#pragma GCC diagnostic pop
        this->Add(this->infoImage);
        this->Add(this->previewImage);
        for (auto& highlight : this->shopGridSelectHighlights)
            this->Add(highlight);
        for (auto& img : this->gridImages)
            this->Add(img);
        for (auto& icon : this->shopGridSelectIcons)
            this->Add(icon);
        this->Add(this->gridHighlight);
        this->Add(this->gridTitleText);
        this->Add(this->imageLoadingText);
        this->Add(this->debugText);
        this->Add(this->emptySectionText);
    }

    bool shopInstPage::isAllSection() const {
        if (this->shopSections.empty())
            return false;
        if (this->selectedSectionIndex < 0 || this->selectedSectionIndex >= (int)this->shopSections.size())
            return false;
        return this->shopSections[this->selectedSectionIndex].id == "all";
    }

    bool shopInstPage::isInstalledSection() const {
        if (this->shopSections.empty())
            return false;
        if (this->selectedSectionIndex < 0 || this->selectedSectionIndex >= (int)this->shopSections.size())
            return false;
        return this->shopSections[this->selectedSectionIndex].id == "installed";
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
            this->pageInfoText->SetText("inst.shop.loading"_lang);
            CenterTextX(this->pageInfoText);
            return;
        }
        const auto& section = this->shopSections[this->selectedSectionIndex];
        this->pageInfoText->SetText(section.title);
        CenterTextX(this->pageInfoText);
    }

    void shopInstPage::updateButtonsText() {
        if (this->isInstalledSection())
            this->setButtonsText("inst.shop.buttons_installed"_lang);
        else
            this->setButtonsText("inst.shop.buttons_all"_lang);
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
                if (!IsBaseTitleCurrentlyInstalled(baseId))
                    continue;
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
            for (s32 i = 0; i < outCount; i++) {
                const auto titleId = records[i].application_id;
                baseInstalled[titleId] = IsBaseTitleCurrentlyInstalled(titleId);
            }
            offset += outCount;
        }

        auto isBaseInstalled = [&](const shopInstStuff::ShopItem& item, std::uint32_t& outVersion) {
            std::uint64_t baseTitleId = 0;
            if (!DeriveBaseTitleId(item, baseTitleId))
                return false;
            auto baseIt = baseInstalled.find(baseTitleId);
            if (baseIt != baseInstalled.end()) {
                if (baseIt->second) {
                    auto verIt = installedUpdateVersion.find(baseTitleId);
                    if (verIt != installedUpdateVersion.end()) {
                        outVersion = verIt->second;
                    } else {
                        tin::util::GetInstalledUpdateVersion(baseTitleId, outVersion);
                        if (outVersion == 0)
                            TryGetInstalledUpdateVersionNcm(baseTitleId, outVersion);
                        installedUpdateVersion[baseTitleId] = outVersion;
                    }
                }
                return baseIt->second;
            }
            bool installed = IsBaseTitleCurrentlyInstalled(baseTitleId);
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
                    if (item.hasTitleId && tin::util::IsTitleInstalled(item.titleId))
                        continue;
                    filtered.push_back(item);
                }
            }
            section.items = std::move(filtered);
        }

        for (auto& section : this->shopSections) {
            if (section.items.empty())
                continue;
            if (section.id == "all" || section.id == "installed")
                continue;
            if (section.id == "updates" || section.id == "dlc")
                continue;

            std::vector<shopInstStuff::ShopItem> filtered;
            filtered.reserve(section.items.size());
            for (const auto& item : section.items) {
                if (item.appType != NcmContentMetaType_AddOnContent) {
                    filtered.push_back(item);
                    continue;
                }
                std::uint32_t installedVersion = 0;
                if (item.hasTitleId && tin::util::IsTitleInstalled(item.titleId))
                    continue;
                if (isBaseInstalled(item, installedVersion))
                    filtered.push_back(item);
            }
            section.items = std::move(filtered);
        }

        if (inst::config::shopHideInstalled) {
            for (auto& section : this->shopSections) {
                if (section.items.empty())
                    continue;
                if (section.id == "all" || section.id == "installed" || section.id == "updates")
                    continue;

                std::vector<shopInstStuff::ShopItem> filtered;
                filtered.reserve(section.items.size());
                for (const auto& item : section.items) {
                    std::uint32_t installedVersion = 0;
                    if (!IsBaseItem(item) || !item.hasTitleId || !isBaseInstalled(item, installedVersion)) {
                        filtered.push_back(item);
                    }
                }
                section.items = std::move(filtered);
            }
        }

        auto hasSuffix = [](const std::string& text, const std::string& suffix) {
            if (text.size() < suffix.size())
                return false;
            return text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        };

        auto appendTypeLabels = [&](shopInstStuff::ShopSection& section) {
            static const std::string kUpdateSuffix = " (Update)";
            static const std::string kDlcSuffix = " (DLC)";
            for (auto& item : section.items) {
                if (item.appType == NcmContentMetaType_Patch) {
                    if (!hasSuffix(item.name, kUpdateSuffix))
                        item.name += kUpdateSuffix;
                } else if (item.appType == NcmContentMetaType_AddOnContent) {
                    if (!hasSuffix(item.name, kDlcSuffix))
                        item.name += kDlcSuffix;
                }
            }
        };

        for (auto& section : this->shopSections) {
            if (section.items.empty())
                continue;
            appendTypeLabels(section);
        }

        ncmExit();
        nsExit();
    }

    void shopInstPage::updatePreview() {
        if (this->shopGridMode) {
            this->previewImage->SetVisible(false);
            this->previewKey.clear();
            this->imageLoadingText->SetVisible(false);
            return;
        }
        if (this->visibleItems.empty()) {
            this->previewImage->SetVisible(false);
            this->previewKey.clear();
            this->imageLoadingText->SetVisible(false);
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

        bool didDownload = false;
        bool loadingShown = false;
        auto applyPreviewLayout = [&]() {
            this->previewImage->SetX(900);
            this->previewImage->SetY(230);
            this->previewImage->SetWidth(320);
            this->previewImage->SetHeight(320);
        };
        auto updateLoadingText = [&]() {
            if (didDownload) {
                const u64 now = armGetSystemTick();
                const u64 freq = armGetSystemTickFreq();
                this->imageLoadingUntilTick = now + (freq * 2);
            }
            if (this->imageLoadingUntilTick > 0) {
                const u64 now = armGetSystemTick();
                bool show = now < this->imageLoadingUntilTick;
                this->imageLoadingText->SetVisible(show);
                if (show) {
                    this->imageLoadingText->SetX(1280 - this->imageLoadingText->GetTextWidth() - 10);
                }
            }
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
            this->previewImage->SetImage("romfs:/images/icons/title-placeholder.png");
            applyPreviewLayout();
            this->previewImage->SetVisible(true);
            updateLoadingText();
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
                if (!loadingShown) {
                    this->imageLoadingText->SetText("Fetching images 0/1");
                    this->imageLoadingText->SetVisible(true);
                    this->imageLoadingText->SetX(1280 - this->imageLoadingText->GetTextWidth() - 10);
                    mainApp->CallForRender();
                    loadingShown = true;
                }
                didDownload = true;
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
                if (loadingShown) {
                    this->imageLoadingText->SetText("Fetching images 1/1");
                    this->imageLoadingText->SetX(1280 - this->imageLoadingText->GetTextWidth() - 10);
                    mainApp->CallForRender();
                }
                updateLoadingText();
                return;
            }

        }

        this->previewImage->SetImage("romfs:/images/icons/title-placeholder.png");
        applyPreviewLayout();
        this->previewImage->SetVisible(true);
        updateLoadingText();
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

        int selectedIndex = this->shopGridMode ? this->shopGridIndex : this->menu->GetSelectedIndex();
        if (this->isInstalledSection() && this->shopGridMode)
            selectedIndex = this->gridSelectedIndex;
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
        this->emptySectionText->SetVisible(false);
        this->menu->ClearItems();
        this->visibleItems.clear();
        const auto& items = this->getCurrentItems();
        if (!this->searchQuery.empty()) {
            const std::string normalizedQuery = NormalizeSearchKey(this->searchQuery);
            for (const auto& item : items) {
                std::string name = NormalizeSearchKey(item.name);
                if (name.find(normalizedQuery) != std::string::npos)
                    this->visibleItems.push_back(item);
            }
        } else {
            this->visibleItems = items;
        }

        if (!this->shopSections.empty() && this->selectedSectionIndex >= 0 && this->selectedSectionIndex < static_cast<int>(this->shopSections.size()) && this->visibleItems.empty()) {
            const auto &section = this->shopSections[this->selectedSectionIndex];
            if (section.id == "updates" || section.id == "dlc") {
                this->emptySectionText->SetText(section.id == "updates" ? "No updates available." : "No DLC available.");
                CenterTextX(this->emptySectionText);
                this->emptySectionText->SetY(350);
                this->emptySectionText->SetVisible(true);
            }
        }

        if (this->isInstalledSection() && this->shopGridMode) {
            this->menu->SetVisible(false);
            this->previewImage->SetVisible(false);
            this->emptySectionText->SetVisible(false);
            if (this->gridSelectedIndex >= (int)this->visibleItems.size())
                this->gridSelectedIndex = 0;
            this->updateInstalledGrid();
            return;
        }

        if (this->shopGridMode) {
            this->updateShopGrid();
            return;
        }

        for (auto& img : this->gridImages)
            img->SetVisible(false);
        this->gridHighlight->SetVisible(false);
        this->gridTitleText->SetVisible(false);
        for (auto& icon : this->shopGridSelectIcons)
            icon->SetVisible(false);
        this->menu->SetVisible(true);
        auto formatSize = [](std::uint64_t bytes) {
            if (bytes == 0)
                return std::string();
            const double kb = 1024.0;
            const double mb = kb * 1024.0;
            const double gb = mb * 1024.0;
            char buf[32] = {0};
            if (bytes >= static_cast<std::uint64_t>(gb)) {
                std::snprintf(buf, sizeof(buf), "%.1f GB", bytes / gb);
            } else {
                std::snprintf(buf, sizeof(buf), "%.0f MB", bytes / mb);
            }
            return std::string(buf);
        };

        const bool installedSection = this->isInstalledSection();
        for (const auto& item : this->visibleItems) {
            std::string sizeText = formatSize(item.size);
            std::string suffix = sizeText.empty() ? "" : (" [" + sizeText + "]");
            int nameLimit = 56;
            if (!suffix.empty()) {
                int maxSuffix = static_cast<int>(suffix.size()) + 1;
                if (nameLimit > maxSuffix)
                    nameLimit -= maxSuffix;
            }
            if (nameLimit < 8)
                nameLimit = 8;
            std::string itm = inst::util::shortenString(item.name, nameLimit, true) + suffix;
            auto entry = pu::ui::elm::MenuItem::New(itm);
            entry->SetColor(COLOR("#FFFFFFFF"));
            if (!installedSection) {
                entry->SetIcon("romfs:/images/icons/checkbox-blank-outline.png");
                for (const auto& selected : this->selectedItems) {
                    if (selected.url == item.url) {
                        entry->SetIcon("romfs:/images/icons/check-box-outline.png");
                        break;
                    }
                }
            }
            this->menu->AddItem(entry);
        }

        if (!this->menu->GetItems().empty()) {
            int sel = this->menu->GetSelectedIndex();
            if (sel < 0 || sel >= (int)this->menu->GetItems().size())
                this->menu->SetSelectedIndex(0);
        }
    }

    void shopInstPage::updateInstalledGrid() {
        if (!this->isInstalledSection() || !this->shopGridMode) {
            for (auto& img : this->gridImages)
                img->SetVisible(false);
            this->gridHighlight->SetVisible(false);
            this->gridTitleText->SetVisible(false);
            this->gridPage = -1;
            return;
        }

        this->menu->SetVisible(false);
        this->previewImage->SetVisible(false);
        this->emptySectionText->SetVisible(false);

        if (this->visibleItems.empty()) {
            for (auto& img : this->gridImages)
                img->SetVisible(false);
            this->gridHighlight->SetVisible(false);
            this->gridTitleText->SetVisible(false);
            this->gridPage = -1;
            return;
        }

        if (this->gridSelectedIndex < 0)
            this->gridSelectedIndex = 0;
        if (this->gridSelectedIndex >= (int)this->visibleItems.size())
            this->gridSelectedIndex = (int)this->visibleItems.size() - 1;

        int page = this->gridSelectedIndex / kGridItemsPerPage;
        int pageStart = page * kGridItemsPerPage;
        int maxIndex = (int)this->visibleItems.size();

        if (page != this->gridPage) {
            bool nsReady = R_SUCCEEDED(nsInitialize());
            for (int i = 0; i < kGridItemsPerPage; i++) {
                int itemIndex = pageStart + i;
                int row = i / kGridCols;
                int col = i % kGridCols;
                int x = kGridStartX + (col * (kGridTileWidth + kGridGap));
                int y = kGridStartY + (row * (kGridTileHeight + kGridGap));
                this->gridImages[i]->SetX(x);
                this->gridImages[i]->SetY(y);

                if (itemIndex >= maxIndex) {
                    this->gridImages[i]->SetVisible(false);
                    continue;
                }

                const auto& item = this->visibleItems[itemIndex];
                bool applied = false;
                if (nsReady && item.hasTitleId) {
                    u64 baseId = tin::util::GetBaseTitleId(item.titleId, static_cast<NcmContentMetaType>(item.appType));
                    NsApplicationControlData appControlData;
                    u64 sizeRead = 0;
                    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, baseId, &appControlData, sizeof(NsApplicationControlData), &sizeRead))) {
                        u64 iconSize = 0;
                        if (sizeRead > sizeof(appControlData.nacp))
                            iconSize = sizeRead - sizeof(appControlData.nacp);
                        if (iconSize > 0) {
                            this->gridImages[i]->SetJpegImage(appControlData.icon, iconSize);
                            this->gridImages[i]->SetWidth(kGridTileWidth);
                            this->gridImages[i]->SetHeight(kGridTileHeight);
                            applied = true;
                        }
                    }
                }

                if (!applied) {
                    this->gridImages[i]->SetImage("romfs:/images/icons/title-placeholder.png");
                    this->gridImages[i]->SetWidth(kGridTileWidth);
                    this->gridImages[i]->SetHeight(kGridTileHeight);
                }

                this->gridImages[i]->SetVisible(true);
            }
            if (nsReady)
                nsExit();
            this->gridPage = page;
        }

        int slot = this->gridSelectedIndex - pageStart;
        if (slot >= 0 && slot < kGridItemsPerPage) {
            int row = slot / kGridCols;
            int col = slot % kGridCols;
            int x = kGridStartX + (col * (kGridTileWidth + kGridGap)) - 4;
            int y = kGridStartY + (row * (kGridTileHeight + kGridGap)) - 4;
            this->gridHighlight->SetX(x);
            this->gridHighlight->SetY(y);
            this->gridHighlight->SetWidth(kGridTileWidth + 8);
            this->gridHighlight->SetHeight(kGridTileHeight + 8);
            this->gridHighlight->SetVisible(true);
        } else {
            this->gridHighlight->SetVisible(false);
        }

        if (this->gridSelectedIndex >= 0 && this->gridSelectedIndex < (int)this->visibleItems.size()) {
            std::string title = BuildGridTitleWithSize(this->visibleItems[this->gridSelectedIndex]);
            this->gridTitleText->SetText(title);
            this->gridTitleText->SetVisible(true);
        } else {
            this->gridTitleText->SetVisible(false);
        }
    }

    void shopInstPage::updateShopGrid() {
        if (!this->shopGridMode || this->visibleItems.empty()) {
            for (auto& img : this->gridImages)
                img->SetVisible(false);
            this->gridHighlight->SetVisible(false);
            this->gridTitleText->SetVisible(false);
            for (auto& highlight : this->shopGridSelectHighlights)
                highlight->SetVisible(false);
            for (auto& icon : this->shopGridSelectIcons)
                icon->SetVisible(false);
            this->imageLoadingText->SetVisible(false);
            this->shopGridPage = -1;
            return;
        }

        this->menu->SetVisible(false);
        this->previewImage->SetVisible(false);

        if (this->shopGridIndex < 0)
            this->shopGridIndex = 0;
        if (this->shopGridIndex >= (int)this->visibleItems.size())
            this->shopGridIndex = (int)this->visibleItems.size() - 1;

        int page = this->shopGridIndex / kGridItemsPerPage;
        int pageStart = page * kGridItemsPerPage;
        int maxIndex = (int)this->visibleItems.size();

        bool didDownload = false;
        if (page != this->shopGridPage) {
            std::string cacheDir = inst::config::appDir + "/shop_icons";
            if (!std::filesystem::exists(cacheDir))
                std::filesystem::create_directory(cacheDir);
            int totalToDownload = 0;
            for (int i = 0; i < kGridItemsPerPage; i++) {
                int itemIndex = pageStart + i;
                if (itemIndex >= maxIndex)
                    continue;
                const auto& item = this->visibleItems[itemIndex];
                if (!item.hasIconUrl)
                    continue;
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
                if (!std::filesystem::exists(filePath))
                    totalToDownload++;
            }

            bool loadingShown = false;
            int downloadedCount = 0;
            if (totalToDownload > 0) {
                this->imageLoadingText->SetText("Fetching images 0/" + std::to_string(totalToDownload));
                this->imageLoadingText->SetVisible(true);
                this->imageLoadingText->SetX(1280 - this->imageLoadingText->GetTextWidth() - 10);
                mainApp->CallForRender();
                loadingShown = true;
            }

            for (int i = 0; i < kGridItemsPerPage; i++) {
                int itemIndex = pageStart + i;
                int row = i / kGridCols;
                int col = i % kGridCols;
                int x = kGridStartX + (col * (kGridTileWidth + kGridGap));
                int y = kGridStartY + (row * (kGridTileHeight + kGridGap));
                this->gridImages[i]->SetX(x);
                this->gridImages[i]->SetY(y);
                this->gridImages[i]->SetWidth(kGridTileWidth);
                this->gridImages[i]->SetHeight(kGridTileHeight);
                if (itemIndex >= maxIndex) {
                    this->gridImages[i]->SetVisible(false);
                    continue;
                }

                const auto& item = this->visibleItems[itemIndex];
                bool applied = false;
                if (item.hasIconUrl) {
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
                        didDownload = true;
                        bool ok = inst::curl::downloadImageWithAuth(item.iconUrl, filePath.c_str(), inst::config::shopUser, inst::config::shopPass, 8000);
                        if (!ok && std::filesystem::exists(filePath))
                            std::filesystem::remove(filePath);
                        if (loadingShown) {
                            downloadedCount++;
                            this->imageLoadingText->SetText("Fetching images " + std::to_string(downloadedCount) + "/" + std::to_string(totalToDownload));
                            this->imageLoadingText->SetX(1280 - this->imageLoadingText->GetTextWidth() - 10);
                            mainApp->CallForRender();
                        }
                    }

                if (std::filesystem::exists(filePath)) {
                    this->gridImages[i]->SetImage(filePath);
                    this->gridImages[i]->SetWidth(kGridTileWidth);
                    this->gridImages[i]->SetHeight(kGridTileHeight);
                    applied = true;
                }
            }

            if (!applied) {
                this->gridImages[i]->SetImage("romfs:/images/icons/title-placeholder.png");
                this->gridImages[i]->SetWidth(kGridTileWidth);
                this->gridImages[i]->SetHeight(kGridTileHeight);
            }
            this->gridImages[i]->SetVisible(true);

        }
            this->shopGridPage = page;
        }

        if (didDownload) {
            const u64 now = armGetSystemTick();
            const u64 freq = armGetSystemTickFreq();
            this->imageLoadingUntilTick = now + (freq * 2);
        }
        if (this->imageLoadingUntilTick > 0) {
            const u64 now = armGetSystemTick();
            bool show = now < this->imageLoadingUntilTick;
            this->imageLoadingText->SetVisible(show);
            if (show) {
                this->imageLoadingText->SetX(1280 - this->imageLoadingText->GetTextWidth() - 10);
            }
        }

        for (int i = 0; i < kGridItemsPerPage; i++) {
            int itemIndex = pageStart + i;
            int row = i / kGridCols;
            int col = i % kGridCols;
            int x = kGridStartX + (col * (kGridTileWidth + kGridGap));
            int y = kGridStartY + (row * (kGridTileHeight + kGridGap));

            if (itemIndex >= maxIndex) {
                this->shopGridSelectHighlights[i]->SetVisible(false);
                this->shopGridSelectIcons[i]->SetVisible(false);
                continue;
            }

            const auto& item = this->visibleItems[itemIndex];
            bool isSelected = false;
            if (!this->selectedItems.empty() && !item.url.empty()) {
                isSelected = std::any_of(this->selectedItems.begin(), this->selectedItems.end(), [&](const auto& entry) {
                    return entry.url == item.url;
                });
            }
            const int iconSize = 120;
            this->shopGridSelectIcons[i]->SetX(x + (kGridTileWidth - iconSize) / 2);
            this->shopGridSelectIcons[i]->SetY(y + (kGridTileHeight - iconSize) / 2);
            this->shopGridSelectIcons[i]->SetVisible(isSelected);
            this->shopGridSelectHighlights[i]->SetX(x - 4);
            this->shopGridSelectHighlights[i]->SetY(y - 4);
            this->shopGridSelectHighlights[i]->SetWidth(kGridTileWidth + 8);
            this->shopGridSelectHighlights[i]->SetHeight(kGridTileHeight + 8);
            this->shopGridSelectHighlights[i]->SetVisible(isSelected);
        }

        int slot = this->shopGridIndex - pageStart;
        if (slot >= 0 && slot < kGridItemsPerPage) {
            int row = slot / kGridCols;
            int col = slot % kGridCols;
            int x = kGridStartX + (col * (kGridTileWidth + kGridGap)) - 4;
            int y = kGridStartY + (row * (kGridTileHeight + kGridGap)) - 4;
            this->gridHighlight->SetX(x);
            this->gridHighlight->SetY(y);
            this->gridHighlight->SetWidth(kGridTileWidth + 8);
            this->gridHighlight->SetHeight(kGridTileHeight + 8);
            this->gridHighlight->SetVisible(true);
        } else {
            this->gridHighlight->SetVisible(false);
        }

        if (this->shopGridIndex >= 0 && this->shopGridIndex < (int)this->visibleItems.size()) {
            std::string title = BuildGridTitleWithSize(this->visibleItems[this->shopGridIndex]);
            this->gridTitleText->SetText(title);
            this->gridTitleText->SetVisible(true);
        } else {
            this->gridTitleText->SetVisible(false);
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
        bool wasSelected = (selected != this->selectedItems.end());
        if (wasSelected)
            this->selectedItems.erase(selected);
        else
            this->selectedItems.push_back(item);
        if (wasSelected && IsBaseItem(item)) {
            std::uint64_t baseTitleId = 0;
            if (DeriveBaseTitleId(item, baseTitleId)) {
                this->selectedItems.erase(std::remove_if(this->selectedItems.begin(), this->selectedItems.end(), [&](const auto& entry) {
                    if (entry.appType != NcmContentMetaType_Patch)
                        return false;
                    std::uint64_t updateBaseId = 0;
                    if (!DeriveBaseTitleId(entry, updateBaseId))
                        return false;
                    return updateBaseId == baseTitleId;
                }), this->selectedItems.end());
            }
        }
        this->updateRememberedSelection();
        this->drawMenuItems(false);
    }

    void shopInstPage::updateRememberedSelection() {
    }

    void shopInstPage::startShop(bool forceRefresh) {
        this->setButtonsText("inst.shop.buttons_loading"_lang);
        this->menu->SetVisible(false);
        this->menu->ClearItems();
        this->infoImage->SetVisible(true);
        this->previewImage->SetVisible(false);
        this->emptySectionText->SetVisible(false);
        this->imageLoadingText->SetVisible(false);
        this->gridHighlight->SetVisible(false);
        this->gridTitleText->SetVisible(false);
        for (auto& img : this->gridImages)
            img->SetVisible(false);
        for (auto& highlight : this->shopGridSelectHighlights)
            highlight->SetVisible(false);
        for (auto& icon : this->shopGridSelectIcons)
            icon->SetVisible(false);
        this->selectedItems.clear();
        this->visibleItems.clear();
        this->shopSections.clear();
        this->availableUpdates.clear();
        this->searchQuery.clear();
        this->previewKey.clear();
        this->pageInfoText->SetText("inst.shop.loading"_lang);
        CenterTextX(this->pageInfoText);
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
        this->shopSections = shopInstStuff::FetchShopSections(shopUrl, inst::config::shopUser, inst::config::shopPass, error, !forceRefresh);
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

        std::string motd = shopInstStuff::FetchShopMotd(shopUrl, inst::config::shopUser, inst::config::shopPass);
        if (!motd.empty())
            mainApp->CreateShowDialog("inst.shop.motd_title"_lang, motd, {"common.ok"_lang}, true);

        if (!inst::config::shopHideInstalledSection)
            this->buildInstalledSection();
        this->cacheAvailableUpdates();
        this->filterOwnedSections();

        this->selectedSectionIndex = 0;
        for (size_t i = 0; i < this->shopSections.size(); i++) {
            if (this->shopSections[i].id == "new") {
                this->selectedSectionIndex = static_cast<int>(i);
                break;
            }
        }
        this->shopGridMode = inst::config::shopStartGridMode;
        this->shopGridIndex = 0;
        this->shopGridPage = -1;
        this->gridSelectedIndex = 0;
        this->gridPage = -1;
        this->updateSectionText();
        this->updateButtonsText();
        this->selectedItems.clear();
        this->drawMenuItems(false);
        this->infoImage->SetVisible(false);
        if (!this->shopGridMode) {
            this->menu->SetSelectedIndex(0);
            this->menu->SetVisible(true);
            this->updatePreview();
        }
    }

    void shopInstPage::startInstall() {
        std::vector<shopInstStuff::ShopItem> autoAddedItems;
        if (!this->selectedItems.empty()) {
            auto isAlreadySelected = [&](const shopInstStuff::ShopItem& candidate) {
                return std::any_of(this->selectedItems.begin(), this->selectedItems.end(), [&](const auto& entry) {
                    if (!candidate.url.empty() && !entry.url.empty())
                        return entry.url == candidate.url;
                    if (candidate.hasTitleId && entry.hasTitleId)
                        return entry.titleId == candidate.titleId;
                    if (candidate.hasAppId && entry.hasAppId)
                        return entry.appId == candidate.appId;
                    return false;
                });
            };

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
                if (!isAlreadySelected(updateIt->second) && !updateIt->second.url.empty())
                    updatesToAdd.push_back(updateIt->second);
            }

            if (!updatesToAdd.empty()) {
                int res = mainApp->CreateShowDialog("inst.shop.update_prompt_title"_lang,
                    "inst.shop.update_prompt_desc"_lang + std::to_string(updatesToAdd.size()),
                    {"common.yes"_lang, "common.no"_lang}, false);
                if (res == 0) {
                    for (const auto& update : updatesToAdd) {
                        this->selectedItems.push_back(update);
                        autoAddedItems.push_back(update);
                    }
                }
            }

            std::unordered_map<std::uint64_t, bool> selectedBaseTitleIds;
            for (const auto& item : this->selectedItems) {
                if (!IsBaseItem(item))
                    continue;
                std::uint64_t baseTitleId = 0;
                if (!DeriveBaseTitleId(item, baseTitleId))
                    continue;
                selectedBaseTitleIds[baseTitleId] = true;
            }

            if (!selectedBaseTitleIds.empty()) {
                std::vector<shopInstStuff::ShopItem> dlcToAdd;
                std::unordered_map<std::string, bool> seenDlcKeys;

                for (const auto& section : this->shopSections) {
                    for (const auto& item : section.items) {
                        if (item.appType != NcmContentMetaType_AddOnContent)
                            continue;
                        if (item.url.empty())
                            continue;
                        std::uint64_t baseTitleId = 0;
                        if (!DeriveBaseTitleId(item, baseTitleId))
                            continue;
                        if (selectedBaseTitleIds.find(baseTitleId) == selectedBaseTitleIds.end())
                            continue;
                        if (isAlreadySelected(item))
                            continue;

                        std::string dedupeKey = item.url;
                        if (dedupeKey.empty() && item.hasTitleId)
                            dedupeKey = std::to_string(item.titleId);
                        if (dedupeKey.empty() && item.hasAppId)
                            dedupeKey = item.appId;
                        if (dedupeKey.empty())
                            continue;
                        if (seenDlcKeys.find(dedupeKey) != seenDlcKeys.end())
                            continue;
                        seenDlcKeys[dedupeKey] = true;
                        dlcToAdd.push_back(item);
                    }
                }

                if (!dlcToAdd.empty()) {
                    int res = mainApp->CreateShowDialog("inst.shop.dlc_prompt_title"_lang,
                        "inst.shop.dlc_prompt_desc"_lang + std::to_string(dlcToAdd.size()),
                        {"common.yes"_lang, "common.no"_lang}, false);
                    if (res == 0) {
                        for (const auto& dlc : dlcToAdd) {
                            this->selectedItems.push_back(dlc);
                            autoAddedItems.push_back(dlc);
                        }
                    }
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
        if (dialogResult == -1) {
            if (!autoAddedItems.empty()) {
                auto matchesItem = [](const shopInstStuff::ShopItem& lhs, const shopInstStuff::ShopItem& rhs) {
                    if (!lhs.url.empty() && !rhs.url.empty())
                        return lhs.url == rhs.url;
                    if (lhs.hasTitleId && rhs.hasTitleId)
                        return lhs.titleId == rhs.titleId;
                    if (lhs.hasAppId && rhs.hasAppId)
                        return lhs.appId == rhs.appId;
                    return false;
                };

                for (const auto& autoItem : autoAddedItems) {
                    auto it = std::find_if(this->selectedItems.begin(), this->selectedItems.end(), [&](const auto& selected) {
                        return matchesItem(selected, autoItem);
                    });
                    if (it != this->selectedItems.end())
                        this->selectedItems.erase(it);
                }

                if (this->shopGridMode)
                    this->updateShopGrid();
                else
                    this->drawMenuItems(false);
            }
            return;
        }

        this->updateRememberedSelection();
        shopInstStuff::installTitleShop(this->selectedItems, dialogResult, "inst.shop.source_string"_lang);
        this->startShop(true);
    }

    void shopInstPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos) {
        int bottomTapX = 0;
        if (DetectBottomHintTap(Pos, this->bottomHintTouch, 668, 52, bottomTapX)) {
            Down |= FindBottomHintButton(this->bottomHintSegments, bottomTapX);
        }
        if (Down & HidNpadButton_B) {
            this->updateRememberedSelection();
            mainApp->LoadLayout(mainApp->mainPage);
        }
        if (Down & HidNpadButton_Minus) {
            this->shopGridMode = !this->shopGridMode;
            this->touchActive = false;
            this->touchMoved = false;
            if (this->shopGridMode) {
                this->shopGridIndex = this->menu->GetSelectedIndex();
                if (this->shopGridIndex < 0)
                    this->shopGridIndex = 0;
                this->shopGridPage = -1;
                this->gridPage = -1;
                if (this->isInstalledSection()) {
                    this->gridSelectedIndex = this->shopGridIndex;
                    this->updateInstalledGrid();
                } else {
                    this->updateShopGrid();
                }
            } else {
                if (!this->menu->GetItems().empty()) {
                    int sel = this->shopGridIndex;
                    if (sel < 0 || sel >= (int)this->menu->GetItems().size())
                        sel = 0;
                    this->menu->SetSelectedIndex(sel);
                }
                this->updateSectionText();
                this->updateButtonsText();
                this->drawMenuItems(false);
                this->updatePreview();
            }
            return;
        }
        if (this->shopGridMode) {
            if (Down & HidNpadButton_Plus) {
                if (!this->isInstalledSection() && !this->visibleItems.empty() && this->selectedItems.empty()) {
                    this->selectTitle(this->shopGridIndex);
                }
                if (!this->isInstalledSection() && !this->selectedItems.empty())
                    this->startInstall();
            }
            if (Down & HidNpadButton_X) {
                this->startShop(true);
            }
            if (Down & HidNpadButton_L) {
                if (this->shopSections.size() > 1) {
                    this->selectedSectionIndex = (this->selectedSectionIndex - 1 + (int)this->shopSections.size()) % (int)this->shopSections.size();
                    this->searchQuery.clear();
                    this->shopGridIndex = 0;
                    this->shopGridPage = -1;
                    this->gridSelectedIndex = 0;
                    this->gridPage = -1;
                    this->updateSectionText();
                    this->updateButtonsText();
                    this->drawMenuItems(false);
                }
            }
            if (Down & HidNpadButton_R) {
                if (this->shopSections.size() > 1) {
                    this->selectedSectionIndex = (this->selectedSectionIndex + 1) % (int)this->shopSections.size();
                    this->searchQuery.clear();
                    this->shopGridIndex = 0;
                    this->shopGridPage = -1;
                    this->gridSelectedIndex = 0;
                    this->gridPage = -1;
                    this->updateSectionText();
                    this->updateButtonsText();
                    this->drawMenuItems(false);
                }
            }
            if (Down & HidNpadButton_ZR) {
                std::string query = inst::util::softwareKeyboard("inst.shop.search_hint"_lang, this->searchQuery, 60);
                this->searchQuery = query;
                this->updateSectionText();
                this->drawMenuItems(false);
            }
            u64 dirKeys = Down & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right);
            if (dirKeys == 0) {
                const bool stickUp = (Held & HidNpadButton_StickLUp) != 0;
                const bool stickDown = (Held & HidNpadButton_StickLDown) != 0;
                const bool stickLeft = (Held & HidNpadButton_StickLLeft) != 0;
                const bool stickRight = (Held & HidNpadButton_StickLRight) != 0;
                if (!stickUp && !stickDown && !stickLeft && !stickRight) {
                    this->gridHoldDirX = 0;
                    this->gridHoldDirY = 0;
                    this->gridHoldStartTick = 0;
                    this->gridHoldLastTick = 0;
                } else {
                    int dirX = stickRight ? 1 : (stickLeft ? -1 : 0);
                    int dirY = stickDown ? 1 : (stickUp ? -1 : 0);
                    u64 now = armGetSystemTick();
                    if (this->gridHoldDirX != dirX || this->gridHoldDirY != dirY || this->gridHoldStartTick == 0) {
                        this->gridHoldDirX = dirX;
                        this->gridHoldDirY = dirY;
                        this->gridHoldStartTick = now;
                        this->gridHoldLastTick = now;
                        if (dirY != 0)
                            dirKeys |= (dirY > 0) ? HidNpadButton_Down : HidNpadButton_Up;
                        else if (dirX != 0)
                            dirKeys |= (dirX > 0) ? HidNpadButton_Right : HidNpadButton_Left;
                    } else {
                        const u64 freq = armGetSystemTickFreq();
                        const u64 delayTicks = (freq * 200) / 1000;
                        const u64 repeatTicks = (freq * 90) / 1000;
                        if (now - this->gridHoldStartTick >= delayTicks && now - this->gridHoldLastTick >= repeatTicks) {
                            if (dirY != 0)
                                dirKeys |= (dirY > 0) ? HidNpadButton_Down : HidNpadButton_Up;
                            else if (dirX != 0)
                                dirKeys |= (dirX > 0) ? HidNpadButton_Right : HidNpadButton_Left;
                            this->gridHoldLastTick = now;
                        }
                    }
                }
            }
            if ((Down & HidNpadButton_A) || (Up & TouchPseudoKey)) {
                if (!this->visibleItems.empty()) {
                    if (this->shopGridIndex < 0)
                        this->shopGridIndex = 0;
                    if (this->shopGridIndex >= (int)this->visibleItems.size())
                        this->shopGridIndex = (int)this->visibleItems.size() - 1;
                    if (this->isInstalledSection()) {
                        this->gridSelectedIndex = this->shopGridIndex;
                        this->showInstalledDetails();
                    } else {
                        this->selectTitle(this->shopGridIndex);
                        if (this->visibleItems.size() == 1 && this->selectedItems.size() == 1) {
                            this->startInstall();
                        }
                    }
                }
            }
            if (!this->visibleItems.empty()) {
                int newIndex = this->shopGridIndex;
                if (dirKeys & HidNpadButton_Up)
                    newIndex -= kGridCols;
                if (dirKeys & HidNpadButton_Down)
                    newIndex += kGridCols;
                if (dirKeys & HidNpadButton_Left)
                    newIndex -= 1;
                if (dirKeys & HidNpadButton_Right)
                    newIndex += 1;

                if (newIndex < 0)
                    newIndex = 0;
                if (newIndex >= (int)this->visibleItems.size())
                    newIndex = (int)this->visibleItems.size() - 1;

                if (newIndex != this->shopGridIndex) {
                    this->shopGridIndex = newIndex;
                    if (this->isInstalledSection()) {
                        this->gridSelectedIndex = this->shopGridIndex;
                        this->updateInstalledGrid();
                    } else {
                        this->updateShopGrid();
                    }
                }
            }
            if (!Pos.IsEmpty()) {
                const int gridW = kGridCols * kGridTileWidth + (kGridCols - 1) * kGridGap;
                const int gridH = kGridRows * kGridTileHeight + (kGridRows - 1) * kGridGap;
                const bool inGrid = (Pos.X >= kGridStartX) && (Pos.X <= (kGridStartX + gridW)) && (Pos.Y >= kGridStartY) && (Pos.Y <= (kGridStartY + gridH));
                if (inGrid) {
                    const int relX = Pos.X - kGridStartX;
                    const int relY = Pos.Y - kGridStartY;
                    const int col = relX / (kGridTileWidth + kGridGap);
                    const int row = relY / (kGridTileHeight + kGridGap);
                    const int tileX = relX - (col * (kGridTileWidth + kGridGap));
                    const int tileY = relY - (row * (kGridTileHeight + kGridGap));
                    if (col >= 0 && col < kGridCols && row >= 0 && row < kGridRows && tileX <= kGridTileWidth && tileY <= kGridTileHeight) {
                        int page = 0;
                        if (this->shopGridIndex > 0)
                            page = this->shopGridIndex / kGridItemsPerPage;
                        int pageStart = page * kGridItemsPerPage;
                        int index = pageStart + (row * kGridCols) + col;
                        if (index >= 0 && index < (int)this->visibleItems.size() && index != this->shopGridIndex) {
                            this->shopGridIndex = index;
                            if (this->isInstalledSection()) {
                                this->gridSelectedIndex = this->shopGridIndex;
                                this->updateInstalledGrid();
                            } else {
                                this->updateShopGrid();
                            }
                        }
                        this->touchActive = true;
                        this->touchMoved = false;
                    }
                }
            } else if (this->touchActive) {
                if (!this->visibleItems.empty()) {
                    if (this->isInstalledSection()) {
                        this->gridSelectedIndex = this->shopGridIndex;
                        this->showInstalledDetails();
                    } else {
                        this->selectTitle(this->shopGridIndex);
                        if (this->visibleItems.size() == 1 && this->selectedItems.size() == 1) {
                            this->startInstall();
                        }
                    }
                }
                this->touchActive = false;
                this->touchMoved = false;
            }
            return;
        }
        if ((Down & HidNpadButton_A) || (Up & TouchPseudoKey)) {
            if (this->isInstalledSection()) {
                this->showInstalledDetails();
            } else {
                this->selectTitle(this->menu->GetSelectedIndex());
                if (this->menu->GetItems().size() == 1 && this->selectedItems.size() == 1) {
                    this->startInstall();
                }
            }
        }
        if (Down & HidNpadButton_L) {
            if (this->shopSections.size() > 1) {
                this->selectedSectionIndex = (this->selectedSectionIndex - 1 + (int)this->shopSections.size()) % (int)this->shopSections.size();
                this->searchQuery.clear();
                this->gridSelectedIndex = 0;
                this->gridPage = -1;
                this->shopGridIndex = 0;
                this->shopGridPage = -1;
                this->updateSectionText();
                this->updateButtonsText();
                this->drawMenuItems(false);
            }
        }
        if (Down & HidNpadButton_R) {
            if (this->shopSections.size() > 1) {
                this->selectedSectionIndex = (this->selectedSectionIndex + 1) % (int)this->shopSections.size();
                this->searchQuery.clear();
                this->gridSelectedIndex = 0;
                this->gridPage = -1;
                this->shopGridIndex = 0;
                this->shopGridPage = -1;
                this->updateSectionText();
                this->updateButtonsText();
                this->drawMenuItems(false);
            }
        }
        if (Down & HidNpadButton_ZR) {
            std::string query = inst::util::softwareKeyboard("inst.shop.search_hint"_lang, this->searchQuery, 60);
            this->searchQuery = query;
            this->updateSectionText();
            this->drawMenuItems(false);
        }
        if (Down & HidNpadButton_Y) {
            if (!this->isInstalledSection()) {
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
        }
        if (Down & HidNpadButton_X) {
            this->startShop(true);
        }
        if (Down & HidNpadButton_Plus) {
            if (!this->isInstalledSection()) {
                if (this->selectedItems.empty()) {
                    this->selectTitle(this->menu->GetSelectedIndex());
                }
                if (!this->selectedItems.empty()) this->startInstall();
            }
        }
        if (!this->shopGridMode && !this->menu->GetItems().empty()) {
            const u64 holdMask = HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_StickLUp | HidNpadButton_StickLDown;
            const bool heldUp = (Held & (HidNpadButton_Up | HidNpadButton_StickLUp)) != 0;
            const bool heldDown = (Held & (HidNpadButton_Down | HidNpadButton_StickLDown)) != 0;
            const bool isHolding = (Held & holdMask) != 0;
            if (!isHolding || (heldUp && heldDown)) {
                this->holdDirection = 0;
                this->holdStartTick = 0;
                this->lastHoldTick = 0;
            } else {
                int direction = heldDown ? 1 : -1;
                u64 now = armGetSystemTick();
                if (this->holdDirection != direction || this->holdStartTick == 0) {
                    this->holdDirection = direction;
                    this->holdStartTick = now;
                    this->lastHoldTick = now;
                }
                if ((int)this->menu->GetItems().size() > this->menu->GetNumberOfItemsToShow()) {
                    const u64 freq = armGetSystemTickFreq();
                    const u64 delayTicks = (freq * 300) / 1000;
                    const u64 repeatTicks = (freq * 70) / 1000;
                    if (now - this->holdStartTick >= delayTicks && now - this->lastHoldTick >= repeatTicks) {
                        int currentIndex = this->menu->GetSelectedIndex();
                        int maxIndex = static_cast<int>(this->menu->GetItems().size()) - 1;
                        if (direction > 0) {
                            if (currentIndex < maxIndex)
                                this->menu->OnInput(HidNpadButton_AnyDown, 0, 0, pu::ui::Touch::Empty);
                        } else {
                            if (currentIndex > 0)
                                this->menu->OnInput(HidNpadButton_AnyUp, 0, 0, pu::ui::Touch::Empty);
                        }
                        this->lastHoldTick = now;
                    }
                }
            }
        } else {
            this->holdDirection = 0;
            this->holdStartTick = 0;
            this->lastHoldTick = 0;
        }
        if (this->menu->IsVisible()) {
            if (!Pos.IsEmpty()) {
                const int menuX = this->menu->GetProcessedX();
                const int menuY = this->menu->GetProcessedY();
                const int menuW = this->menu->GetWidth();
                const int menuH = this->menu->GetHeight();
                const bool inMenu = (Pos.X >= menuX) && (Pos.X <= (menuX + menuW)) && (Pos.Y >= menuY) && (Pos.Y <= (menuY + menuH));
                if (!this->touchActive && inMenu) {
                    this->touchActive = true;
                    this->touchMoved = false;
                }
            } else if (this->touchActive) {
                if (!this->touchMoved && !this->menu->GetItems().empty()) {
                    if (this->isInstalledSection()) {
                        this->showInstalledDetails();
                    } else {
                        this->selectTitle(this->menu->GetSelectedIndex());
                        if (this->menu->GetItems().size() == 1 && this->selectedItems.size() == 1) {
                            this->startInstall();
                        }
                    }
                }
                this->touchActive = false;
                this->touchMoved = false;
            }
        } else {
            this->touchActive = false;
            this->touchMoved = false;
        }
        if (this->shopGridMode && this->isInstalledSection() && !this->visibleItems.empty()) {
            if (!Pos.IsEmpty()) {
                const int gridW = kGridCols * kGridTileWidth + (kGridCols - 1) * kGridGap;
                const int gridH = kGridRows * kGridTileHeight + (kGridRows - 1) * kGridGap;
                const bool inGrid = (Pos.X >= kGridStartX) && (Pos.X <= (kGridStartX + gridW)) && (Pos.Y >= kGridStartY) && (Pos.Y <= (kGridStartY + gridH));
                if (!this->touchActive) {
                    if (inGrid) {
                        this->touchActive = true;
                        this->touchMoved = false;
                    }
                } else if (inGrid) {
                    const int relX = Pos.X - kGridStartX;
                    const int relY = Pos.Y - kGridStartY;
                    const int col = relX / (kGridTileWidth + kGridGap);
                    const int row = relY / (kGridTileHeight + kGridGap);
                    const int tileX = relX - (col * (kGridTileWidth + kGridGap));
                    const int tileY = relY - (row * (kGridTileHeight + kGridGap));
                    if (col >= 0 && col < kGridCols && row >= 0 && row < kGridRows && tileX <= kGridTileWidth && tileY <= kGridTileHeight) {
                        int page = 0;
                        if (this->gridSelectedIndex > 0)
                            page = this->gridSelectedIndex / kGridItemsPerPage;
                        int pageStart = page * kGridItemsPerPage;
                        int index = pageStart + (row * kGridCols) + col;
                        if (index >= 0 && index < (int)this->visibleItems.size() && index != this->gridSelectedIndex) {
                            this->gridSelectedIndex = index;
                            this->updateInstalledGrid();
                        }
                    }
                }
            } else if (this->touchActive) {
                if (!this->touchMoved) {
                    this->showInstalledDetails();
                }
                this->touchActive = false;
                this->touchMoved = false;
            }
        }
        if (this->shopGridMode) {
            if (this->isInstalledSection()) {
                this->gridSelectedIndex = this->shopGridIndex;
                this->updateInstalledGrid();
            } else {
                this->updateShopGrid();
            }
        } else {
            this->updatePreview();
            this->updateShopGrid();
        }
        this->updateDebug();
    }

    void shopInstPage::showInstalledDetails() {
        if (!this->isInstalledSection())
            return;
        int selectedIndex = this->shopGridMode ? this->gridSelectedIndex : this->menu->GetSelectedIndex();
        if (selectedIndex < 0 || selectedIndex >= (int)this->visibleItems.size())
            return;
        const auto& item = this->visibleItems[selectedIndex];

        const char* typeLabel = "Base";
        if (item.appType == NcmContentMetaType_Patch)
            typeLabel = "Update";
        else if (item.appType == NcmContentMetaType_AddOnContent)
            typeLabel = "DLC";

        char titleIdBuf[32] = {0};
        if (item.hasTitleId)
            std::snprintf(titleIdBuf, sizeof(titleIdBuf), "%016lx", static_cast<unsigned long>(item.titleId));
        else
            std::snprintf(titleIdBuf, sizeof(titleIdBuf), "unknown");

        std::string body;
        body += "inst.shop.detail_type"_lang + std::string(typeLabel) + "\n";
        body += "inst.shop.detail_titleid"_lang + std::string(titleIdBuf) + "\n";
        if (item.hasAppVersion)
            body += "inst.shop.detail_version"_lang + std::to_string(item.appVersion);
        else
            body += "inst.shop.detail_version"_lang + "0";

        mainApp->CreateShowDialog(item.name, body, {"common.ok"_lang}, true);
    }

    void shopInstPage::setButtonsText(const std::string& text) {
        this->butText->SetText(text);
        this->bottomHintSegments = BuildBottomHintSegments(text, 10, 20);
    }
}
