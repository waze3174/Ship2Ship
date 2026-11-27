// combo/gui/ComboArchipelagoConsole.cpp — see ComboArchipelagoConsole.h
//
// The log is drawn line by line with the draw list instead of one widget per piece of text: the lines are coloured in
// pieces and wrap at word boundaries (ComboApConsole::WrapLine), and only the rows in view are drawn, so the 2000-line
// scrollback costs a handful of rows per frame.
//
// The reading side of the scrollback follows Archipelago's own text client (kvui.py, ArchipelagoMW/Archipelago at the
// pinned core): it keeps following the newest line unless the player scrolled up.
#include "ComboArchipelagoConsole.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>
#include <libultraship/libultraship.h>
#include <misc/cpp/imgui_stdlib.h>
#include <ship/Context.h>
#include <ship/window/gui/GuiWindow.h>

#include "ComboArchipelagoPanel.h"
#include "net/ComboApConsole.h"
#include "net/ComboApConsoleInput.h"

namespace ComboRando {

namespace {

constexpr const char* kPopOutCvar = "gCombo.Archipelago.ConsolePopOut";
constexpr const char* kWindowName = "Archipelago Console##Combo";
constexpr float kPanelLogLines = 14.0f; // the embedded log is this many text lines tall
constexpr float kWrapQuantum = 8.0f; // the wrap width changes in steps of this, so a resize does not re-wrap each frame
constexpr float kBottomSlack = 1.0f; // scrolled this close to the end counts as at the end
constexpr float kMinWrapWidth = 48.0f;
constexpr ImVec2 kWindowFirstSize = { 560.0f, 340.0f };

ImU32 ToImColor(ComboApConsole::Color color, ImU32 fallback) {
    if (color == ComboApConsole::kDefaultColor) {
        return fallback;
    }
    return IM_COL32((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 0xFF);
}

float MeasureText(const char* begin, const char* end) {
    return ImGui::CalcTextSize(begin, end).x;
}

// The copy of the launcher's scrollback this side draws, with each line's wrapping at the width last drawn.
class ConsoleView {
  public:
    // Takes the lines added since the last frame.
    void Refresh(const ComboApMenuApi& api) {
        const uint64_t revision = api.consoleRevision();
        if (revision == revision_) {
            return;
        }
        revision_ = revision;
        incoming_.clear();
        const bool dropped = api.consoleVisit(lastSequence_, &ConsoleView::Collect, this) != 0;
        if (dropped) {
            entries_.clear();
        }
        for (Entry& entry : incoming_) {
            lastSequence_ = std::max(lastSequence_, entry.sequence);
            entries_.push_back(std::move(entry));
        }
        while (entries_.size() > ComboApConsole::kMaxLines) {
            entries_.pop_front();
        }
        layoutDirty_ = true;
    }

    // The log in a scrolling child of `size`.
    void DrawLog(const char* id, const ImVec2& size) {
        ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - kBottomSlack;
        Layout(std::max(kMinWrapWidth, std::floor(ImGui::GetContentRegionAvail().x / kWrapQuantum) * kWrapQuantum));

        const ImVec2 origin = ImGui::GetCursorScreenPos(); // already scrolled
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float viewTop = ImGui::GetScrollY();
        const float viewBottom = viewTop + ImGui::GetWindowHeight();
        size_t first = LineAt(viewTop);
        for (size_t i = first; i < entries_.size() && tops_[i] < viewBottom; ++i) {
            DrawEntry(draw, entries_[i], origin.x, origin.y + tops_[i], lineHeight, textColor);
        }
        HandleContextMenu(origin.y);
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(layoutWidth_, tops_.empty() ? 0.0f : tops_.back()));
        if (wasAtBottom) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    // The input line. Returns true on the frame a line was sent.
    bool DrawInput(const ComboApMenuApi& api, bool connected) {
        const char* hint =
            connected ? "Chat, !command or /command; Enter sends" : "Not connected: type /connect or /help";
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool entered =
            ImGui::InputTextWithHint("##ApConsoleInput", hint, &input_,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                                     &ConsoleView::OnHistoryKey, this);
        if (!entered) {
            return false;
        }
        history_.Push(ComboApConsole::Trimmed(input_));
        api.consoleSubmit(input_.c_str());
        input_.clear();
        ImGui::SetKeyboardFocusHere(-1); // keep typing
        return true;
    }

    void CopyAll() const {
        std::string text;
        for (const Entry& entry : entries_) {
            text += entry.line.PlainText();
            text.push_back('\n');
        }
        ImGui::SetClipboardText(text.c_str());
    }

  private:
    struct Entry {
        uint64_t sequence = 0;
        ComboApConsole::Line line;
        std::vector<ComboApConsole::Row> rows; // at layoutWidth_
        float rowsWidth = -1.0f;
    };

    static void Collect(void* user, const ComboApConsoleLine* line) {
        Entry entry;
        entry.sequence = line->sequence;
        for (int32_t i = 0; i < line->spanCount; ++i) {
            entry.line.spans.push_back({ line->spans[i].color, line->spans[i].text });
        }
        static_cast<ConsoleView*>(user)->incoming_.push_back(std::move(entry));
    }

    static int OnHistoryKey(ImGuiInputTextCallbackData* data) {
        ConsoleView* self = static_cast<ConsoleView*>(data->UserData);
        const std::string current(data->Buf, static_cast<size_t>(data->BufTextLen));
        const std::string next =
            data->EventKey == ImGuiKey_UpArrow ? self->history_.Older(current) : self->history_.Newer(current);
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, next.c_str());
        return 0;
    }

    // Wraps what changed since the last frame (everything, after a resize) and recomputes where each line starts.
    void Layout(float width) {
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        if (width != layoutWidth_ || lineHeight != layoutLineHeight_) {
            layoutWidth_ = width;
            layoutLineHeight_ = lineHeight;
            layoutDirty_ = true;
        }
        if (!layoutDirty_) {
            return;
        }
        layoutDirty_ = false;
        tops_.assign(entries_.size() + 1, 0.0f);
        for (size_t i = 0; i < entries_.size(); ++i) {
            Entry& entry = entries_[i];
            if (entry.rowsWidth != layoutWidth_) {
                entry.rows = ComboApConsole::WrapLine(entry.line, layoutWidth_, &MeasureText);
                entry.rowsWidth = layoutWidth_;
            }
            tops_[i + 1] = tops_[i] + static_cast<float>(entry.rows.size()) * lineHeight;
        }
    }

    // The index of the line that holds the y offset (from the top of the content).
    size_t LineAt(float y) const {
        if (entries_.empty()) {
            return 0;
        }
        const auto next =
            std::upper_bound(tops_.begin(), tops_.begin() + static_cast<std::ptrdiff_t>(entries_.size()), y);
        return next == tops_.begin() ? 0 : static_cast<size_t>(next - tops_.begin()) - 1;
    }

    void DrawEntry(ImDrawList* draw, const Entry& entry, float x, float y, float lineHeight, ImU32 textColor) const {
        for (const ComboApConsole::Row& row : entry.rows) {
            for (const ComboApConsole::Piece& piece : row) {
                const ComboApConsole::Span& span = entry.line.spans[piece.span];
                const char* begin = span.text.data() + piece.begin;
                draw->AddText(ImVec2(x + piece.x, y), ToImColor(span.color, textColor), begin, begin + piece.length);
            }
            y += lineHeight;
        }
    }

    // A right click on a line offers to copy it.
    void HandleContextMenu(float originY) {
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !entries_.empty()) {
            menuLine_ = LineAt(ImGui::GetMousePos().y - originY);
            ImGui::OpenPopup("##ApConsoleLineMenu");
        }
        if (!ImGui::BeginPopup("##ApConsoleLineMenu")) {
            return;
        }
        if (menuLine_ < entries_.size() && ImGui::Selectable("Copy line")) {
            ImGui::SetClipboardText(entries_[menuLine_].line.PlainText().c_str());
        }
        if (ImGui::Selectable("Copy all")) {
            CopyAll();
        }
        ImGui::EndPopup();
    }

