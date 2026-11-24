#!/usr/bin/env python3
"""Generate combo/gui/ComboPlayPresetData.inl from one or more real comboship.json files.

    scripts/wombo/gen-play-preset.py \
        --preset "Captain_Backwash's Controls:How the owner plays:/path/to/ComboShip-data/comboship.json" \
        > combo/gui/ComboPlayPresetData.inl

Only three slices of a config are carried, because a preset is a control setup, not a whole profile:
  bindings  gSettings.Controllers.*        (shared by both games; libultraship owns them)
  colors    gCosmetics.HUD.* (OOT)  +  gCosmetic.Buttons.* / gCosmetic.HUD.* (MM)
  prefs     the camera / ocarina / D-pad preferences each game keeps its own copy of (+ --extra)

Everything else in a config — saves, rando settings, Anchor room, window size, file paths — is
deliberately left out.
"""
import argparse
import json
import sys

# Control preferences worth carrying, per game. Keep in step with kRows in ComboSettingsLink.cpp.
PREF_KEYS = [
    # OOT
    "gSettings.FreeLook.Enabled",
    "gSettings.FreeLook.CameraSensitivity.X",
    "gSettings.FreeLook.CameraSensitivity.Y",
    "gSettings.FreeLook.InvertXAxis",
    "gSettings.FreeLook.InvertYAxis",
    "gSettings.FreeLook.MaxCameraDistance",
    "gSettings.FreeLook.TransitionSpeed",
    "gSettings.FreeLook.UseGameDistance",
    "gSettings.Controls.RightStickAim",
    "gSettings.MoveInFirstPerson",
    "gSettings.DisableFirstPersonAutoCenterView",
    "gSettings.FirstPersonCameraSensitivity.Enabled",
    "gSettings.FirstPersonCameraSensitivity.X",
    "gSettings.FirstPersonCameraSensitivity.Y",
    "gSettings.CustomOcarina.Enabled",
    "gSettings.CustomOcarina.RightStick",
    "gSettings.CustomOcarina.Dpad",
    "gSettings.DPadOnPause",
    "gSettings.DpadInText",
    "gSettings.DpadHoldChange",
    "gEnhancements.DpadEquips",
    "gCombo.DpadEquips",
    # MM
    "gEnhancements.Camera.FreeLook.Enable",
    "gEnhancements.Camera.FreeLook.MaxCameraDistance",
    "gEnhancements.Camera.FreeLook.TransitionSpeed",
    "gEnhancements.Camera.RightStick.CameraSensitivity.X",
    "gEnhancements.Camera.RightStick.CameraSensitivity.Y",
    "gEnhancements.Camera.RightStick.InvertXAxis",
    "gEnhancements.Camera.RightStick.InvertYAxis",
    "gEnhancements.Camera.FirstPerson.RightStickEnabled",
    "gEnhancements.Camera.FirstPerson.MoveInFirstPerson",
    "gEnhancements.Camera.FirstPerson.DisableFirstPersonAutoCenterView",
    "gEnhancements.Camera.FirstPerson.SensitivityX",
    "gEnhancements.Camera.FirstPerson.SensitivityY",
    "gEnhancements.Camera.FirstPerson.RightStickSensitivityX",
    "gEnhancements.Camera.FirstPerson.RightStickSensitivityY",
    "gEnhancements.Playback.RightStickOcarina",
    "gEnhancements.Playback.DpadOcarina",
    "gEnhancements.Dpad.DpadEquips",
]

# Set by --extra KEY=VALUE: preferences the preset carries whatever the source config says (the owner's
# "keep letterbox bars out": both games' black-bar switches, which his config leaves at the default "on").
EXTRA_PREFS = {}

COLOR_PREFIXES = ("gCosmetics.HUD.", "gCosmetic.Buttons.", "gCosmetic.HUD.")
BINDING_PREFIX = "gSettings.Controllers."


def flatten(node, prefix, out):
    """Flatten the CVar tree to dotted leaves. A {R,G,B,A,Type} node becomes a 4-element array so
    the loader writes it with CVarSetColor, which is how both games read colours."""
    if isinstance(node, dict):
        if "Type" in node and {"R", "G", "B"} <= set(node):
            out[prefix] = [int(node["R"]), int(node["G"]), int(node["B"]), int(node.get("A", 255))]
            return
        for key, value in node.items():
            flatten(value, f"{prefix}.{key}" if prefix else key, out)
    else:
        out[prefix] = node


def slice_config(path):
    with open(path, "r", encoding="utf-8") as fh:
        cfg = json.load(fh)
    flat = {}
    flatten(cfg.get("CVars", {}), "", flat)
    bindings, colors, prefs = {}, {}, {}
    for key, value in sorted(flat.items()):
        if key.startswith(BINDING_PREFIX):
            bindings[key] = value
        elif key.startswith(COLOR_PREFIXES):
            colors[key] = value
        elif key in PREF_KEYS:
            prefs[key] = value
    for key, value in EXTRA_PREFS.items():
        prefs[key] = value
    missing = [k for k in PREF_KEYS if k not in prefs]
    print(f"// {path}: {len(bindings)} bindings, {len(colors)} colours, "
          f"{len(prefs)} preferences ({len(missing)} left at default)", file=sys.stderr)
    return {"bindings": bindings, "colors": colors, "prefs": prefs}


def c_string(text):
    """A JSON blob as a C string literal, split per line so the .inl stays readable in review."""
    chunks = []
    for i in range(0, len(text), 110):
        piece = text[i:i + 110].replace("\\", "\\\\").replace('"', '\\"')
        chunks.append(f'        "{piece}"')
    return "\n".join(chunks)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", action="append", required=True,
                    metavar="NAME:DESCRIPTION:PATH",
                    help="repeatable; NAME and DESCRIPTION must not contain a colon")
    ap.add_argument("--extra", action="append", default=[], metavar="KEY=VALUE",
                    help="repeatable; a preference every preset carries (VALUE parsed as JSON)")
    args = ap.parse_args()
    for spec in args.extra:
        key, value = spec.split("=", 1)
        EXTRA_PREFS[key] = json.loads(value)

    print("// Generated by scripts/wombo/gen-play-preset.py — do not edit by hand.")
    print("// Each preset is the control slice of a real comboship.json (bindings, HUD button")
    print("// colours, control preferences). Regenerate when the source config changes.")
    print("const PresetDef kPresets[] = {")
    for spec in args.preset:
        name, description, path = spec.split(":", 2)
        data = json.dumps(slice_config(path), separators=(",", ":"), sort_keys=True)
        print("    {")
        print(f'        "{name}",')
        print(f'        "{description}",')
        print(c_string(data) + ",")
        print("    },")
    print("};")


if __name__ == "__main__":
    main()
