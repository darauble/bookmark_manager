#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <core.h>
#include <thread>
#include <radio_interface.h>
#include <signal_path/signal_path.h>
#include <vector>
#include <gui/tuner.h>
#include <gui/file_dialogs.h>
#include <utils/freq_formatting.h>
#include <gui/dialogs/dialog_box.h>
#include <fstream>
#include "utc.h"

SDRPP_MOD_INFO{
    /* Name:            */ "bookmark_manager",
    /* Description:     */ "Bookmark manager module for SDR++",
    /* Author:          */ "Ryzerth;Zimm;Darau Ble;Davide Rovelli",
    /* Version:         */ 0, 1, 7,
    /* Max instances    */ 1
};

constexpr auto MAX_LINES = 10;

struct FrequencyBookmark {
    double frequency;
    double bandwidth;
    int mode;
    bool selected;
    int startTime;
    int endTime;
    bool days[7];
    std::string notes;
    std::string geoinfo;
};

struct WaterfallBookmark {
    std::string listName;
    std::string bookmarkName;
    ImU32 color;
    FrequencyBookmark bookmark;
    ImVec2 clampedRectMin;
    ImVec2 clampedRectMax;
};

struct BookmarkRectangle {
    double min;
    double max;
    int row;
};

ConfigManager config;

const char* demodModeList[] = {
    "NFM",
    "WFM",
    "AM",
    "DSB",
    "USB",
    "CW",
    "LSB",
    "RAW"
};

const char* demodModeListTxt = "NFM\0WFM\0AM\0DSB\0USB\0CW\0LSB\0RAW\0";

enum {
    BOOKMARK_DISP_MODE_OFF,
    BOOKMARK_DISP_MODE_TOP,
    BOOKMARK_DISP_MODE_BOTTOM,
    _BOOKMARK_DISP_MODE_COUNT
};

const char* bookmarkDisplayModesTxt = "Off\0Top\0Bottom\0";
const char* bookmarkRowsTxt = "1\0""2\0""3\0""4\0""5\0""6\0""7\0""8\0""9\0""10\0";

bool compareWaterfallBookmarks(WaterfallBookmark wbm1, WaterfallBookmark wbm2) {
    return (wbm1.bookmark.frequency < wbm2.bookmark.frequency);
}

// Define the comparator lambda function
auto comparatorFreqAsc = [](const std::pair<std::string, FrequencyBookmark>& a, const std::pair<std::string, FrequencyBookmark>& b) {
    return a.second.frequency < b.second.frequency;
};

auto comparatorFreqDesc = [](const std::pair<std::string, FrequencyBookmark>& a, const std::pair<std::string, FrequencyBookmark>& b) {
    return a.second.frequency > b.second.frequency;
};


bool compareBookmarksFreq(FrequencyBookmark bm1, FrequencyBookmark bm2) {
    return (bm1.frequency < bm2.frequency);
}

bool timeValid(int time) {
    // Check HHMM time validity.
    int hours = time / 100;
    int minutes = time % 100;

    return (hours >= 0 && hours <= 23 && minutes >= 0 && minutes <= 59);
}

bool bookmarkOnline(FrequencyBookmark bm, int now, int weekDay) {
    if (!bm.days[weekDay]) {
        return false;
    }

    if (bm.startTime == 0 && bm.endTime == 0) {
        return true;
    } else if (bm.startTime < bm.endTime) {
        return (bm.startTime <= now) && (now < bm.endTime);
    } else if (bm.startTime > bm.endTime) {
        return ((bm.startTime <= now) && (now <= 2359))
            || ((bm.startTime >= 0) && (now <= bm.endTime));
    } else {
        return false; // When start and end times are equal (except 0000).
    }
}

ImU32 hexStrToColor(std::string col) {
    // std::cout << "hexStrToColor: " << col << std::endl;

    int r, g, b;

    if (col[0] == '#' && std::all_of(col.begin() + 1, col.end(), ::isxdigit)) {
        r = std::stoi(col.substr(1, 2), NULL, 16);
        g = std::stoi(col.substr(3, 2), NULL, 16);
        b = std::stoi(col.substr(5, 2), NULL, 16);
        // std::cout << "  color is: " << r << " " << g << " " << b << std:: endl;
    } else {
        r = 255;
        g = 255;
        b = 0;
        // std::cout << "  color is not valid, returning default";
    }

    return IM_COL32(r, g, b, 255);
}

ImVec4 color32ToVec4(ImU32 col) {
    ImVec4 val;

    float sc = 1.0f / 255.0f;
    val.x = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) * sc;
    val.y = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) * sc;
    val.z = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) * sc;
    val.w = (float)((col >> IM_COL32_A_SHIFT) & 0xFF) * sc;

    return val;
}

class BookmarkManagerModule : public ModuleManager::Instance {
public:
    BookmarkManagerModule(std::string name) {
        this->name = name;

        config.acquire();
        std::string selList = config.conf["selectedList"];
        bookmarkDisplayMode = config.conf["bookmarkDisplayMode"];
        bookmarkRows = config.conf["bookmarkRows"];
        bookmarkRectangle = config.conf["bookmarkRectangle"];
        bookmarkCentered = config.conf["bookmarkCentered"];
        bookmarkNoClutter = config.conf["bookmarkNoClutter"];
        config.release();

        refreshLists();
        loadByName(selList);
        refreshWaterfallBookmarks();

        fftRedrawHandler.ctx = this;
        fftRedrawHandler.handler = fftRedraw;
        inputHandler.ctx = this;
        inputHandler.handler = fftInput;

        gui::menu.registerEntry(name, menuHandler, this, NULL);
        gui::waterfall.onFFTRedraw.bindHandler(&fftRedrawHandler);
        gui::waterfall.onInputProcess.bindHandler(&inputHandler);
    }

    ~BookmarkManagerModule() {
        gui::menu.removeEntry(name);
        gui::waterfall.onFFTRedraw.unbindHandler(&fftRedrawHandler);
        gui::waterfall.onInputProcess.unbindHandler(&inputHandler);
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static void applyBookmark(FrequencyBookmark bm, std::string vfoName) {
        if (vfoName == "") {
            // TODO: Replace with proper tune call
            gui::waterfall.setCenterFrequency(bm.frequency);
            gui::waterfall.centerFreqMoved = true;
        }
        else {
            if (core::modComManager.interfaceExists(vfoName)) {
                if (core::modComManager.getModuleName(vfoName) == "radio") {
                    int mode = bm.mode;
                    float bandwidth = bm.bandwidth;
                    core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
                    core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, NULL);
                }
            }
            tuner::tune(tuner::TUNER_MODE_NORMAL, vfoName, bm.frequency);
        }        
    }

