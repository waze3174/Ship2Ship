# MM forms in Hyrule — audit against MM's player code (2026-11-13)

Owner, after playing B-M8: "completely re-audit how transformations remap your inputs in MM … i want the exact walk
animations, all the particle effects and punch effects, screen shakes, the whole damn thing … some parts are nice and
others feel very half baked." Goron got stuck in the pound jump and couldn't roll; Deku had no spin on A.

This file is the reference the rebuild works from. Line numbers are `mm/src/overlays/actors/ovl_player_actor/z_player.c`
(MM) and `mm/src/code/z_player_lib.c` (MM lib) on this branch. Rule: port MM's path; never approximate.

## 1. What MM actually does

### 1.1 Input → action (MM's action-handler lists, z_player.c:5385-5532)

Every frame the current action runs a list of handlers in order until one returns true. Form-specific handlers:

| Input | Handler | Human / FD | Goron | Zora | Deku |
|---|---|---|---|---|---|
| A, not Z-targeting | `_6` (8556) | moving forward → roll (`func_80836B3C` → `Player_Action_26`) | `func_80839F98` (8519): moving → `func_80836B3C` Goron branch = curl straight into the roll (`Player_Action_96`, `landing_roll` anim at 1.5×, frames 0-6); standing → `func_80836AD8` + `pg_maru_change` frames 0-7 (curl in place) | like human | `func_80839A84` (8411): on a Deku-flower floor → `func_80836DC0` burrow (`Player_Action_93`, `pn_attack`, `NA_SE_PL_DEKUNUTS_IN_GRD`); else **spin** (`Player_Action_95`, `func_808373A4`: `pn_attack`, `unk_B10[0]=20000`, `unk_B10[1]=0x30000`, `NA_SE_PL_DEKUNUTS_ATTACK`) |
| A, Z-targeting | `_10` (8427) | sidehop/backflip (`func_80839860`) or jump slash | sidehops; **no jump slash** (`transformation != GORON`) | jump slash → `PLAYER_MWA_ZORA_JUMPKICK_START` (8300/8332) | sidehops / spin |
| B | `_8` (8654) → `func_808335B0` → `Player_Action_30` / melee | sword combo | **melee system**: `func_808335F4` (5745) picks `D_8085D094[1]` = GORON_PUNCH_LEFT → RIGHT → BUTT by `unk_ADD`; action `Player_Action_84` | `D_8085D094[0]` = ZORA_PUNCH_LEFT → COMBO → KICK; B held = fins (`func_80830E30`, `pz_cutterwait*`) | **no melee**: B = Deku bubble (`pn_tamahakidf` ready, hold to grow) |
| R | `_11` (8581) | shield | Goron guard (`Player_Action_18`, Goron branch: separate `unk_2C8` skel update) | Zora barrier (fins not thrown) | `pn_gurd` (Deku hides) |
| A in water | `_6` / `_14` | swim | sinks | `func_8083A04C` fast swim | hops / drowns |

### 1.2 Body and movement tables

- **Per-form body**: `sPlayerAgeProperties[PLAYER_FORM_MAX]` (z_player.c:753) — ceiling, shadow, root scale `unk_08`
  (Goron 0.74), ledge/climb heights, water depths, wall radius, voice/surface sfx offsets (Goron voice bank 6,
  surface 0x150), and every climb/door/chest anim per form (`pg_climb_*`, `pg_Tbox_open`, …).
- **Per-form movement**: MM picks a "boots" row per form (`D_801BFF90`, lib:1027) from `D_801BFE14` (lib:455-600):
  Goron row = run limit 600, accel REG(34) 700, REG(38) 200, **gravity REG(68) −140** (human −100), IREG(66) 590,
  IREG(69) 200, MREG(95) 130; Zora/Deku rows differ in REG(37)/REG(38). `func_80123140` also sets REG(48) (Goron
  underwater 200) and indoor run limit 500.
- **Strength** per form (`sPlayerStrengths`).

### 1.3 Animations

- Forms walk/run/idle/land/turn with the shared table `D_8085BE84[group][modelAnimType]` — MM's `link_normal_*`
  data, not OOT's. Only the special moves have form anims (`pg_*`, `pz_*`, `pn_*`). Idle: Zora `pz_wait`, Goron
  `pg_wait` (`Player_GetIdleAnim`, 2819).
- Goron/Zora melee anims and hit windows (`sMeleeAttackAnimInfo`, 3620-3631): punchA 6-8, punchB 12-18,
  punchC 8-14; Zora attackA 2-5, attackB 3-8, attackC 3-10; ends `*end` / `*endR` (lock-on).

### 1.4 Effects, shakes, hit feel

- Goron punch: `unk_3D0.unk_00 = 3` (punch trail drawn by `func_80122BA4`), lunge 15.0 on the stab frame, dust
  from the feet when `speedXZ > 12` (`func_8083FBC4`).
- Goron fist into a wall (`func_808401F4`, 10735+): hit-stop `func_8082DF2C`, `Player_RequestQuakeAndRumble`
  (`NA_SE_IT_HAMMER_HIT`), `Actor_SetPlayerImpact(PLAYER_IMPACT_BONK, 2, 100)`, `ACTOR_EN_TEST` spawned at the
  hit point rotated to the wall (params 500). Other forms: shield sparks/wood chips + recoil −14.
- Goron roll (`Player_Action_96`, 19965-20335): speed ramp, wall bounce with `NA_SE_IT_GORON_ROLLING_REFLECTION`,
  roll dust `func_808576BC` (`func_8083F8A8`, slip sfx), spike charge (`actionVar1` 5→0x36, `NA_SE_PL_GORON_BALL_CHARGE`),
  spike mode (magic, `object_link_goron_DL_00C540`, red charge aura `DL_0127B0`/`DL_0134D0` with anim mats),
  squash/stretch (`unk_ABC`/`unk_B48`), ground trail `func_800AE930`.
