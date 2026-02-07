#pragma once
#include <filesystem>
#include <pu/Plutonium>
#include "ui/bottomHint.hpp"

using namespace pu::ui::elm;
namespace inst::ui {
    class sdInstPage : public pu::ui::Layout
    {
        public:
            sdInstPage();
            PU_SMART_CTOR(sdInstPage)
            pu::ui::elm::Menu::Ref menu;
            void startInstall();
            void onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos);
            TextBlock::Ref pageInfoText;
            void drawMenuItems(bool clearItems, std::filesystem::path ourPath);
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
            std::vector<std::filesystem::path> ourDirectories;
            std::vector<std::filesystem::path> ourFiles;
            std::vector<std::filesystem::path> selectedTitles;
            std::filesystem::path currentDir;
            BottomHintTouchState bottomHintTouch;
            std::vector<BottomHintSegment> bottomHintSegments;
            TextBlock::Ref butText;
            Rectangle::Ref topRect;
            Rectangle::Ref infoRect;
            Rectangle::Ref botRect;
            void followDirectory();
            void selectNsp(int selectedIndex);
    };
}
