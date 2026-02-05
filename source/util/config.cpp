#include <fstream>
#include <iomanip>
#include "util/config.hpp"
#include "util/json.hpp"

namespace inst::config {
    std::string gAuthKey;
    std::string sigPatchesUrl;
    std::string lastNetUrl;
    std::string shopUrl;
    std::string shopUser;
    std::string shopPass;
    std::vector<std::string> updateInfo;
    int languageSetting;
    bool autoUpdate;
    bool deletePrompt;
    bool gayMode;
    bool soundEnabled;
    bool oledMode;
    bool mtpExposeAlbum;
    bool ignoreReqVers;
    bool overClock;
    bool usbAck;
    bool validateNCAs;
    bool shopHideInstalled;
    bool shopHideInstalledSection;

    void setConfig() {
        nlohmann::json j = {
            {"autoUpdate", autoUpdate},
            {"deletePrompt", deletePrompt},
            {"gAuthKey", gAuthKey},
            {"gayMode", gayMode},
            {"soundEnabled", soundEnabled},
            {"oledMode", oledMode},
            {"mtpExposeAlbum", mtpExposeAlbum},
            {"ignoreReqVers", ignoreReqVers},
            {"languageSetting", languageSetting},
            {"overClock", overClock},
            {"sigPatchesUrl", sigPatchesUrl},
            {"usbAck", usbAck},
            {"validateNCAs", validateNCAs},
            {"lastNetUrl", lastNetUrl},
            {"shopUrl", shopUrl},
            {"shopUser", shopUser},
            {"shopPass", shopPass},
            {"shopHideInstalled", shopHideInstalled},
            {"shopHideInstalledSection", shopHideInstalledSection},
            {"shopRememberSelection", false},
            {"shopSelection", nlohmann::json::array()}
        };
        std::ofstream file(inst::config::configPath);
        file << std::setw(4) << j << std::endl;
    }

    void parseConfig() {
        gAuthKey = {0x41,0x49,0x7a,0x61,0x53,0x79,0x42,0x4d,0x71,0x76,0x34,0x64,0x58,0x6e,0x54,0x4a,0x4f,0x47,0x51,0x74,0x5a,0x5a,0x53,0x33,0x43,0x42,0x6a,0x76,0x66,0x37,0x34,0x38,0x51,0x76,0x78,0x53,0x7a,0x46,0x30};
        sigPatchesUrl = "https://sigmapatches.coomer.party/sigpatches.zip";
        languageSetting = 99;
        autoUpdate = true;
        deletePrompt = true;
        gayMode = true;
        soundEnabled = true;
        oledMode = false;
        mtpExposeAlbum = true;
        ignoreReqVers = true;
        overClock = false;
        usbAck = false;
        validateNCAs = true;
        lastNetUrl = "https://";
        shopUrl.clear();
        shopUser.clear();
        shopPass.clear();
        shopHideInstalled = false;
        shopHideInstalledSection = false;

        try {
            std::ifstream file(inst::config::configPath);
            nlohmann::json j;
            file >> j;
            if (j.contains("autoUpdate")) autoUpdate = j["autoUpdate"].get<bool>();
            if (j.contains("deletePrompt")) deletePrompt = j["deletePrompt"].get<bool>();
            if (j.contains("gAuthKey")) gAuthKey = j["gAuthKey"].get<std::string>();
            if (j.contains("gayMode")) gayMode = j["gayMode"].get<bool>();
            if (j.contains("soundEnabled")) soundEnabled = j["soundEnabled"].get<bool>();
            if (j.contains("oledMode")) oledMode = j["oledMode"].get<bool>();
            if (j.contains("mtpExposeAlbum")) mtpExposeAlbum = j["mtpExposeAlbum"].get<bool>();
            if (j.contains("ignoreReqVers")) ignoreReqVers = j["ignoreReqVers"].get<bool>();
            if (j.contains("languageSetting")) languageSetting = j["languageSetting"].get<int>();
            if (j.contains("overClock")) overClock = j["overClock"].get<bool>();
            if (j.contains("sigPatchesUrl")) sigPatchesUrl = j["sigPatchesUrl"].get<std::string>();
            if (j.contains("usbAck")) usbAck = j["usbAck"].get<bool>();
            if (j.contains("validateNCAs")) validateNCAs = j["validateNCAs"].get<bool>();
            if (j.contains("lastNetUrl")) lastNetUrl = j["lastNetUrl"].get<std::string>();
            if (j.contains("shopUrl")) shopUrl = j["shopUrl"].get<std::string>();
            if (j.contains("shopUser")) shopUser = j["shopUser"].get<std::string>();
            if (j.contains("shopPass")) shopPass = j["shopPass"].get<std::string>();
            if (j.contains("shopHideInstalled")) shopHideInstalled = j["shopHideInstalled"].get<bool>();
            if (j.contains("shopHideInstalledSection")) shopHideInstalledSection = j["shopHideInstalledSection"].get<bool>();
        }
        catch (...) {
            // If loading values from the config fails, we just load the defaults and overwrite the old config
            setConfig();
        }
        if (sigPatchesUrl == "https://github.com/Huntereb/Awoo-Installer/releases/download/SignaturePatches/patches.zip")
            sigPatchesUrl = "https://sigmapatches.coomer.party/sigpatches.zip";
    }
}
