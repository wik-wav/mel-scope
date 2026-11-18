// Gui.h -- Win32 + GDI+ editor window for the mel spectrogram.
#pragma once

#include "Plugin.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace melscope {

class Gui {
public:
    explicit Gui(App& app);
    ~Gui();

    // All of these run on the host's main (GUI) thread.
    bool create(HWND parent, int width, int height);
    void setResizeCallback(std::function<void()> cb) { onScaleResize_ = std::move(cb); }
    void destroy();
    void show();
    void hide();
    void setSize(int width, int height);
    void getSize(int& width, int& height) const { width = wantW_; height = wantH_; }
    void setScale(double s);
    HWND hwnd() const { return hwnd_; }
    void markDirty() { dirty_ = true; }

private:
    struct Row {
        int pid = -1;      // parameter id, or -1 for a group header
        float y = 0, h = 0;
        std::string title;
    };
    struct Dropdown {
        bool open = false;
        int pid = -1;
        int hover = -1;
        std::vector<int> items;
        float x = 0, y = 0, w = 0, itemH = 0;
    };

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void layout();
    void ensureBuffer();
    void paint();
    void drawPanel();
    void drawHeader();
    void drawGridOverlay();
    void drawDropdown();
    void drawHelp();
    void helpCard(float& x, float& y, float& w, float& h) const;
    void drawKeyboard();
    void collectKeyGuides(std::vector<int>& rows, std::vector<int>& boosts);
    int rowAt(int x, int y) const;
    int dropdownItemAt(int x, int y) const;
    void beginDrag(int pid, int mx);
    void updateDrag(int mx);
    void openDropdown(int pid);
    void closeDropdown();
    void setStatus(const std::string& text);
    // host DPI scale times the user's UI scale parameter
    void applyUserScale();

    void zoomFrequency(int mouseY, int wheelDelta);
    void panBarRect(float& x, float& y, float& w, float& h) const;
    void clampView();
    void panViewTo(float mouseY);
    void savePresetDialog();
    void loadPresetDialog();
    bool buttonRect(int index, float& x, float& y, float& w, float& h) const;
    bool rowBlocked(int pid) const;
    bool paramEditable(int pid) const;
    void beginEdit(int pid);
    void commitEdit();
    void cancelEdit();
    int editPid_ = -1;
    int editPendingPid_ = -1; // pressed on the value: click types, drag slides
    int editPendingX_ = 0;
    std::string editText_;
    bool editFresh_ = false;
    void stepperRects(float rowY, float rowH, float innerX, float innerW, float* out) const;
    void formatValue(int pid, float v, char* out, int cap) const;

    App& app_;
    HWND hwnd_ = nullptr;
    int width_ = 0, height_ = 0;   // physical pixels
    int wantW_ = 1180, wantH_ = 720; // requested (physical)
    float scale_ = 1.0f;      // effective: host scale * UI scale parameter
    float hostScale_ = 1.0f;  // what the host reported
    bool scaleResizePending_ = false;
    unsigned long long scaleResizeAt_ = 0;
    std::function<void()> onScaleResize_;

    HDC memDC_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HBITMAP oldBitmap_ = nullptr;
    void* pixels_ = nullptr;
    int bufW_ = 0, bufH_ = 0;

    // layout (physical px)
    int pad_ = 14, headerH_ = 42, panelW_ = 290, rowH_ = 26, groupH_ = 26;
    int plotX_ = 0, plotY_ = 0, plotW_ = 0, plotH_ = 0;
    int keysW_ = 44; // piano gutter on the left of the plot
    int gearX_ = 0, gearY_ = 0, gearW_ = 0, gearH_ = 0;
    int freezeX_ = 0, freezeY_ = 0, freezeW_ = 0, freezeH_ = 0;
    int rangeX_ = 0, rangeY_ = 0, rangeW_ = 0, rangeH_ = 0;
    int helpX_ = 0, helpY_ = 0, helpW_ = 0, helpH_ = 0;
    bool showHelp_ = false;
    float helpScroll_ = 0.0f;
    float helpContent_ = 0.0f;
    float helpView_ = 0.0f;
    std::string statusMsg_;
    double statusUntil_ = 0.0;
    bool showPanel_ = false;
    float scrollY_ = 0.0f;
    float contentH_ = 0.0f;
    int panelX_ = 0, panelY_ = 0, panelH_ = 0;
    int buttonY_ = 0, buttonH_ = 0;
    int listBottom_ = 0;
    std::vector<Row> rows_;
    std::vector<mel::MidiNote> notes_; // snapshot handed to the renderer

    int hoverRow_ = -1, dragPid_ = -1, hoverX_ = -1, hoverY_ = -1;
    bool dirty_ = true;
    float shownFmin_ = 30.0f, shownFmax_ = 1500.0f; // what is on screen (the view window)
    float viewFmin_ = 30.0f, viewFmax_ = 1500.0f;    // zoom window inside the analysed range
    float bankFmin_ = 30.0f, bankFmax_ = 1500.0f;    // the range the mel bank was built for
    bool viewReady_ = false;
    bool keepView_ = false;
    float reqFmin_ = 30.0f, reqFmax_ = 1500.0f;
    bool panDrag_ = false;
    float panStartY_ = 0.0f;
    float panStartLo_ = 0.0f;
    float panStartHi_ = 0.0f;
    int shownBands_ = 0, shownFft_ = 0, shownHop_ = 0;
    int curAxis_ = 0;
    bool plotLight_ = false;
    int dragStartX_ = 0;
    float dragStartValue_ = 0;
    double lastPaintMs_ = 0;
    double lastPaintTime_ = 0;
    double fps_ = 0;

    Dropdown dropdown_;

    std::unique_ptr<struct GdiResources> res_;
};

} // namespace melscope
