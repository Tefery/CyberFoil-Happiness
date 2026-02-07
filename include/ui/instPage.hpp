#pragma once
#include <pu/Plutonium>
#include "ui/bottomHint.hpp"

using namespace pu::ui::elm;
namespace inst::ui {
    class instPage : public pu::ui::Layout
    {
        public:
            instPage();
            PU_SMART_CTOR(instPage)
            void onInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos);
            TextBlock::Ref pageInfoText;
            TextBlock::Ref installInfoText;
            pu::ui::elm::ProgressBar::Ref installBar;
            Image::Ref awooImage;
            Image::Ref installIconImage;
            Image::Ref titleImage;
            TextBlock::Ref appVersionText;
            TextBlock::Ref hintText;
            TextBlock::Ref progressText;
            TextBlock::Ref progressDetailText;
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
            static void setTopInstInfoText(std::string ourText);
            static void setInstInfoText(std::string ourText);
            static void setInstBarPerc(double ourPercent);
            static void setProgressDetailText(const std::string& ourText);
            static void clearProgressDetailText();
            static void setInstallIconFromTitleId(u64 titleId);
            static void setInstallIcon(const std::string& imagePath);
            static void clearInstallIcon();
            static void loadMainMenu();
            static void loadInstallScreen();
        private:
            Rectangle::Ref infoRect;
            Rectangle::Ref topRect;
            Rectangle::Ref botRect;
            BottomHintTouchState bottomHintTouch;
            std::vector<BottomHintSegment> bottomHintSegments;
    };
}
