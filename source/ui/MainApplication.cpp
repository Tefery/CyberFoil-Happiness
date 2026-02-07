#include "ui/MainApplication.hpp"
#include "util/lang.hpp"
#include "util/config.hpp"
#include "util/util.hpp"
#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <thread>
#include "mtp_install.hpp"
#include "mtp_server.hpp"
#include "switch.h"

#define COLOR(hex) pu::ui::Color::FromHex(hex)

namespace inst::ui {
    MainApplication *mainApp;

    void MainApplication::OnLoad() {
        mainApp = this;

        Language::Load();

        this->mainPage = MainPage::New();
        this->netinstPage = netInstPage::New();
        this->shopinstPage = shopInstPage::New();
        this->sdinstPage = sdInstPage::New();
        this->usbinstPage = usbInstPage::New();
        this->hddinstPage = hddInstPage::New();
        this->instpage = instPage::New();
        this->optionspage = optionsPage::New();
        this->mainPage->SetOnInput(std::bind(&MainPage::onInput, this->mainPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->netinstPage->SetOnInput(std::bind(&netInstPage::onInput, this->netinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->shopinstPage->SetOnInput(std::bind(&shopInstPage::onInput, this->shopinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->sdinstPage->SetOnInput(std::bind(&sdInstPage::onInput, this->sdinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->usbinstPage->SetOnInput(std::bind(&usbInstPage::onInput, this->usbinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->hddinstPage->SetOnInput(std::bind(&hddInstPage::onInput, this->hddinstPage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->instpage->SetOnInput(std::bind(&instPage::onInput, this->instpage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->optionspage->SetOnInput(std::bind(&optionsPage::onInput, this->optionspage, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->LoadLayout(this->mainPage);

        this->AddThread([this]() {
            static AppletFocusState last_focus = AppletFocusState_InFocus;
            static u64 last_check_tick = 0;
            const u64 now = armGetSystemTick();
            const u64 freq = armGetSystemTickFreq();
            if (last_check_tick != 0 && (now - last_check_tick) < (freq / 2))
                return;
            last_check_tick = now;

            AppletFocusState focus = appletGetFocusState();
            if (focus != last_focus && focus == AppletFocusState_InFocus) {
                padConfigureInput(1, HidNpadStyleSet_NpadStandard);
                padInitializeDefault(&this->input_pad);
            }
            last_focus = focus;

            if (focus == AppletFocusState_InFocus && !padIsConnected(&this->input_pad)) {
                padConfigureInput(1, HidNpadStyleSet_NpadStandard);
                padInitializeDefault(&this->input_pad);
            }
        });

        this->AddThread([this]() {
            static bool last_active = false;
            static bool last_server_running = false;
            static std::string last_name;
            static bool icon_set = false;
            static bool complete_notified = false;
            static auto last_time = std::chrono::steady_clock::now();
            static std::uint64_t last_bytes = 0;
            static double ema_rate = 0.0;

            const bool active = inst::mtp::IsStreamInstallActive();
            const bool server_running = inst::mtp::IsInstallServerRunning();

            if (server_running && !active && !last_server_running) {
                this->LoadLayout(this->instpage);
                this->instpage->pageInfoText->SetText("inst.mtp.waiting.title"_lang);
                this->instpage->installInfoText->SetText("inst.mtp.waiting.desc"_lang + std::string("\n\n") + "inst.mtp.waiting.hint"_lang);
                this->instpage->installBar->SetVisible(false);
                this->instpage->installBar->SetProgress(0);
                this->instpage->installIconImage->SetVisible(false);
                this->instpage->awooImage->SetVisible(!inst::config::gayMode);
                this->instpage->hintText->SetVisible(true);
                this->instpage->progressText->SetVisible(false);
                icon_set = false;
            }

            if (active && !last_active) {
                last_name = inst::mtp::GetStreamInstallName();
                if (last_name.empty()) {
                    last_name = "MTP Install";
                }
                complete_notified = false;
                last_time = std::chrono::steady_clock::now();
                last_bytes = 0;
                ema_rate = 0.0;
                this->LoadLayout(this->instpage);
                this->instpage->pageInfoText->SetText("inst.info_page.top_info0"_lang + last_name + " (MTP)");
                this->instpage->installInfoText->SetText("inst.info_page.preparing"_lang);
                this->instpage->installBar->SetVisible(true);
                this->instpage->installBar->SetProgress(0);
                this->instpage->installIconImage->SetVisible(false);
                this->instpage->awooImage->SetVisible(!inst::config::gayMode);
                this->instpage->hintText->SetVisible(true);
                this->instpage->progressText->SetVisible(true);
                icon_set = false;
            }

            if (active) {
                std::uint64_t received = 0;
                std::uint64_t total = 0;
                inst::mtp::GetStreamInstallProgress(&received, &total);
                if (total > 0) {
                    const double percent = (double)received / (double)total * 100.0;
                    this->instpage->installBar->SetVisible(true);
                    this->instpage->installBar->SetProgress(percent);
                    this->instpage->installInfoText->SetText("inst.info_page.downloading"_lang + last_name);

                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
                    if (elapsed >= 1000) {
                        const auto delta = received - last_bytes;
                        const double rate = (elapsed > 0) ? (double)delta / ((double)elapsed / 1000.0) : 0.0;
                        if (rate > 0.0) {
                            if (ema_rate <= 0.0) {
                                ema_rate = rate;
                            } else {
                                ema_rate = (ema_rate * 0.7) + (rate * 0.3);
                            }
                        }
                        last_bytes = received;
                        last_time = now;
                    }

                    std::string eta_text = "Calculating...";
                    if (ema_rate > 0.0 && received < total) {
                        const auto remaining = total - received;
                        const auto seconds = static_cast<std::uint64_t>(remaining / ema_rate);
                        const auto h = seconds / 3600;
                        const auto m = (seconds % 3600) / 60;
                        const auto s = seconds % 60;
                        if (h > 0) {
                            eta_text = std::to_string(h) + ":" + (m < 10 ? "0" : "") + std::to_string(m) + ":" + (s < 10 ? "0" : "") + std::to_string(s);
                        } else {
                            eta_text = std::to_string(m) + ":" + (s < 10 ? "0" : "") + std::to_string(s);
                        }
                        eta_text = eta_text + " remaining";
                    }

                    std::string speed_text;
                    if (ema_rate > 0.0) {
                        const double mbps = ema_rate / (1024.0 * 1024.0);
                        const double rounded = std::round(mbps * 10.0) / 10.0;
                        speed_text = std::to_string(rounded);
                        if (speed_text.find('.') != std::string::npos) {
                            while (!speed_text.empty() && speed_text.back() == '0') speed_text.pop_back();
                            if (!speed_text.empty() && speed_text.back() == '.') speed_text.pop_back();
                        }
                        speed_text += " MB/s";
                    } else {
                        speed_text = "-- MB/s";
                    }

                    std::string format_text;
                    const auto dot = last_name.find_last_of('.');
                    if (dot != std::string::npos) {
                        format_text = last_name.substr(dot + 1);
                        std::transform(format_text.begin(), format_text.end(), format_text.begin(), ::toupper);
                    }

                    const int pct = static_cast<int>(percent + 0.5);
                    std::string progress_text = std::to_string(pct) + "% • " + eta_text + " • " + speed_text;
                    if (!format_text.empty()) {
                        progress_text += " • " + format_text;
                    }
                    this->instpage->progressText->SetText(progress_text);
                    this->instpage->progressText->SetX((1280 - this->instpage->progressText->GetTextWidth()) / 2);
                    this->instpage->progressText->SetVisible(true);
                }

                if (!icon_set) {
                    std::uint64_t title_id = 0;
                    if (inst::mtp::GetStreamInstallTitleId(&title_id) && title_id != 0) {
                        NsApplicationControlData appControlData{};
                        size_t sizeRead = 0;
                        Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, title_id, &appControlData, sizeof(NsApplicationControlData), &sizeRead);
                        if (R_SUCCEEDED(rc) && sizeRead > sizeof(appControlData.nacp)) {
                            const size_t iconSize = sizeRead - sizeof(appControlData.nacp);
                            if (iconSize > 0) {
                                this->instpage->installIconImage->SetJpegImage(appControlData.icon, iconSize);
                                this->instpage->installIconImage->SetVisible(true);
                                this->instpage->awooImage->SetVisible(false);
                                icon_set = true;
                            }
                        }
                    }
                }
            }

            if (inst::mtp::ConsumeStreamInstallComplete()) {
                this->instpage->installBar->SetVisible(true);
                this->instpage->installBar->SetProgress(100);
                this->instpage->installInfoText->SetText("inst.info_page.complete"_lang + std::string("\n\n") + "inst.mtp.waiting.hint"_lang);
                this->instpage->hintText->SetVisible(true);
                this->instpage->progressText->SetText("100% • done");
                this->instpage->progressText->SetX((1280 - this->instpage->progressText->GetTextWidth()) / 2);
                this->instpage->progressText->SetVisible(true);
                if (!complete_notified) {
                    std::string audioPath = "romfs:/audio/success.wav";
                    if (!inst::config::soundEnabled) audioPath = "";
                    if (std::filesystem::exists(inst::config::appDir + "/success.wav")) {
                        audioPath = inst::config::appDir + "/success.wav";
                    }
                    std::thread audioThread(inst::util::playAudio, audioPath);
                    this->CreateShowDialog(last_name + "inst.info_page.desc1"_lang, Language::GetRandomMsg(), {"common.ok"_lang}, true);
                    audioThread.join();
                    complete_notified = true;
                }
            }


            if (!server_running && last_server_running) {
                this->instpage->hintText->SetVisible(false);
            }

            last_active = active;
            last_server_running = server_running;
        });

        this->AddThread([this]() {
            static u64 lastTick = 0;
            const u64 now = armGetSystemTick();
            const u64 freq = armGetSystemTickFreq();
            if (lastTick != 0 && (now - lastTick) < freq)
                return;
            lastTick = now;

            std::string timeText = "--:--";
            if (R_SUCCEEDED(timeInitialize())) {
                u64 posix = 0;
                if (R_SUCCEEDED(timeGetCurrentTime(TimeType_LocalSystemClock, &posix))) {
                    std::time_t t = static_cast<std::time_t>(posix);
                    std::tm* local = std::localtime(&t);
                    char buf[16] = {0};
                    if (local && std::strftime(buf, sizeof(buf), "%I:%M %p", local) > 0)
                        timeText = buf;
                }
                timeExit();
            }

            bool internetUp = false;
            bool wifiConnected = false;
            u32 wifiStrength = 0;
            if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
                NifmInternetConnectionStatus status = static_cast<NifmInternetConnectionStatus>(0);
                NifmInternetConnectionType type = static_cast<NifmInternetConnectionType>(0);
                if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &wifiStrength, &status))) {
                    internetUp = (status == NifmInternetConnectionStatus_Connected);
                    wifiConnected = internetUp && (type == NifmInternetConnectionType_WiFi);
                }
                nifmExit();
            }

            int batteryPct = -1;
            if (R_SUCCEEDED(psmInitialize())) {
                u32 pct = 0;
                if (R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))) {
                    batteryPct = static_cast<int>(pct);
                }
                psmExit();
            }

            s64 systemTotal = 0;
            s64 systemFree = 0;
            s64 sdTotalBytes = 0;
            s64 sdFreeBytes = 0;
            if (R_SUCCEEDED(ncmInitialize())) {
                NcmContentStorage storage{};
                Result rc = ncmOpenContentStorage(&storage, NcmStorageId_BuiltInUser);
                if (R_SUCCEEDED(rc)) {
                    ncmContentStorageGetTotalSpaceSize(&storage, &systemTotal);
                    ncmContentStorageGetFreeSpaceSize(&storage, &systemFree);
                    ncmContentStorageClose(&storage);
                }
                rc = ncmOpenContentStorage(&storage, NcmStorageId_SdCard);
                if (R_SUCCEEDED(rc)) {
                    ncmContentStorageGetTotalSpaceSize(&storage, &sdTotalBytes);
                    ncmContentStorageGetFreeSpaceSize(&storage, &sdFreeBytes);
                    ncmContentStorageClose(&storage);
                }
                ncmExit();
            }

            const int cardWidth = 180;
            const int cardGap = 12;
            const int timeY = 44;
            const int iconY = 70;
            const int cardsTopY = 44;
            const int barY = 62;
            const int freeY = 72;
            const int netSize = 6;
            const int wifiBarW = 4;
            const int wifiBarGap = 2;
            const int wifiWidth = (wifiBarW * 3) + (wifiBarGap * 2);
            const int wifiMaxH = 10;
            const int batteryW = 24;
            const int batteryH = 12;
            const int batteryCapW = 3;
            const int iconGap = 6;
            const int iconsWidth = netSize + iconGap + wifiWidth + iconGap + batteryW + batteryCapW;
            auto applyStatus = [&](TextBlock::Ref timeBlock,
                                   TextBlock::Ref ipText,
                                   TextBlock::Ref sysLabel,
                                   TextBlock::Ref sysFree,
                                   Rectangle::Ref sysBarBack,
                                   Rectangle::Ref sysBarFill,
                                   TextBlock::Ref sdLabel,
                                   TextBlock::Ref sdFree,
                                   Rectangle::Ref sdBarBack,
                                   Rectangle::Ref sdBarFill,
                                   Rectangle::Ref netIndicator,
                                   Rectangle::Ref wifiBar1,
                                   Rectangle::Ref wifiBar2,
                                   Rectangle::Ref wifiBar3,
                                   Rectangle::Ref batteryOutline,
                                   Rectangle::Ref batteryFill,
                                   Rectangle::Ref batteryCap) {
                int right = 1280 - 10;
                int cardsRight = right;
                int timeX = right;
                int timeW = 0;
                if (timeBlock) {
                    timeBlock->SetText(timeText);
                    timeBlock->SetY(timeY);
                    timeW = timeBlock->GetTextWidth();
                    timeX = right - timeW;
                    timeBlock->SetX(timeX);
                    cardsRight = timeX - cardGap;
                }

                int iconsX = right - iconsWidth;
                if (timeW > 0) {
                    iconsX = timeX + (timeW - iconsWidth) / 2;
                    if (iconsX < 0)
                        iconsX = 0;
                }
                int netX = iconsX;
                int wifiX = netX + netSize + iconGap;
                int batteryX = wifiX + wifiWidth + iconGap;

                if (netIndicator) {
                    netIndicator->SetX(netX);
                    netIndicator->SetY(iconY + 2);
                    netIndicator->SetColor(internetUp ? COLOR("#4CD964FF") : COLOR("#FF3B30FF"));
                }

                if (wifiBar1 && wifiBar2 && wifiBar3) {
                    const int wifiBaseY = iconY + wifiMaxH;
                    wifiBar1->SetX(wifiX);
                    wifiBar1->SetY(wifiBaseY - 4);
                    wifiBar2->SetX(wifiX + wifiBarW + wifiBarGap);
                    wifiBar2->SetY(wifiBaseY - 7);
                    wifiBar3->SetX(wifiX + (wifiBarW + wifiBarGap) * 2);
                    wifiBar3->SetY(wifiBaseY - 10);
                    pu::ui::Color onColor = COLOR("#FFFFFFFF");
                    pu::ui::Color offColor = COLOR("#FFFFFF55");
                    const bool showWifi = wifiConnected && wifiStrength > 0;
                    wifiBar1->SetColor((showWifi && wifiStrength >= 1) ? onColor : offColor);
                    wifiBar2->SetColor((showWifi && wifiStrength >= 2) ? onColor : offColor);
                    wifiBar3->SetColor((showWifi && wifiStrength >= 3) ? onColor : offColor);
                    wifiBar1->SetVisible(true);
                    wifiBar2->SetVisible(true);
                    wifiBar3->SetVisible(true);
                }

                if (batteryOutline && batteryFill && batteryCap) {
                    batteryOutline->SetX(batteryX);
                    batteryOutline->SetY(iconY);
                    batteryOutline->SetWidth(batteryW);
                    batteryOutline->SetHeight(batteryH);
                    batteryCap->SetX(batteryX + batteryW + 1);
                    batteryCap->SetY(iconY + 3);
                    batteryCap->SetWidth(batteryCapW);
                    batteryCap->SetHeight(6);

                    int fillWidth = 0;
                    if (batteryPct >= 0) {
                        double ratio = static_cast<double>(batteryPct) / 100.0;
                        if (ratio < 0.0) ratio = 0.0;
                        if (ratio > 1.0) ratio = 1.0;
                        fillWidth = static_cast<int>((batteryW - 2) * ratio);
                        if (fillWidth < 2 && ratio > 0.0) fillWidth = 2;
                    }
                    batteryFill->SetX(batteryX + 1);
                    batteryFill->SetY(iconY + 1);
                    batteryFill->SetWidth(fillWidth);
                    batteryFill->SetHeight(batteryH - 2);
                    batteryFill->SetColor((batteryPct >= 0 && batteryPct <= 20) ? COLOR("#FF3B30FF") : COLOR("#4CD964FF"));
                }

                int sdX = cardsRight - cardWidth;
                int sysX = sdX - cardGap - cardWidth;

                if (ipText) {
                    std::string ipAddress = inst::util::getIPAddress();
                    if (ipAddress == "1.0.0.127") ipAddress = "--";
                    ipText->SetText("IP: " + ipAddress);
                    int ipWidth = ipText->GetTextWidth();
                    int ipX = sysX - cardGap - ipWidth;
                    if (ipX < 10) ipX = 10;
                    ipText->SetX(ipX);
                    ipText->SetY(cardsTopY);
                }

                if (sysLabel) {
                    sysLabel->SetX(sysX);
                    sysLabel->SetY(cardsTopY);
                }
                if (sysFree) {
                    sysFree->SetX(sysX);
                    sysFree->SetY(freeY);
                }
                if (sysBarBack) {
                    sysBarBack->SetX(sysX);
                    sysBarBack->SetY(barY);
                }
                if (sysBarFill) {
                    sysBarFill->SetX(sysX);
                    sysBarFill->SetY(barY);
                }

                if (sdLabel) {
                    sdLabel->SetX(sdX);
                    sdLabel->SetY(cardsTopY);
                }
                if (sdFree) {
                    sdFree->SetX(sdX);
                    sdFree->SetY(freeY);
                }
                if (sdBarBack) {
                    sdBarBack->SetX(sdX);
                    sdBarBack->SetY(barY);
                }
                if (sdBarFill) {
                    sdBarFill->SetX(sdX);
                    sdBarFill->SetY(barY);
                }
            };

            auto updateCards = [&](TextBlock::Ref sysLabel,
                                   TextBlock::Ref sysFree,
                                   Rectangle::Ref sysBarBack,
                                   Rectangle::Ref sysBarFill,
                                   TextBlock::Ref sdLabel,
                                   TextBlock::Ref sdFree,
                                   Rectangle::Ref sdBarBack,
                                   Rectangle::Ref sdBarFill) {
                if (sysLabel) sysLabel->SetText("System Memory");
                if (sdLabel) sdLabel->SetText("microSD Card");

                auto setCard = [&](TextBlock::Ref freeText, Rectangle::Ref barFill, s64 freeBytes, s64 totalBytes) {
                    if (freeText) {
                        char buf[64] = {0};
                        if (totalBytes > 0) {
                            double freeGb = static_cast<double>(freeBytes) / (1024.0 * 1024.0 * 1024.0);
                            std::snprintf(buf, sizeof(buf), "Free Space %.1f GB", freeGb);
                        } else {
                            std::snprintf(buf, sizeof(buf), "Free Space --");
                        }
                        freeText->SetText(buf);
                    }
                    if (barFill) {
                        int width = 0;
                        if (totalBytes > 0) {
                            double usedBytes = static_cast<double>(totalBytes - freeBytes);
                            if (usedBytes < 0.0) usedBytes = 0.0;
                            double ratio = usedBytes / static_cast<double>(totalBytes);
                            if (ratio < 0.0) ratio = 0.0;
                            if (ratio > 1.0) ratio = 1.0;
                            width = static_cast<int>(cardWidth * ratio);
                            if (width < 2 && ratio > 0.0) width = 2;
                        }
                        barFill->SetWidth(width);
                    }
                };

                setCard(sysFree, sysBarFill, systemFree, systemTotal);
                setCard(sdFree, sdBarFill, sdFreeBytes, sdTotalBytes);
            };

            auto applyAll = [&](auto& page) {
                applyStatus(page->timeText, page->ipText, page->sysLabelText, page->sysFreeText, page->sysBarBack, page->sysBarFill,
                            page->sdLabelText, page->sdFreeText, page->sdBarBack, page->sdBarFill,
                            page->netIndicator, page->wifiBar1, page->wifiBar2, page->wifiBar3,
                            page->batteryOutline, page->batteryFill, page->batteryCap);
                updateCards(page->sysLabelText, page->sysFreeText, page->sysBarBack, page->sysBarFill,
                            page->sdLabelText, page->sdFreeText, page->sdBarBack, page->sdBarFill);
            };

            applyAll(this->mainPage);
            applyAll(this->netinstPage);
            applyAll(this->shopinstPage);
            applyAll(this->sdinstPage);
            applyAll(this->usbinstPage);
            applyAll(this->hddinstPage);
            applyAll(this->instpage);
            applyAll(this->optionspage);
        });
    }
}
