// combo/rando/ComboBackToBackLocks.h
// ComboShip: what Ocarina of Time's items may do in Termina in a back-to-back slot (vanilla OoT, then vanilla MM). Pure
// rules, header-only and std only, so 2ship.dll, soh.dll and the unit test share them. See
// docs/deviations/back2back.md.
//
// While the locks are in force (a back-to-back slot, before both games are beaten) an OoT item that Majora's Mask has
// too stays out of Termina until Link has found MM's own copy, unless its switch on the Back to back page lets it in
// early; an OoT-only item stays out unless its switch is on. Once both games are beaten nothing is locked.
#pragma once

#include "ComboBackToBackSettings.h"
#include "ComboCrossGate.h"

namespace ComboRando::BackToBack {

enum class LockKind {
    UntilMmHolds, // MM has the item too: locked until MM's own copy is found, unless its early-use switch is on
    Bottle,       // an OoT bottle or what is in it: needs the Bottles switch, and waits for MM's first bottle
    Option,       // OoT only: locked unless its switch is on
    Free,         // never locked (the Goron and Zora masks: worn as plain masks until MM's own is found)
    Closed,       // OoT only and no switch: locked until both games are beaten
};

struct ItemLock {
    LockKind kind;
    int option; // the Option bit: the early-use switch for UntilMmHolds, the item's switch for Option
};

// OoT item ids (soh/include/z64item.h).
namespace OotItem {
inline constexpr int kOotStick = 0x00;
inline constexpr int kOotNut = 0x01;
inline constexpr int kOotBomb = 0x02;
inline constexpr int kOotBow = 0x03;
inline constexpr int kOotArrowFire = 0x04;
inline constexpr int kOotDinsFire = 0x05;
inline constexpr int kOotSlingshot = 0x06;
inline constexpr int kOotOcarinaFairy = 0x07;
inline constexpr int kOotOcarinaTime = 0x08;
inline constexpr int kOotBombchu = 0x09;
inline constexpr int kOotHookshot = 0x0A;
inline constexpr int kOotLongshot = 0x0B;
inline constexpr int kOotArrowIce = 0x0C;
inline constexpr int kOotFaroresWind = 0x0D;
inline constexpr int kOotBoomerang = 0x0E;
inline constexpr int kOotLens = 0x0F;
inline constexpr int kOotBean = 0x10;
inline constexpr int kOotHammer = 0x11;
inline constexpr int kOotArrowLight = 0x12;
inline constexpr int kOotNayrusLove = 0x13;
inline constexpr int kOotBottleFirst = 0x14; // the empty bottle
inline constexpr int kOotLetterRuto = 0x1B;  // in a bottle, but OoT's alone
inline constexpr int kOotBottleLast = 0x20;  // Poe
inline constexpr int kOotMaskKeaton = 0x24;
inline constexpr int kOotMaskBunny = 0x27;
inline constexpr int kOotMaskGoron = 0x28;
inline constexpr int kOotMaskZora = 0x29;
inline constexpr int kOotMaskTruth = 0x2B;
} // namespace OotItem

inline ItemLock ClassifyOotItem(int ootItem) {
    switch (ootItem) {
        case OotItem::kOotStick:
            return { LockKind::UntilMmHolds, kEarlySticks };
        case OotItem::kOotNut:
            return { LockKind::UntilMmHolds, kEarlyNuts };
        case OotItem::kOotBomb:
            return { LockKind::UntilMmHolds, kEarlyBombs };
        case OotItem::kOotBow:
            return { LockKind::UntilMmHolds, kEarlyBow };
        case OotItem::kOotArrowFire:
            return { LockKind::UntilMmHolds, kEarlyFireArrows };
        case OotItem::kOotArrowIce:
            return { LockKind::UntilMmHolds, kEarlyIceArrows };
        case OotItem::kOotArrowLight:
            return { LockKind::UntilMmHolds, kEarlyLightArrows };
        case OotItem::kOotOcarinaFairy:
        case OotItem::kOotOcarinaTime:
            return { LockKind::UntilMmHolds, kEarlyOcarina };
        case OotItem::kOotBombchu:
            return { LockKind::UntilMmHolds, kEarlyBombchus };
        case OotItem::kOotHookshot:
        case OotItem::kOotLongshot:
            return { LockKind::UntilMmHolds, kEarlyHookshot };
        case OotItem::kOotLens:
            return { LockKind::UntilMmHolds, kEarlyLens };
        case OotItem::kOotBean:
            return { LockKind::UntilMmHolds, kEarlyBeans };
        case OotItem::kOotMaskKeaton:
            return { LockKind::UntilMmHolds, kEarlyKeatonMask };
        case OotItem::kOotMaskBunny:
            return { LockKind::UntilMmHolds, kEarlyBunnyHood };
        case OotItem::kOotMaskTruth:
            return { LockKind::UntilMmHolds, kEarlyMaskOfTruth };
        case OotItem::kOotMaskGoron:
        case OotItem::kOotMaskZora:
            return { LockKind::Free, 0 };
        case OotItem::kOotDinsFire:
            return { LockKind::Option, kDinsFire };
        case OotItem::kOotSlingshot:
            return { LockKind::Option, kSlingshot };
        case OotItem::kOotFaroresWind:
            return { LockKind::Option, kFaroresWind };
        case OotItem::kOotBoomerang:
            return { LockKind::Option, kBoomerang };
        case OotItem::kOotHammer:
            return { LockKind::Option, kHammer };
        case OotItem::kOotNayrusLove:
            return { LockKind::Option, kNayrusLove };
        default:
            break;
    }
    if (ootItem >= OotItem::kOotBottleFirst && ootItem <= OotItem::kOotBottleLast && ootItem != OotItem::kOotLetterRuto)
        return { LockKind::Bottle, 0 };
    return { LockKind::Closed, 0 };
}

// OoT's tunics and boots, which have no inventory slot.
inline ItemLock GearLock(bool boots) {
    return { LockKind::Option, boots ? kBoots : kTunics };
}

// mmHoldsCounterpart: Link has found MM's own copy (for a bottle: any bottle at all).
inline bool Locked(int options, ItemLock lock, bool mmHoldsCounterpart) {
    switch (lock.kind) {
        case LockKind::UntilMmHolds:
            return (options & lock.option) == 0 && !mmHoldsCounterpart;
        case LockKind::Bottle:
            return (options & kBottlesCarry) == 0 || ((options & kEarlyBottles) == 0 && !mmHoldsCounterpart);
        case LockKind::Option:
            return (options & lock.option) == 0;
        case LockKind::Free:
            return false;
        case LockKind::Closed:
            return true;
    }
    return true;
}

// The locks are in force: a back-to-back slot that has not yet beaten both games. Every other slot has none, and a
// slot that has beaten both has everything usable in both games.
inline bool LocksActive(int crossGateMask) {
    return CrossGate::IsBackToBack(crossGateMask) && !CrossGate::ToTerminaOpen(crossGateMask);
}

// An item the loaded slot keeps out of Termina right now: Locked(), but only while the locks are in force.
inline bool LockedInTermina(int crossGateMask, int options, ItemLock lock, bool mmHoldsCounterpart) {
    return LocksActive(crossGateMask) && Locked(options, lock, mmHoldsCounterpart);
}

} // namespace ComboRando::BackToBack
