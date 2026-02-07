#include <filesystem>
#include <switch.h>
#include "ui/MainApplication.hpp"
#include "ui/mainPage.hpp"
#include "ui/instPage.hpp"
#include "ui/optionsPage.hpp"
#include "util/util.hpp"
#include "util/config.hpp"
#include "util/curl.hpp"
#include "util/unzip.hpp"
#include "util/lang.hpp"
#include "ui/instPage.hpp"
#include "shopInstall.hpp"
#include "ui/bottomHint.hpp"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    extern MainApplication *mainApp;

    std::vector<std::string> languageStrings = {"English", "日本語", "Français", "Deutsch", "Italiano", "Español", "Português", "한국어", "Русский", "簡体中文","繁體中文"};

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
        this->topRect = Rectangle::New(0, 0, 1280, 94, topColor);
        this->infoRect = Rectangle::New(0, 95, 1280, 60, infoColor);
        this->botRect = Rectangle::New(0, 660, 1280, 60, botColor);
        this->sideNavRect = Rectangle::New(20, 156, 260, 504, inst::config::oledMode ? COLOR("#FFFFFF18") : COLOR("#170909A0"), 14);
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
        this->pageInfoText = TextBlock::New(10, 109, "options.title"_lang, 30);
        this->pageInfoText->SetColor(COLOR("#FFFFFFFF"));
        const std::string optionsHintText = " Select/Change    / Section     Back";
        this->butText = TextBlock::New(10, 678, optionsHintText, 20);
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->bottomHintSegments = BuildBottomHintSegments(optionsHintText, 10, 20);
        this->menu = pu::ui::elm::Menu::New(300, 156, 960, COLOR("#FFFFFF00"), 72, (506 / 72), 20);
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
            auto sectionHighlight = Rectangle::New(30, 172 + (i * 56), 240, 48, COLOR("#FFFFFF00"), 10);
            this->sectionHighlights.push_back(sectionHighlight);
            this->Add(sectionHighlight);
            auto sectionText = TextBlock::New(40, 190 + (i * 56), "", 26);
            sectionText->SetColor(COLOR("#FFFFFFFF"));
            this->sectionTexts.push_back(sectionText);
            this->Add(sectionText);
        }
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
            std::string shopUrlDisplay = inst::config::shopUrl.empty() ? "-" : inst::util::shortenString(inst::config::shopUrl, 42, false);
            addItem("options.menu_items.shop_url"_lang + shopUrlDisplay, false, false);
            std::string shopUserDisplay = inst::config::shopUser.empty() ? "-" : inst::util::shortenString(inst::config::shopUser, 42, false);
            addItem("options.menu_items.shop_user"_lang + shopUserDisplay, false, false);
            std::string shopPassDisplay = inst::config::shopPass.empty() ? "-" : "********";
            addItem("options.menu_items.shop_pass"_lang + shopPassDisplay, false, false);
            addItem("options.menu_items.shop_hide_installed"_lang, true, inst::config::shopHideInstalled);
            addItem("options.menu_items.shop_hide_installed_section"_lang, true, inst::config::shopHideInstalledSection);
            addItem("options.menu_items.shop_reset_icons"_lang, false, false);
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

        const int sectionCount = static_cast<int>(this->sectionTexts.size());
        const bool leftPressed = (Down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) != 0;
        const bool rightPressed = (Down & (HidNpadButton_Right | HidNpadButton_StickLRight)) != 0;
        const bool upPressed = (Down & (HidNpadButton_Up | HidNpadButton_StickLUp)) != 0;
        const bool downPressed = (Down & (HidNpadButton_Down | HidNpadButton_StickLDown)) != 0;

        if (leftPressed && !this->tabsFocused) {
            this->tabsFocused = true;
            this->lockedMenuIndex = this->menu->GetSelectedIndex();
            this->setSectionNavText();
        } else if (rightPressed && this->tabsFocused) {
            this->tabsFocused = false;
            this->setSectionNavText();
        }

        if (Down & HidNpadButton_L) {
            this->tabsFocused = true;
            this->selectedSection--;
            if (this->selectedSection < 0) this->selectedSection = sectionCount - 1;
            this->refreshOptions(true);
            this->lockedMenuIndex = this->menu->GetSelectedIndex();
        }
        if (Down & HidNpadButton_R) {
            this->tabsFocused = true;
            this->selectedSection++;
            if (this->selectedSection >= sectionCount) this->selectedSection = 0;
            this->refreshOptions(true);
            this->lockedMenuIndex = this->menu->GetSelectedIndex();
        }

        if (this->tabsFocused) {
            if (upPressed && !downPressed) {
                this->selectedSection--;
                if (this->selectedSection < 0) this->selectedSection = sectionCount - 1;
                this->refreshOptions(true);
                this->lockedMenuIndex = this->menu->GetSelectedIndex();
            } else if (downPressed) {
                this->selectedSection++;
                if (this->selectedSection >= sectionCount) this->selectedSection = 0;
                this->refreshOptions(true);
                this->lockedMenuIndex = this->menu->GetSelectedIndex();
            }
            this->menu->SetSelectedIndex(this->lockedMenuIndex);
        } else {
            this->lockedMenuIndex = this->menu->GetSelectedIndex();
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
                        this->selectedSection = touchedSection;
                        this->refreshOptions(true);
                        this->lockedMenuIndex = this->menu->GetSelectedIndex();
                    } else {
                        this->setSectionNavText();
                    }
                } else if (this->touchRegion == 2) {
                    this->tabsFocused = false;
                    this->setSectionNavText();
                    touchSelect = true;
                }
            }
            this->touchActive = false;
            this->touchMoved = false;
            this->touchRegion = 0;
        }

        if ((Down & HidNpadButton_A) && this->tabsFocused) {
            this->tabsFocused = false;
            this->setSectionNavText();
        }

        if (((Down & HidNpadButton_A) && !this->tabsFocused) || touchSelect) {
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
                selectedIndex += 9;
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
                case 9:
                    keyboardResult = inst::util::softwareKeyboard("options.shop.url_hint"_lang, inst::config::shopUrl.c_str(), 200);
                    if (keyboardResult.size() > 0) {
                        inst::config::shopUrl = keyboardResult;
                        inst::config::setConfig();
                        this->refreshOptions();
                    }
                    break;
                case 10:
                    keyboardResult = inst::util::softwareKeyboard("options.shop.user_hint"_lang, inst::config::shopUser.c_str(), 100);
                    inst::config::shopUser = keyboardResult;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
                case 11:
                    keyboardResult = inst::util::softwareKeyboard("options.shop.pass_hint"_lang, inst::config::shopPass.c_str(), 100);
                    inst::config::shopPass = keyboardResult;
                    inst::config::setConfig();
                    this->refreshOptions();
                    break;
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