    std::deque<Entry> entries_;
    std::vector<Entry> incoming_;
    uint64_t revision_ = ~0ull;
    uint64_t lastSequence_ = 0;
    bool layoutDirty_ = true;
    float layoutWidth_ = -1.0f;
    float layoutLineHeight_ = -1.0f;
    std::vector<float> tops_; // tops_[i] is where line i starts; the last is the total height
    size_t menuLine_ = 0;
    std::string input_;
    ComboApConsole::InputHistory history_;
};

ConsoleView sView;

bool IsPoppedOutNow();
void SetPoppedOut(bool poppedOut);

// The buttons above the log.
void DrawToolbar(const ComboApMenuApi& api, bool poppedOut) {
    if (ImGui::Button("Clear")) {
        api.consoleClear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        sView.CopyAll();
    }
    ImGui::SameLine();
    if (ImGui::Button(poppedOut ? "Dock" : "Pop out")) {
        SetPoppedOut(!poppedOut);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Right-click a line to copy it.");
}

// The log and the input line, filling `logHeight` and one input row.
void DrawConsoleBody(const ComboApMenuApi& api, bool connected, float logHeight, bool poppedOut) {
    sView.Refresh(api);
    DrawToolbar(api, poppedOut);
    sView.DrawLog(poppedOut ? "##ApConsoleLogWindow" : "##ApConsoleLogPanel", ImVec2(-FLT_MIN, logHeight));
    sView.DrawInput(api, connected);
}

bool IsConnected(const ComboApMenuApi& api) {
    ComboApMenuState state{};
    api.getState(&state);
    return state.phase == COMBO_AP_PHASE_CONNECTED;
}

class ArchipelagoConsoleWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void Draw() override {
        if (!IsVisible()) {
            return;
        }
        auto ctx = Ship::Context::GetRawInstance();
        if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
            return;
        }
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
        ImGui::SetNextWindowSize(kWindowFirstSize, ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin(kWindowName, &open, ImGuiWindowFlags_NoFocusOnAppearing)) {
            DrawElement();
        }
        ImGui::End();
        if (!open) {
            SetVisibility(false);
        }
    }

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
        const ComboApMenuApi* api = ArchipelagoApi();
        if (api == nullptr || api->getState == nullptr) {
            ImGui::TextDisabled("Archipelago is not available yet.");
            return;
        }
        if (!ArchipelagoFileLoaded()) {
            ImGui::TextDisabled("No Archipelago file is loaded.");
            return;
        }
        const float inputRow = ImGui::GetFrameHeightWithSpacing();
        const float toolbarRow = ImGui::GetFrameHeightWithSpacing();
        const float logHeight =
            std::max(ImGui::GetTextLineHeight() * 4.0f, ImGui::GetContentRegionAvail().y - inputRow - toolbarRow);
        DrawConsoleBody(*api, IsConnected(*api), logHeight, true);
    }
};

