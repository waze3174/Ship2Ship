#!/usr/bin/env python3
"""Generate combo/rando/CrossDamageMap.h: how each OOT enemy reacts to the MM attacks the forms bring to Hyrule.

Every number comes from MM's own data: the hit enemy's MM DamageTable row for the attack (DMG_ENTRY(multiplier,
effect), parsed from mm/src/overlays/actors), and the meaning of that effect in the MM actor's damage handler (the
`mm` class maps below, read from each MM handler — see docs/combo/forms-audit.md "Form attacks vs enemies"). The OOT
side is only *how* the OOT actor shows that class: the OOT effect code its own handler uses for a stun, a freeze, a
burn, plain damage (the `oot` maps, read from each OOT handler).

Run from the repo root:  python3 scripts/combo/gen-cross-damage-map.py  (rewrites the header, prints the doc table
with --doc).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MM_ACTORS = os.path.join(ROOT, "mm", "src", "overlays", "actors")
OUT = os.path.join(ROOT, "combo", "rando", "CrossDamageMap.h")

# The MM attacks that reach Hyrule: (tag name, MM DMG_* bit index, what it is, MM AT damage).
ATTACKS = [
    ("DEKU_SPIN", 0x0F, "Deku spin", "1"),
    ("DEKU_BUBBLE", 0x10, "Deku bubble", "2"),
    ("GORON_PUNCH", 0x08, "Goron punch", "2"),
    ("GORON_POUND", 0x0A, "Goron pound", "4"),
    ("GORON_SPIKES", 0x0E, "Goron spike roll", "1"),
    ("NORMAL_ROLL", 0x1A, "Goron roll", "1"),
    ("ZORA_PUNCH", 0x17, "Zora jab / jump kick", "1/2"),
    ("ZORA_BOOMERANG", 0x04, "Zora fins", "2"),
    ("ZORA_BARRIER", 0x13, "Zora barrier", "2"),
    ("EXPLOSIVES", 0x03, "Blast Mask", "2"),
    ("POWDER_KEG", 0x1F, "Powder Keg", "4"),
    ("SWORD_BEAM", 0x19, "Fierce Deity beam", "3"),
]
CLASSES = ["OOT", "PASS", "DAMAGE", "STUN", "FIRE", "ICE", "LIGHT", "ELECTRIC", "KNOCKBACK", "NOREACT"]
MULT = [0.0, 1.0, 2.0, 0.5, 0.25, 3.0, 4.0] + [1.0] * 9

# One row per OOT enemy (variant -1 = all). mm: MM actor dir + table. mmcls: MM effect (enum name or hex) -> class,
# from the MM handler. oot: class -> the OOT actor's own effect code for it (from the OOT handler).
# override: attack -> class, where MM's collider masks change what the table alone says.
ROWS = [
    dict(id="ACTOR_EN_FIREFLY", var=-1, name="Keese", mm="ovl_En_Firefly:sDamageTable", mmname="Keese",
         mmcls={"KEESE_DMGEFF_NONE": "DAMAGE", "KEESE_DMGEFF_STUN": "STUN", "KEESE_DMGEFF_ELECTRIC": "ELECTRIC"},
         oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_DEKUBABA", var=-1, name="Deku Baba", mm="ovl_En_Dekubaba:sDamageTable", mmname="Deku Baba",
         mmcls={"DEKUBABA_DMGEFF_NONE": "DAMAGE", "DEKUBABA_DMGEFF_CUT": "DAMAGE_CUT",
                "DEKUBABA_DMGEFF_ELECTRIC": "ELECTRIC"},
         # MM CUT (sword-like: prunes it when stunned) = OOT's sword code 0xF; MM NONE (a plain hit) = OOT 0.
         oot={"DAMAGE": 0, "DAMAGE_CUT": 0xF, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_KAREBABA", var=-1, name="Withered Deku Baba", mm="ovl_En_Karebaba:sDamageTable", mmmask=True,
         mmname="Withered Deku Baba",
         mmcls={"KAREBABA_DMGEFF_NONE": "DAMAGE", "KAREBABA_DMGEFF_ELECTRIC": "DAMAGE"},
         oot={"DAMAGE": 0}),
    dict(id="ACTOR_EN_OKUTA", var=-1, name="Octorok", mm="ovl_En_Okuta:sDamageTable", mmname="Octorok",
         mmcls={"EN_OKUTA_DMGEFF_NONE": "DAMAGE"}, oot={"DAMAGE": 0}),
    dict(id="ACTOR_EN_ST", var=-1, name="Skulltula", mm="ovl_En_St:sDamageTable", mmname="Skulltula",
         mmcls={"0x0": "DAMAGE", "0x1": "STUN", "0x5": "ELECTRIC"}, oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_SW", var=-1, name="Skullwalltula / Gold Skulltula", mm="ovl_En_Sw:sDamageTable", mmmask=True,
         mmname="Skullwalltula / Gold Skulltula",
         # MM: the barrier (5, x1) only flinches and damages it — no stun.
         mmcls={"0x0": "DAMAGE", "0x5": "DAMAGE"}, oot={"DAMAGE": 0}),
    # MM Dodongo: the metal overlay (collider2, mask 0x77C34FE6) refuses these, so they reach the body.
    dict(id="ACTOR_EN_DODONGO", var=-1, name="Dodongo", mm="ovl_En_Dodongo:sDamageTable", mmname="Dodongo",
         hardpass=["DEKU_SPIN", "ZORA_BOOMERANG", "ZORA_BARRIER", "EXPLOSIVES", "POWDER_KEG"],
         mmcls={"0x0": "DAMAGE", "0xE": "DAMAGE", "0x1": "STUN", "0x5": "ELECTRIC"},
         oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_RR", var=-1, name="Like Like", mm="ovl_En_Rr:sDamageTable", mmname="Like Like",
         # MM: the barrier (5, x2) damages it with sparks and a long flinch — no stun.
         mmcls={"0x0": "DAMAGE", "0x1": "STUN", "0x5": "DAMAGE"}, oot={"DAMAGE": 0xF, "STUN": 1}),
    dict(id="ACTOR_EN_WF", var=0, name="Wolfos", mm="ovl_En_Wf:sDamageTable2", mmname="Wolfos",
         mmcls={"0x0": "DAMAGE", "0x1": "STUN", "0x5": "ELECTRIC"}, oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_WF", var=1, name="White Wolfos", mm="ovl_En_Wf:sDamageTable1", mmname="White Wolfos",
         mmcls={"0x0": "DAMAGE", "0x1": "STUN", "0x5": "ELECTRIC"}, oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_RD", var=-1, name="ReDead / Gibdo", mm="ovl_En_Rd:sDamageTable", mmname="ReDead / Gibdo",
         # MM RECOIL: the hit registers (fins bounce off) but the handler does nothing = OOT's ignored code 0.
         mmcls={"EN_RD_DMGEFF_DAMAGE": "DAMAGE", "EN_RD_DMGEFF_STUN": "STUN", "EN_RD_DMGEFF_RECOIL": "NOREACT",
                "EN_RD_DMGEFF_ZORA_MAGIC": "ELECTRIC", "EN_RD_DMGEFF_NONE": "NOREACT"},
         oot={"DAMAGE": 0xF, "STUN": 1, "ELECTRIC": 1, "NOREACT": 0}),
    dict(id="ACTOR_EN_TITE", var=-1, name="Tektite", mm="ovl_En_Tite:sDamageTable", mmname="Tektite",
         mmcls={"0x0": "DAMAGE", "0x1": "STUN", "0x5": "ELECTRIC"}, oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_TEST", var=-1, name="Stalfos", mm="ovl_En_Knight:sDamageTableStanding",
         mmname="Igos's knights (closest: skeletal sword-and-shield fighters)",
         # MM GORON_POUND: it hops back, no damage = OOT's flinch (code 0) at 0 damage.
         mmcls={"KNIGHT_DMGEFF_OTHER": "DAMAGE", "KNIGHT_DMGEFF_STUN": "STUN",
                "KNIGHT_DMGEFF_ZORA_BARRIER": "ELECTRIC", "KNIGHT_DMGEFF_GORON_POUND": "KNOCKBACK",
                "KNIGHT_DMGEFF_NONE": "NOREACT"},
         oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1, "KNOCKBACK": 0, "NOREACT": 0xD}),
    dict(id="ACTOR_EN_ZF", var=-1, name="Lizalfos / Dinolfos", mm="ovl_En_Dinofos:sDamageTable",
         mmname="Dinolfos",
         mmcls={"DINOLFOS_DMGEFF_NONE": "DAMAGE", "DINOLFOS_DMGEFF_STUN": "STUN",
                "DINOLFOS_DMGEFF_ZORA_MAGIC": "ELECTRIC"},
         oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_PEEHAT", var=-1, name="Peahat", mm="ovl_En_Peehat:sDamageTable", mmname="Peahat",
         mmcls={"0x0": "DAMAGE", "0x1": "STUN", "0x5": "ELECTRIC"},
         oot={"DAMAGE": 0, "STUN": 0xE, "ELECTRIC": 0xE}),
    dict(id="ACTOR_EN_BB", var=-1, name="Bubble (all colours)", mm="ovl_En_Bb:sDamageTable",
         mmname="Blue Bubble / Red Bubble (EnBb, EnBbfall: same rows)",
         # OOT 0xE = damage + flame knocked out (MM's NONE: hit + knockback); 0xF = the OOT stun.
         mmcls={"EN_BB_DMGEFF_NONE": "DAMAGE", "EN_BB_DMGEFF_STUN": "STUN", "EN_BB_DMGEFF_ZORA_MAGIC": "ELECTRIC"},
         oot={"DAMAGE": 0xE, "STUN": 0xF, "ELECTRIC": 0xF}),
    dict(id="ACTOR_EN_WALLMAS", var=-1, name="Wallmaster", mm="ovl_En_Wallmas:sDamageTable", mmname="Wallmaster",
         mmcls={"WALLMASTER_DMGEFF_NONE": "DAMAGE", "WALLMASTER_DMGEFF_STUN": "STUN",
                "WALLMASTER_DMGEFF_ZORA_MAGIC": "ELECTRIC"},
         oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_FLOORMAS", var=-1, name="Floormaster", mm="ovl_En_Floormas:sDamageTable",
         mmname="Floormaster",
         mmcls={"0x0": "DAMAGE", "0x1": "STUN", "0x5": "ELECTRIC"}, oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_CROW", var=-1, name="Guay", mm="ovl_En_Crow:sDamageTable", mmname="Guay",
         mmcls={"GUAY_DMGEFF_NONE": "DAMAGE", "GUAY_DMGEFF_STUN": "STUN", "GUAY_DMGEFF_ELECTRIC": "ELECTRIC"},
         oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_DEKUNUTS", var=-1, name="Mad Scrub", mm="ovl_En_Dekunuts:sDamageTable", mmname="Mad Scrub",
         # MM: the barrier (5, x1) damages + knocks back with sparks — no stun.
         mmcls={"0x0": "DAMAGE", "0x5": "DAMAGE"}, oot={"DAMAGE": 0}),
    dict(id="ACTOR_EN_IK", var=1, name="Iron Knuckle (armoured)", mm="ovl_En_Ik:sDamageTableArmor",
         mmname="Iron Knuckle (armoured)",
         # MM IMMUNE: registers, armour clank, nothing = OOT's metal-spark ignore code 0xD.
         mmcls={"DMG_EFF_NONE": "DAMAGE", "DMG_EFF_IMMUNE": "NOREACT"}, oot={"DAMAGE": 0xF, "NOREACT": 0xD}),
    dict(id="ACTOR_EN_IK", var=0, name="Iron Knuckle (armour off)", mm="ovl_En_Ik:sDamageTableNoArmor",
         mmname="Iron Knuckle (armour off)",
         mmcls={"DMG_EFF_NONE": "DAMAGE"}, oot={"DAMAGE": 0xF}),
    dict(id="ACTOR_EN_FZ", var=-1, name="Freezard", mm="ovl_En_Fz:sDamageTable", mmname="Freezard",
         mmcls={"0xF": "DAMAGE"}, oot={"DAMAGE": 0xF}),
    dict(id="ACTOR_EN_POH", var=-1, name="Poe", mm="ovl_En_Poh:sDamageTable", mmname="Poe",
         mmcls={"0x0": "DAMAGE"}, oot={"DAMAGE": 0}),
    dict(id="ACTOR_EN_PO_FIELD", var=0, name="Field Poe", mm="ovl_En_Poh:sDamageTable", mmname="Poe",
         mmcls={"0x0": "DAMAGE"}, oot={"DAMAGE": 0}),
    dict(id="ACTOR_EN_PO_FIELD", var=1, name="Big Poe", mm="ovl_En_Bigpo:sDamageTable", mmname="Big Poe",
         mmcls={"0x0": "DAMAGE"}, oot={"DAMAGE": 0}),
    dict(id="ACTOR_EN_PO_SISTERS", var=-1, name="Poe Sisters", mm="ovl_En_Po_Sisters:sDamageTable",
         mmname="Poe Sisters", mmcls={"POE_SISTERS_DMGEFF_NONE": "DAMAGE"}, oot={"DAMAGE": 0xE}),
    dict(id="ACTOR_EN_AM", var=-1, name="Armos", mm="ovl_En_Am:sDamageTable", mmname="Armos",
         # MM: any damaging hit knocks it back and (1 HP) kills it = OOT's KILL 15. MM's body collider refuses the
         # bubble and fins, so they hit the stone and ricochet = OOT's ricochet code 0.
         mmcls={"0x0": "DAMAGE", "0xF": "DAMAGE"}, oot={"DAMAGE": 15, "NOREACT": 0},
         override={"DEKU_BUBBLE": "NOREACT", "ZORA_BOOMERANG": "NOREACT"}),
    dict(id="ACTOR_EN_SKB", var=-1, name="Stalchild", mm="ovl_En_Skb:sDamageTable", mmname="Stalchild",
         # MM 0xC/0xD/0xF: damage + knockback; 0xB: barrier stun.
         mmcls={"0x0": "DAMAGE", "0xC": "DAMAGE", "0xD": "DAMAGE", "0xF": "DAMAGE", "0x1": "STUN",
                "0xB": "ELECTRIC"},
         oot={"DAMAGE": 0xE, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_SB", var=-1, name="Shellblade", mm="ovl_En_Sb:sDamageTable", mmname="Shellblade",
         mmcls={"0xF": "DAMAGE"}, oot={"DAMAGE": 0xF}),
    dict(id="ACTOR_EN_REEBA", var=-1, name="Leever", mm="ovl_En_Neo_Reeba:sDamageTable", mmname="Leever",
         mmcls={"EN_NEO_REEBA_DMGEFF_NONE": "DAMAGE", "EN_NEO_REEBA_DMGEFF_SHATTER": "DAMAGE",
                "EN_NEO_REEBA_DMGEFF_STUN": "STUN", "EN_NEO_REEBA_DMGEFF_ELECTRIC_STUN": "ELECTRIC"},
         oot={"DAMAGE": 0xE, "STUN": 1, "ELECTRIC": 1}),
    dict(id="ACTOR_EN_GELDB", var=-1, name="Gerudo fighter", mm="ovl_En_Kaizoku:sDamageTable",
         mmname="Gerudo pirate",
         # MM STUNNED_ONLY (the pound, x0): she jumps clear unless already stunned = no hit reaction (OOT code 6:
         # ignored). IFRAME_PROTECTED / ALWAYS_HIT: damage.
         mmcls={"KAIZOKU_DMGEFF_ALWAYS_HIT": "DAMAGE", "KAIZOKU_DMGEFF_IFRAME_PROTECTED": "DAMAGE",
                "KAIZOKU_DMGEFF_STUN": "STUN", "KAIZOKU_DMGEFF_ZORA_SHIELD": "ELECTRIC",
                "KAIZOKU_DMGEFF_STUNNED_ONLY": "NOREACT"},
         oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1, "NOREACT": 6}),
    dict(id="ACTOR_EN_DODOJR", var=-1, name="Baby Dodongo", mm="ovl_En_Rat:sDamageTable",
         mmname="Real Bombchu (closest: a small crawler that blows up when hit)",
         mmcls={"EN_RAT_DMGEFF_NONE": "DAMAGE", "EN_RAT_DMGEFF_STUN": "STUN"}, oot={"DAMAGE": 0, "STUN": 1}),
    dict(id="ACTOR_EN_BILI", var=-1, name="Biri", mm="ovl_En_Slime:sDamageTable",
         mmname="Chuchu (closest: a jelly blob)",
         # MM BLUNT (Goron blows, bubble, x0): it squashes and slides back, no damage = OOT's recoil 0xE.
         mmcls={"EN_SLIME_DMGEFF_NORMAL": "DAMAGE", "EN_SLIME_DMGEFF_BLUNT": "KNOCKBACK",
                "EN_SLIME_DMGEFF_ELECTRIC": "DAMAGE"},
         oot={"DAMAGE": 0xF, "KNOCKBACK": 0xE}),
    dict(id="ACTOR_EN_VALI", var=-1, name="Bari", mm="ovl_En_Slime:sDamageTable",
         mmname="Chuchu (closest: a jelly blob)",
         mmcls={"EN_SLIME_DMGEFF_NORMAL": "DAMAGE", "EN_SLIME_DMGEFF_BLUNT": "KNOCKBACK",
                "EN_SLIME_DMGEFF_ELECTRIC": "DAMAGE"},
         oot={"DAMAGE": 0xF, "KNOCKBACK": 0xE}),
    dict(id="ACTOR_EN_EIYER", var=-1, name="Stinger (sand)", mm="ovl_En_Pr:sDamageTable",
         mmname="Skullfish (closest: a lunging fish)",
         mmcls={"0xF": "DAMAGE", "0x5": "DAMAGE"}, oot={"DAMAGE": 0}),
    dict(id="ACTOR_EN_WEIYER", var=-1, name="Stinger (water)", mm="ovl_En_Pr:sDamageTable",
         mmname="Skullfish (closest: a lunging fish)",
         mmcls={"0xF": "DAMAGE", "0x5": "DAMAGE"}, oot={"DAMAGE": 0}),
    dict(id="ACTOR_EN_TORCH2", var=-1, name="Dark Link", mm="ovl_En_Jso:sDamageTable",
         mmname="Garo (closest: a sword duellist)",
         mmcls={"EN_JSO_DMGEFF_NONE": "DAMAGE", "EN_JSO_DMGEFF_STUN": "STUN",
                "EN_JSO_DMGEFF_ELECTRIC_STUN": "ELECTRIC"},
         oot={"DAMAGE": 0, "STUN": 1, "ELECTRIC": 1}),
]


def parse_tables():
    tables = {}
    for dp, _, files in os.walk(MM_ACTORS):
        for f in files:
            if not f.endswith(".c"):
                continue
            s = open(os.path.join(dp, f), errors="ignore").read()
            for m in re.finditer(r"DamageTable\s+(\w+)\s*=\s*\{(.*?)\};", s, re.S):
                rows = re.findall(r"DMG_ENTRY\(\s*([^,]+?)\s*,\s*([^)]+?)\s*\)", m.group(2))
                if len(rows) == 32:
                    tables[os.path.basename(dp) + ":" + m.group(1)] = rows
    return tables


def parse_enums(actor_dir):
    """Name -> value for every enum in the actor's sources (+ the shared DMG_EFF_* enum)."""
    vals = {}
    srcs = []
    d = os.path.join(MM_ACTORS, actor_dir)
    for f in os.listdir(d):
        if f.endswith((".c", ".h")):
            srcs.append(open(os.path.join(d, f), errors="ignore").read())
    srcs.append(open(os.path.join(ROOT, "mm", "include", "z64collision_check.h"), errors="ignore").read())
    for s in srcs:
        for m in re.finditer(r"enum\s*\w*\s*\{(.*?)\}", s, re.S):
            body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
            body = re.sub(r"//[^\n]*", "", body)
            nxt = 0
            for item in body.split(","):
                item = item.strip()
                if not item:
                    continue
                if "=" in item:
                    name, v = [x.strip() for x in item.split("=", 1)]
                    try:
                        nxt = int(v, 0)
                    except ValueError:
                        nxt = vals.get(v, 0)
                else:
                    name = item
                vals[name] = nxt
                nxt += 1
    return vals


