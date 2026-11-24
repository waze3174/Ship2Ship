#include "libultraship/libultraship.h"
#include <SDL2/SDL.h>
#include <ratio>

// Establish a chrono duration for the N64 46.875MHz clock rate
typedef std::ratio<3000, 64> n64ClockRatio;
typedef std::ratio_divide<std::micro, n64ClockRatio> n64CycleRate;
typedef std::chrono::duration<long long, n64CycleRate> n64CycleRateDuration;

extern "C" {
uint8_t __osMaxControllers = MAXCONTROLLERS;
uint64_t __osCurrentTime = 0;

int32_t osContInit(OSMesgQueue* mq, uint8_t* controllerBits, OSContStatus* status) {
    *controllerBits = 0;
    status->status |= 1;

    // The SDL game-controller subsystem and mappings are brought up earlier, in Context::InitControlDeck,
    // so controllers work in pre-game UI. ControlDeck::Init stays here as it needs the game's controllerBits.
    Ship::Context::GetRawInstance()->GetControlDeck()->Init(controllerBits);

    return 0;
}

int32_t osContStartReadData(OSMesgQueue* mesg) {
    return 0;
}

#if defined(COMBO_BUILD) && defined(COMBO_HARNESS)
// ComboShip test harness ONLY (built with -DCOMBO_HARNESS=ON; release builds have none of this): a virtual controller
// on port 1, the one place both games read pads, so tests drive the games exactly as a real pad does and gameplay
// code never fakes input. Switched on only by the env var COMBO_VPAD at launch (never by any file):
//   COMBO_VPAD="<step>,<step>,..."   step = <frames>:<buttons>[@<stickX>/<stickY>]   e.g. 300:,10:B,60:B@0/80,5:
//   buttons: any of A B Z R L S(tart) CU CD CL CR DU DD DL DR joined with '+', empty = nothing held
//   a step "loop" restarts the script; when the script ends the virtual pad releases everything.
// The virtual pad's buttons are OR-ed with the real pad's, its stick replaces the real stick while non-zero.
#include <cstdlib>
#include <string>
#include <vector>
#include "spdlog/spdlog.h"
namespace {
struct VpadStep {
    int frames;
    uint16_t buttons;
    int8_t sx, sy;
};
std::vector<VpadStep> sVpad;
bool sVpadLoop = false;
bool sVpadParsed = false;
size_t sVpadIdx = 0;
int sVpadLeft = 0;

uint16_t VpadButton(const std::string& t) {
    static const struct {
        const char* n;
        uint16_t b;
    } kMap[] = { { "A", BTN_A },      { "B", BTN_B },         { "Z", BTN_Z },         { "R", BTN_R },
                 { "L", BTN_L },      { "S", BTN_START },     { "CU", BTN_CUP },      { "CD", BTN_CDOWN },
                 { "CL", BTN_CLEFT }, { "CR", BTN_CRIGHT },   { "DU", BTN_DUP },      { "DD", BTN_DDOWN },
                 { "DL", BTN_DLEFT }, { "DR", BTN_DRIGHT } };
    for (const auto& m : kMap) {
        if (t == m.n) {
            return m.b;
        }
    }
    return 0;
}

void VpadParse() {
    sVpadParsed = true;
    const char* env = std::getenv("COMBO_VPAD");
    if (env == nullptr) {
        return;
    }
    std::string all(env);
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t end = all.find(',', pos);
        std::string tok = all.substr(pos, (end == std::string::npos) ? std::string::npos : end - pos);
        pos = (end == std::string::npos) ? all.size() + 1 : end + 1;
        if (tok.empty()) {
            continue;
        }
        if (tok == "loop") {
            sVpadLoop = true;
            break;
        }
        VpadStep st{ 1, 0, 0, 0 };
        size_t colon = tok.find(':');
        st.frames = std::atoi(tok.substr(0, colon).c_str());
        std::string rest = (colon == std::string::npos) ? "" : tok.substr(colon + 1);
        size_t at = rest.find('@');
        if (at != std::string::npos) {
            std::string stick = rest.substr(at + 1);
            size_t slash = stick.find('/');
            st.sx = (int8_t)std::atoi(stick.substr(0, slash).c_str());
            st.sy = (slash == std::string::npos) ? 0 : (int8_t)std::atoi(stick.substr(slash + 1).c_str());
            rest = rest.substr(0, at);
        }
        size_t b = 0;
        while (b < rest.size()) {
            size_t plus = rest.find('+', b);
            st.buttons |= VpadButton(rest.substr(b, (plus == std::string::npos) ? std::string::npos : plus - b));
            b = (plus == std::string::npos) ? rest.size() : plus + 1;
        }
        sVpad.push_back(st);
    }
    if (!sVpad.empty()) {
        sVpadLeft = sVpad[0].frames;
        SPDLOG_INFO("[ComboShip] COMBO_VPAD: {} steps{}", sVpad.size(), sVpadLoop ? " (looping)" : "");
    }
}

void VpadApply(OSContPad* pad) {
    if (!sVpadParsed) {
        VpadParse();
    }
    while ((sVpadIdx < sVpad.size()) && (sVpadLeft <= 0)) {
        sVpadIdx++;
        if ((sVpadIdx >= sVpad.size()) && sVpadLoop) {
            sVpadIdx = 0;
        }
        if (sVpadIdx < sVpad.size()) {
            sVpadLeft = sVpad[sVpadIdx].frames;
        }
    }
    if (sVpadIdx >= sVpad.size()) {
        return;
    }
    const VpadStep& st = sVpad[sVpadIdx];
    pad[0].button |= st.buttons;
    if ((st.sx != 0) || (st.sy != 0)) {
        pad[0].stick_x = st.sx;
        pad[0].stick_y = st.sy;
    }
    sVpadLeft--;
}
} // namespace
#endif

