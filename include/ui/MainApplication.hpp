#pragma once
#include <pu/Plutonium>
#include "ui/mainPage.hpp"
#include "ui/netInstPage.hpp"
#include "ui/shopInstPage.hpp"
#include "ui/sdInstPage.hpp"
#include "ui/usbInstPage.hpp"
#include "ui/hddInstPage.hpp"
#include "ui/instPage.hpp"
#include "ui/optionsPage.hpp"

namespace inst::ui {
    class MainApplication : public pu::ui::Application {
        public:
            using Application::Application;
            PU_SMART_CTOR(MainApplication)
            void OnLoad() override;
            pu::ui::Layout::Ref GetCurrentLayout() const { return this->lyt; }
            MainPage::Ref mainPage;
            netInstPage::Ref netinstPage;
            shopInstPage::Ref shopinstPage;
            sdInstPage::Ref sdinstPage;
            usbInstPage::Ref usbinstPage;
            hddInstPage::Ref hddinstPage;
            instPage::Ref instpage;
            optionsPage::Ref optionspage;
    };
}
