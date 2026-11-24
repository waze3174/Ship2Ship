// combo/menu/ComboPauseHarness.h — ComboShip debug (test harness only): scripted pad input for the merged pause
// menus. Header-only; each game feeds the result into its own Input struct at the start of its game-state main.
//
// COMBO_DEBUG_PAUSE=<step>,<step>,...   (OOT; COMBO_DEBUG_MM_PAUSE for MM) runs once a save is in play:
//   wN            wait N frames (each other step also takes one frame)
//   start a b z r l cup cdown cleft cright dup ddown dleft dright
//                 press that button for one frame
//   left right up down
//                 push the stick that way for one frame (one cursor step)
//   hcup          hold C-up through the NEXT step (the D-pad-on-pause modifier: "hcup,dleft")
//   +X / -X       start / stop holding button X (any name above) across every following step ("+a,w30,b,-a")
//   +up +down +left +right / -stick
//                 start / stop holding the stick that way
//   dump          log the pause/tab/button state (the game's own dump callback)
//   dpad          turn the game's D-pad item equips on
//   face          turn Link around to face the camera (screenshots of what he wears)
//   give=XX       (test data only) put the OTHER game's item XX (hex) into its dormant save, at its home slot
//   slot=N        put the item-page cursor on slot N (0-23)
//   stall=N       hold the next page turn at its halfway point for N frames (mid-rotation screenshots)
//   cross=XXXX    switch games through that (hex) entrance of the other game, as a crossed door does
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ComboPauseHarness {

enum Pad : uint16_t {
    PAD_A = 0x8000,
    PAD_B = 0x4000,
    PAD_Z = 0x2000,
    PAD_START = 0x1000,
    PAD_DUP = 0x0800,
    PAD_DDOWN = 0x0400,
    PAD_DLEFT = 0x0200,
    PAD_DRIGHT = 0x0100,
    PAD_L = 0x0020,
    PAD_R = 0x0010,
    PAD_CUP = 0x0008,
    PAD_CDOWN = 0x0004,
    PAD_CLEFT = 0x0002,
    PAD_CRIGHT = 0x0001,
};

struct Frame {
    uint16_t press = 0; // pressed this frame (also held)
    uint16_t hold = 0;  // held, not newly pressed
    int8_t stickX = 0;
    int8_t stickY = 0;
    bool dump = false;
    bool dpadOn = false;
    bool faceCam = false;
    int give = -1;
    int cross = -1;
    int slot = -1;
    int stall = -1;
    bool done = false;
};

class Script {
  public:
    explicit Script(const char* env) {
        if (env == nullptr)
            return;
        std::string s(env);
        size_t pos = 0;
        while (pos <= s.size()) {
            size_t comma = s.find(',', pos);
            if (comma == std::string::npos)
                comma = s.size();
            std::string tok = s.substr(pos, comma - pos);
            if (!tok.empty())
                mSteps.push_back(tok);
            pos = comma + 1;
        }
        mActive = !mSteps.empty();
    }
    bool Active() const {
        return mActive;
    }
    // One frame of input. Call once per game frame while the save is in play.
    Frame Step() {
        Frame f;
        if (!mActive || mIndex >= mSteps.size()) {
            f.done = true;
            return f;
        }
        f.stickX = mStickX;
        f.stickY = mStickY;
        if (mWait > 0) {
            mWait--;
            f.hold = mHeld | mSticky;
            return f;
        }
        const std::string& t = mSteps[mIndex++];
        uint16_t held = mHeld;
        mHeld = 0;
        f.hold = held | mSticky;
        if (t[0] == 'w' && t.size() > 1 && isdigit((unsigned char)t[1])) {
            mWait = atoi(t.c_str() + 1);
            return f;
        }
        static const struct {
            const char* name;
            uint16_t bit;
        } kBtns[] = { { "a", PAD_A },         { "b", PAD_B },           { "z", PAD_Z },     { "r", PAD_R },
                      { "l", PAD_L },         { "start", PAD_START },   { "cup", PAD_CUP }, { "cdown", PAD_CDOWN },
                      { "cleft", PAD_CLEFT }, { "cright", PAD_CRIGHT }, { "dup", PAD_DUP }, { "ddown", PAD_DDOWN },
                      { "dleft", PAD_DLEFT }, { "dright", PAD_DRIGHT } };
        for (const auto& b : kBtns) {
            if (t == b.name) {
                f.press = b.bit;
                return f;
            }
            if ((t.size() > 1) && (t[0] == '+') && (t.compare(1, std::string::npos, b.name) == 0)) {
                f.press = b.bit;
                mSticky |= b.bit;
                f.hold |= b.bit;
                return f;
            }
            if ((t.size() > 1) && (t[0] == '-') && (t.compare(1, std::string::npos, b.name) == 0)) {
                mSticky &= (uint16_t)~b.bit;
                f.hold &= (uint16_t)~b.bit;
                return f;
            }
        }
        if (t == "+up" || t == "+down" || t == "+left" || t == "+right" || t == "-stick") {
            mStickX = (t == "+left") ? -60 : (t == "+right") ? 60 : 0;
            mStickY = (t == "+up") ? 60 : (t == "+down") ? -60 : 0;
            f.stickX = mStickX;
            f.stickY = mStickY;
            return f;
        }
        if (t == "left")
            f.stickX = -60;
        else if (t == "right")
            f.stickX = 60;
        else if (t == "up")
            f.stickY = 60;
        else if (t == "down")
            f.stickY = -60;
        else if (t == "hcup") {
            f.press = PAD_CUP;
            mHeld = PAD_CUP;
        } else if (t == "dump")
            f.dump = true;
        else if (t == "dpad")
            f.dpadOn = true;
        else if (t == "face")
            f.faceCam = true;
        else if (t.rfind("give=", 0) == 0)
            f.give = (int)strtol(t.c_str() + 5, nullptr, 16);
        else if (t.rfind("stall=", 0) == 0)
            f.stall = atoi(t.c_str() + 6);
        else if (t.rfind("slot=", 0) == 0)
            f.slot = atoi(t.c_str() + 5);
        else if (t.rfind("cross=", 0) == 0)
            f.cross = (int)strtol(t.c_str() + 6, nullptr, 16);
        return f;
    }

  private:
    std::vector<std::string> mSteps;
    size_t mIndex = 0;
    int mWait = 0;
    uint16_t mHeld = 0;
    uint16_t mSticky = 0;
    int8_t mStickX = 0;
    int8_t mStickY = 0;
    bool mActive = false;
};

} // namespace ComboPauseHarness
