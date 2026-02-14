#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <switch.h>
#include "ui/MainApplication.hpp"
#include "ui/mainPage.hpp"
#include "ui/instPage.hpp"
#include "ui/optionsPage.hpp"
#include "util/util.hpp"
#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/offline_db_update.hpp"
#include "util/unzip.hpp"
#include "util/lang.hpp"
#include "ui/instPage.hpp"
#include "shopInstall.hpp"
#include "ui/bottomHint.hpp"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    extern MainApplication *mainApp;

    std::vector<std::string> languageStrings = {"English", "日本語", "Français", "Deutsch", "Italiano", "Español", "Português", "한국어", "Русский", "簡体中文","繁體中文"};

    namespace {
        bool IsActiveShop(const inst::config::ShopProfile& shop)
        {
            return inst::config::BuildShopUrl(shop) == inst::config::shopUrl &&
                   shop.username == inst::config::shopUser &&
                   shop.password == inst::config::shopPass;
        }

        std::string TrimString(const std::string& value)
        {
            if (value.empty())
                return "";
            std::size_t start = value.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                return "";
            std::size_t end = value.find_last_not_of(" \t\r\n");
            return value.substr(start, (end - start) + 1);
        }

        std::string ActiveShopLabel(const std::vector<inst::config::ShopProfile>& shops)
        {
            for (const auto& shop : shops) {
                if (!IsActiveShop(shop))
                    continue;
                if (shop.favourite)
                    return "* " + shop.title;
                return shop.title;
            }

            if (!inst::config::shopUrl.empty())
                return inst::util::shortenString(inst::config::shopUrl, 42, false);
            return "-";
        }

        std::vector<std::string> BuildShopChoices(const std::vector<inst::config::ShopProfile>& shops)
        {
            std::vector<std::string> labels;
            labels.reserve(shops.size());
            for (const auto& shop : shops) {
                std::string label = (shop.favourite ? "* " : "") + shop.title;
                if (IsActiveShop(shop))
                    label += " (Active)";
                labels.push_back(label);
            }
            return labels;
        }
    }

    optionsPage::optionsPage() : Layout::Layout() {
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
        this->topRect = Rectangle::New(0, 0, 1280, 74, topColor);
        this->infoRect = Rectangle::New(0, 75, 1280, 60, infoColor);
        this->botRect = Rectangle::New(0, 660, 1280, 60, botColor);
        this->sideNavRect = Rectangle::New(0, 136, 300, 523, inst::config::oledMode ? COLOR("#FFFFFF18") : COLOR("#170909A0"));
        if (inst::config::gayMode) {
            this->titleImage = Image::New(-113, -8, "romfs:/images/logo.png");
            this->appVersionText = TextBlock::New(367, 29, "v" + inst::config::appVersion, 22);
        }
        else {
            this->titleImage = Image::New(0, -8, "romfs:/images/logo.png");
            this->appVersionText = TextBlock::New(480, 29, "v" + inst::config::appVersion, 22);
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
        this->pageInfoText = TextBlock::New(10, 89, "options.title"_lang, 30);
        this->pageInfoText->SetColor(COLOR("#FFFFFFFF"));
        const std::string optionsHintText = " Select/Change    / Section     Back";
        this->butText = TextBlock::New(10, 678, optionsHintText, 20);
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->bottomHintSegments = BuildBottomHintSegments(optionsHintText, 10, 20);
        this->menu = pu::ui::elm::Menu::New(301, 136, 979, COLOR("#FFFFFF00"), 72, (506 / 72), 20);
        if (inst::config::oledMode) {
            this->menu->SetOnFocusColor(COLOR("#FFFFFF33"));
            this->menu->SetScrollbarColor(COLOR("#FFFFFF66"));
        } else {
            this->menu->SetOnFocusColor(COLOR("#00000033"));
            this->menu->SetScrollbarColor(COLOR("#17090980"));
        }
        this->Add(this->topRect);
        this->Add(this->infoRect);
        this->Add(this->botRect);
        this->Add(this->sideNavRect);
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
        for (int i = 0; i < 3; i++) {
            auto sectionHighlight = Rectangle::New(30, 152 + (i * 56), 240, 48, COLOR("#FFFFFF00"), 10);
            this->sectionHighlights.push_back(sectionHighlight);
            this->Add(sectionHighlight);
            auto sectionText = TextBlock::New(40, 170 + (i * 56), "", 26);
            sectionText->SetColor(COLOR("#FFFFFFFF"));
            this->sectionTexts.push_back(sectionText);
            this->Add(sectionText);
        }
        this->sectionMenuIndices.assign(this->sectionTexts.size(), 0);
        this->refreshOptions(true);
        this->Add(this->menu);
    }

    void optionsPage::askToUpdate(std::vector<std::string> updateInfo) {
            if (!mainApp->CreateShowDialog("options.update.title"_lang, "options.update.desc0"_lang + updateInfo[0] + "options.update.desc1"_lang, {"options.update.opt0"_lang, "common.cancel"_lang}, false)) {
                inst::ui::instPage::loadInstallScreen();
                inst::ui::instPage::setTopInstInfoText("options.update.top_info"_lang + updateInfo[0]);
                inst::ui::instPage::setInstBarPerc(0);
                inst::ui::instPage::setInstInfoText("options.update.bot_info"_lang + updateInfo[0]);
                try {
                    std::string downloadName = inst::config::appDir + "/temp_download.zip";
                    inst::curl::downloadFile(updateInfo[1], downloadName.c_str(), 0, true);
                    romfsExit();
                    inst::ui::instPage::setInstInfoText("options.update.bot_info2"_lang + updateInfo[0]);
                    inst::zip::extractFile(downloadName, "sdmc:/");
                    std::filesystem::remove(downloadName);
                    mainApp->CreateShowDialog("options.update.complete"_lang, "options.update.end_desc"_lang, {"common.ok"_lang}, false);
                } catch (...) {
                    mainApp->CreateShowDialog("options.update.failed"_lang, "options.update.end_desc"_lang, {"common.ok"_lang}, false);
                }
                mainApp->FadeOut();
                mainApp->Close();
            }
        return;
    }

    std::string optionsPage::getMenuOptionIcon(bool ourBool) {
        if(ourBool) return "romfs:/images/icons/check-box-outline.png";
        else return "romfs:/images/icons/checkbox-blank-outline.png";
    }

    std::string optionsPage::getMenuLanguage(int ourLangCode) {
        switch (ourLangCode) {
            case 1:
            case 12:
                return languageStrings[0];
            case 0:
                return languageStrings[1];
            case 2:
            case 13:
                return languageStrings[2];
            case 3:
                return languageStrings[3];
            case 4:
                return languageStrings[4];
            case 5:
            case 14:
                return languageStrings[5];
            case 9:
                return languageStrings[6];
            case 7:
                return languageStrings[7];
            case 10:
                return languageStrings[8];
            case 6:
                return languageStrings[9];
            case 11:
                return languageStrings[10];
            default:
                return "options.language.system_language"_lang;
        }
    }

    void optionsPage::setSectionNavText() {
        static const std::vector<std::string> sectionLabels = {"General", "Shop", "System"};
        for (size_t i = 0; i < this->sectionTexts.size() && i < sectionLabels.size(); i++) {
            const bool selected = static_cast<int>(i) == this->selectedSection;
            this->sectionHighlights[i]->SetColor(selected
                ? (this->tabsFocused
                    ? (inst::config::oledMode ? COLOR("#FFFFFF55") : COLOR("#FFFFFF66"))
                    : (inst::config::oledMode ? COLOR("#FFFFFF33") : COLOR("#FFFFFF40")))
                : COLOR("#FFFFFF00"));
            this->sectionTexts[i]->SetText(sectionLabels[i]);
            this->sectionTexts[i]->SetColor(selected ? COLOR("#FFFFFFFF") : (this->tabsFocused ? COLOR("#FFFFFFCC") : COLOR("#FFFFFF99")));
        }

        // Make the settings-list row highlight clearly reflect which area is active.
        if (inst::config::oledMode) {
            this->menu->SetOnFocusColor(this->tabsFocused ? COLOR("#FFFFFF18") : COLOR("#FFFFFF66"));
        } else {
            this->menu->SetOnFocusColor(this->tabsFocused ? COLOR("#00000022") : COLOR("#00000070"));
        }
    }

    void optionsPage::setSettingsMenuText() {
        this->menu->ClearItems();

        auto addItem = [this](const std::string &label, bool toggle, bool value) {
            auto item = pu::ui::elm::MenuItem::New(label);
            item->SetColor(COLOR("#FFFFFFFF"));
            if (toggle) item->SetIcon(this->getMenuOptionIcon(value));
            this->menu->AddItem(item);
        };

        if (this->selectedSection == 0) {
            addItem("options.menu_items.ignore_firm"_lang, true, inst::config::ignoreReqVers);
            addItem("options.menu_items.nca_verify"_lang, true, inst::config::validateNCAs);
            addItem("options.menu_items.boost_mode"_lang, true, inst::config::overClock);
            addItem("options.menu_items.ask_delete"_lang, true, inst::config::deletePrompt);
            addItem("options.menu_items.sound"_lang, true, inst::config::soundEnabled);
            addItem("options.menu_items.oled"_lang, true, inst::config::oledMode);
            addItem("options.menu_items.mtp_album"_lang, true, inst::config::mtpExposeAlbum);
            return;
        }

        if (this->selectedSection == 1) {
            std::vector<inst::config::ShopProfile> shops = inst::config::LoadShops();
            std::string dbVersion = inst::offline::dbupdate::GetInstalledVersion();
            if (dbVersion.empty())
                dbVersion = "not installed";
            else
                dbVersion = inst::util::shortenString(dbVersion, 24, false);
            addItem("Active shop: " + inst::util::shortenString(ActiveShopLabel(shops), 42, false), false, false);
            addItem("Memorized shops: " + std::to_string(shops.size()), false, false);
            addItem("Add new shop", false, false);
            addItem("options.menu_items.shop_hide_installed"_lang, true, inst::config::shopHideInstalled);
            addItem("options.menu_items.shop_hide_installed_section"_lang, true, inst::config::shopHideInstalledSection);
            addItem("options.menu_items.shop_start_grid_mode"_lang, true, inst::config::shopStartGridMode);
            addItem("options.menu_items.shop_reset_icons"_lang, false, false);
            addItem("Offline DB auto-check on startup", true, inst::config::offlineDbAutoCheckOnStartup);
            addItem("Offline DB update (" + dbVersion + ")", false, false);
            return;
        }

        addItem("options.menu_items.sig_url"_lang + inst::util::shortenString(inst::config::sigPatchesUrl, 42, false), false, false);
        addItem("options.menu_items.auto_update"_lang, true, inst::config::autoUpdate);
        addItem("options.menu_items.gay_option"_lang, true, inst::config::gayMode);
        addItem("options.menu_items.language"_lang + this->getMenuLanguage(inst::config::languageSetting), false, false);
        addItem("options.menu_items.check_update"_lang, false, false);
        addItem("options.menu_items.credits"_lang, false, false);
    }

    void optionsPage::refreshOptions(bool resetSelection) {
        this->setSectionNavText();
        this->setSettingsMenuText();
        if (resetSelection) this->menu->SetSelectedIndex(0);
    }

    void optionsPage::rememberCurrentSectionMenuIndex() {
        if (this->selectedSection < 0)
            return;
        if (this->selectedSection >= static_cast<int>(this->sectionMenuIndices.size()))
            this->sectionMenuIndices.resize(this->sectionTexts.size(), 0);
        int selected = this->menu->GetSelectedIndex();
        if (selected < 0)
            selected = 0;
        this->sectionMenuIndices[this->selectedSection] = selected;
    }

    void optionsPage::restoreSelectedSectionMenuIndex() {
        if (this->selectedSection < 0 || this->selectedSection >= static_cast<int>(this->sectionMenuIndices.size()))
            return;
        const int itemCount = static_cast<int>(this->menu->GetItems().size());
        if (itemCount <= 0)
            return;
        int selected = this->sectionMenuIndices[this->selectedSection];
        if (selected < 0)
            selected = 0;
        if (selected >= itemCount)
            selected = itemCount - 1;
        this->sectionMenuIndices[this->selectedSection] = selected;
        this->menu->SetSelectedIndex(selected);
    }

    void optionsPage::setSelectedSectionAndRefresh(int newSection) {
        if (this->sectionTexts.empty())
            return;
        const int sectionCount = static_cast<int>(this->sectionTexts.size());
        if (newSection < 0)
            newSection = sectionCount - 1;
        if (newSection >= sectionCount)
            newSection = 0;

        this->rememberCurrentSectionMenuIndex();
        this->selectedSection = newSection;
        this->refreshOptions(false);
        this->restoreSelectedSectionMenuIndex();
        this->lockedMenuIndex = this->menu->GetSelectedIndex();
    }

    int optionsPage::getSectionFromTouch(int x, int y) const {
        const int navX = this->sideNavRect->GetProcessedX();
        const int navY = this->sideNavRect->GetProcessedY();
        const int navW = this->sideNavRect->GetWidth();
        const int navH = this->sideNavRect->GetHeight();
        const bool inNav = (x >= navX) && (x <= (navX + navW)) && (y >= navY) && (y <= (navY + navH));
        if (!inNav) return -1;

        for (size_t i = 0; i < this->sectionTexts.size(); i++) {
            const int secY = this->sectionTexts[i]->GetProcessedY();
            const int hitTop = secY - 14;
            const int hitBottom = secY + 42;
            if ((y >= hitTop) && (y <= hitBottom)) {
                return static_cast<int>(i);
            }
        }

        int nearestIdx = -1;
        int nearestDist = 1 << 30;
        for (size_t i = 0; i < this->sectionTexts.size(); i++) {
            const int centerY = this->sectionTexts[i]->GetProcessedY() + 14;
            int dist = y - centerY;
            if (dist < 0) dist = -dist;
            if (dist < nearestDist) {
                nearestDist = dist;
                nearestIdx = static_cast<int>(i);
            }
        }
        if (nearestDist <= 90) return nearestIdx;
        return -1;
    }

    void optionsPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos) {
        (void)Up;
        (void)Held;
        int bottomTapX = 0;
        if (DetectBottomHintTap(Pos, this->bottomHintTouch, 668, 52, bottomTapX)) {
            Down |= FindBottomHintButton(this->bottomHintSegments, bottomTapX);
        }
        if (Down & HidNpadButton_B) {
            mainApp->LoadLayout(mainApp->mainPage);
        }

        const bool leftPressed = (Down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) != 0;
        const bool rightPressed = (Down & (HidNpadButton_Right | HidNpadButton_StickLRight)) != 0;
        const bool upPressed = (Down & (HidNpadButton_Up | HidNpadButton_StickLUp)) != 0;
        const bool downPressed = (Down & (HidNpadButton_Down | HidNpadButton_StickLDown)) != 0;

        if (leftPressed && !this->tabsFocused) {
            this->tabsFocused = true;
            this->rememberCurrentSectionMenuIndex();
            this->lockedMenuIndex = this->menu->GetSelectedIndex();
            this->setSectionNavText();
        } else if (rightPressed && this->tabsFocused) {
            this->tabsFocused = false;
            this->restoreSelectedSectionMenuIndex();
            this->setSectionNavText();
        }

        if (Down & HidNpadButton_L) {
            this->tabsFocused = true;
            this->setSelectedSectionAndRefresh(this->selectedSection - 1);
        }
        if (Down & HidNpadButton_R) {
            this->tabsFocused = true;
            this->setSelectedSectionAndRefresh(this->selectedSection + 1);
        }

        if (this->tabsFocused) {
            if (upPressed && !downPressed) {
                this->setSelectedSectionAndRefresh(this->selectedSection - 1);
            } else if (downPressed) {
                this->setSelectedSectionAndRefresh(this->selectedSection + 1);
            }
            this->menu->SetSelectedIndex(this->lockedMenuIndex);
        } else {
            this->lockedMenuIndex = this->menu->GetSelectedIndex();
            this->rememberCurrentSectionMenuIndex();
        }

        bool touchSelect = false;
        if (!Pos.IsEmpty()) {
            if (!this->touchActive) {
                this->touchActive = true;
                this->touchMoved = false;
                this->touchStartX = Pos.X;
                this->touchStartY = Pos.Y;
                const bool inMenu = (Pos.X >= this->menu->GetProcessedX()) &&
                    (Pos.X <= (this->menu->GetProcessedX() + this->menu->GetWidth())) &&
                    (Pos.Y >= this->menu->GetProcessedY()) &&
                    (Pos.Y <= (this->menu->GetProcessedY() + this->menu->GetHeight()));
                const bool inSideNav = this->getSectionFromTouch(Pos.X, Pos.Y) >= 0;
                this->touchRegion = inSideNav ? 1 : (inMenu ? 2 : 0);
            } else {
                int dx = Pos.X - this->touchStartX;
                int dy = Pos.Y - this->touchStartY;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx > 12 || dy > 12) this->touchMoved = true;
            }
        } else if (this->touchActive) {
            if (!this->touchMoved) {
                if (this->touchRegion == 1) {
                    this->tabsFocused = true;
                    int touchedSection = this->getSectionFromTouch(this->touchStartX, this->touchStartY);
                    if (touchedSection >= 0 && touchedSection != this->selectedSection) {
                        this->setSelectedSectionAndRefresh(touchedSection);
                    } else {
                        this->setSectionNavText();
                    }
                } else if (this->touchRegion == 2) {
                    this->tabsFocused = false;
                    this->restoreSelectedSectionMenuIndex();
                    this->setSectionNavText();
                    touchSelect = true;
                }
            }
            this->touchActive = false;
            this->touchMoved = false;
            this->touchRegion = 0;
        }

        bool tabAcceptOnly = false;
        if ((Down & HidNpadButton_A) && this->tabsFocused) {
            this->tabsFocused = false;
            this->restoreSelectedSectionMenuIndex();
            this->setSectionNavText();
            tabAcceptOnly = true;
        }

        if ((((Down & HidNpadButton_A) && !this->tabsFocused) && !tabAcceptOnly) || touchSelect) {
            std::string keyboardResult;
            int rc;
            std::vector<std::string> downloadUrl;
            std::vector<std::string> languageList;
            int selectedIndex = this->menu->GetSelectedIndex();
            if (this->selectedSection == 0) {
                static const int kGeneralMap[] = {0, 1, 2, 3, 6, 7, 8};
                if ((selectedIndex < 0) || (selectedIndex >= static_cast<int>(sizeof(kGeneralMap) / sizeof(kGeneralMap[0])))) return;
                selectedIndex = kGeneralMap[selectedIndex];
            } else if (this->selectedSection == 1) {
                static const int kShopMap[] = {9, 20, 21, 12, 13, 19, 14, 23, 22};
                if ((selectedIndex < 0) || (selectedIndex >= static_cast<int>(sizeof(kShopMap) / sizeof(kShopMap[0])))) return;
                selectedIndex = kShopMap[selectedIndex];
            } else {
                static const int kSystemMap[] = {15, 4, 5, 16, 17, 18};
                if ((selectedIndex < 0) || (selectedIndex >= static_cast<int>(sizeof(kSystemMap) / sizeof(kSystemMap[0])))) return;
                selectedIndex = kSystemMap[selectedIndex];
            }
            switch (selectedIndex) {
                case 0:
                    inst::config::ignoreReqVers = !inst::config::ignoreReqVers;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 1:
                    if (inst::config::validateNCAs) {
                        if (inst::ui::mainApp->CreateShowDialog("options.nca_warn.title"_lang, "options.nca_warn.desc"_lang, {"common.cancel"_lang, "options.nca_warn.opt1"_lang}, false) == 1) inst::config::validateNCAs = false;
                    } else inst::config::validateNCAs = true;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 2:
                    inst::config::overClock = !inst::config::overClock;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 3:
                    inst::config::deletePrompt = !inst::config::deletePrompt;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 4:
                    inst::config::autoUpdate = !inst::config::autoUpdate;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 5:
                    if (inst::config::gayMode) {
                        inst::config::gayMode = false;
                        mainApp->mainPage->awooImage->SetVisible(true);
                        mainApp->instpage->awooImage->SetVisible(true);
                        mainApp->instpage->titleImage->SetX(0);
                        mainApp->instpage->appVersionText->SetX(480);
                        mainApp->mainPage->titleImage->SetX(0);
                        mainApp->mainPage->appVersionText->SetX(480);
                        mainApp->netinstPage->titleImage->SetX(0);
                        mainApp->netinstPage->appVersionText->SetX(480);
                        mainApp->shopinstPage->titleImage->SetX(0);
                        mainApp->shopinstPage->appVersionText->SetX(480);
                        mainApp->optionspage->titleImage->SetX(0);
                        mainApp->optionspage->appVersionText->SetX(480);
                        mainApp->sdinstPage->titleImage->SetX(0);
                        mainApp->sdinstPage->appVersionText->SetX(480);
                        mainApp->usbinstPage->titleImage->SetX(0);
                        mainApp->usbinstPage->appVersionText->SetX(480);
                    }
                    else {
                        inst::config::gayMode = true;
                        mainApp->mainPage->awooImage->SetVisible(false);
                        mainApp->instpage->awooImage->SetVisible(false);
                        mainApp->instpage->titleImage->SetX(-113);
                        mainApp->instpage->appVersionText->SetX(367);
                        mainApp->mainPage->titleImage->SetX(-113);
                        mainApp->mainPage->appVersionText->SetX(367);
                        mainApp->netinstPage->titleImage->SetX(-113);
                        mainApp->netinstPage->appVersionText->SetX(367);
                        mainApp->shopinstPage->titleImage->SetX(-113);
                        mainApp->shopinstPage->appVersionText->SetX(367);
                        mainApp->optionspage->titleImage->SetX(-113);
                        mainApp->optionspage->appVersionText->SetX(367);
                        mainApp->sdinstPage->titleImage->SetX(-113);
                        mainApp->sdinstPage->appVersionText->SetX(367);
                        mainApp->usbinstPage->titleImage->SetX(-113);
                        mainApp->usbinstPage->appVersionText->SetX(367);
                    }
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 6:
                    inst::config::soundEnabled = !inst::config::soundEnabled;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 7:
                    inst::config::oledMode = !inst::config::oledMode;
                    inst::config::setConfig();
                    {
                        auto keepAlive = mainApp->optionspage;
                        mainApp->mainPage = MainPage::New();
                        mainApp->instpage = instPage::New();
                        mainApp->sdinstPage = sdInstPage::New();
                        mainApp->netinstPage = netInstPage::New();
                        mainApp->usbinstPage = usbInstPage::New();
                        mainApp->shopinstPage = shopInstPage::New();
                        mainApp->optionspage = optionsPage::New();
                        mainApp->mainPage->SetOnInput(std::bind(&MainPage::onInput, mainApp->mainPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
                        mainApp->netinstPage->SetOnInput(std::bind(&netInstPage::onInput, mainApp->netinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
                        mainApp->shopinstPage->SetOnInput(std::bind(&shopInstPage::onInput, mainApp->shopinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
                        mainApp->sdinstPage->SetOnInput(std::bind(&sdInstPage::onInput, mainApp->sdinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
                        mainApp->usbinstPage->SetOnInput(std::bind(&usbInstPage::onInput, mainApp->usbinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
                        mainApp->instpage->SetOnInput(std::bind(&instPage::onInput, mainApp->instpage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
                        mainApp->optionspage->SetOnInput(std::bind(&optionsPage::onInput, mainApp->optionspage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
                        mainApp->LoadLayout(mainApp->optionspage);
                    }
                    break;
                case 8:
                    inst::config::mtpExposeAlbum = !inst::config::mtpExposeAlbum;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 9: {
                    std::vector<inst::config::ShopProfile> shops = inst::config::LoadShops();
                    if (shops.empty()) {
                        inst::ui::mainApp->CreateShowDialog("Active shop", "No memorized shops found. Add one first.", {"common.ok"_lang}, true);
                        break;
                    }
                    std::vector<std::string> choices = BuildShopChoices(shops);
                    int selectedShop = inst::ui::mainApp->CreateShowDialog("Active shop", "Choose which memorized shop to use.", choices, false);
                    if (selectedShop < 0 || selectedShop >= static_cast<int>(shops.size()))
                        break;
                    if (inst::config::SetActiveShop(shops[selectedShop], true))
                        this->refreshOptions();
                    break;
                }
                case 20: {
                    std::vector<inst::config::ShopProfile> shops = inst::config::LoadShops();
                    if (shops.empty()) {
                        inst::ui::mainApp->CreateShowDialog("Memorized shops", "No memorized shops found.", {"common.ok"_lang}, true);
                        break;
                    }

                    std::vector<std::string> choices = BuildShopChoices(shops);
                    int selectedShop = inst::ui::mainApp->CreateShowDialog("Memorized shops", "Select one to manage.", choices, false);
                    if (selectedShop < 0 || selectedShop >= static_cast<int>(shops.size()))
                        break;

                    auto selected = shops[selectedShop];
                    std::string favouriteLabel = selected.favourite ? "Unset favourite" : "Set favourite";
                    int action = inst::ui::mainApp->CreateShowDialog(
                        selected.title,
                        "Choose an action for this shop.",
                        {"Use this shop", "Edit shop", favouriteLabel, "Delete shop", "Cancel"},
                        false
                    );

                    if (action == 0) {
                        if (inst::config::SetActiveShop(selected, true))
                            this->refreshOptions();
                    } else if (action == 1) {
                        const bool wasActive = IsActiveShop(selected);
                        inst::config::ShopProfile edited = selected;

                        std::string shopTitle = TrimString(inst::util::softwareKeyboard("Enter shop title (required)", edited.title, 80));
                        if (shopTitle.empty()) {
                            inst::ui::mainApp->CreateShowDialog("Invalid shop", "Title is required.", {"common.ok"_lang}, true);
                            break;
                        }

                        int protocolChoice = inst::ui::mainApp->CreateShowDialog("Shop protocol", "Choose the protocol used by this shop.", {"HTTP", "HTTPS"}, false);
                        if (protocolChoice < 0)
                            break;
                        edited.protocol = (protocolChoice == 1) ? "https" : "http";

                        std::string shopHost = TrimString(inst::util::softwareKeyboard("Enter shop host or IP", edited.host, 200));
                        if (shopHost.empty()) {
                            inst::ui::mainApp->CreateShowDialog("Invalid shop", "Host is required.", {"common.ok"_lang}, true);
                            break;
                        }
                        if (shopHost.rfind("http://", 0) == 0)
                            shopHost = shopHost.substr(7);
                        else if (shopHost.rfind("https://", 0) == 0)
                            shopHost = shopHost.substr(8);
                        std::size_t pathPos = shopHost.find('/');
                        if (pathPos != std::string::npos)
                            shopHost = shopHost.substr(0, pathPos);
                        shopHost = TrimString(shopHost);

                        int defaultPort = inst::config::DefaultPortForProtocol(edited.protocol);
                        int currentPort = edited.port;
                        if (currentPort <= 0 || currentPort > 65535)
                            currentPort = defaultPort;
                        int shopPort = currentPort;

                        std::string defaultPortLabel = "Use default (" + std::to_string(defaultPort) + ")";
                        std::string keepPortLabel = "Keep current (" + std::to_string(currentPort) + ")";
                        int portMode = inst::ui::mainApp->CreateShowDialog("Shop port", "Pick which port to use.", {defaultPortLabel, "Custom", keepPortLabel}, false);
                        if (portMode < 0)
                            break;
                        if (portMode == 0) {
                            shopPort = defaultPort;
                        } else if (portMode == 1) {
                            std::string currentPortText = std::to_string(currentPort);
                            std::string portText = TrimString(inst::util::softwareKeyboard("Enter shop port (1-65535)", currentPortText, 6));
                            if (portText.empty()) {
                                inst::ui::mainApp->CreateShowDialog("Invalid port", "Port is required in custom mode.", {"common.ok"_lang}, true);
                                break;
                            }
                            try {
                                int parsedPort = std::stoi(portText);
                                if (parsedPort <= 0 || parsedPort > 65535)
                                    throw std::out_of_range("port");
                                shopPort = parsedPort;
                            } catch (...) {
                                inst::ui::mainApp->CreateShowDialog("Invalid port", "Port must be between 1 and 65535.", {"common.ok"_lang}, true);
                                break;
                            }
                        }

                        std::size_t hostColon = shopHost.rfind(':');
                        if (hostColon != std::string::npos && shopHost.find(':') == hostColon) {
                            std::string hostPart = TrimString(shopHost.substr(0, hostColon));
                            std::string inlinePort = TrimString(shopHost.substr(hostColon + 1));
                            if (!hostPart.empty() && !inlinePort.empty()) {
                                try {
                                    int parsedPort = std::stoi(inlinePort);
                                    if (parsedPort > 0 && parsedPort <= 65535) {
                                        shopHost = hostPart;
                                        if (portMode == 0)
                                            shopPort = parsedPort;
                                    }
                                } catch (...) {
                                    inst::ui::mainApp->CreateShowDialog("Invalid host", "Host contains an invalid inline port.", {"common.ok"_lang}, true);
                                    break;
                                }
                            } else {
                                inst::ui::mainApp->CreateShowDialog("Invalid host", "Host format is invalid.", {"common.ok"_lang}, true);
                                break;
                            }
                        }

                        if (shopHost.empty()) {
                            inst::ui::mainApp->CreateShowDialog("Invalid shop", "Host is required.", {"common.ok"_lang}, true);
                            break;
                        }

                        std::string shopUser = inst::util::softwareKeyboard("options.shop.user_hint"_lang, edited.username, 100);
                        std::string shopPass = inst::util::softwareKeyboard("options.shop.pass_hint"_lang, edited.password, 100);

                        edited.title = shopTitle;
                        edited.host = shopHost;
                        edited.port = shopPort;
                        edited.username = shopUser;
                        edited.password = shopPass;

                        std::string error;
                        if (!inst::config::SaveShop(edited, &error)) {
                            inst::ui::mainApp->CreateShowDialog("Failed to save shop", error.empty() ? "Unknown error." : error, {"common.ok"_lang}, true);
                            break;
                        }

                        if (wasActive)
                            inst::config::SetActiveShop(edited, true);
                        this->refreshOptions();
                    } else if (action == 2) {
                        selected.favourite = !selected.favourite;
                        std::string error;
                        if (!inst::config::SaveShop(selected, &error))
                            inst::ui::mainApp->CreateShowDialog("Failed to save shop", error.empty() ? "Unknown error." : error, {"common.ok"_lang}, true);
                        else
                            this->refreshOptions();
                    } else if (action == 3) {
                        int confirmDelete = inst::ui::mainApp->CreateShowDialog("Delete shop?", "This cannot be undone.", {"Delete", "common.cancel"_lang}, false);
                        if (confirmDelete == 0) {
                            if (!inst::config::DeleteShop(selected.fileName)) {
                                inst::ui::mainApp->CreateShowDialog("Failed to delete shop", "Could not remove the shop file.", {"common.ok"_lang}, true);
                                break;
                            }
                            if (IsActiveShop(selected)) {
                                inst::config::shopUrl.clear();
                                inst::config::shopUser.clear();
                                inst::config::shopPass.clear();
                                inst::config::setConfig();
                            }
                            this->refreshOptions();
                        }
                    }
                    break;
                }
                case 21: {
                    std::string shopTitle = TrimString(inst::util::softwareKeyboard("Enter shop title (required)", "", 80));
                    if (shopTitle.empty()) {
                        inst::ui::mainApp->CreateShowDialog("Invalid shop", "Title is required.", {"common.ok"_lang}, true);
                        break;
                    }

                    int protocolChoice = inst::ui::mainApp->CreateShowDialog("Shop protocol", "Choose the protocol used by this shop.", {"HTTP", "HTTPS"}, false);
                    if (protocolChoice < 0)
                        break;
                    const std::string shopProtocol = (protocolChoice == 1) ? "https" : "http";

                    std::string shopHost = TrimString(inst::util::softwareKeyboard("Enter shop host or IP", "", 200));
                    if (shopHost.empty()) {
                        inst::ui::mainApp->CreateShowDialog("Invalid shop", "Host is required.", {"common.ok"_lang}, true);
                        break;
                    }
                    if (shopHost.rfind("http://", 0) == 0)
                        shopHost = shopHost.substr(7);
                    else if (shopHost.rfind("https://", 0) == 0)
                        shopHost = shopHost.substr(8);
                    std::size_t pathPos = shopHost.find('/');
                    if (pathPos != std::string::npos)
                        shopHost = shopHost.substr(0, pathPos);
                    shopHost = TrimString(shopHost);

                    const int defaultPort = inst::config::DefaultPortForProtocol(shopProtocol);
                    int shopPort = defaultPort;
                    const std::string useDefaultPortLabel = "Use default (" + std::to_string(defaultPort) + ")";
                    int portMode = inst::ui::mainApp->CreateShowDialog("Shop port", "Pick which port to use.", {useDefaultPortLabel, "Custom"}, false);
                    if (portMode < 0)
                        break;
                    if (portMode == 1) {
                        std::string portText = TrimString(inst::util::softwareKeyboard("Enter shop port (1-65535)", std::to_string(defaultPort), 6));
                        if (portText.empty()) {
                            inst::ui::mainApp->CreateShowDialog("Invalid port", "Port is required in custom mode.", {"common.ok"_lang}, true);
                            break;
                        }
                        try {
                            int parsedPort = std::stoi(portText);
                            if (parsedPort <= 0 || parsedPort > 65535)
                                throw std::out_of_range("port");
                            shopPort = parsedPort;
                        } catch (...) {
                            inst::ui::mainApp->CreateShowDialog("Invalid port", "Port must be between 1 and 65535.", {"common.ok"_lang}, true);
                            break;
                        }
                    }

                    std::size_t hostColon = shopHost.rfind(':');
                    if (hostColon != std::string::npos && shopHost.find(':') == hostColon) {
                        std::string hostPart = TrimString(shopHost.substr(0, hostColon));
                        std::string inlinePort = TrimString(shopHost.substr(hostColon + 1));
                        if (!hostPart.empty() && !inlinePort.empty()) {
                            try {
                                int parsedPort = std::stoi(inlinePort);
                                if (parsedPort > 0 && parsedPort <= 65535) {
                                    shopHost = hostPart;
                                    if (portMode == 0)
                                        shopPort = parsedPort;
                                }
                            } catch (...) {
                                inst::ui::mainApp->CreateShowDialog("Invalid host", "Host contains an invalid inline port.", {"common.ok"_lang}, true);
                                break;
                            }
                        } else {
                            inst::ui::mainApp->CreateShowDialog("Invalid host", "Host format is invalid.", {"common.ok"_lang}, true);
                            break;
                        }
                    }

                    if (shopHost.empty()) {
                        inst::ui::mainApp->CreateShowDialog("Invalid shop", "Host is required.", {"common.ok"_lang}, true);
                        break;
                    }

                    std::string shopUser = inst::util::softwareKeyboard("options.shop.user_hint"_lang, "", 100);
                    std::string shopPass = inst::util::softwareKeyboard("options.shop.pass_hint"_lang, "", 100);
                    int favouriteChoice = inst::ui::mainApp->CreateShowDialog("Favourite shop", "Keep this shop at the top of the list?", {"common.no"_lang, "common.yes"_lang}, false);
                    if (favouriteChoice < 0)
                        break;

                    inst::config::ShopProfile newShop;
                    newShop.title = shopTitle;
                    newShop.protocol = shopProtocol;
                    newShop.host = shopHost;
                    newShop.port = shopPort;
                    newShop.username = shopUser;
                    newShop.password = shopPass;
                    newShop.favourite = (favouriteChoice == 1);

                    std::string error;
                    if (!inst::config::SaveShop(newShop, &error)) {
                        inst::ui::mainApp->CreateShowDialog("Failed to save shop", error.empty() ? "Unknown error." : error, {"common.ok"_lang}, true);
                        break;
                    }

                    inst::config::SetActiveShop(newShop, true);
                    this->refreshOptions();
                    break;
                }
                case 12:
                    inst::config::shopHideInstalled = !inst::config::shopHideInstalled;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 13:
                    inst::config::shopHideInstalledSection = !inst::config::shopHideInstalledSection;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 14:
                    if (!inst::config::shopUrl.empty()) {
                        int confirm = inst::ui::mainApp->CreateShowDialog("options.cache_reset.title"_lang, "options.cache_reset.desc"_lang, {"options.cache_reset.confirm"_lang, "common.cancel"_lang}, false);
                        if (confirm == 0) {
                            shopInstStuff::ResetShopIconCache(inst::config::shopUrl);
                        }
                    }
                    break;
                case 23:
                    inst::config::offlineDbAutoCheckOnStartup = !inst::config::offlineDbAutoCheckOnStartup;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 22: {
                    if (inst::util::getIPAddress() == "1.0.0.127") {
                        inst::ui::mainApp->CreateShowDialog("main.net.title"_lang, "main.net.desc"_lang, {"common.ok"_lang}, true);
                        break;
                    }

                    const std::string manifestUrl = inst::config::offlineDbManifestUrl;
                    if (manifestUrl.empty()) {
                        inst::ui::mainApp->CreateShowDialog("Offline DB update", "Manifest URL is empty. Set offlineDbManifestUrl in config.json.", {"common.ok"_lang}, true);
                        break;
                    }

                    const auto check = inst::offline::dbupdate::CheckForUpdate(manifestUrl);
                    if (!check.success) {
                        inst::ui::mainApp->CreateShowDialog("Offline DB update", "Check failed:\n" + check.error, {"common.ok"_lang}, true);
                        break;
                    }

                    if (!check.updateAvailable) {
                        std::string body = "Offline DB is up to date.";
                        if (!check.remoteVersion.empty())
                            body += "\nVersion: " + check.remoteVersion;
                        inst::ui::mainApp->CreateShowDialog("Offline DB update", body, {"common.ok"_lang}, true);
                        break;
                    }

                    std::string prompt = "A new offline DB is available.";
                    if (!check.localVersion.empty())
                        prompt += "\nCurrent: " + check.localVersion;
                    if (!check.remoteVersion.empty())
                        prompt += "\nLatest: " + check.remoteVersion;
                    prompt += "\n\nDownload and install now?";
                    int applyNow = inst::ui::mainApp->CreateShowDialog("Offline DB update", prompt, {"Update", "common.cancel"_lang}, false);
                    if (applyNow != 0)
                        break;

                    inst::ui::instPage::loadInstallScreen();
                    inst::ui::instPage::setTopInstInfoText("Updating Offline DB");
                    inst::ui::instPage::setInstInfoText("Preparing...");
                    inst::ui::instPage::setInstBarPerc(0);
                    const auto apply = inst::offline::dbupdate::ApplyUpdate(manifestUrl, false,
                        [](const std::string& stage, double percent) {
                            inst::ui::instPage::setInstInfoText(stage);
                            inst::ui::instPage::setInstBarPerc(percent);
                        });
                    mainApp->LoadLayout(mainApp->optionspage);

                    if (!apply.success) {
                        inst::ui::mainApp->CreateShowDialog("Offline DB update", "Update failed:\n" + apply.error, {"common.ok"_lang}, true);
                        break;
                    }

                    this->refreshOptions();
                    std::string done = apply.updated ? "Offline DB updated successfully." : "Offline DB is already up to date.";
                    if (!apply.version.empty())
                        done += "\nVersion: " + apply.version;
                    inst::ui::mainApp->CreateShowDialog("Offline DB update", done, {"common.ok"_lang}, true);
                    break;
                }
                case 19:
                    inst::config::shopStartGridMode = !inst::config::shopStartGridMode;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 15:
                    keyboardResult = inst::util::softwareKeyboard("options.sig_hint"_lang, inst::config::sigPatchesUrl.c_str(), 500);
                    if (keyboardResult.size() > 0) {
                        inst::config::sigPatchesUrl = keyboardResult;
                        inst::config::setConfig();
                        this->refreshOptions();
                    }
                    break;
                case 16:
                    languageList = languageStrings;
                    languageList.push_back("options.language.system_language"_lang);
                    rc = inst::ui::mainApp->CreateShowDialog("options.language.title"_lang, "options.language.desc"_lang, languageList, false);
                    if (rc == -1) break;
                    switch(rc) {
                        case 0:
                            inst::config::languageSetting = 1;
                            break;
                        case 1:
                            inst::config::languageSetting = 0;
                            break;
                        case 2:
                            inst::config::languageSetting = 2;
                            break;
                        case 3:
                            inst::config::languageSetting = 3;
                            break;
                        case 4:
                            inst::config::languageSetting = 4;
                            break;
                        case 5:
                            inst::config::languageSetting = 14;
                            break;
                        case 6:
                            inst::config::languageSetting = 9;
                            break;
                        case 7:
                            inst::config::languageSetting = 7;
                            break;
                        case 8:
                            inst::config::languageSetting = 10;
                            break;
                        case 9:
                            inst::config::languageSetting = 6;
                            break;
                        case 10:
                            inst::config::languageSetting = 11;
                            break;
                        default:
                            inst::config::languageSetting = 99;
                    }
                    inst::config::setConfig();
                    mainApp->FadeOut();
                    mainApp->Close();
                    break;
                case 17:
                    if (inst::util::getIPAddress() == "1.0.0.127") {
                        inst::ui::mainApp->CreateShowDialog("main.net.title"_lang, "main.net.desc"_lang, {"common.ok"_lang}, true);
                        break;
                    }
                    downloadUrl = inst::util::checkForAppUpdate();
                    if (!downloadUrl.size()) {
                        mainApp->CreateShowDialog("options.update.title_check_fail"_lang, "options.update.desc_check_fail"_lang, {"common.ok"_lang}, false);
                        break;
                    }
                    this->askToUpdate(downloadUrl);
                    break;
                case 18:
                    inst::ui::mainApp->CreateShowDialog("options.credits.title"_lang, "options.credits.desc"_lang, {"common.close"_lang}, true);
                    break;
                default:
                    break;
            }
        }
    }
}