std::shared_ptr<ArchipelagoConsoleWindow> sWindow;

bool IsPoppedOutNow() {
    return sWindow != nullptr && sWindow->IsVisible();
}

void SetPoppedOut(bool poppedOut) {
    if (sWindow == nullptr) {
        return;
    }
    if (poppedOut) {
        sWindow->Show();
    } else {
        sWindow->Hide();
    }
}

} // namespace

void DrawArchipelagoConsoleSection(const ComboApMenuApi& api, bool connected) {
    ImGui::SeparatorText("Console");
    if (api.consoleRevision == nullptr) {
        ImGui::TextDisabled("This build has no console.");
        return;
    }
    if (IsPoppedOutNow()) {
        ImGui::TextDisabled("The console is open in its own window.");
        if (ImGui::Button("Dock the console here")) {
            SetPoppedOut(false);
        }
        return;
    }
    DrawConsoleBody(api, connected, ImGui::GetTextLineHeightWithSpacing() * kPanelLogLines, false);
}

void RegisterArchipelagoConsoleWindow() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    if (!sWindow) {
        sWindow = std::make_shared<ArchipelagoConsoleWindow>(kPopOutCvar, kWindowName);
    }
    ctx->GetWindow()->GetGui()->AddGuiWindow(sWindow);
}

} // namespace ComboRando