- Goron pound: B while curled on ground (`func_80857640`): jump 14 with voice, `GORON_BALLJUMP`; apex hover,
  gravity −0.2 then −10 slam (`unk_3D0.unk_00 = 1` slam effect); landing `func_80857AEC`: quake+rumble
  `GORON_PUNCH`, `actorCtx.unk2 = 4`, white shockwave, `ACTOR_EN_TEST`, 10-frame `DMG_GORON_POUND` radius-60
  cylinder + `PLAYER_IMPACT_GORON_GROUND_POUND` radius 100.
- Deku spin (`Player_Action_95`, 19811): `DMG_DEKU_SPIN` cylinder r30, spin decays from 20000 by 800/frame, dust
  `func_808566C0` + `func_8083F8A8` while fast, ground trail, slip sfx loop.
- Transformation: `func_808388B8` (mask-on sequence), not an instant swap.

## 2. Our implementation vs MM (B-M8)

| Area | Ours | Verdict |
|---|---|---|
| Movement rows | OOT Kokiri-boots row for every form | **wrong** — port MM rows |
| Body table | hand-built subset (`Combo_FormAgeProperties`) | **partial** — port MM's table field by field |
| Walk/run/idle anims | OOT's `link_normal_*` data; only pg/pz/pn routed | **wrong** — route every anim to MM's data when transformed |
| Goron A | OOT roll handler → our curl; Z-target A still sidehops/jumps | roll/curl must be MM's `func_80839F98`; owner: "stuck doing the jump, can't roll" |
| Goron roll + pound | hand-built (`Player_Action_ComboGoronRoll`: hang at apex, flat −20 drop) | **replace** with a port of `Player_Action_96` + helpers |
| Goron/Zora punches | hand-built action, own hit quads | **replace** with MM's melee entries through OOT's `Player_Action_808502D0` path (or a port of `Player_Action_84`) |
| Deku A | OOT roll | **wrong** — spin (`Player_Action_95`) / flower burrow |
| Deku B | tap = spin, hold = bubble | **wrong** — B is only the bubble |
| R guards | none | missing |
| Effects | shockwave only | missing: punch trail, EN_TEST impact, roll dust, spike aura, hit-stop, slam effect |
| Transform | instant | missing MM sequence |
| Zora | ported EnBoom fins; hand-built jabs | fins good (owner: best part); jabs → MM melee |

## 3. Rebuild order

1. Foundation: MM movement rows + MM body table + all form anims from MM data. (Fixes "feel", walk anims.)
2. Input map exactly as §1.1 (A/B/R/Z+A per form), removing our ad-hoc B/A hooks.
3. Goron: port `Player_Action_96` (roll, spikes, pound) + `func_80836AD8`/`func_80839F98` + the ball draw state;
   port `ACTOR_EN_TEST` impact; punch melee entries + wall-hit block.
4. Deku: port `Player_Action_95` (spin) + `Player_Action_93/94` (flower) + water hop; bubble on B only.
5. Zora: melee entries + jump kick, barrier, fast swim.
6. Transformation sequence; R guards; drums/guitar/pipes.

## 4. Status after B-M10 (2026-11-13)

Done (ported, headless-verified): movement rows, body table, MM animation data + per-form door/instrument/drink/
item-get swaps, Player_GetHeight per form (camera/targeting), Goron ball (`Player_Action_96`) incl. pound, Deku spin
(`Player_Action_95`), Goron/Zora melee (`Player_Action_84` entries, combo window, 2/3 speed + root motion, hit
frames, Goron wall punch), Zora jab -> held-B fins, tire-mark trails, body-cylinder hits registered (were silently
dropped), rotation flags (spin/roll no longer fight the shape yaw/camera), Anchor colour on form palettes, clean form
changes, Deku crawlspaces, Blast Mask B.

Still owed (owner: "every effect needs to work"):
- Goron punch fist trail (`unk_3D0.unk_00 = 3`) and the slam/charge afterimages (`func_80122BA4`), spike-charge aura
  (`DL_0127B0`/`DL_0134D0` with MM anim mats), `ACTOR_EN_TEST` impact actor (wall punch, pound).
- Per-limb keyframed scale (`func_80124618`: Goron torso breathing in `pg_wait`, Zora torso stretch, Deku head
  squash for the bubble/drink).
- MM camera modes per form (GORONDASH while rolling, ZORAFIN aim, DEKUSHOOT) — OOT's camera data has none; and
  Normal1's per-form distance (Goron/Zora 115).
- Zora jump kick (Z+A), barrier (R + magic), fast swim; Deku flower burrow/launch/glide, water hop; R guards
  (Goron/Deku); transformation sequence (`func_808388B8`); Goron footstep sfx (surface offset 0x150 is MM-table).

## 5. Status after B-M13 (2026-11-13, later)