    bool bookmarkEditDialog() {
        bool open = true;
        gui::mainWindow.lockWaterfallControls = true;

        std::string id = "Edit##freq_manager_edit_popup_" + name;
        ImGui::OpenPopup(id.c_str());

        char nameBuf[1024];
        strcpy(nameBuf, editedBookmarkName.c_str());

        char notesBuf[4096];
        strcpy(notesBuf, editedBookmark.notes.c_str());

        char geoinfoBuf[2048];
        strcpy(geoinfoBuf, editedBookmark.geoinfo.c_str());

        if (ImGui::BeginPopup(id.c_str(), ImGuiWindowFlags_NoResize)) {
            float edit_win_size = 250.0f * style::uiScale;
            ImGui::BeginTable(("freq_manager_edit_table" + name).c_str(), 2);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Name");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(edit_win_size);
            if (ImGui::InputText(("##freq_manager_edit_name" + name).c_str(), nameBuf, 1023)) {
                editedBookmarkName = nameBuf;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Frequency");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(edit_win_size);
            ImGui::InputDouble(("##freq_manager_edit_freq" + name).c_str(), &editedBookmark.frequency);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Bandwidth");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(edit_win_size);
            ImGui::InputDouble(("##freq_manager_edit_bw" + name).c_str(), &editedBookmark.bandwidth);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Start Time");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(edit_win_size);
            ImGui::InputScalarN(
                ("##freq_manager_edit_start_time" + name).c_str(),
                ImGuiDataType_S32,
                &editedBookmark.startTime, 1,
                NULL, NULL, "%04d", 0);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("End Time");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(edit_win_size);
            ImGui::InputScalarN(
                ("##freq_manager_edit_end_time" + name).c_str(),
                ImGuiDataType_S32,
                &editedBookmark.endTime, 1,
                NULL, NULL, "%04d", 0);
            
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Days");
            ImGui::TableSetColumnIndex(1);

            ImGui::BeginGroup();
            ImGui::Columns(5, "BookmarkDays", false);
            ImGui::NextColumn();
            ImGui::Checkbox("Su", &editedBookmark.days[0]);
            ImGui::Checkbox("Th", &editedBookmark.days[4]);
            ImGui::NextColumn();
            ImGui::Checkbox("Mo", &editedBookmark.days[1]);
            ImGui::Checkbox("Fr", &editedBookmark.days[5]);
            ImGui::NextColumn();
            ImGui::Checkbox("Tu", &editedBookmark.days[2]);
            ImGui::Checkbox("Sa", &editedBookmark.days[6]);
            ImGui::NextColumn();
            ImGui::Checkbox("We", &editedBookmark.days[3]);

            ImGui::Columns(1, "EndBookmarkDays", false);
            ImGui::EndGroup();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Mode");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(edit_win_size);

            ImGui::Combo(("##freq_manager_edit_mode" + name).c_str(), &editedBookmark.mode, demodModeListTxt);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Geo Info");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(edit_win_size);

            if (ImGui::InputText(("##freq_manager_edit_geoinfo" + name).c_str(), geoinfoBuf, 2047)) {
                editedBookmark.geoinfo = geoinfoBuf;
            }


            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::LeftLabel("Notes");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(edit_win_size);

            if (ImGui::InputTextMultiline(("##freq_manager_edit_notes" + name).c_str(), notesBuf, 4095)) {
                editedBookmark.notes = notesBuf;
            }


            ImGui::EndTable();

            bool applyDisabled = 
                (strlen(nameBuf) == 0) 
                || (bookmarks.find(editedBookmarkName) != bookmarks.end() && editedBookmarkName != firstEditedBookmarkName)
                || !timeValid(editedBookmark.startTime) || !timeValid(editedBookmark.endTime);
            if (applyDisabled) { style::beginDisabled(); }
            if (ImGui::Button("Apply")) {
                open = false;

                // If editing, delete the original one
                if (editOpen) {
                    bookmarks.erase(firstEditedBookmarkName);
                }
                bookmarks[editedBookmarkName] = editedBookmark;

                saveByName(selectedListName);
            }
            if (applyDisabled) { style::endDisabled(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                open = false;
            }
            ImGui::EndPopup();
        }
        return open;
    }

    bool newListDialog() {
        bool open = true;
        gui::mainWindow.lockWaterfallControls = true;

        float menuWidth = ImGui::GetContentRegionAvail().x;

        std::string id = "New##freq_manager_new_popup_" + name;
        ImGui::OpenPopup(id.c_str());

        char nameBuf[1024];
        strcpy(nameBuf, editedListName.c_str());

        if (ImGui::BeginPopup(id.c_str(), ImGuiWindowFlags_NoResize)) {
            ImGui::LeftLabel("Name");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::InputText(("##freq_manager_edit_name" + name).c_str(), nameBuf, 1023)) {
                editedListName = nameBuf;
            }

            ImGui::LeftLabel("Color");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());

            // wbm.color = hexStrToColor(list["color"]);


            if (ImGui::ColorEdit3(("##list_color_" + name).c_str(), (float*)&editedListColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {

            }

            // std::cout << "Edit list: " << firstEditedListName << " / " << editedListName.c_str() << " / " << nameBuf <<std::endl;
            bool alreadyExists = (std::find(listNames.begin(), listNames.end(), editedListName) != listNames.end()) && strcmp(firstEditedListName.c_str(), nameBuf) != 0;

            if (strlen(nameBuf) == 0 || alreadyExists) { style::beginDisabled(); }
            if (ImGui::Button("Apply")) {
                open = false;

                config.acquire();
                if (renameListOpen) {
                    if (strcmp(firstEditedListName.c_str(), nameBuf) != 0) {
                        config.conf["lists"][editedListName] = config.conf["lists"][firstEditedListName];
                        config.conf["lists"].erase(firstEditedListName);
                    }
                }
                else {
                    config.conf["lists"][editedListName]["showOnWaterfall"] = true;
                    config.conf["lists"][editedListName]["bookmarks"] = json::object();
                }

                char buf[16];
                sprintf(buf, "#%02X%02X%02X", (int)roundf(editedListColor.x * 255), (int)roundf(editedListColor.y * 255), (int)roundf(editedListColor.z * 255));
                config.conf["lists"][editedListName]["color"] = buf;

                refreshWaterfallBookmarks(false);
                config.release(true);
                refreshLists();
                loadByName(editedListName);
            }
            if (strlen(nameBuf) == 0 || alreadyExists) { style::endDisabled(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                open = false;
            }
            ImGui::EndPopup();
        }
        return open;
    }

    bool selectListsDialog() {
        gui::mainWindow.lockWaterfallControls = true;

        float menuWidth = ImGui::GetContentRegionAvail().x;

        std::string id = "Select lists##freq_manager_sel_popup_" + name;
        ImGui::OpenPopup(id.c_str());

        bool open = true;

        if (ImGui::BeginPopup(id.c_str(), ImGuiWindowFlags_NoResize)) {
            // No need to lock config since we're not modifying anything and there's only one instance
            for (auto [listName, list] : config.conf["lists"].items()) {
                bool shown = list["showOnWaterfall"];
                if (ImGui::Checkbox((listName + "##freq_manager_sel_list_").c_str(), &shown)) {
                    config.acquire();
                    config.conf["lists"][listName]["showOnWaterfall"] = shown;
                    refreshWaterfallBookmarks(false);
                    config.release(true);
                }
            }

            if (ImGui::Button("Ok")) {
                open = false;
            }
            ImGui::EndPopup();
        }
        return open;
    }

    void refreshLists() {
        listNames.clear();
        sortSpecsDirty = true;
        listNamesTxt = "";

        config.acquire();
        for (auto [_name, list] : config.conf["lists"].items()) {
            listNames.push_back(_name);
            listNamesTxt += _name;
            listNamesTxt += '\0';
        }
        config.release();
    }

    void refreshWaterfallBookmarks(bool lockConfig = true) {
        if (lockConfig) { config.acquire(); }
        waterfallBookmarks.clear();
        for (auto [listName, list] : config.conf["lists"].items()) {
            if (!((bool)list["showOnWaterfall"])) { continue; }
            WaterfallBookmark wbm;
            wbm.listName = listName;
            wbm.color = IM_COL32(255, 255, 0, 255);

            if (list.contains("color")) {
                // std::cout << "List " << listName << " is of color " << list["color"] << std::endl;
                // std::cout << "Default color: " << wbm.color << std::endl;
                wbm.color = hexStrToColor(list["color"]);
            }

            for (auto [bookmarkName, bm] : config.conf["lists"][listName]["bookmarks"].items()) {
                wbm.bookmarkName = bookmarkName;
                wbm.bookmark.frequency = bm["frequency"];
                wbm.bookmark.bandwidth = bm["bandwidth"];
                wbm.bookmark.startTime = bm.contains("startTime") ? (int)bm["startTime"] : 0;
                wbm.bookmark.endTime = bm.contains("endTime") ? (int)bm["endTime"] : 0;
                
                if (bm.contains("days")) {
                    std::copy(bm["days"].begin(), bm["days"].end(), wbm.bookmark.days);
                } else {
                    for (int i = 0; i < 7; i++) {
                        wbm.bookmark.days[i] = true;
                    }
                }

                if (bm.contains("geoinfo")) {
                    wbm.bookmark.geoinfo = bm["geoinfo"];
                } else {
                    wbm.bookmark.geoinfo = "";
                }

                if (bm.contains("notes")) {
                    wbm.bookmark.notes = bm["notes"];
                } else {
                    wbm.bookmark.notes = "";
                }

                wbm.bookmark.mode = bm["mode"];
                wbm.bookmark.selected = false;
                wbm.clampedRectMin = ImVec2(-1, -1);
                wbm.clampedRectMax = ImVec2(-1, -1);
                waterfallBookmarks.push_back(wbm);
            }
        }
        std::sort(waterfallBookmarks.begin(), waterfallBookmarks.end(), compareWaterfallBookmarks);
        if (lockConfig) { config.release(); }
    }

    void loadFirst() {
        if (listNames.size() > 0) {
            loadByName(listNames[0]);
            return;
        }
        selectedListName = "";
        selectedListId = 0;
    }

    void loadByName(std::string listName) {
        bookmarks.clear();
        sortSpecsDirty = true;
        if (std::find(listNames.begin(), listNames.end(), listName) == listNames.end()) {
            selectedListName = "";
            selectedListId = 0;
            loadFirst();
            return;
        }
        selectedListId = std::distance(listNames.begin(), std::find(listNames.begin(), listNames.end(), listName));
        selectedListName = listName;
        config.acquire();
        for (auto [bmName, bm] : config.conf["lists"][listName]["bookmarks"].items()) {
            FrequencyBookmark fbm;
            fbm.frequency = bm["frequency"];
            fbm.bandwidth = bm["bandwidth"];
            fbm.startTime = bm.contains("startTime") ? (int)bm["startTime"] : 0;
            fbm.endTime = bm.contains("endTime") ? (int)bm["endTime"] : 0;
            
            if (bm.contains("days")) {
                std::copy(bm["days"].begin(), bm["days"].end(), fbm.days);
            } else {
                for (int i = 0; i < 7; i++) {
                    fbm.days[i] = true;
                }
            }

            if (bm.contains("geoinfo")) {
                fbm.geoinfo = bm["geoinfo"];
            } else {
                fbm.geoinfo = "";
            }

            if (bm.contains("notes")) {
                fbm.notes = bm["notes"];
            } else {
                fbm.notes = "";
            }

            fbm.mode = bm["mode"];
            fbm.selected = false;
            bookmarks[bmName] = fbm;
        }
        config.release();
    }

    void saveByName(std::string listName) {
        config.acquire();
        config.conf["lists"][listName]["bookmarks"] = json::object();
        for (auto [bmName, bm] : bookmarks) {
            config.conf["lists"][listName]["bookmarks"][bmName]["frequency"] = bm.frequency;
            config.conf["lists"][listName]["bookmarks"][bmName]["bandwidth"] = bm.bandwidth;
            config.conf["lists"][listName]["bookmarks"][bmName]["startTime"] = bm.startTime;
            config.conf["lists"][listName]["bookmarks"][bmName]["endTime"] = bm.endTime;
            config.conf["lists"][listName]["bookmarks"][bmName]["days"] = bm.days;
            config.conf["lists"][listName]["bookmarks"][bmName]["geoinfo"] = bm.geoinfo;
            config.conf["lists"][listName]["bookmarks"][bmName]["notes"] = bm.notes;
            config.conf["lists"][listName]["bookmarks"][bmName]["mode"] = bm.mode;
        }
        refreshWaterfallBookmarks(false);
        sortSpecsDirty = true;
        config.release(true);
    }

    static void menuHandler(void* ctx) {
        BookmarkManagerModule* _this = (BookmarkManagerModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        // TODO: Replace with something that won't iterate every frame
        std::vector<std::string> selectedNames;

        if (selectedNames.size() == 0)
        {
            for (auto& [name, bm] : _this->bookmarks) {
                if (bm.selected) { 
                    selectedNames.push_back(name); 
                }
            }
        }

        float lineHeight = ImGui::GetTextLineHeightWithSpacing();

        float btnSize = ImGui::CalcTextSize("Rename").x + 8;
        float sizetarget = menuWidth - btnSize - 2 * lineHeight - 24 * style::uiScale;
        ImGui::SetNextItemWidth(sizetarget);
        if (ImGui::Combo(("##freq_manager_list_sel" + _this->name).c_str(), &_this->selectedListId, _this->listNamesTxt.c_str())) {
            _this->loadByName(_this->listNames[_this->selectedListId]);
            config.acquire();
            config.conf["selectedList"] = _this->selectedListName;
            config.release(true);
        }
        ImGui::SameLine();
        if (_this->listNames.size() == 0) { style::beginDisabled(); }
        if (ImGui::Button(("Rename##_freq_mgr_ren_lst_" + _this->name).c_str(), ImVec2(btnSize, 0))) {
            _this->firstEditedListName = _this->listNames[_this->selectedListId];
            _this->editedListName = _this->firstEditedListName;
            _this->renameListOpen = true;

            if (config.conf["lists"][_this->firstEditedListName].contains("color")) {
                _this->editedListColor = color32ToVec4(hexStrToColor(config.conf["lists"][_this->firstEditedListName]["color"]));
            } else {
                _this->editedListColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
            }
        }
        if (_this->listNames.size() == 0) { style::endDisabled(); }
        ImGui::SameLine();
        if (ImGui::Button(("+##_freq_mgr_add_lst_" + _this->name).c_str(), ImVec2(lineHeight, 0))) {
            // Find new unique default name
            if (std::find(_this->listNames.begin(), _this->listNames.end(), "New List") == _this->listNames.end()) {
                _this->editedListName = "New List";
            }
            else {
                char buf[64];
                for (int i = 1; i < 1000; i++) {
                    sprintf(buf, "New List (%d)", i);
                    if (std::find(_this->listNames.begin(), _this->listNames.end(), buf) == _this->listNames.end()) { break; }
                }
                _this->editedListName = buf;
            }
            _this->newListOpen = true;
            _this->editedListColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
        }
        ImGui::SameLine();
        if (_this->selectedListName == "") { style::beginDisabled(); }
        if (ImGui::Button(("-##_freq_mgr_del_lst_" + _this->name).c_str(), ImVec2(lineHeight, 0))) {
            _this->deleteListOpen = true;
        }
        if (_this->selectedListName == "") { style::endDisabled(); }

        // List delete confirmation
        if (ImGui::GenericDialog(("freq_manager_del_list_confirm" + _this->name).c_str(), _this->deleteListOpen, GENERIC_DIALOG_BUTTONS_YES_NO, [_this]() {
                ImGui::Text("Deleting list named \"%s\". Are you sure?", _this->selectedListName.c_str());
            }) == GENERIC_DIALOG_BUTTON_YES) {
            config.acquire();
            config.conf["lists"].erase(_this->selectedListName);
            _this->refreshWaterfallBookmarks(false);
            config.release(true);
            _this->refreshLists();
            _this->selectedListId = std::clamp<int>(_this->selectedListId, 0, _this->listNames.size());
            if (_this->listNames.size() > 0) {
                _this->loadByName(_this->listNames[_this->selectedListId]);
            }
            else {
                _this->selectedListName = "";
            }
        }

        if (_this->selectedListName == "") { style::beginDisabled(); }
        //Draw buttons on top of the list
        ImGui::BeginTable(("freq_manager_btn_table" + _this->name).c_str(), 3);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button(("Add##_freq_mgr_add_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            // If there's no VFO selected, just save the center freq
            if (gui::waterfall.selectedVFO == "") {
                _this->editedBookmark.frequency = gui::waterfall.getCenterFrequency();
                _this->editedBookmark.bandwidth = 0;
                _this->editedBookmark.mode = 7;
            }
            else {
                _this->editedBookmark.frequency = gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(gui::waterfall.selectedVFO);
                _this->editedBookmark.bandwidth = sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);
                _this->editedBookmark.mode = 7;
                if (core::modComManager.getModuleName(gui::waterfall.selectedVFO) == "radio") {
                    int mode;
                    core::modComManager.callInterface(gui::waterfall.selectedVFO, RADIO_IFACE_CMD_GET_MODE, NULL, &mode);
                    _this->editedBookmark.mode = mode;
                }
            }

            // Set default values for new bookmark
            _this->editedBookmark.startTime = 0;
            _this->editedBookmark.endTime = 0;

            for (int i = 0; i < 7; i++) {
                _this->editedBookmark.days[i] = true;
            }

            _this->editedBookmark.geoinfo = "";

            _this->editedBookmark.notes = "";

            _this->editedBookmark.selected = false;

            _this->createOpen = true;

            // Find new unique default name
            if (_this->bookmarks.find("New Bookmark") == _this->bookmarks.end()) {
                _this->editedBookmarkName = "New Bookmark";
            }
            else {
                char buf[64];
                for (int i = 1; i < 1000; i++) {
                    sprintf(buf, "New Bookmark (%d)", i);
                    if (_this->bookmarks.find(buf) == _this->bookmarks.end()) { break; }
                }
                _this->editedBookmarkName = buf;
            }
        }

        ImGui::TableSetColumnIndex(1);
        if (selectedNames.size() == 0 && _this->selectedListName != "") { style::beginDisabled(); }
        if (ImGui::Button(("Remove##_freq_mgr_rem_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            _this->deleteBookmarksOpen = true;
        }
        if (selectedNames.size() == 0 && _this->selectedListName != "") { style::endDisabled(); }
        ImGui::TableSetColumnIndex(2);
        if (selectedNames.size() != 1 && _this->selectedListName != "") { style::beginDisabled(); }
        if (ImGui::Button(("Edit##_freq_mgr_edt_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            _this->editOpen = true;
            _this->editedBookmark = _this->bookmarks[selectedNames[0]];
            _this->editedBookmarkName = selectedNames[0];
            _this->firstEditedBookmarkName = selectedNames[0];
        }
        if (selectedNames.size() != 1 && _this->selectedListName != "") { style::endDisabled(); }

        ImGui::EndTable();

        // Bookmark delete confirm dialog
        // List delete confirmation
        if (ImGui::GenericDialog(("freq_manager_del_list_confirm" + _this->name).c_str(), _this->deleteBookmarksOpen, GENERIC_DIALOG_BUTTONS_YES_NO, [_this]() {
                ImGui::TextUnformatted("Deleting selected bookmaks. Are you sure?");
            }) == GENERIC_DIALOG_BUTTON_YES) {
            for (auto& _name : selectedNames) { _this->bookmarks.erase(_name); }
            _this->saveByName(_this->selectedListName);
        }

        // Bookmark list
        if (ImGui::BeginTable(("freq_manager_bkm_table" + _this->name).c_str(), 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable, ImVec2(0, 200.0f * style::uiScale))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
            ImGui::TableSetupColumn("Bookmark", ImGuiTableColumnFlags_DefaultSort, 0.0f, 1);
            ImGui::TableSetupScrollFreeze(2, 1);
            ImGui::TableHeadersRow();

            // Sort by column name or by column bookmark when the column header is clicked
            if (ImGui::TableGetSortSpecs() != nullptr) {
                ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
                if (sortSpecs->SpecsDirty || _this->sortSpecsDirty) {
                    if (sortSpecs->SpecsCount > 0) {
                        ImGuiTableColumnSortSpecs spec = sortSpecs->Specs[0];
                        // Sort by Name column
                        _this->sortedBookmarks.clear();
                        if (spec.ColumnUserID == 0) {
                            //flog::info("Sort by Name column");
                            if (spec.SortDirection == ImGuiSortDirection_Descending) {
                                //flog::info("Sort Descending");
                                _this->sortedBookmarks.insert(_this->sortedBookmarks.begin(), _this->bookmarks.begin(), _this->bookmarks.end());
                                std::reverse(_this->sortedBookmarks.begin(), _this->sortedBookmarks.end());
                            }
                            if (spec.SortDirection == ImGuiSortDirection_Ascending) {
                                //flog::info("Sort Ascending");
                                _this->sortedBookmarks.insert(_this->sortedBookmarks.begin(), _this->bookmarks.begin(), _this->bookmarks.end());
                            }                            
                        } else if (spec.ColumnUserID == 1) {
                            // Sort by Bookmark column
                            //flog::info("Sort by Bookmarks column");
                            if (spec.SortDirection == ImGuiSortDirection_Descending) {
                                //flog::info("Sort Descending");
                                _this->sortedBookmarks.insert(_this->sortedBookmarks.begin(), _this->bookmarks.begin(), _this->bookmarks.end());
                                std::sort(_this->sortedBookmarks.begin(), _this->sortedBookmarks.end(), comparatorFreqDesc);
                            }
                            if (spec.SortDirection == ImGuiSortDirection_Ascending) {
                                //flog::info("Sort Ascending");
                                _this->sortedBookmarks.insert(_this->sortedBookmarks.begin(), _this->bookmarks.begin(), _this->bookmarks.end());
                                std::sort(_this->sortedBookmarks.begin(), _this->sortedBookmarks.end(), comparatorFreqAsc);
                            }
                        }
                        sortSpecs->SpecsDirty = false;
                        _this->sortSpecsDirty = false;                
                    }
                }
            }

            for (auto& [name, bm] : _this->sortedBookmarks) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                FrequencyBookmark& cbm = _this->bookmarks[name];
                if (ImGui::Selectable((name + "##_freq_mgr_bkm_name_" + _this->name).c_str(), &cbm.selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnClick)) {
                    // if shift or control isn't pressed, deselect all others
                    if (!ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl) {
                        for (auto& [_name, _bm] : _this->bookmarks) {
                            if (name == _name) {
                                continue; 
                            }
                            _bm.selected = false;
                        }
                    }
                }
                if (ImGui::TableGetHoveredColumn() >= 0 && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    applyBookmark(bm, gui::waterfall.selectedVFO);
                    cbm.selected = true;
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s %s", utils::formatFreq(bm.frequency).c_str(), demodModeList[bm.mode]);

                if (_this->scrollToClickedBookmark && cbm.selected) {
                    ImGui::SetScrollHereY(0.5f);
                    _this->scrollToClickedBookmark = false;                   
                }
            }
            ImGui::EndTable();
        }


        if (selectedNames.size() != 1 && _this->selectedListName != "") { style::beginDisabled(); }
        if (ImGui::Button(("Apply##_freq_mgr_apply_" + _this->name).c_str(), ImVec2(menuWidth, 0))) {
            FrequencyBookmark& bm = _this->bookmarks[selectedNames[0]];
            applyBookmark(bm, gui::waterfall.selectedVFO);
            bm.selected = false;
        }
        if (selectedNames.size() != 1 && _this->selectedListName != "") { style::endDisabled(); }

        //Draw import and export buttons
        ImGui::BeginTable(("freq_manager_bottom_btn_table" + _this->name).c_str(), 2);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button(("Import##_freq_mgr_imp_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !_this->importOpen) {
            _this->importOpen = true;
            _this->importDialog = new pfd::open_file("Import bookmarks", "", { "JSON Files (*.json)", "*.json", "All Files", "*" }, true);
        }

        ImGui::TableSetColumnIndex(1);
        if (selectedNames.size() == 0 && _this->selectedListName != "") { style::beginDisabled(); }
        if (ImGui::Button(("Export##_freq_mgr_exp_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)) && !_this->exportOpen) {
            _this->exportedBookmarks = json::object();
            config.acquire();
            for (auto& _name : selectedNames) {
                _this->exportedBookmarks["bookmarks"][_name] = config.conf["lists"][_this->selectedListName]["bookmarks"][_name];
            }
            config.release();
            _this->exportOpen = true;
            _this->exportDialog = new pfd::save_file("Export bookmarks", "", { "JSON Files (*.json)", "*.json", "All Files", "*" }, true);
        }
        if (selectedNames.size() == 0 && _this->selectedListName != "") { style::endDisabled(); }
        ImGui::EndTable();

        if (ImGui::Button(("Select displayed lists##_freq_mgr_exp_" + _this->name).c_str(), ImVec2(menuWidth, 0))) {
            _this->selectListsOpen = true;
        }

        ImGui::LeftLabel("Bookmark display mode");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo(("##_freq_mgr_dms_" + _this->name).c_str(), &_this->bookmarkDisplayMode, bookmarkDisplayModesTxt)) {
            config.acquire();
            config.conf["bookmarkDisplayMode"] = _this->bookmarkDisplayMode;
            config.release(true);
        }

        ImGui::LeftLabel("Rows of bookmarks");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo(("##_freq_mgr_rob_" + _this->name).c_str(), &_this->bookmarkRows, bookmarkRowsTxt)) {
            config.acquire();
            config.conf["bookmarkRows"] = _this->bookmarkRows;
            config.release(true);
        }

        if (ImGui::Checkbox(("Rectangles##_freq_mgr_rect_" + _this->name).c_str(), &_this->bookmarkRectangle)) {
            config.acquire();
            config.conf["bookmarkRectangle"] = _this->bookmarkRectangle;
            config.release(true);
        }

        ImGui::SameLine();
        if (ImGui::Checkbox(("Centered##_freq_mgr_cen_" + _this->name).c_str(), &_this->bookmarkCentered)) {
            config.acquire();
            config.conf["bookmarkCentered"] = _this->bookmarkCentered;
            config.release(true);
        }

        if (ImGui::Checkbox(("Avoid clutter on last row##_freq_mgr_noClut_" + _this->name).c_str(), &_this->bookmarkNoClutter)) {
            config.acquire();
            config.conf["bookmarkNoClutter"] = _this->bookmarkNoClutter;
            config.release(true);
        }

        if (_this->selectedListName == "") { style::endDisabled(); }

        if (_this->createOpen) {
            _this->createOpen = _this->bookmarkEditDialog();
        }

        if (_this->editOpen) {
            _this->editOpen = _this->bookmarkEditDialog();
        }

        if (_this->newListOpen) {
            _this->newListOpen = _this->newListDialog();
        }

        if (_this->renameListOpen) {
            _this->renameListOpen = _this->newListDialog();
        }

        if (_this->selectListsOpen) {
            _this->selectListsOpen = _this->selectListsDialog();
        }

        // Handle import and export
        if (_this->importOpen && _this->importDialog->ready()) {
            _this->importOpen = false;
            std::vector<std::string> paths = _this->importDialog->result();
            if (paths.size() > 0 && _this->listNames.size() > 0) {
                _this->importBookmarks(paths[0]);
            }
            delete _this->importDialog;
        }
        if (_this->exportOpen && _this->exportDialog->ready()) {
            _this->exportOpen = false;
            std::string path = _this->exportDialog->result();
            if (path != "") {
                _this->exportBookmarks(path);
            }
            delete _this->exportDialog;
        }
    }

    static void fftRedraw(ImGui::WaterFall::FFTRedrawArgs args, void* ctx) {
        BookmarkManagerModule* _this = (BookmarkManagerModule*)ctx;
        if (_this->bookmarkDisplayMode == BOOKMARK_DISP_MODE_OFF) { return; }

        
#ifdef _WIN32
        std::vector<BookmarkRectangle> bookmarkRectangles[MAX_LINES];
#else
        std::vector<BookmarkRectangle> bookmarkRectangles[_this->bookmarkRows+1];
#endif

        int now = getUTCTime();
        int weekDay = getWeekDay();

        for (auto &bm : _this->waterfallBookmarks) {
            double centerXpos = args.min.x + std::round((bm.bookmark.frequency - args.lowFreq) * args.freqToPixelRatio);

            if (bm.bookmark.frequency >= args.lowFreq && bm.bookmark.frequency <= args.highFreq) {
                ImVec2 nameSize = ImGui::CalcTextSize(bm.bookmarkName.c_str());

                int row = 0;
                double bmMinX = 0.0;
                double bmMaxX = 0.0;
                if (_this->bookmarkCentered) {
                    bmMinX = centerXpos - (nameSize.x / 2) - 5;
                    bmMaxX = centerXpos + (nameSize.x / 2) + 5;
                } else {
                    bmMinX = centerXpos - 5;
                    bmMaxX = centerXpos + nameSize.x + 5;
                }
                // std::cout << "BR_X: " << bm.bookmarkName << " " << bmMinX << " " << bmMaxX << std::endl;
                bool foundOnrow = false;
                for (int i = 0; i < _this->bookmarkRows; i++) {
                    foundOnrow = false;
                    for (auto const br: bookmarkRectangles[i]) {
                        if (((bmMinX >= br.min && bmMinX <= br.max) || (bmMaxX >= br.min && bmMaxX <= br.max)) || (br.max <= bmMaxX && br.min >= bmMinX)) {
                            row = i + 1;
                            foundOnrow = true;
                            break;
                        }
                    }
                    if (foundOnrow == false) {
                        row = i;
                        break; 
                    }
                }
                // avoid clutter on the last row
                if (row == _this->bookmarkRows && _this->bookmarkNoClutter) {
                   foundOnrow = false;
                    for (auto const br: bookmarkRectangles[row]) {
                        if (((bmMinX >= br.min && bmMinX <= br.max) || (bmMaxX >= br.min && bmMaxX <= br.max)) || (br.max <= bmMaxX && br.min >= bmMinX)) {
                            foundOnrow = true;
                            break;
                        }
                    }
                    if (foundOnrow) { continue; }
                }

                ImVec2 rectMin, rectMax;

                if (_this->bookmarkDisplayMode == BOOKMARK_DISP_MODE_TOP) {
                    double bottomright = args.min.y + nameSize.y + (nameSize.y * row);
                    rectMin = ImVec2(bmMinX, args.min.y + (nameSize.y * row));
                    if (bottomright >= args.max.y) { continue; }
                    rectMax = ImVec2(bmMaxX, bottomright);
                } else {
                    double topleft = args.max.y - nameSize.y - (nameSize.y * row);
                    if (topleft <= args.min.y) { continue; }
                    rectMin = ImVec2(bmMinX, topleft);
                    rectMax = ImVec2(bmMaxX, args.max.y - (nameSize.y * row));
                }

                bm.clampedRectMin = ImVec2(std::clamp<double>(rectMin.x, args.min.x, args.max.x), rectMin.y);
                bm.clampedRectMax = ImVec2(std::clamp<double>(rectMax.x, args.min.x, args.max.x), rectMax.y);

                /* BookmarkRectangle br = {
                    .min = bmMinX,
                    .max = bmMaxX,
                    .row = row
                };*/
                BookmarkRectangle br = { bmMinX, bmMaxX, row };

                bookmarkRectangles[row].push_back(br);

                // ImU32 bookmarkColor = IM_COL32(255, 255, 0, 255);
                ImU32 bookmarkColor = bm.color;
                ImU32 bookmarkTextColor = IM_COL32(0, 0, 0, 255);


                if (!bookmarkOnline(bm.bookmark, now, weekDay)) {
                    bookmarkColor = IM_COL32(128, 128, 128, 255);
                }

                if (_this->bookmarkRectangle) {
                    args.window->DrawList->AddRectFilled(bm.clampedRectMin, bm.clampedRectMax, bookmarkColor);
                } else {
                    bookmarkTextColor = bookmarkColor;
                }
                
                
                
                if (_this->bookmarkDisplayMode == BOOKMARK_DISP_MODE_TOP) {
                    args.window->DrawList->AddLine(ImVec2(centerXpos, args.min.y + (nameSize.y * (row + 1))), ImVec2(centerXpos, args.max.y), bookmarkColor);
                    if (_this->bookmarkCentered) {                        
                        if (((centerXpos - (nameSize.x / 2)) >= args.min.x) && ((centerXpos + (nameSize.x / 2) <= args.max.x))) {
                            args.window->DrawList->AddText(ImVec2(centerXpos - (nameSize.x / 2), args.min.y + (nameSize.y * row)), bookmarkTextColor, bm.bookmarkName.c_str());
                        }
                    } else {
                        if (((bmMinX + 6) >= args.min.x) && ((bmMinX + nameSize.x) <= args.max.x)) {
                            args.window->DrawList->AddText(ImVec2(bmMinX + 6, args.min.y + (nameSize.y * row)), bookmarkTextColor, bm.bookmarkName.c_str());
                        }
                    }
                } else {
                    args.window->DrawList->AddLine(ImVec2(centerXpos, args.min.y), ImVec2(centerXpos, args.max.y - (nameSize.y * (row + 1))), bookmarkColor);
                    if (_this->bookmarkCentered) {
                        args.window->DrawList->AddText(ImVec2(centerXpos - (nameSize.x / 2), args.max.y - nameSize.y - (nameSize.y * row)), bookmarkTextColor, bm.bookmarkName.c_str());
                    } else {
                        args.window->DrawList->AddText(ImVec2(bmMinX + 6, args.max.y - nameSize.y - (nameSize.y * row)), bookmarkTextColor, bm.bookmarkName.c_str());
                    }
                }
            }
        }
    }

    bool mouseAlreadyDown = false;
    bool mouseClickedInLabel = false;
    static void fftInput(ImGui::WaterFall::InputHandlerArgs args, void* ctx) {
        BookmarkManagerModule* _this = (BookmarkManagerModule*)ctx;
        if (_this->bookmarkDisplayMode == BOOKMARK_DISP_MODE_OFF) { return; }

        if (_this->mouseClickedInLabel) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                _this->mouseClickedInLabel = false;
            }
            gui::waterfall.inputHandled = true;
            return;
        }

        // First check that the mouse clicked outside of any label. Also get the bookmark that's hovered
        bool inALabel = false;
        WaterfallBookmark hoveredBookmark;
        std::string hoveredBookmarkName;

        for (auto& bm : _this->waterfallBookmarks) {
            if (bm.bookmark.frequency >= args.lowFreq && bm.bookmark.frequency <= args.highFreq) {
                if (ImGui::IsMouseHoveringRect(bm.clampedRectMin, bm.clampedRectMax)) {
                    inALabel = true;
                    hoveredBookmark = bm;
                    hoveredBookmarkName = bm.bookmarkName;
                }
            }
        }

        // Check if mouse was already down
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inALabel) {
            _this->mouseAlreadyDown = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            _this->mouseAlreadyDown = false;
            _this->mouseClickedInLabel = false;
        }

        // If yes, cancel
        if (_this->mouseAlreadyDown || !inALabel) { return; }

        gui::waterfall.inputHandled = true;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            _this->mouseClickedInLabel = true;
            applyBookmark(hoveredBookmark.bookmark, gui::waterfall.selectedVFO);
            /* if the clicked list is different from the selected, switch */
            if (hoveredBookmark.listName != _this->selectedListName) {
                _this->loadByName(hoveredBookmark.listName);
                _this->selectedListName = hoveredBookmark.listName;
                config.acquire();
                config.conf["selectedList"] = _this->selectedListName;
                config.release(true);
            }
            /* search in _this->bookmarks the selected hoveredBookmark */
            for (auto& [name, b] : _this->bookmarks) {
                if (name == hoveredBookmarkName) {
                    b.selected = true;
                } else {
                    b.selected = false;
                }
            }                
            _this->scrollToClickedBookmark = true;
        }

        char bookmarkDays[8];
        bookmarkDays[7] = 0;

        for (int i = 0; i < 7; i++) {
            bookmarkDays[i] = hoveredBookmark.bookmark.days[i] ? 49 + i : '-';
        }

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(hoveredBookmarkName.c_str());
        ImGui::Separator();
        ImGui::Text("List: %s", hoveredBookmark.listName.c_str());
        ImGui::Text("Frequency: %s", utils::formatFreq(hoveredBookmark.bookmark.frequency).c_str());
        ImGui::Text("Bandwidth: %s", utils::formatFreq(hoveredBookmark.bookmark.bandwidth).c_str());
        ImGui::Text("Start Time: %s", std::to_string(hoveredBookmark.bookmark.startTime).c_str());
        ImGui::Text("End Time: %s", std::to_string(hoveredBookmark.bookmark.endTime).c_str());
        ImGui::Text("Days: %s", bookmarkDays);
        ImGui::Text("Mode: %s", demodModeList[hoveredBookmark.bookmark.mode]);
        ImGui::Text("Geo info: %s", hoveredBookmark.bookmark.geoinfo.c_str());
        ImGui::Text("Notes: %s", hoveredBookmark.bookmark.notes.c_str());
        ImGui::EndTooltip();
    }

    json exportedBookmarks;
    bool importOpen = false;
    bool exportOpen = false;
    pfd::open_file* importDialog;
    pfd::save_file* exportDialog;

    void importBookmarks(std::string path) {
        std::ifstream fs(path);
        json importBookmarks;
        fs >> importBookmarks;

        if (!importBookmarks.contains("bookmarks")) {
            flog::error("File does not contains any bookmarks");
            return;
        }

        if (!importBookmarks["bookmarks"].is_object()) {
            flog::error("Bookmark attribute is invalid");
            return;
        }

        int imported_entries = 0; 
        // Load every bookmark
        for (auto const [_name, bm] : importBookmarks["bookmarks"].items()) {
            if (bookmarks.find(_name) != bookmarks.end()) {
                flog::warn("Bookmark with the name '{0}' already exists in list, skipping", _name);
                continue;
            }
            FrequencyBookmark fbm;
            fbm.frequency = bm["frequency"];
            fbm.bandwidth = bm["bandwidth"];
            fbm.startTime = bm.contains("startTime") ? (int)bm["startTime"] : 0;
            fbm.endTime = bm.contains("endTime") ? (int)bm["endTime"] : 0;
            
            if (bm.contains("days")) {
                std::copy(bm["days"].begin(), bm["days"].end(), fbm.days);
            } else {
                for (int i = 0; i < 7; i++) {
                    fbm.days[i] = true;
                }
            }

            if (bm.contains("geoinfo")) {
                fbm.geoinfo = bm["geoinfo"];
            } else {
                fbm.geoinfo = "";
            }

            if (bm.contains("notes")) {
                fbm.notes = bm["notes"];
            } else {
                fbm.notes = "";
            }

            fbm.mode = bm["mode"];
            fbm.selected = false;
            bookmarks[_name] = fbm;
            imported_entries++;
        }
        saveByName(selectedListName);
        fs.close();

		flog::info("Imported {0} entries", imported_entries);
    }

    void exportBookmarks(std::string path) {
        std::ofstream fs(path);
        exportedBookmarks >> fs;
        fs.close();
    }

    std::string name;
    bool enabled = true;
    bool createOpen = false;
    bool editOpen = false;
    bool newListOpen = false;
    bool renameListOpen = false;
    bool selectListsOpen = false;

    bool deleteListOpen = false;
    bool deleteBookmarksOpen = false;

    EventHandler<ImGui::WaterFall::FFTRedrawArgs> fftRedrawHandler;
    EventHandler<ImGui::WaterFall::InputHandlerArgs> inputHandler;

    std::map<std::string, FrequencyBookmark> bookmarks;
    std::vector<std::pair<std::string, FrequencyBookmark>> sortedBookmarks;
    bool sortSpecsDirty = true;

    std::string editedBookmarkName = "";
    std::string firstEditedBookmarkName = "";
    FrequencyBookmark editedBookmark;

    std::vector<std::string> listNames;
    std::string listNamesTxt = "";
    std::string selectedListName = "";
    int selectedListId = 0;

    std::string editedListName;
    std::string firstEditedListName;
    ImVec4 editedListColor;

    std::vector<WaterfallBookmark> waterfallBookmarks;

    int bookmarkDisplayMode = 0;
    int bookmarkRows = 0;
    bool bookmarkRectangle;
    bool bookmarkCentered;
    bool bookmarkNoClutter;
    int currentSortColumn = -1;
    bool currentSortAscending = true;    
    bool scrollToClickedBookmark = false;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["selectedList"] = "General";
    def["bookmarkDisplayMode"] = BOOKMARK_DISP_MODE_TOP;
    def["bookmarkRows"] = 5;
    def["bookmarkRectangle"] = true;
    def["bookmarkCentered"] = true;
    def["bookmarkNoClutter"] = false;
    def["lists"]["General"]["showOnWaterfall"] = true;
    def["lists"]["General"]["bookmarks"] = json::object();

    config.setPath(core::args["root"].s() + "/bookmark_manager_config.json");
    config.load(def);
    config.enableAutoSave();

    // Check if of list and convert if they're the old type
    config.acquire();
    if (!config.conf.contains("bookmarkDisplayMode")) {
        config.conf["bookmarkDisplayMode"] = BOOKMARK_DISP_MODE_TOP;
    }
    if (!config.conf.contains("bookmarkRows")) {
        config.conf["bookmarkRows"] = 5;
    }
    if (!config.conf.contains("bookmarkRectangle")) {
        config.conf["bookmarkRectangle"] = true;
    }
    if (!config.conf.contains("bookmarkCentered")) {
        config.conf["bookmarkCentered"] = true;
    }
    if (!config.conf.contains("bookmarkNoClutter")) {
        config.conf["bookmarkNoClutter"] = false;
    }

    for (auto [listName, list] : config.conf["lists"].items()) {
        if (list.contains("bookmarks") && list.contains("showOnWaterfall") && list["showOnWaterfall"].is_boolean()) { continue; }
        json newList;
        newList = json::object();
        newList["showOnWaterfall"] = true;
        newList["bookmarks"] = list;
        config.conf["lists"][listName] = newList;
    }
    config.release(true);
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new BookmarkManagerModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (BookmarkManagerModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}

