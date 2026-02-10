#pragma once
#include <pu/Plutonium>
#include "ui/bottomHint.hpp"

using namespace pu::ui::elm;
namespace inst::ui {
    class MainPage : public pu::ui::Layout
    {
        public:
            MainPage();
            PU_SMART_CTOR(MainPage)
            void installMenuItem_Click();
            void netInstallMenuItem_Click();
            void shopInstallMenuItem_Click();
            void usbInstallMenuItem_Click();
            void hddInstallMenuItem_Click();
            void mtpInstallMenuItem_Click();
            void sigPatchesMenuItem_Click();
            void settingsMenuItem_Click();
            void exitMenuItem_Click();
            void onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos);
            Image::Ref awooImage;
            Image::Ref titleImage;
            Image::Ref boobsImage;
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
            bool appletFinished;
            bool updateFinished;
            bool touchActive = false;
            bool touchMoved = false;
            int touchStartX = 0;
            int touchStartY = 0;
            BottomHintTouchState bottomHintTouch;
            std::vector<BottomHintSegment> bottomHintSegments;
            TextBlock::Ref butText;
            Rectangle::Ref topRect;
            Rectangle::Ref botRect;
            pu::ui::elm::Menu::Ref optionMenu;
            pu::ui::elm::MenuItem::Ref installMenuItem;
            pu::ui::elm::MenuItem::Ref netInstallMenuItem;
            pu::ui::elm::MenuItem::Ref shopInstallMenuItem;
            pu::ui::elm::MenuItem::Ref usbInstallMenuItem;
            pu::ui::elm::MenuItem::Ref hddInstallMenuItem;
            pu::ui::elm::MenuItem::Ref mtpInstallMenuItem;
            pu::ui::elm::MenuItem::Ref sigPatchesMenuItem;
            pu::ui::elm::MenuItem::Ref settingsMenuItem;
            pu::ui::elm::MenuItem::Ref exitMenuItem;
            std::vector<Rectangle::Ref> mainGridTiles;
            std::vector<Image::Ref> mainGridIcons;
            std::vector<TextBlock::Ref> mainGridLabels;
            Rectangle::Ref mainGridHighlight;
            int selectedMainIndex = 0;
            void updateMainGridSelection();
            int getMainGridIndexFromTouch(int x, int y) const;
            void activateSelectedMainItem();
            void showSelectedMainInfo();
    };
}