def num(tok, enums):
    try:
        return int(tok, 0)
    except ValueError:
        if tok not in enums:
            sys.exit("unresolved MM token %s" % tok)
        return enums[tok]


def build():
    tables = parse_tables()
    out = []
    for r in ROWS:
        key = r["mm"]
        if key not in tables:
            sys.exit("no MM table " + key)
        enums = parse_enums(key.split(":")[0])
        entries = []
        for tag, bit, _, _ in ATTACKS:
            mult_tok, eff_tok = tables[key][bit]
            mult = num(mult_tok, enums)
            eff = num(eff_tok, enums)
            if mult == 0 and eff == 0:
                cls = "PASS"  # MM: damage < 1 with effect 0 never registers
            elif tag in r.get("override", {}):
                cls = r["override"][tag]
            else:
                cls = r["mmcls"].get(eff_tok)
                if cls is None:
                    cls = r["mmcls"].get("0x%X" % eff)
                if cls is None:
                    sys.exit("%s: no class for MM effect %s (%s)" % (r["name"], eff_tok, tag))
            oot_cls = cls
            if cls == "DAMAGE_CUT":
                cls = "DAMAGE"
            oot_eff = 0 if cls == "PASS" else r["oot"][oot_cls]
            entries.append((mult, eff, cls, oot_eff, eff_tok))
        out.append((r, entries))
    return out


