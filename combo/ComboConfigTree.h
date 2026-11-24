// combo/ComboConfigTree.h
//
// ComboShip: helpers for the "CVars" block of a Ship config file (comboship.json). The block is a
// nested JSON tree whose leaves are numbers, booleans, strings, or colour objects
// ({"R","G","B","A","Type":"RGBA"|"RGB"}); arrays are never CVars. Presets and the settings import
// work on a flat {"dotted.key": value} view of that tree, with colours as 3/4-number arrays.
//
// Header-only and std + nlohmann only, so both comboui (menu import, presets) and the launcher
// (first-launch import) share one definition of what a config file looks like.
#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>

namespace ComboConfigTree {

using Leaves = std::map<std::string, nlohmann::json>;

constexpr const char* kCVarsBlock = "CVars";
// Present in every config ComboShip has written; neither game's own config has it.
constexpr const char* kComboCVarRoot = "gCombo";
constexpr const char* kColorTypeKey = "Type";
constexpr const char* kColorTypeRgba = "RGBA";
constexpr const char* kColorTypeRgb = "RGB";

inline bool IsColorNode(const nlohmann::json& node) {
    if (!node.is_object() || !node.contains(kColorTypeKey) || !node[kColorTypeKey].is_string()) {
        return false;
    }
    const std::string type = node[kColorTypeKey].get<std::string>();
    return type == kColorTypeRgba || type == kColorTypeRgb;
}

// {"R","G","B"[,"A"]} -> [r, g, b[, a]]; a missing or non-numeric channel reads as 0.
inline nlohmann::json ColorNodeToArray(const nlohmann::json& node) {
    auto channel = [&](const char* name) -> int {
        auto it = node.find(name);
        return (it != node.end() && it->is_number_integer()) ? it->get<int>() : 0;
    };
    nlohmann::json out = nlohmann::json::array({ channel("R"), channel("G"), channel("B") });
    if (node[kColorTypeKey].get<std::string>() == kColorTypeRgba) {
        out.push_back(channel("A"));
    }
    return out;
}

// Calls fn(dottedPath, leafNode) for every CVar under `node`. A colour object counts as one leaf.
template <typename Fn> void VisitLeaves(const nlohmann::json& node, const std::string& path, Fn&& fn) {
    if (IsColorNode(node)) {
        fn(path, node);
    } else if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            VisitLeaves(it.value(), path.empty() ? it.key() : path + "." + it.key(), fn);
        }
    } else if (!node.is_array() && !node.is_null()) {
        fn(path, node);
    }
}

inline Leaves Flatten(const nlohmann::json& cvars) {
    Leaves out;
    VisitLeaves(cvars, "", [&](const std::string& path, const nlohmann::json& leaf) {
        out[path] = IsColorNode(leaf) ? ColorNodeToArray(leaf) : leaf;
    });
    return out;
}

inline void SetByPath(nlohmann::json& root, const std::string& path, const nlohmann::json& leaf) {
    nlohmann::json* node = &root;
    size_t start = 0;
    for (size_t dot = path.find('.'); dot != std::string::npos; dot = path.find('.', start)) {
        nlohmann::json& child = (*node)[path.substr(start, dot - start)];
        if (!child.is_object()) {
            child = nlohmann::json::object();
        }
        node = &child;
        start = dot + 1;
    }
    (*node)[path.substr(start)] = leaf;
}

// A copy of the tree that keeps only the leaves keep(dottedPath) accepts (same nested shape).
template <typename Keep> nlohmann::json FilterTree(const nlohmann::json& cvars, Keep&& keep) {
    nlohmann::json out = nlohmann::json::object();
    VisitLeaves(cvars, "", [&](const std::string& path, const nlohmann::json& leaf) {
        if (keep(path)) {
            SetByPath(out, path, leaf);
        }
    });
    return out;
}

// True for a config file that ComboShip itself has written: an object whose CVars block has the
// gCombo section. Vanilla SoH / 2S2H configs and arbitrary JSON are refused.
inline bool LooksLikeComboShipConfig(const nlohmann::json& root) {
    if (!root.is_object() || !root.contains(kCVarsBlock) || !root[kCVarsBlock].is_object()) {
        return false;
    }
    const nlohmann::json& cvars = root[kCVarsBlock];
    return cvars.contains(kComboCVarRoot) && cvars[kComboCVarRoot].is_object();
}

} // namespace ComboConfigTree
