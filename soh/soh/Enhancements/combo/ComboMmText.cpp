// soh/soh/Enhancements/combo/ComboMmText.cpp — ComboShip: Majora's Mask messages in OOT's textbox.
//
// MM items used in Hyrule answer with MM's own lines (e.g. a trade item shown with nobody to take it: MM message 0xFE
// "This item doesn't seem to work here..."). The text is read from MM's resident archive
// (text/message_data_static/message_data_static, MM's message table as 2S2H exports it) and its control codes are
// translated to OOT's (colours, newline, box breaks, quick text, choices, button glyphs); MM-only codes with no OOT
// equivalent (timers, delays, MM sfx) are dropped. The box is OOT's (OOT's message system draws it).
//
// Use (C): u16 id = Combo_MmTextPrepare(mmTextId); then show OOT text `id` the usual way (actor.textId /
// Message_StartTextbox). The OnOpenText hook for TEXT_COMBO_MM_TEXT loads the prepared MM message.
#ifdef COMBO_BUILD
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include <libultraship/libultraship.h>
#include <ship/resource/CrossRMRegistry.h>

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

namespace {

struct MmMessage {
    uint8_t boxType;
    uint8_t boxPos;
    std::string raw; // MM-coded bytes, ending with 0xBF
};

std::unordered_map<uint16_t, MmMessage> sTable;
bool sLoaded = false;
uint16_t sPending = 0xFFFF;

// 2S2H's text resource: a 0x40-byte resource header, u32 count, then per message: u16 id, u8 box type, u8 box
// position, 8 header bytes (icon, next id, prices), u32 length, the message bytes.
void LoadTable() {
    if (sLoaded) {
        return;
    }
    sLoaded = true;
    auto rm = Ship::CrossRMRegistry::Get("mm");
    if (rm == nullptr) {
        return;
    }
    std::shared_ptr<Ship::File> file;
    try {
        file = rm->GetArchiveManager()->LoadFile("text/message_data_static/message_data_static");
    } catch (...) { return; }
    if ((file == nullptr) || (file->Buffer == nullptr)) {
        return;
    }
    const std::vector<char>& d = *file->Buffer;
    auto rd32 = [&](size_t p) -> uint32_t {
        return (uint8_t)d[p] | ((uint8_t)d[p + 1] << 8) | ((uint8_t)d[p + 2] << 16) | ((uint32_t)(uint8_t)d[p + 3] << 24);
    };
    size_t p = 0x40;
    if (d.size() < p + 4) {
        return;
    }
    const uint32_t count = rd32(p);
    p += 4;
    for (uint32_t i = 0; (i < count) && (p + 16 <= d.size()); i++) {
        const uint16_t id = (uint8_t)d[p] | ((uint8_t)d[p + 1] << 8);
        MmMessage m;
        m.boxType = (uint8_t)d[p + 2];
        m.boxPos = (uint8_t)d[p + 3];
        const uint32_t len = rd32(p + 12);
        if (p + 16 + len > d.size()) {
            break;
        }
        m.raw.assign(&d[p + 16], len);
        sTable[id] = std::move(m);
        p += 16 + len;
    }
    fprintf(stderr, "[ComboShip] MM text: %zu messages read from MM's archive\n", sTable.size());
}

// MM colour index -> OOT colour (0x40 + OOT index). OOT has no silver/orange: silver -> default, orange -> yellow.
const uint8_t kColor[9] = { 0x40, 0x41, 0x42, 0x43, 0x46, 0x44, 0x45, 0x40, 0x46 };

std::string Translate(const std::string& mm) {
    std::string out;
    for (size_t i = 0; i < mm.size(); i++) {
        const uint8_t c = (uint8_t)mm[i];
        auto skip = [&](size_t n) { i += n; };
        if (c <= 0x08) {
            out += '\x05';
            out += (char)kColor[c];
        } else if (c == 0x0A) {
            skip(1); // text speed + its arg
        } else if (c >= 0x0B && c <= 0x0F) {
            // MM counters (boat archery, stray fairies, tokens, points): nothing in Hyrule to count
        } else if (c == 0x10 || c == 0x12) {
            out += '\x04'; // box break
        } else if (c == 0x11) {
            out += '\x01'; // newline
        } else if (c == 0x13) {
            // carriage return: no OOT equivalent
        } else if (c == 0x14) {
            out += '\x06'; // shift
            if (i + 1 < mm.size()) {
                out += mm[++i];
            }
        } else if (c == 0x15) {
            // continue
        } else if (c == 0x16) {
            out += '\x0F'; // player name
        } else if (c == 0x17) {
            out += '\x08';
        } else if (c == 0x18) {
            out += '\x09';
        } else if (c == 0x19) {
            out += '\x0B'; // event
        } else if (c == 0x1A) {
            out += '\x0A'; // persistent
        } else if (c == 0x1B) {
            out += '\x0C'; // box break delayed: OOT takes one byte
            const int frames = (i + 2 < mm.size()) ? (((uint8_t)mm[i + 1] << 8) | (uint8_t)mm[i + 2]) : 0;
            out += (char)(frames > 0xFF ? 0xFF : frames);
            skip(2);
        } else if (c == 0x1C || c == 0x1D) {
            out += '\x0E'; // fade
            const int frames = (i + 2 < mm.size()) ? (((uint8_t)mm[i + 1] << 8) | (uint8_t)mm[i + 2]) : 0;
            out += (char)(frames > 0xFF ? 0xFF : frames);
            skip(2);
        } else if (c == 0x1E || c == 0x1F) {
            skip(2); // MM sfx id / delay: not OOT's
        } else if (c >= 0xB0 && c <= 0xBB) {
            out += (char)(0x9F + (c - 0xB0)); // A B C L R Z C-up C-down C-left C-right Z-target stick
        } else if (c == 0xBF) {
            break;
        } else if (c == 0xC1) {
            // background: not used by MM's item lines
        } else if (c == 0xC2) {
            out += '\x1B';
        } else if (c == 0xC3) {
            out += '\x1C';
        } else if (c >= 0xC4) {
            // MM timers / counters / codes: no Hyrule meaning
        } else {
            out += (char)c;
        }
    }
    out += '\x02';
    return out;
}

void OnOpen(uint16_t* textId, bool* loadFromMessageTable) {
    LoadTable();
    auto it = sTable.find(sPending);
    if (it == sTable.end()) {
        return; // OOT shows whatever it has for the id (nothing): never reached when Prepare succeeded
    }
    const TextBoxType type = (it->second.boxType <= 3) ? (TextBoxType)it->second.boxType : TEXTBOX_TYPE_BLACK;
    CustomMessage msg(Translate(it->second.raw), type, TEXTBOX_POS_BOTTOM);
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

void Register() {
    COND_ID_HOOK(OnOpenText, TEXT_COMBO_MM_TEXT, true, OnOpen);
}

RegisterShipInitFunc sInit(Register, {});

} // namespace

extern "C" {

// Prepares MM message `mmTextId`; returns the OOT text id that shows it (0 if MM's text is unavailable).
u16 Combo_MmTextPrepare(u16 mmTextId) {
    LoadTable();
    if (sTable.find(mmTextId) == sTable.end()) {
        return 0;
    }
    sPending = mmTextId;
    return TEXT_COMBO_MM_TEXT;
}

} // extern "C"

#endif // COMBO_BUILD