void osContGetReadData(OSContPad* pad) {
    memset(pad, 0, sizeof(OSContPad) * __osMaxControllers);

    Ship::Context::GetRawInstance()->GetControlDeck()->WriteToPad(pad);
#if defined(COMBO_BUILD) && defined(COMBO_HARNESS)
    VpadApply(pad);
#endif
}

void osSetTime(OSTime time) {
    __osCurrentTime =
        std::chrono::duration_cast<n64CycleRateDuration>(std::chrono::steady_clock::now().time_since_epoch()).count() +
        time;
}

// Returns the OS time matching the N64 46.875MHz cycle rate
uint64_t osGetTime() {
    return std::chrono::duration_cast<n64CycleRateDuration>(std::chrono::steady_clock::now().time_since_epoch())
               .count() -
           __osCurrentTime;
}

// Returns the CPU clock count matching the N64 46.875Mhz cycle rate
uint32_t osGetCount() {
    return std::chrono::duration_cast<n64CycleRateDuration>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

OSPiHandle* osCartRomInit() {
    return NULL;
}

int osSetTimer(OSTimer* t, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    return 0;
}

int32_t osEPiStartDma(OSPiHandle* pihandle, OSIoMesg* mb, int32_t direction) {
    return 0;
}

uint32_t osAiGetLength() {
    // TODO: Implement
    return 0;
}

int32_t osAiSetNextBuffer(void* buff, size_t len) {
    // TODO: Implement
    return 0;
}

int32_t __osMotorAccess(OSPfs* pfs, uint32_t vibrate) {
    auto io = Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(pfs->channel)->GetRumble();
    if (vibrate) {
        io->StartRumble();
    } else {
        io->StopRumble();
    }

    return 0;
}

int32_t osMotorInit(OSMesgQueue* ctrlrqueue, OSPfs* pfs, int32_t channel) {
    pfs->channel = channel;
    return 0;
}
}
