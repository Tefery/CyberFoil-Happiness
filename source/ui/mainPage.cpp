#include <filesystem>
#include <switch.h>
#include "ui/MainApplication.hpp"
#include "ui/mainPage.hpp"
#include "util/util.hpp"
#include "util/config.hpp"
#include "util/lang.hpp"
#include "sigInstall.hpp"
#include "data/buffered_placeholder_writer.hpp"
#include "mtp_server.hpp"
#include "nx/usbhdd.h"
#include "ui/bottomHint.hpp"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    extern MainApplication *mainApp;
    bool appletFinished = false;
    bool updateFinished = false;
    constexpr int kMainGridCols = 3;
    constexpr int kMainGridRows = 3;
    constexpr int kMainGridTileWidth = 360;
    constexpr int kMainGridTileHeight = 170;
    constexpr int kMainGridGapX = 20;
    constexpr int kMainGridGapY = 18;
    constexpr int kMainGridStartX = (1280 - ((kMainGridCols * kMainGridTileWidth) + ((kMainGridCols - 1) * kMainGridGapX))) / 2;
    constexpr int kMainGridStartY = 120;

    void mainMenuThread() {
        bool menuLoaded = mainApp->IsShown();
        if (!appletFinished && appletGetAppletType() == AppletType_LibraryApplet) {
            tin::data::NUM_BUFFER_SEGMENTS = 2;
            if (menuLoaded) {
                inst::ui::appletFinished = true;
                mainApp->CreateShowDialog("main.applet.title"_lang, "main.applet.desc"_lang, {"common.ok"_lang}, true);
            } 
        } else if (!appletFinished) {
            inst::ui::appletFinished = true;
            tin::data::NUM_BUFFER_SEGMENTS = 128;
        }
        if (!updateFinished && (!inst::config::autoUpdate || inst::util::getIPAddress() == "1.0.0.127")) updateFinished = true;
        if (!updateFinished && menuLoaded && inst::config::updateInfo.size()) {
            updateFinished = true;
            optionsPage::askToUpdate(inst::config::updateInfo);
        }
    }

    MainPage::MainPage() : Layout::Layout() {
        if (inst::config::oledMode) {
            this->SetBackgroundColor(COLOR("#000000FF"));
        } else {
            this->SetBackgroundColor(COLOR("#670000FF"));
            if (std::filesystem::exists(inst::config::appDir + "/background.png")) this->SetBackgroundImage(inst::config::appDir + "/background.png");
            else this->SetBackgroundImage("romfs:/images/background.jpg");
        }
        const auto topColor = inst::config::oledMode ? COLOR("#000000FF") : COLOR("#170909FF");
        const auto botColor = inst::config::oledMode ? COLOR("#000000FF") : COLOR("#17090980");
        this->topRect = Rectangle::New(0, 0, 1280, 94, topColor);
        this->botRect = Rectangle::New(0, 659, 1280, 61, botColor);
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
        const std::string mainButtonsText = "main.buttons"_lang + "     " + "main.info.button"_lang;
        this->butText = TextBlock::New(10, 678, mainButtonsText, 20);
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->bottomHintSegments = BuildBottomHintSegments(mainButtonsText, 10, 20);
        this->installMenuItem = pu::ui::elm::MenuItem::New("main.menu.sd"_lang);
        this->installMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->installMenuItem->SetIcon("romfs:/images/icons/micro-sd.png");
        this->netInstallMenuItem = pu::ui::elm::MenuItem::New("main.menu.net"_lang);
        this->netInstallMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->netInstallMenuItem->SetIcon("romfs:/images/icons/cloud-download.png");
        this->shopInstallMenuItem = pu::ui::elm::MenuItem::New("main.menu.shop"_lang);
        this->shopInstallMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->shopInstallMenuItem->SetIcon("romfs:/images/icons/eshop.png");
        this->usbInstallMenuItem = pu::ui::elm::MenuItem::New("main.menu.usb"_lang);
        this->usbInstallMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->usbInstallMenuItem->SetIcon("romfs:/images/icons/usb-port.png");
        this->hddInstallMenuItem = pu::ui::elm::MenuItem::New("main.menu.hdd"_lang);
        this->hddInstallMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->hddInstallMenuItem->SetIcon("romfs:/images/icons/usb-install.png");
        this->mtpInstallMenuItem = pu::ui::elm::MenuItem::New("main.menu.mtp"_lang);
        this->mtpInstallMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->mtpInstallMenuItem->SetIcon("romfs:/images/icons/usb-port.png");
        this->sigPatchesMenuItem = pu::ui::elm::MenuItem::New("main.menu.sig"_lang);
        this->sigPatchesMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->sigPatchesMenuItem->SetIcon("romfs:/images/icons/wrench.png");
        this->settingsMenuItem = pu::ui::elm::MenuItem::New("main.menu.set"_lang);
        this->settingsMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->settingsMenuItem->SetIcon("romfs:/images/icons/settings.png");
        this->exitMenuItem = pu::ui::elm::MenuItem::New("main.menu.exit"_lang);
        this->exitMenuItem->SetColor(COLOR("#FFFFFFFF"));
        this->exitMenuItem->SetIcon("romfs:/images/icons/exit-run.png");
        const auto tileColor = inst::config::oledMode ? COLOR("#1A1A1ACC") : COLOR("#170909CC");
        const auto highlightColor = inst::config::oledMode ? COLOR("#FF4D4D66") : COLOR("#FF4D4D88");
        const std::vector<std::string> gridLabels = {
            "main.menu.shop"_lang,
            "main.menu.sd"_lang,
            "main.menu.hdd"_lang,
            "main.menu.mtp"_lang,
            "main.menu.usb"_lang,
            "main.menu.net"_lang,
            "main.menu.sig"_lang,
            "main.menu.set"_lang,
            "main.menu.exit"_lang
        };
        const std::vector<std::string> gridIcons = {
            "romfs:/images/icons/eshop.png",
            "romfs:/images/icons/micro-sd.png",
            "romfs:/images/icons/usb-install.png",
            "romfs:/images/icons/usb-port.png",
            "romfs:/images/icons/usb-port.png",
            "romfs:/images/icons/cloud-download.png",
            "romfs:/images/icons/wrench.png",
            "romfs:/images/icons/settings.png",
            "romfs:/images/icons/exit-run.png"
        };
        this->mainGridTiles.reserve(kMainGridCols * kMainGridRows);
        this->mainGridIcons.reserve(kMainGridCols * kMainGridRows);
        this->mainGridLabels.reserve(kMainGridCols * kMainGridRows);
        for (int i = 0; i < (kMainGridCols * kMainGridRows); i++) {
            const int col = i % kMainGridCols;
            const int row = i / kMainGridCols;
            const int x = kMainGridStartX + (col * (kMainGridTileWidth + kMainGridGapX));
            const int y = kMainGridStartY + (row * (kMainGridTileHeight + kMainGridGapY));
            auto tile = Rectangle::New(x, y, kMainGridTileWidth, kMainGridTileHeight, tileColor, 18);
            constexpr int kMainIconSize = 96;
            auto icon = Image::New(x + ((kMainGridTileWidth - kMainIconSize) / 2), y + 22, gridIcons[i]);
            icon->SetWidth(kMainIconSize);
            icon->SetHeight(kMainIconSize);
            auto label = TextBlock::New(0, y + 116, gridLabels[i], 22);
            label->SetX(x + ((kMainGridTileWidth - label->GetTextWidth()) / 2));
            label->SetColor(COLOR("#FFFFFFFF"));
            this->mainGridTiles.push_back(tile);
            this->mainGridIcons.push_back(icon);
            this->mainGridLabels.push_back(label);
        }
        this->mainGridHighlight = Rectangle::New(0, 0, kMainGridTileWidth + 8, kMainGridTileHeight + 8, highlightColor, 20);
        if (std::filesystem::exists(inst::config::appDir + "/awoo_main.png")) this->awooImage = Image::New(410, 190, inst::config::appDir + "/awoo_main.png");
        else this->awooImage = Image::New(410, 190, "romfs:/images/awoos/5bbdbcf9a5625cd307c9e9bc360d78bd.png");
        this->Add(this->awooImage);
        this->Add(this->topRect);
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
        for (auto& tile : this->mainGridTiles)
            this->Add(tile);
        for (auto& icon : this->mainGridIcons)
            this->Add(icon);
        for (auto& label : this->mainGridLabels)
            this->Add(label);
        this->Add(this->mainGridHighlight);
        this->awooImage->SetVisible(!inst::config::gayMode);
        this->updateMainGridSelection();
        this->AddThread(mainMenuThread);
    }

    void MainPage::installMenuItem_Click() {
        mainApp->sdinstPage->drawMenuItems(true, "sdmc:/");
        mainApp->sdinstPage->menu->SetSelectedIndex(0);
        mainApp->LoadLayout(mainApp->sdinstPage);
    }

    void MainPage::netInstallMenuItem_Click() {
        if (inst::util::getIPAddress() == "1.0.0.127") {
            inst::ui::mainApp->CreateShowDialog("main.net.title"_lang, "main.net.desc"_lang, {"common.ok"_lang}, true);
            return;
        }
        mainApp->netinstPage->startNetwork();
    }

    void MainPage::shopInstallMenuItem_Click() {
        if (inst::util::getIPAddress() == "1.0.0.127") {
            inst::ui::mainApp->CreateShowDialog("main.net.title"_lang, "main.net.desc"_lang, {"common.ok"_lang}, true);
            return;
        }
        mainApp->shopinstPage->startShop();
    }

    void MainPage::usbInstallMenuItem_Click() {
        if (!inst::config::usbAck) {
            if (mainApp->CreateShowDialog("main.usb.warn.title"_lang, "main.usb.warn.desc"_lang, {"common.ok"_lang, "main.usb.warn.opt1"_lang}, false) == 1) {
                inst::config::usbAck = true;
                inst::config::setConfig();
            }
        }
        if (inst::util::usbIsConnected()) mainApp->usbinstPage->startUsb();
        else mainApp->CreateShowDialog("main.usb.error.title"_lang, "main.usb.error.desc"_lang, {"common.ok"_lang}, false);
    }

    void MainPage::hddInstallMenuItem_Click() {
        if (nx::hdd::count() && nx::hdd::rootPath()) {
            mainApp->hddinstPage->drawMenuItems(true, nx::hdd::rootPath());
            mainApp->hddinstPage->menu->SetSelectedIndex(0);
            mainApp->LoadLayout(mainApp->hddinstPage);
        } else {
            mainApp->CreateShowDialog("main.hdd.title"_lang, "main.hdd.notfound"_lang, {"common.ok"_lang}, true);
        }
    }

    void MainPage::mtpInstallMenuItem_Click() {
        int dialogResult = mainApp->CreateShowDialog("inst.mtp.target.title"_lang, "inst.mtp.target.desc"_lang, {"inst.target.opt0"_lang, "inst.target.opt1"_lang}, false);
        if (dialogResult == -1) return;

        if (!inst::mtp::StartInstallServer(dialogResult)) {
            mainApp->CreateShowDialog("inst.mtp.error.title"_lang, "inst.mtp.error.desc"_lang, {"common.ok"_lang}, true);
            return;
        }

        inst::ui::instPage::loadInstallScreen();
        inst::ui::instPage::setTopInstInfoText("inst.mtp.waiting.title"_lang);
        inst::ui::instPage::setInstInfoText("inst.mtp.waiting.desc"_lang);
    }

    void MainPage::sigPatchesMenuItem_Click() {
        sig::installSigPatches();
    }

    void MainPage::exitMenuItem_Click() {
        mainApp->FadeOut();
        mainApp->Close();
    }

    void MainPage::settingsMenuItem_Click() {
        mainApp->LoadLayout(mainApp->optionspage);
    }

    void MainPage::updateMainGridSelection() {
        if (this->selectedMainIndex < 0)
            this->selectedMainIndex = 0;
        const int maxIndex = (kMainGridCols * kMainGridRows) - 1;
        if (this->selectedMainIndex > maxIndex)
            this->selectedMainIndex = maxIndex;
        const int col = this->selectedMainIndex % kMainGridCols;
        const int row = this->selectedMainIndex / kMainGridCols;
        const int x = kMainGridStartX + (col * (kMainGridTileWidth + kMainGridGapX));
        const int y = kMainGridStartY + (row * (kMainGridTileHeight + kMainGridGapY));
        this->mainGridHighlight->SetX(x - 4);
        this->mainGridHighlight->SetY(y - 4);
    }

    int MainPage::getMainGridIndexFromTouch(int x, int y) const {
        for (int i = 0; i < (kMainGridCols * kMainGridRows); i++) {
            const int col = i % kMainGridCols;
            const int row = i / kMainGridCols;
            const int tx = kMainGridStartX + (col * (kMainGridTileWidth + kMainGridGapX));
            const int ty = kMainGridStartY + (row * (kMainGridTileHeight + kMainGridGapY));
            if (x >= tx && x <= (tx + kMainGridTileWidth) && y >= ty && y <= (ty + kMainGridTileHeight))
                return i;
        }
        return -1;
    }

    void MainPage::activateSelectedMainItem() {
        switch (this->selectedMainIndex) {
            case 0:
                this->shopInstallMenuItem_Click();
                break;
            case 1:
                this->installMenuItem_Click();
                break;
            case 2:
                this->hddInstallMenuItem_Click();
                break;
            case 3:
                this->mtpInstallMenuItem_Click();
                break;
            case 4:
                this->usbInstallMenuItem_Click();
                break;
            case 5:
                this->netInstallMenuItem_Click();
                break;
            case 6:
                this->sigPatchesMenuItem_Click();
                break;
            case 7:
                this->settingsMenuItem_Click();
                break;
            case 8:
                this->exitMenuItem_Click();
                break;
            default:
                break;
        }
    }

    void MainPage::showSelectedMainInfo() {
        std::string title;
        std::string desc;
        switch (this->selectedMainIndex) {
            case 0:
                title = "main.menu.shop"_lang;
                desc = "main.info.shop"_lang;
                break;
            case 1:
                title = "main.menu.sd"_lang;
                desc = "main.info.sd"_lang;
                break;
            case 2:
                title = "main.menu.hdd"_lang;
                desc = "main.info.hdd"_lang;
                break;
            case 3:
                title = "main.menu.mtp"_lang;
                desc = "main.info.mtp"_lang;
                break;
            case 4:
                title = "main.menu.usb"_lang;
                desc = "main.info.usb"_lang;
                break;
            case 5:
                title = "main.menu.net"_lang;
                desc = "main.info.net"_lang;
                break;
            case 6:
                title = "main.menu.sig"_lang;
                desc = "main.info.sig"_lang;
                break;
            case 7:
                title = "main.menu.set"_lang;
                desc = "main.info.set"_lang;
                break;
            case 8:
                title = "main.menu.exit"_lang;
                desc = "main.info.exit"_lang;
                break;
            default:
                return;
        }
        mainApp->CreateShowDialog(title, desc, {"common.ok"_lang}, true);
    }

    void MainPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos) {
        int bottomTapX = 0;
        if (DetectBottomHintTap(Pos, this->bottomHintTouch, 668, 52, bottomTapX)) {
            Down |= FindBottomHintButton(this->bottomHintSegments, bottomTapX);
        }
        if (((Down & HidNpadButton_Plus) || (Down & HidNpadButton_Minus) || (Down & HidNpadButton_B)) && mainApp->IsShown()) {
            mainApp->FadeOut();
            mainApp->Close();
        }
        if (Down & HidNpadButton_Y) {
            this->showSelectedMainInfo();
        }
        if (Down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) {
            if ((this->selectedMainIndex % kMainGridCols) > 0) {
                this->selectedMainIndex--;
                this->updateMainGridSelection();
            }
        }
        if (Down & (HidNpadButton_Right | HidNpadButton_StickLRight)) {
            if ((this->selectedMainIndex % kMainGridCols) < (kMainGridCols - 1) && this->selectedMainIndex < ((kMainGridCols * kMainGridRows) - 1)) {
                this->selectedMainIndex++;
                this->updateMainGridSelection();
            }
        }
        if (Down & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            if (this->selectedMainIndex >= kMainGridCols) {
                this->selectedMainIndex -= kMainGridCols;
                this->updateMainGridSelection();
            }
        }
        if (Down & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            if (this->selectedMainIndex + kMainGridCols < (kMainGridCols * kMainGridRows)) {
                this->selectedMainIndex += kMainGridCols;
                this->updateMainGridSelection();
            }
        }
        bool touchSelect = false;
        if (!Pos.IsEmpty()) {
            const int touchedIndex = this->getMainGridIndexFromTouch(Pos.X, Pos.Y);
            if (!this->touchActive && touchedIndex >= 0) {
                this->touchActive = true;
                this->touchMoved = false;
                this->touchStartX = Pos.X;
                this->touchStartY = Pos.Y;
                this->selectedMainIndex = touchedIndex;
                this->updateMainGridSelection();
            } else if (this->touchActive) {
                int dx = Pos.X - this->touchStartX;
                int dy = Pos.Y - this->touchStartY;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx > 12 || dy > 12) {
                    this->touchMoved = true;
                }
                if (touchedIndex >= 0 && touchedIndex != this->selectedMainIndex) {
                    this->selectedMainIndex = touchedIndex;
                    this->updateMainGridSelection();
                }
            }
        } else if (this->touchActive) {
            if (!this->touchMoved) {
                touchSelect = true;
            }
            this->touchActive = false;
            this->touchMoved = false;
        }

        if ((Down & HidNpadButton_A) || touchSelect)
            this->activateSelectedMainItem();
    }
}
