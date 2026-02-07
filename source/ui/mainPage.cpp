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
        this->butText = TextBlock::New(10, 678, "main.buttons"_lang, 20);
        this->butText->SetColor(COLOR("#FFFFFFFF"));
        this->bottomHintSegments = BuildBottomHintSegments("main.buttons"_lang, 10, 20);
        this->optionMenu = pu::ui::elm::Menu::New(0, 95, 1280, COLOR("#67000000"), 60, 9);
        if (inst::config::oledMode) {
            this->optionMenu->SetOnFocusColor(COLOR("#FFFFFF33"));
            this->optionMenu->SetScrollbarColor(COLOR("#FFFFFF66"));
        } else {
            this->optionMenu->SetOnFocusColor(COLOR("#00000033"));
            this->optionMenu->SetScrollbarColor(COLOR("#170909FF"));
        }
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
        if (std::filesystem::exists(inst::config::appDir + "/awoo_main.png")) this->awooImage = Image::New(410, 190, inst::config::appDir + "/awoo_main.png");
        else this->awooImage = Image::New(410, 190, "romfs:/images/awoos/5bbdbcf9a5625cd307c9e9bc360d78bd.png");
        if (std::filesystem::exists(inst::config::appDir + "/awoo_alter.png")) this->boobsImage = Image::New(410, 190, inst::config::appDir + "/awoo_alter.png");
        else this->boobsImage = Image::New(410, 190, "romfs:/images/awoos/7d5d18b92253bc63e61c7cbc88fe3092.png");
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
        this->optionMenu->AddItem(this->shopInstallMenuItem);
        this->optionMenu->AddItem(this->installMenuItem);
        this->optionMenu->AddItem(this->hddInstallMenuItem);
        this->optionMenu->AddItem(this->mtpInstallMenuItem);
        this->optionMenu->AddItem(this->usbInstallMenuItem);
        this->optionMenu->AddItem(this->netInstallMenuItem);
        this->optionMenu->AddItem(this->sigPatchesMenuItem);
        this->optionMenu->AddItem(this->settingsMenuItem);
        this->optionMenu->AddItem(this->exitMenuItem);
        this->Add(this->optionMenu);
        this->Add(this->awooImage);
        this->Add(this->boobsImage);
        this->awooImage->SetVisible(!inst::config::gayMode);
        this->boobsImage->SetVisible(false);
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

    void MainPage::onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos) {
        int bottomTapX = 0;
        if (DetectBottomHintTap(Pos, this->bottomHintTouch, 668, 52, bottomTapX)) {
            Down |= FindBottomHintButton(this->bottomHintSegments, bottomTapX);
        }
        if (((Down & HidNpadButton_Plus) || (Down & HidNpadButton_Minus) || (Down & HidNpadButton_B)) && mainApp->IsShown()) {
            mainApp->FadeOut();
            mainApp->Close();
        }
        bool touchSelect = false;
        if (!Pos.IsEmpty()) {
            const int menuX = this->optionMenu->GetProcessedX();
            const int menuY = this->optionMenu->GetProcessedY();
            const int menuW = this->optionMenu->GetWidth();
            const int menuH = this->optionMenu->GetHeight();
            const bool inMenu = (Pos.X >= menuX) && (Pos.X <= (menuX + menuW)) && (Pos.Y >= menuY) && (Pos.Y <= (menuY + menuH));
            if (!this->touchActive && inMenu) {
                this->touchActive = true;
                this->touchMoved = false;
                this->touchStartX = Pos.X;
                this->touchStartY = Pos.Y;
            } else if (this->touchActive) {
                int dx = Pos.X - this->touchStartX;
                int dy = Pos.Y - this->touchStartY;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx > 12 || dy > 12) {
                    this->touchMoved = true;
                }
            }
        } else if (this->touchActive) {
            if (!this->touchMoved) {
                touchSelect = true;
            }
            this->touchActive = false;
            this->touchMoved = false;
        }

        if ((Down & HidNpadButton_A) || touchSelect) {
            switch (this->optionMenu->GetSelectedIndex()) {
                case 0:
                    this->shopInstallMenuItem_Click();
                    break;
                case 1:
                    this->installMenuItem_Click();
                    break;
                case 2:
                    MainPage::hddInstallMenuItem_Click();
                    break;
                case 3:
                    MainPage::mtpInstallMenuItem_Click();
                    break;
                case 4:
                    MainPage::usbInstallMenuItem_Click();
                    break;
                case 5:
                    this->netInstallMenuItem_Click();
                    break;
                case 6:
                    MainPage::sigPatchesMenuItem_Click();
                    break;
                case 7:
                    MainPage::settingsMenuItem_Click();
                    break;
                case 8:
                    MainPage::exitMenuItem_Click();
                    break;
                default:
                    break;
            }
        }

        if (!inst::config::gayMode) {
            if (Down & HidNpadButton_X) {
                this->awooImage->SetVisible(false);
                this->boobsImage->SetVisible(true);
            } else if (Up & HidNpadButton_X) {
                this->awooImage->SetVisible(true);
                this->boobsImage->SetVisible(false);
            }
        }
    }
}
