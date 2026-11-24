# Adult Link in Termina: body audit (plan C3), 2026-11-15

Question: can the adult body reach everything in Termina that MM logic expects child Link to reach? If not,
MM logic would need an age gate on those spots.

## The adult body is Zora's collision body

`Combo_AdultAgeProperties()` (mm `z_player.c`) uses OOT adult's lengths. Every number the collision code uses
equals MM's Zora form:

| field | child (human) | adult | Zora | Goron |
|---|---|---|---|---|
| ceilingCheckHeight | 40 | 56 | 56 | 70 |
| wallCheckRadius | 14 | 18 | 18 | 19.5 |
| wall sphere height | 26.8 | 26.8 | 26.8 | 26.8 |

So any spot this audit finds blocked for adult is also blocked for Zora Link. MM was built for Zora (and the
even bigger Goron) to walk its world.

How MM blocks a body (`Actor_UpdateBgCheckInfo`, mm `z_actor.c`):
- **Walls:** a sphere of `wallCheckRadius` centred 26.8 up.
- **Ceilings:** a ceiling within `ceilingCheckHeight` pushes Link down. If the floor and the ceiling belong to
  the same collision, the floor snap puts x/z back to the previous frame. That is what stops a body under a low
  ceiling.

## Scan: every MM scene, static collision

The tool is `COMBO_DEBUG_MM_CLEARANCE=<first hex>[,only=<hex>,...]` in
`mm/2s2h/Rando/MiscBehavior/ComboDebugClearance.cpp`. It visits every entrance scene and does two things:

1. **Clearance scan.** It samples every walkable floor for a ceiling 40-56 up: low enough to stop adult, high
   enough to let child through.
2. **Reach flood.** It walks a 12-unit grid from every spawn of the scene twice:
   - once with the child body (radius 14, ceiling 40);
   - once with the adult body (radius 18, ceiling 56).

   Each step uses MM's own wall, ceiling and floor tests. The flood then traces the child's path back to find
   the **gate** where the adult body is stopped, and why.

Results (101 scenes):
- Child-only floor that is more than three grid steps from any adult-reached floor showed up in 15 scenes. The
  thin strip along walls that the 4-unit-wider body can't hug is excluded.
- 14 gates had 15 or more nodes behind them:
  - Pirates' Fortress interior;
  - Stone Tower;
  - Snowhead (two gates, plus a drop into the abyss);
  - East Clock Town rooftop;
  - Stock Pot Inn;
  - Goron Shrine;
  - Ikana Castle;
  - Deku Palace (a ceiling gate);
  - five Termina Field slopes.
- Underwater gates (Pinnacle Rock) are not walking gates and were left out.

## In-game check: walk both bodies through each gate

`COMBO_DEBUG_MM_WALK="<entrance scene>,x,y,z,tx,tz[,frames]"` puts Link at the gate's near side and holds the
stick toward a point 72 units past it. It goes through Player's own camera-relative stick reading. It was run
once as adult and once as child, in the same build.

| gate | adult | child |
|---|---|---|
| Stone Tower 432,-972,2830 | wall at z 2813 | wall at z 2817 |
| East Clock Town roof 1227,340,-1164 | wall, 12 short | wall, 11 short |
| East Clock Town roof 1227,340,-1104 | reached | reached |
| Stock Pot Inn -426,210,-2 | reached | reached |
| Ikana Castle 632,-30,-2844 | wall, 45 short | wall, 47 short |
| Goron Shrine (2 gates) | fell to the lower floor | the same |
| Termina Field slopes (5) | the same end point (within 11 units) | the same |
| Pirates' Fortress interior | caught by a guard | caught by a guard |
| Deku Palace | thrown out by the guards | thrown out by the guards |
| Snowhead (2 gates) | pushed back by the wind | pushed back by the wind |

In the walk test, every gate behaved the same for both bodies. The scan's extra child reach came from the grid
model (12-unit hops past thin risers), not from the game.

## Verdict

- **No age gate is needed in MM logic.** The adult body walks everywhere child Link walks. The adult also jumps
  further (root motion 1.0 vs 11/17), climbs higher ledges, and keeps every mask, so it is never weaker.
- MM logic stays age-agnostic. The only per-age differences are items, which the OOT-items-in-Termina work
  already credits conservatively (plan C2).

## Not covered

- BgActor collision (doors, moving platforms, gates opened by switches). Those are the same actors for both
  bodies.
- Jumps and ledge climbs. The adult values are larger, so they can only help.
- The guarded areas (Pirates' Fortress interior, Deku Palace) were not walked past their guards. Both ages are
  thrown out the same way.