Ported since B-M11 (each traced from MM's code, headless-verified):
- Deku: MM ledge jumps (func_808373F8 via the fall check), water skipping (five hops, rising DEKUNUTS_JUMP,
  last hop spins, then lost), Player_Action_25's Deku air control (half speed, quicker turns) and the spin carrying
  on in the air; no ledge catch without hops; R = pn_gurd duck. The spin's camera holds still.
- Goron: R guard on MM's own gLinkGoronShieldingSkel (pg_gurdmotion) with the hard guard cylinder; afterimages
  (unk_3D0: pound-slam ball ghosts, punching-fist trail), the red fist flash on punches A/B, spikes popping out,
  the red spike-charge aura with its animated materials. Never catches a ledge.
- Zora: R guard (fins as shield), R+B barrier (magic drain, scene lighting, point light, spark hum, thunder shell,
  shocking cylinder), Z+A jump kick (pz_jumpAT, right-shin hit), fast swim on A (turn-in dash, pitch, turn +
  barrel roll, floor bounce, wind-down) with the leap out of the water and the dive back in, root tilt draw.
- Shared: MM animated materials (all six types) over MM TextureAnimation resources.

MM features that do not apply in Hyrule: Deku flowers (burrow/launch/glide: OOT has no Deku-flower actors), Goron
snow/heat drums and Zora guitar/Deku pipes as songs (the ocarina covers songs; instrument swaps are drawn).

Still owed: per-limb keyframed scale (func_80124618: Goron breathing, Deku head), MM camera modes per form
(GORONDASH / ZORAFIN / DEKUSHOOT), the transformation sequence (func_808388B8), Goron slope curl (SlideOnSlope), Goron sinking/Zora walking underwater (boots
rows exist; actions not ported), Zora ice/electric breakout (Action_82), HUD per-form B/A labels.

Zora swim fins + their blur trails (2026-11-14): MM func_80126BD0's swim half, ported in z_player_lib.c
(Combo_ZoraSwimFin): the swim fin models sized by pz_waterroll / pz_swimtowait / pz_fishswim (MM func_80124618 keys
D_801C05C8 / D_801C05F0, fishswim from unk_B8A), and per fin an EFFECT_BLURE2 trail (MM meleeWeaponEffectIndex[0/1],
sBlureColors[1], elemDuration 8) fed while in water. Verified headless at Lake Hylia: both fins trail through the dive.

## 6. Form attacks vs enemies (2026-11-13, branch feat/form-damage-tables)

Owner: "make sure deku spin actually stuns some enemies instead of damaging them, not sure where the cutoff is there,
does it stun all MM enemies or just larger ones? every attack should behave the same way."

### 6.1 How MM decides (the answer)

There is no size cutoff and no global rule. Every MM enemy has its own `DamageTable` with one entry per MM attack
(`DMG_ENTRY(multiplier, effect)`, `mm/include/z64collision_check.h`), and the effect's meaning is decided by that
enemy's own damage handler. MM's collision code (`CollisionCheck_GetDamageAndEffectOnElementAC`) multiplies the
attack's own damage by the entry's multiplier (x0, x1, x2, x0.5, x0.25, x3, x4) and drops the hit entirely when the
result is below 1 and the effect is 0: the attack passes through as if the enemy were not there.

The Deku spin (`DMG_DEKU_SPIN`, attack damage 1, radius 30) in MM's tables:
- **Stunned, no damage** (`DMG_ENTRY(0, stun)`): Wolfos and White Wolfos, Dinolfos, ReDead and Gibdo (all kinds),
  Tektite, Dodongo, Like Like, Floormaster, Wallmaster, Stalchild, Leever, Garo and Garo Master, Gerudo pirates,
  Igos's knights (standing), Gekko and Mad Jelly, Eeno, Hiploop, Snapper, Bio Deku Baba's walking head, Odolwa's
  bugs. Odolwa himself is dazed and takes 1.
- **Damaged for 1** (the same as a Kokiri sword swing): Keese, Bad Bat, Guay, Deku Baba (it also cuts them like a
  sword), withered Deku Baba, Octorok, Chuchu, Blue and Red Bubbles, Peahat, Mad Scrub, Armos, Skullwalltula and
  Gold Skulltula, Poe, Big Poe, Poe sisters, Real Bombchu, Wizrobes, Dragonfly, Takkuri, Majora's forms.
- **No effect at all** (`DMG_ENTRY(0, 0)`: the spin passes through): Skulltula, Freezard, Shellblade, Skullfish,
  Dexihand, Twinmold. Iron Knuckle: armoured it clanks off (immune), unarmoured the spin passes through.

So the spin stuns the walking fighters that have a recoil pose (Wolfos, Dodongo, ReDead, Tektite, Stalchild…), takes
1 off the flimsy or flying things (Keese, Octorok, Bubbles, Peahat, Poes, Deku Babas), and does nothing to shelled,
armoured or out-of-reach things. Size is not the rule: a small Tektite or Stalchild is stunned, a big Peahat or
Armos is damaged. Every other attack works the same way, enemy by enemy (table below).

### 6.2 What Hyrule does now

Each MM attack in Hyrule carries its MM damage type next to its OOT damage bit: `ColliderTouch.comboMmDmg` (MM
`DMG_*` bit + 1) and `comboMmAtDamage` (MM's attack damage), in the struct's old padding (size unchanged). Tagged in
`z_player.c` (Deku spin, Goron punch / pound / spike roll / plain roll, Zora jab / jump kick, Zora barrier),
`z_en_boom_combo_fins.c` (fins), `z_en_arrow_combo_bubble.c` (bubble), `z_en_bom_combo_keg.c` (Blast Mask bomb, Powder
Keg). Constants: `combo/rando/CrossDamageTags.h`.

When a tagged attack touches an OOT enemy that exists in MM, OOT's collision code (`z_collision_check.c`, fenced
`COMBO_BUILD`) looks the enemy up in `combo/rando/CrossDamageMap.h`:
- `CollisionCheck_SetATvsAC`: MM's no-register rule — if MM's entry gives damage < 1 with effect 0, the hit is not
  registered (no flinch, no hit mark; fins and bubbles fly on).
- `CollisionCheck_ApplyDamage`: after OOT's own table lookup, the damage becomes MM's (attack damage x multiplier,
  truncated as MM's u8 does) and `damageEffect` becomes the OOT actor's own code for MM's reaction class. The OOT
  actor then runs its own stun / freeze / burn / hurt code — so a stun is OOT's stun for that enemy (blue filter,
  its own duration), not a hand-made imitation.
- `CollisionCheck_NoSharedFlags`: for the two enemies whose soft hurtbox mask only exists to pick OOT weapons
  (withered Deku Baba, Skullwalltula — `CROSS_ROW_MM_MASK`), an attack MM's version takes lands whatever OOT bit it
  carries (a Goron punch kills a withered Deku Baba as in MM). Hard / metal colliders keep OOT's masks everywhere.

`CrossDamageMap.h` is generated by `scripts/combo/gen-cross-damage-map.py` from MM's sources: it parses the MM
tables and enums directly; the only hand-written data is which class each MM effect means (read from each MM
handler) and which OOT effect code shows that class (read from each OOT handler) — both cited in the script.

The OOT damage bit each attack also carries is unchanged, because OOT's puzzle actors read it: hammer switches and
rocks (Goron pound / punch = hammer bits), boomerang targets (fins), eye switches (bubble = slingshot), bombable
walls (keg, Blast Mask), grass and bushes (spin). Only actors in the map are affected; Link's own OOT weapons carry
no tag and are vanilla. Debug trace: `COMBO_DEBUG_CROSSDMG` (every mapped hit: OOT's verdict vs MM's).

### 6.3 Reactions in Hyrule (MM damage numbers; "stun" = MM stuns it, shown with the OOT enemy's own stun)

"shock stun" = MM's Zora-barrier electric stun (MM draws sparks; OOT has no spark effect, so it is the enemy's stun).
"bounces off" = MM registers the hit but the enemy ignores it (fins / bubble bounce, armour clanks).
"knockback 0" = MM pushes it back without damage. Jab / jump kick = 1 / 2 (MM's normal / strong Zora damage).

| OOT enemy | MM reaction source | Deku spin | Deku bubble | Goron punch | Goron pound | Goron spike roll | Goron roll | Zora jab / jump kick | Zora fins | Zora barrier | Blast Mask | Powder Keg |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Keese | Keese | 1 | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Deku Baba | Deku Baba | 1 | 1 | 4 | 1 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Withered Deku Baba | Withered Deku Baba | 1 | 2 | 4 | 4 | 1 | passes | 1/2 | 2 | 2 | 2 | 4 |
| Octorok | Octorok | 1 | 2 | 2 | passes | 1 | passes | 1/2 | 2 | passes | 2 | 4 |
| Skulltula | Skulltula | passes | 2 | 2 | passes | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Skullwalltula / Gold Skulltula | Skullwalltula / Gold Skulltula | 1 | 2 | 2 | 8 | 1 | passes | 1/2 | 2 | 2 | 2 | 4 |
| Dodongo | Dodongo | **stun** | 2 | 4 | 2 | 1 | passes | 1/2 | **stun** | **shock stun** | 2 | 4 |
| Like Like | Like Like | **stun** | 2 | 2 | 2 | 2 | passes | 2/4 | **stun** | 4 | 2 | 4 |
| Wolfos | Wolfos | **stun** | 2 | 2 | 2 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| White Wolfos | White Wolfos | **stun** | 2 | 4 | 2 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| ReDead / Gibdo | ReDead / Gibdo | **stun** | bounces off | 2 | 4 | 1 | passes | 1/2 | bounces off | **shock stun** | 2 | 4 |
| Tektite | Tektite | **stun** | 2 | 2 | passes | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Stalfos | Igos's knights (closest: skeletal sword-and-shield fighters) | **stun** | 4 | 2 | knockback 0 | 1 | passes | 1/2 | **stun** | **shock stun** | 2 | 4 |
| Lizalfos / Dinolfos | Dinolfos | **stun** | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Peahat | Peahat | 1 | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Bubble (all colours) | Blue Bubble / Red Bubble (EnBb, EnBbfall: same rows) | 1 | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Wallmaster | Wallmaster | **stun** | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Floormaster | Floormaster | **stun** | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Guay | Guay | 1 | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Mad Scrub | Mad Scrub | 1 | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | 2 | 2 | 4 |
| Iron Knuckle (armoured) | Iron Knuckle (armoured) | bounces off | bounces off | 4 | 2 | bounces off | passes | bounces off | bounces off | passes | 2 | 4 |
| Iron Knuckle (armour off) | Iron Knuckle (armour off) | passes | 2 | 4 | 2 | 1 | passes | 1/2 | 2 | passes | 2 | 4 |
| Freezard | Freezard | passes | passes | 4 | 2 | 1 | passes | 1/2 | passes | passes | 4 | 4 |
| Poe | Poe | 1 | 2 | 2 | passes | 1 | passes | 1/2 | 2 | passes | 2 | 4 |
| Field Poe | Poe | 1 | 2 | 2 | passes | 1 | passes | 1/2 | 2 | passes | 2 | 4 |
| Big Poe | Big Poe | 1 | 2 | 2 | passes | 1 | passes | 1/2 | 2 | passes | 2 | 4 |
| Poe Sisters | Poe Sisters | 1 | 2 | 2 | passes | 1 | passes | 1/2 | 2 | passes | 2 | 4 |
| Armos | Armos | 1 | bounces off | 2 | 4 | 1 | passes | 1/2 | bounces off | passes | 2 | 4 |
| Stalchild | Stalchild | **stun** | 2 | 4 | 4 | 1 | passes | 1/2 | 1 | **shock stun** | 2 | 4 |
| Shellblade | Shellblade | passes | 2 | passes | passes | passes | passes | 1/2 | 2 | 2 | 2 | 4 |
| Leever | Leever | **stun** | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |
| Gerudo fighter | Gerudo pirate | **stun** | 2 | 2 | bounces off | 1 | passes | 1/2 | **stun** | **shock stun** | 2 | 4 |
| Baby Dodongo | Real Bombchu (closest: a small crawler that blows up when hit) | 1 | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | 2 | 2 | 4 |
| Biri | Chuchu (closest: a jelly blob) | 1 | knockback 0 | knockback 0 | knockback 0 | 1 | passes | 1/2 | 2 | 2 | 2 | 4 |
| Bari | Chuchu (closest: a jelly blob) | 1 | knockback 0 | knockback 0 | knockback 0 | 1 | passes | 1/2 | 2 | 2 | 2 | 4 |
| Stinger (sand) | Skullfish (closest: a lunging fish) | passes | 2 | passes | passes | passes | passes | 1/2 | 2 | 2 | 2 | 4 |
| Stinger (water) | Skullfish (closest: a lunging fish) | passes | 2 | passes | passes | passes | passes | 1/2 | 2 | 2 | 2 | 4 |
| Dark Link | Garo (closest: a sword duellist) | **stun** | 2 | 2 | 4 | 1 | passes | 1/2 | 2 | **shock stun** | 2 | 4 |

Counterparts marked "closest" are OOT-only enemies given the MM enemy nearest in behaviour; the rest are the same
enemy in both games (Lizalfos/Dinolfos use MM's Dinolfos, OOT's Gerudo fighters MM's Gerudo pirates, Leevers MM's
Leever, Big Poe MM's Big Poe). Stalfos use Igos's knights (MM has no Stalfos; the knights are its skeletal
sword-and-shield fighters).

**OOT enemies kept as OOT** (no MM counterpart close enough that copying its table would be MM's behaviour rather
than an invention, or the OOT actor ignores damage tables so there is nothing to map):
Beamos (OOT has no table: any eye hit stuns, explosives kill — already MM's rule), Big Octo (OOT reads flags; MM's
Big Octo has no table either), Moblins, Gohma larvae and Gohma eggs (flag-driven), Tailpasaran, Spike, Anubis (only
fire kills, OOT's design), Torch Slug, Flare Dancer (table ignored), Shabom, Jabu's tentacles and Parasitic
Tentacle, Dead Hand and its hands, Flobbery, hint / business Scrubs.

**Bosses.** OOT bosses decide hits from the damage bits themselves (`CollisionCheck_GetSwordDamage`, custom flag
masks), not from damage tables, and they gate their fights on OOT items, so they keep OOT's rules. One rule is
applied to every OOT boss (`ACTORCAT_BOSS`): the Zora barrier passes through. No MM boss ever takes damage from it
(Odolwa / Goht / Majora: 0, Twinmold / Majora's Mask: immune), while OOT reads its `DMG_MAGIC_LIGHT` bit as a hit
(Gohma fell off the ceiling, Phantom Ganon took 2 and was stunned, Bongo Bongo's hands were knocked back). MM bosses
are never stun-locked by form attacks either; in OOT the remaining cases are OOT's own: Phantom Ganon re-stunned
by every hit after 10 invincible frames, Barinade's stun-lock with the fins (as with OOT's boomerang). Fight gates
the forms cannot pass (OOT design, documented, not changed): Ganondorf needs a light arrow, Twinrova a shield
reflect, Bongo Bongo's open eye only takes sword bits (a Goron can stun it, never hurt it), Barinade's stunned body
refuses hammer bits, Ganon's tail only dies to Master-Sword bits (the Zora's jabs count as those).

### 6.4 Known gaps (and why)

- **Spark effects.** MM's Zora-barrier hits draw electric sparks on the enemy (`Actor_DrawDamageEffects`); OOT
  actors have no such draw, so the shock is the enemy's plain stun.
- **Stun length and look are the OOT enemy's.** The class is MM's; the stun itself is the OOT actor's own code. MM
  stuns most of these for 40 frames (Like Like 80); OOT's stuns run 80 (Skulltula 120, Baby Dodongo 120). Shortening
  them would mean editing each OOT actor's stun state, which this change does not touch.
- **Health is OOT's.** Damage numbers are MM's, hit points are the OOT enemy's (an OOT Octorok or Keese dies to any
  hit, as in OOT; MM's Octorok has 4 HP).
- **OOT collider masks still gate a few hits** (only the withered Deku Baba and Skullwalltula opt into MM's verdict):
  the barrier only reaches a Skulltula's metal front in OOT (it sways instead of being stunned as in MM), a buried
  Stinger still only takes nuts / bombs / boomerang. MM's per-collider exclusions (e.g. MM Dodongo's armoured front
  spheres refusing Goron blows) are not modelled; OOT's own hard colliders do the equivalent.
- **Not tagged:** Fierce Deity's sword (2026-11-14: B is the Double Helix on OOT's Biggoron action, two-handed,
  unbreakable; it hits as OOT's Biggoron), the Great Fairy's Sword (a human item: hits as OOT's Biggoron), Deku
  flower launch (no flowers in Hyrule).
- **Fierce Deity sword beam (2026-11-14):** MM Player_ActionHandler_7 / func_808332A0 / EnMThunder_SwordBeam_Attack
  ported into OOT's Player_ActionHandler_7 and En_M_Thunder (comboBeam). A swing while Z-targeting, with magic,
  fires MM's gameplay_keep beam aimed at the lock-on target's focus. It costs 1 magic at once (MM
  MAGIC_CONSUME_DEITY_BEAM, free under Chateau Romani) and is tagged SWORD_BEAM, 3. MM's tables let it hurt only
  Lizalfos / Dinolfos, Big Poe and Stalchild among the mapped OOT enemies (x1 / x1 / x2); every other mapped enemy
  is DMG_ENTRY(0, 0) in MM, and the beam passes through it. Beam look: GREAT (MM picks it once the great spin is
  learned; OOT Link always knows it). Verified headless: Z-target + B spawns the beam and spends 1 magic per swing,
  and a pinned Stalchild takes MM's 6 and dies.

### 6.5 Verified (headless, Xvfb, harness build)

`COMBO_DEBUG_BOOT_PLAY=0xCD COMBO_DEBUG_FORM=<form> COMBO_TEST_ENEMY=<id>,<params>,22,70 COMBO_TEST_ENEMY_PIN=1
COMBO_DEBUG_CROSSDMG=1 COMBO_VPAD=4:A,40:,loop` (B for punches / jabs, held B for fins). "OOT alone" is what OOT's
own table gave the same hit:
- **Deku spin -> stun, health unchanged** (blue filter, timer 80): Wolfos (OOT alone: 1 dmg), ReDead (OOT alone:
  1 dmg 0xF), Tektite, Dodongo (reaches the body through OOT's hard tris now, as through MM's armour), Stalchild
  (OOT alone: 1 dmg), Stalfos (Igos's knights row).
- **Deku spin -> 1 damage:** Keese (killed), Deku Baba (sword-cut code 0xF, 2 -> 1 HP).
- **Goron punch -> 2:** Tektite (killed), ReDead (8 -> 6). **Zora jab -> 1** (OOT alone gave 2): Tektite, Dodongo.
- **Zora fins -> 2 damage on a Wolfos** (OOT alone: boomerang stun).
Not landed in these runs (positioning, not mapping): Skulltula (hangs above the pin), Like Like (stays buried / left
the area), Octorok (despawns on dry land), perched Keese vs Goron punch, Deku Baba vs punch.

**Owed:** a headless pass for the barrier (R+B), bubble, pound / rolls, Powder Keg and Blast Mask against the
table, and the pass-through cases (spin vs Skulltula, roll vs Keese) seen in a trace; an in-water Octorok check.

## 7. Water, objects and the Goron ball (2026-11-14, B-M21 / B-M22)

### Water (MM z_player.c ~9292-9330, func_8083A04C, Player_UseItem gate ~4786)
| Form | Deep water | Masks while swimming |
|---|---|---|
| Goron | sinks with MM's swim-down anim, voids out, respawns **human, mask off** (MM func_808345C8) — no drown loop from an underwater entrance | refused |
| Deku | hops; out of hops → drowns, respawns human | refused |
| Zora | B sinks him onto the bottom (walks there, OOT iron-boots water logic on his MM row), A floats him up; no breath timer | Zora Mask on/off allowed; sunk on the bottom: everything |
| Fierce Deity / human | swim as OOT | Zora Mask only |

Transformed Link never takes Iron / Hover boots physics from the human's equipment (MM forms have no boots).
Goron ball: no item / mask / D-pad use while curled or pounding (MM's roll never runs the upper body). Release-A
uncurl is as MM's source has it.

### Form moves vs Hyrule objects (soh/src/code/z_collision_check.c sCrossObjects)
Each row is the MM counterpart's own collider masks (accept / break, MM source line cited in the table). The object
then receives the OOT flag it reads as that outcome. B = breaks/cuts/triggers, C = clink only, – = doesn't register.

| Object | Roll | Spikes | Pound | Punch | Deku spin | Bubble | Zora jab/kick | Fins | Barrier |
|---|---|---|---|---|---|---|---|---|---|
| Brown boulder (Obj_Bombiwa) | – (held by it) | C | B | B | – | C | C | C | – |
| Bronze boulder (Obj_Hamishi, 2 hits) | – | C | hit | hit | – | C | C | C | – |
| Small rock (En_Ishi) | – | C | B | B | – | C | C | C | – |
| Silver rock | – | C | – | C | – | C | C | C | – |
| Small crate / pot / grass | B | B | B | B | B | per MM mask | B | B | B (pot, crate) |
| Big crate (Obj_Kibako2) | bonk → B | bonk → B | B | B | – | – | – | – | – |
| Tree (En_Wood02) | bonk → shakes, drops | bonk | – | C | – | – | C | – | – |
| Rusty floor switch | – | – | B | – | – | – | – | – | – |
| Crystal switch | per MM mask (0x01CBFFBE) ||||||||
| Bomb flower | – | – | – | – | – | B (detonates) | – | – | – |
| Bombable walls (Bg_Breakwall / Bg_Bombwall), grotto holes (Door_Ana) | – | – | – | – | – | – | – | – | – |
| Fire Temple hammer stairs/pillars/totem | – | – | B | B | – | – | – | – | – |

**Owner rulings (2026-11-14, revised B-M47):**
- Bombable walls and grotto holes: every MM counterpart accepts explosives only (0x08), so no form move opens them.
  Powder Keg and Blast Mask blasts do (MM's keg is 0x80000008), as do OOT's own bombs and hammer.
- Fire Temple hammer obstacles: these are Hyrule's counterpart of MM's Goron-only giant snowballs, so the punch and
  pound count as the hammer on them.
- Goron strength: he lifts what the Golden Gauntlets lift, at either age.
- Termina: B swings OOT's Megaton Hammer, so it breaks MM's snowballs and boulders through MM's punch and pound bits.
- Seed checker:
  - The Goron credits the Goron's Bracelet and the Silver and Golden Gauntlets.
  - Inside `BlastOrSmash()` he gets no hammer credit: the call sites mix walls and boulders, so it stays off.

Verified headless (COMBO_TEST_ENEMY + harness pad): boulder vs spike roll = clink, intact; punch and pound break it;
roll into a big crate breaks it; punch and pound open a bombable wall.

### Still owed
- Zora arm / fin blur trails.
- Goron roll ramp launch (MM's slope split is ported, z_player.c ~10845; the launch is lost somewhere OOT-side —
  reproduce on a slope with COMBO_DEBUG_ROLL before changing anything).
- Rando logic: Zora Mask counts as Iron Boots (owner ruling 2026-11-14).
- Signs (En_Kanban cut shape reads Link's sword animation), En_Goroiwa (no hurtbox in OOT), pound / wall-punch
  shockwave timing (MM: once at landing; port: every pound frame).
- In-game check of the water mask gate and the ball's item block.

### Spiked roll vs Tektite (2026-11-14, owner report "clipped right through a tektite")
MM, read in source:
- **Tektite (MM En_Tite):** 2 HP, Goron spikes DMG_ENTRY(1, 0), the spikes' AT damage is 1, so it takes two hits.
  A hit knocks it back at speed 6, away from the blow (func_80894B2C), and clears AC_ON until it has stopped on
  the ground. OOT's Tektite does the same through its own recoil (no damage while action < TEKTITE_IDLE).
- **Its body stays solid** (OC, MASS_HEAVY): the ball is shoved behind it or slid around it.
- **The player (MM func_80834D50):** a frame where Link's own body attack connected (cylinder AT_HIT) costs no
  damage. An enemy that reaches the curled ball otherwise knocks it away (response 1, Player_Action_21).
Ported the two player rules (OOT's damage handler lacked both). Verified headless at the ranch, spiked roll into a
live Tektite:
- **Head-on:** hit 1 (2 -> 1), 12 frames shoving the recoiling Tektite, then the kill as its recoil ends.
- **Glancing:** hit 1, 7 frames shoving, then its heavy body slides the ball past while it is still recoiling
  (1 HP left), as MM's identical OC + AC-off rules give.
- The ball is never hurt in a frame its spikes land.
- A plain roll (MM DMG_ENTRY(0, 0): it never hurts a Tektite) meeting a lunging Tektite is knocked out of the
  ball, red flash, half a heart.

### Sunk Zora's B, and the hookshot Zora (2026-11-14)
- **B on the bottom (MM):** func_8083A04C only spends B to sink a swimming Zora. On the floor in his underwater
  boots his B is the punch (MM doActionB), and a held B readies the fins. The port refused every punch in water
  (Combo_GoronCanPunch); it now lets the sunk Zora through. Verified at Lake Hylia: three jabs, then held B ->
  fins thrown and caught.
- **Hookshot / Longshot as Zora (owner ruling, not MM):** so a Zora who swims into the Water Temple isn't stuck at
  its targets. Combo_OotFormBlocksItem lets them through for the Zora. OOT's water rule still applies (on the floor
  only), and the C buttons now see the sunk Zora as "on the floor" (Player_GetEnvironmentalHazard: his breath timer
  never runs, so OOT's deep-water test never passed). His hand draws adult Link's hookshot hand (the only
  always-present hookshot model). Verified on land and on the Lake Hylia floor.

### Softlock checker: a form that keeps dying on its own respawn point (MM, 2026-11-14)
Owner: "if i come out of the sunken woodfall temple as deku link, it's a death sentence softlock where you keep
bouncing and drowning forever". A Deku who dies in water comes back at the respawn point still a Deku (MM unmasks
only in Great Bay / Zora Cape / the Pirates' Fortress, func_808345C8). If that point is itself over deep water, he
dies there again forever. Two routes reach it: the void floor under Woodfall's water (func_80169EFC, a room reload)
and the drown (Player_Action_1, put back in place).
- **The checker (z_player.c Combo_SoftlockGuard):** a transformed Link who has not stood on dry ground since he
  (re)spawned comes back human, as MM's own func_808345C8 does for its three scenes. It is asked by every void-out
  (z_play.c func_80169EFC) and by the drown, which then becomes a real void-out.
- **No change otherwise:** a form that walked around first and then fell in keeps vanilla behaviour, since its
  respawn point is proven safe.
- **Test hook (launch env only):** COMBO_DEBUG_MM_DROWNLOOP=1 (ComboDebugDrownLoop.cpp) puts the respawn point in
  the scene's largest water box and makes Link a Deku.
- **Verified in Woodfall (0x8600):** he respawns in the poison water as a Deku, sinks onto the void floor, the
  checker fires once, and he respawns human and swims; the loop never repeats.

## 2026-11-14 B-M40 — owner's five (pipes, pound vanish, prerender punches, Zora combo, underwater fins)

- **Deku pipes 90° back:** MM draws them in PLAYER_LIMB_HEAD space (drums stay on the torso); the port had both on
  the torso. `Combo_FormInstrumentDraw(play, this, limbIndex)`.
- **Model vanishing after a pound / punch:** `Combo_Crater_Draw` runs inside `Player_Draw` before the skeleton and
  its debris used `MTXMODE_NEW` with no push/pop, so for the crater's life Link was drawn at the last debris chunk's
  matrix. Now wrapped in `Matrix_Push/Pop`. This is also the "strange stuff" by buildings (wall craters). Checked in
  the prerendered Market square, the Potion Shop, and the back-alley house.
- **Zora combo:**
  - Ported MM's attack no-fall rule (`func_808381F8`): a moving form attack holds its ground while the floor it
    just left is within 10, so a jab carried down a step no longer drops into the fall mid-combo.
  - MM's still camera for the whole form attack and its follow-through (OOT only held it during the hit window, so
    the view lurched).
  - MM's anim flags `1|8|NOMOVE`.
  - MM's swing trail (`func_8012669C` entry [0]) for the Zora jabs and kick; the Goron gets the wall-test line only.
  - Hit quads armed only at `meleeWeaponState >= 1`.
  - The arm-fin scale keyframes for gakkistart / waterroll / swimtowait / bladeon / jumpAT / jumpATend; no arm fins
    during gakkiplay.
- **Fins stuck after one throw:** with the caught fins in hand, B was dead: no jab (fins held), no ready (press
  swallowed), no put-away (B held). Now a fresh B with the fins in hand puts them away and jabs, as MM's fins are
  just his arms again. A B pressed while they fly and held through the catch readies them again. Underwater
  harness: 5/5 holds throw.

## Animation audit (2026-11-15)

I checked every `gPlayerAnim_pg_/pz_/pn_` use in MM `z_player.c` (88 names) and every human animation MM picks
by form, against the OOT port.

- **Ported:** doors, instrument, drink, item-get (kSubs), Deku bubble aim/shoot (`pn_tamahakidf`), Deku guard
  (`pn_gurd`), idles (`pg_wait`/`pz_wait`), punches/attacks, climbs, chests, Goron curl, Zora swim/fins/boomerang,
  and Mask of Scents idle. Every other `link_*` animation plays from MM's data while transformed.
- **MISSING: the transformation sequence.** OOT swaps form instantly (`z_player.c` ~4303/~4442, CHANGE_ARMS sfx). MM
  does all of this: `func_808388B8` plays `cl_setmask` (human) or `D_8085D160` `p?_maskoffstart` (form), then
  `Player_Action_86`:
  - the mask-transformation cutscene camera;
  - the light rays (`func_808550D0`/`func_80855218`, `D_8085D910`);
  - LIGHTNING_HARD, the TRANSFORM_VOICE/FACE_CHANGE sounds, MASK_FLASH, and the white fill;
  - a re-init into the new form.

  `Player_Action_87` then plays `cl_setmaskend` (form) or reversed `cl_setmask` (human), or `cl_maskoff`. 2S2H's
  `gEnhancements.Masks.FastTransformation` skips this (flash + MASK_FLASH sfx only). Even that is not matched: the
  OOT port plays CHANGE_ARMS with no flash.
  **PORTED B-M55** (`z_player.c` Combo_TransformBegin, Player_Action_ComboTransform/End):
  - the start animations and the end animations;
  - Camera_Demo4/5 on a sub camera, the MASK_TRANSFORM_1/2 distortion, and the MM light, fog and point light;
  - the mask in hand, the mask squash on the face (and its fused variant), and the head glow;
  - the anim sfx, the white fill and the skip after first viewing;
  - 2S2H FastTransformation;
  - a direct form-to-form switch.

  The watched-transformation record is shared (`seenTransform` <-> MM weekEventReg 30).
- **N/A in Hyrule:** `pn_kakku`, `pn_kakkufinish`, `pn_rakkafinish`, `pn_batabata` are Deku Flower launch/glide
  (OOT has no Deku Flowers). `L_okarina_get` (`D_8085E354`) is an MM cutscene action.

## Steam Deck report (2026-11-24): fins, stuck pound, spiked roll vs grass

Owner, on build 2a683d1: holding B as the Zora never readies the fins (it does in MM); a roll sometimes turns
into a ground pound, and then every curl pounds until the mask comes off and back on; the spiked roll doesn't
cut grass.

- **Fins and the stuck pound: one cause.** SoH's Assignable Tunics and Boots
  (`gEnhancements.AssignableTunicsAndBoots`, on in the owner's config) answers `VB_EXECUTE_PLAYER_ACTION_FUNC`
  with false and calls `actionFunc` itself. All of ComboShip's per-frame form work sat inside that `if`, so it
  never ran:
  - the held-B fins step (`Player_UseItem(Combo_FormHeldBItem())`) and its traces (COMBO_DEBUG_FINS, the FORM_B
    "held-B item" line), so the fins never came out;
  - the end-of-frame clear of `sComboGoronPunchRequest` and `sComboPoundRequest`. After one Goron B press the
    pound request stayed set, and the ball pounds on the first grounded frame it reads it: the frame a curl
    finishes, and again right after each landing. Only `Combo_RefreshFormBody` (a mask change) cleared it.
  - Also skipped: the fin put-away, the Zora jump kick's shin hit, and the interrupted-ball cleanup.

  Fix: the form work now brackets the VB call. It runs whenever the action may run (`!PAUSE_ACTION_FUNC`), whoever
  runs it. Deck trace evidence: every pound starts on a curl's last frame (`b08` = the walking speed the curl
  started from, `vy 14.0`), and pounds repeat on landing at the same spot.
- **Spiked roll vs grass.** The ball draws no skeleton, so `bodyPartsPos` stayed where the Goron curled.
  `Player_UpdateCommon` sizes the body cylinder from them (feet to head + 10), so the roll's attack, hurtbox and
  OC stayed at the height of the curl. The Deck roll curled at y 0 and reached the grass near Kokiri Forest at
  y -200, so its cylinder passed about 200 units above the grass. MM places the parts on the ball every draw
  (`func_80846460` in `Player_Draw`). The port now does the same in the ball's draw (`Combo_GoronCurledBodyParts`,
  shared with the guard): the cylinder runs from the ground to 34 up, as in MM. The grass row in `sCrossObjects`
  already accepts the roll and the spikes (MM `z_en_kusa.c:89`, 0x0580C71C).

## Steam Deck report (2026-11-24): rolling through a loading zone

Owner, on build 2a683d1 (Assignable Tunics and Boots on): a Goron rolling through a loading zone arrives frozen in
the ball, in whatever phase of the roll he was in, until the mask comes off.

- **Cause.** The ball's state (`sRoll`, `gComboGoronBall` and the other `gComboGoronBall*` globals) is in statics,
  which outlive the player actor. Nothing on a scene change cleared it: `Player_Init` reset the blast-mask timer
  and the safe ground but not the forms, and `Player_Destroy` only the fin blurs. Only the end-of-frame
  "interrupted ball" cleanup or a mask change cleared it. A floor exit replaces the roll action with the walk-out
  (`func_80838E70`) before the action runs, so that cleanup clears the ball the same frame, but on 2a683d1 the
  cleanup sat inside the VB `if` that Assignable Tunics and Boots answers false. With the setting on it never ran,
  in either scene, and the draw kept showing the ball with its last spin, tilt and stretch.
- **Left after the VB fix.** The bracket fix lets the cleanup run, but an exit that keeps the roll action (an
  airborne exit, a void-out) still carries the ball into the new scene. There the cleanup's
  `Combo_GoronBallClear` restored the old scene's saved cylinder onto the new Link, with the old Link as its
  `base.actor`.
- **Fix.** `Player_Init` now calls `Combo_FormStateNewScene`. It drops the ball's state without restoring the
  cylinder, and clears the move latches (the same list a form change clears), the jump kick and the Zora barrier.
  It forgets the barrier's light node, which lived in the light buffer that the new scene's `LightContext_Init`
  zeroed, and the guard collider's set-up. Both of those were keyed on the `PlayState` address, which a new
  scene's `PlayState` can reuse.
- **Already safe across scenes:**
  - The Deku spin resets itself when the action isn't the spin.
  - The Zora fast swim is drawn only in the swim actions, and its setup sets the speed again.
  - The fins in flight are an actor of the old scene, and Link's `boomerangActor` is new.
  - The fin blurs are cleared in `Player_Destroy`.
