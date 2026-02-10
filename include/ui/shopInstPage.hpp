#pragma once

#include <pu/Plutonium>
#include "shopInstall.hpp"
#include "ui/bottomHint.hpp"

using namespace pu::ui::elm;
namespace inst::ui {
    class shopInstPage : public pu::ui::Layout
    {
        public:
            shopInstPage();
            PU_SMART_CTOR(shopInstPage)
            void startShop(bool forceRefresh = false);
            void startInstall();
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
            std::vector<shopInstStuff::ShopSection> shopSections;
            std::vector<shopInstStuff::ShopItem> selectedItems;
            std::vector<shopInstStuff::ShopItem> visibleItems;
            std::vector<shopInstStuff::ShopItem> availableUpdates;
            BottomHintTouchState bottomHintTouch;
            std::vector<BottomHintSegment> bottomHintSegments;
            int selectedSectionIndex = 0;
            std::string searchQuery;
            std::string previewKey;
            bool debugVisible = false;
            int gridSelectedIndex = 0;
            int gridPage = -1;
            bool shopGridMode = false;
            int shopGridIndex = 0;
            int shopGridPage = -1;
            int gridHoldDirX = 0;
            int gridHoldDirY = 0;
            u64 gridHoldStartTick = 0;
            u64 gridHoldLastTick = 0;
            int holdDirection = 0;
            u64 holdStartTick = 0;
            u64 lastHoldTick = 0;
            bool touchActive = false;
            bool touchMoved = false;
            u64 imageLoadingUntilTick = 0;
            TextBlock::Ref butText;
            Rectangle::Ref topRect;
            Rectangle::Ref infoRect;
            Rectangle::Ref botRect;
            pu::ui::elm::Menu::Ref menu;
            Image::Ref infoImage;
            Image::Ref previewImage;
            Rectangle::Ref gridHighlight;
            std::vector<Image::Ref> gridImages;
            std::vector<Rectangle::Ref> shopGridSelectHighlights;
            std::vector<Image::Ref> shopGridSelectIcons;
            TextBlock::Ref gridTitleText;
            TextBlock::Ref imageLoadingText;
            TextBlock::Ref debugText;
            TextBlock::Ref emptySectionText;
            TextBlock::Ref searchInfoText;
            void centerPageInfoText();
            void drawMenuItems(bool clearItems);
            void selectTitle(int selectedIndex);
            void updateRememberedSelection();
            void updateSectionText();
            void updateButtonsText();
            void setButtonsText(const std::string& text);
            void buildInstalledSection();
            void cacheAvailableUpdates();
            void filterOwnedSections();
            void updatePreview();
            void updateInstalledGrid();
            void updateShopGrid();
            void updateDebug();
            const std::vector<shopInstStuff::ShopItem>& getCurrentItems() const;
            bool isAllSection() const;
            bool isInstalledSection() const;
            void showInstalledDetails();
    };
}
