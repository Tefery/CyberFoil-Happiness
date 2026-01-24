#pragma once

#include <pu/Plutonium>
#include "shopInstall.hpp"

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
        private:
            std::vector<shopInstStuff::ShopSection> shopSections;
            std::vector<shopInstStuff::ShopItem> selectedItems;
            std::vector<shopInstStuff::ShopItem> visibleItems;
            std::vector<shopInstStuff::ShopItem> availableUpdates;
            int selectedSectionIndex = 0;
            std::string searchQuery;
            std::string previewKey;
            bool debugVisible = false;
            int gridSelectedIndex = 0;
            int gridPage = -1;
            TextBlock::Ref butText;
            Rectangle::Ref topRect;
            Rectangle::Ref infoRect;
            Rectangle::Ref botRect;
            pu::ui::elm::Menu::Ref menu;
            Image::Ref infoImage;
            Image::Ref previewImage;
            Rectangle::Ref gridHighlight;
            std::vector<Image::Ref> gridImages;
            TextBlock::Ref gridTitleText;
            TextBlock::Ref debugText;
            void drawMenuItems(bool clearItems);
            void selectTitle(int selectedIndex);
            void updateRememberedSelection();
            void updateSectionText();
            void updateButtonsText();
            void buildInstalledSection();
            void cacheAvailableUpdates();
            void filterOwnedSections();
            void updatePreview();
            void updateInstalledGrid();
            void updateDebug();
            const std::vector<shopInstStuff::ShopItem>& getCurrentItems() const;
            bool isAllSection() const;
            bool isInstalledSection() const;
            void showInstalledDetails();
    };
}