def damage_text(mult, at):
    lo = [float(x) for x in at.split("/")]
    vals = sorted(set(int(a * MULT[mult]) for a in lo))
    return "/".join(str(v) for v in vals)


def write_header(rows):
    L = []
    L.append("// combo/rando/CrossDamageMap.h — GENERATED by scripts/combo/gen-cross-damage-map.py; do not edit by hand.")
    L.append("//")
    L.append("// How each OOT enemy reacts to the MM attacks the forms bring to Hyrule, taken from MM's data: the hit")
    L.append("// enemy's MM DamageTable row (DMG_ENTRY(multiplier, effect)) for the attack, the class that effect has in")
    L.append("// the MM actor's damage handler, and the OOT actor's own effect code for that class. Included by")
    L.append("// soh/src/code/z_collision_check.c only (CollisionCheck_SetATvsAC / CollisionCheck_ApplyDamage).")
    L.append("// Tags on the attacks: combo/rando/CrossDamageTags.h. Doc: docs/combo/forms-audit.md \"Form attacks vs enemies\".")
    L.append("#pragma once")
    L.append("")
    L.append('#include "CrossDamageTags.h"')
    L.append("")
    L.append("enum CrossDmgAttack {")
    for i, (tag, bit, what, at) in enumerate(ATTACKS):
        L.append("    CROSS_ATK_%s, // %s: MM DMG_%s, AT damage %s" % (tag, what, tag, at))
    L.append("    CROSS_ATK_COUNT,")
    L.append("};")
    L.append("")
    L.append("// The reaction class (from the MM handler). OOT = no MM say (keep OOT); PASS = MM never registers the hit;")
    L.append("// NOREACT = it registers (the attack bounces off) but the enemy does nothing.")
    L.append("enum CrossDmgClass {")
    for c in CLASSES:
        L.append("    CROSS_CLS_%s," % c)
    L.append("};")
    L.append("")
    L.append("// The row's MM verdict also decides whether the attack can touch the enemy's soft hurtbox (its OOT mask only")
    L.append("// picks OOT weapons). See Combo_CrossDamageMaskAccepts.")
    L.append("#define CROSS_ROW_MM_MASK 1")
    L.append("")
    L.append("typedef struct {")
    L.append("    unsigned char mult;   // MM DMG_ENTRY damage nibble (x0, x1, x2, x0.5, x0.25, x3, x4)")
    L.append("    unsigned char mmEff;  // MM DMG_ENTRY effect (MM's no-register rule: damage < 1 && effect 0)")
    L.append("    unsigned char cls;    // enum CrossDmgClass")
    L.append("    unsigned char ootEff; // the OOT actor's damageEffect for that class")
    L.append("} CrossDmgEntry;")
    L.append("")
    L.append("typedef struct {")
    L.append("    short ootId;")
    L.append("    signed char variant; // -1 = every variant (CrossDamage_Variant)")
    L.append("    unsigned char flags; // CROSS_ROW_*")
    L.append("    unsigned short hardPass; // attacks (1 << CROSS_ATK_*) MM's metal/armour collider lets through")
    L.append("    const char* oot;")
    L.append("    const char* mm;")
    L.append("    CrossDmgEntry e[CROSS_ATK_COUNT];")
    L.append("} CrossDmgRow;")
    L.append("")
    L.append("static inline int CrossDamage_AttackIndex(unsigned char tag) {")
    L.append("    switch (tag) {")
    for tag, bit, what, at in ATTACKS:
        L.append("        case CROSS_MM_TAG_%s:" % tag)
        L.append("            return CROSS_ATK_%s;" % tag)
    L.append("        default:")
    L.append("            return -1;")
    L.append("    }")
    L.append("}")
    L.append("")
    L.append("static inline const char* CrossDamage_AttackName(unsigned char tag) {")
    L.append("    switch (tag) {")
    for tag, bit, what, at in ATTACKS:
        L.append("        case CROSS_MM_TAG_%s:" % tag)
        L.append('            return "%s";' % what)
    L.append("        default:")
    L.append('            return "?";')
    L.append("    }")
    L.append("}")
    L.append("")
    L.append("static inline const char* CrossDamage_ClassName(unsigned char cls) {")
    L.append("    static const char* const sNames[] = { %s };" % ", ".join('"%s"' % c.lower() for c in CLASSES))
    L.append("    return (cls < sizeof(sNames) / sizeof(sNames[0])) ? sNames[cls] : \"?\";")
    L.append("}")
    L.append("")
    L.append("// clang-format off")
    L.append("static const CrossDmgRow sCrossDamageMap[] = {")
    hdr = " ".join("%-14s" % t[0].lower() for t in ATTACKS)
    L.append("    //                                  " + hdr)
    for r, entries in rows:
        hp = " | ".join("(1 << CROSS_ATK_%s)" % a for a in r.get("hardpass", [])) or "0"
        L.append('    { %s, %d, %s, %s, "%s", "%s", {' % (r["id"], r["var"], "CROSS_ROW_MM_MASK" if r.get("mmmask") else "0",
                                                   hp, r["name"], r["mmname"].split(" (")[0]))
        for (tag, bit, what, at), (mult, eff, cls, oot_eff, tok) in zip(ATTACKS, entries):
            L.append("        { %d, 0x%X, CROSS_CLS_%s, 0x%X }, // %s: MM DMG_ENTRY(%s, %s)" %
                     (mult, eff, cls, oot_eff, what, mult, tok))
        L.append("    } },")
    L.append("};")
    L.append("// clang-format on")
    L.append("")
    open(OUT, "w").write("\n".join(L))


def doc(rows):
    cols = [a[2] for a in ATTACKS]
    print("| OOT enemy | MM reaction source | " + " | ".join(cols) + " |")
    print("|---" * (len(cols) + 2) + "|")
    for r, entries in rows:
        cells = []
        for (tag, bit, what, at), (mult, eff, cls, oot_eff, tok) in zip(ATTACKS, entries):
            if cls == "PASS" or (eff == 0 and damage_text(mult, at) == "0"):
                cells.append("passes")
            elif cls == "STUN":
                cells.append("**stun**")
            elif cls == "ELECTRIC":
                cells.append("**shock stun**")
            elif cls == "NOREACT":
                cells.append("bounces off")
            elif cls == "KNOCKBACK":
                cells.append("knockback 0")
            else:
                cells.append(damage_text(mult, at))
        print("| %s | %s | %s |" % (r["name"], r["mmname"], " | ".join(cells)))


if __name__ == "__main__":
    rows = build()
    write_header(rows)
    if "--doc" in sys.argv:
        doc(rows)
