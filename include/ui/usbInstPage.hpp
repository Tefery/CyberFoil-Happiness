#pragma once
#include <chrono>
#include <pu/Plutonium>
#include "ui/bottomHint.hpp"

using namespace pu::ui::elm;
namespace inst::ui {
    class usbInstPage : public pu::ui::Layout
    {
        public:
            usbInstPage();
            PU_SMART_CTOR(usbInstPage)
            void startInstall();
            void startUsb();
            void onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos);
            TextBlock::Ref pageInfoText;
            Image::Ref titleImage;
            TextBlock::Ref appVersionText;
            TextBlock::Ref timeText;
            TextBlock::Ref ipText;
            TextBlock::Ref sysLabelText;
            TextBlock::Ref sysFreeText;
            TextBlock::Ref sdLabelText;
            TextBlock::Ref sdFreeText;
            Rectangle::Ref sysBarBack;
            Rectangle::Ref sysBarFill;
            Rectangle::Ref sdBarBack;
            Rectangle::Ref sdBarFill;
            Rectangle::Ref netIndicator;
            Rectangle::Ref wifiBar1;
            Rectangle::Ref wifiBar2;
            Rectangle::Ref wifiBar3;
            Rectangle::Ref batteryOutline;
            Rectangle::Ref batteryFill;
            Rectangle::Ref batteryCap;
        private:
            std::vector<std::string> ourTitles;
            std::vector<std::string> selectedTitles;
            std::string lastUrl;
            std::string lastFileID;
            BottomHintTouchState bottomHintTouch;
            std::vector<BottomHintSegment> bottomHintSegments;
            TextBlock::Ref butText;
            Rectangle::Ref topRect;
            Rectangle::Ref infoRect;
            Rectangle::Ref botRect;
            bool touchTapActive = false;
            bool touchTapMoved = false;
            int touchTapStartX = 0;
            int touchTapStartY = 0;
            bool hasLastTap = false;
            int lastTapIndex = -1;
            std::chrono::steady_clock::time_point lastTapTime{};
            pu::ui::elm::Menu::Ref menu;
            Image::Ref infoImage;
            void setButtonsText(const std::string& text);
            void drawMenuItems(bool clearItems);
            void selectTitle(int selectedIndex);
    };
} 
