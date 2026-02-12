#pragma once

#include <pu/Plutonium>
#include "shopInstall.hpp"
#include "ui/bottomHint.hpp"
#include "util/save_sync.hpp"
#include <vector>

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
            std::vector<inst::save_sync::SaveSyncEntry> saveSyncEntries;
            bool nativeUpdatesSectionPresent = false;
            bool nativeDlcSectionPresent = false;
            bool saveSyncEnabled = false;
            std::string activeShopUrl;
            BottomHintTouchState bottomHintTouch;
            std::vector<BottomHintSegment> bottomHintSegments;
            int selectedSectionIndex = 0;
            std::string searchQuery;
            std::string previewKey;
            bool debugVisible = false;
            bool descriptionVisible = false;
            bool descriptionOverlayVisible = false;
            std::vector<std::string> descriptionOverlayLines;
            int descriptionOverlayOffset = 0;
            int descriptionOverlayVisibleLines = 16;
            bool saveVersionSelectorVisible = false;
            std::uint64_t saveVersionSelectorTitleId = 0;
            bool saveVersionSelectorLocalAvailable = false;
            bool saveVersionSelectorDeleteMode = false;
            int saveVersionSelectorPreviousSectionIndex = 0;
            std::string saveVersionSelectorTitleName;
            std::vector<inst::save_sync::SaveSyncRemoteVersion> saveVersionSelectorVersions;
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
            Rectangle::Ref descriptionRect;
            TextBlock::Ref descriptionText;
            Rectangle::Ref descriptionOverlayRect;
            TextBlock::Ref descriptionOverlayTitleText;
            TextBlock::Ref descriptionOverlayBodyText;
            TextBlock::Ref descriptionOverlayHintText;
            Rectangle::Ref saveVersionSelectorRect;
            TextBlock::Ref saveVersionSelectorTitleText;
            TextBlock::Ref saveVersionSelectorDetailText;
            TextBlock::Ref saveVersionSelectorHintText;
            pu::ui::elm::Menu::Ref saveVersionSelectorMenu;
            void centerPageInfoText();
            void drawMenuItems(bool clearItems);
            void selectTitle(int selectedIndex);
            void updateRememberedSelection();
            void updateSectionText();
            void updateButtonsText();
            void setButtonsText(const std::string& text);
            void buildInstalledSection();
            void buildLegacyOwnedSections();
            void cacheAvailableUpdates();
            void filterOwnedSections();
            void updatePreview();
            void updateInstalledGrid();
            void updateShopGrid();
            void updateDebug();
            const std::vector<shopInstStuff::ShopItem>& getCurrentItems() const;
            bool isAllSection() const;
            bool isInstalledSection() const;
            bool isSaveSyncSection() const;
            void showInstalledDetails();
            void buildSaveSyncSection(const std::string& shopUrl);
            void refreshSaveSyncSection(std::uint64_t selectedTitleId, int previousSectionIndex);
            bool openSaveVersionSelector(const inst::save_sync::SaveSyncEntry& entry, int previousSectionIndex, bool deleteMode = false);
            void closeSaveVersionSelector(bool refreshList);
            void refreshSaveVersionSelectorDetailText();
            bool handleSaveVersionSelectorInput(u64 Down, u64 Up, u64 Held, pu::ui::Touch Pos);
            void handleSaveSyncAction(int selectedIndex);
            void showCurrentDescriptionDialog();
            bool tryGetCurrentDescription(std::string& outTitle, std::string& outDescription) const;
            void openDescriptionOverlay();
            void closeDescriptionOverlay();
            void scrollDescriptionOverlay(int delta);
            void refreshDescriptionOverlayBody();
            void updateDescriptionPanel();
    };
}
