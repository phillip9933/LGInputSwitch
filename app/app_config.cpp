#include "app_config.h"
#include "util.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <windows.h>
#include "../external/json.hpp"

using nlohmann::json;

// ---------- Paths ----------

static std::string ExeDir() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    return std::filesystem::path(exePath).parent_path().string();
}

std::string ConfigPath() {
    return (std::filesystem::path(ExeDir()) / "config.json").string();
}

bool EnsureConfigDir() {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(ConfigPath()).parent_path(), ec);
    return !ec;
}

// ---------- Defaults ----------

static AppConfig Defaults() {
    AppConfig c;
    c.targets = { {5, 0} };
    c.inputs = {
        {"DisplayPort", "0xD0"},
        {"USB-C",       "0xD1"},
        {"HDMI1",       "0x90"},
        {"HDMI2",       "0x91"}
    };
    c.cycleOrder = { "DisplayPort", "USB-C", "HDMI1", "HDMI2" };
    c.i2cSourceAddr = "0x50"; // LG alt path
    c.hotkeys.cycle = "CTRL+ALT+1";
    c.hotkeys.direct = {
        {"DisplayPort", "CTRL+ALT+2"},
        {"USB-C",       "CTRL+ALT+3"},
        {"HDMI1",       "CTRL+ALT+4"},
        {"HDMI2",       "CTRL+ALT+5"}
    };
    c.debounceMs = 750;
    c.showNotifications = true;
    c.startWithWindows = false;
    return c;
}

// ---------- JSON helpers ----------

static std::string Escape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
        case '\"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\b': o << "\\b";  break;
        case '\f': o << "\\f";  break;
        case '\n': o << "\\n";  break;
        case '\r': o << "\\r";  break;
        case '\t': o << "\\t";  break;
        default:   o << c;      break;
        }
    }
    return o.str();
}

static std::string ToJson(const AppConfig& c) {
    std::ostringstream out;
    out << "{\n";

    // targets
    out << "  \"targets\": [";
    for (size_t i = 0; i < c.targets.size(); ++i) {
        if (i) out << ", ";
        out << "[" << c.targets[i].first << ", " << c.targets[i].second << "]";
    }
    out << "],\n";

    // inputs
    out << "  \"inputs\": [\n";
    for (size_t i = 0; i < c.inputs.size(); ++i) {
        const auto& in = c.inputs[i];
        out << "    {\"label\": \"" << Escape(in.label) << "\", \"code\": \"" << Escape(in.code) << "\"}";
        out << (i + 1 < c.inputs.size() ? ",\n" : "\n");
    }
    out << "  ],\n";

    // cycleOrder
    out << "  \"cycleOrder\": [";
    for (size_t i = 0; i < c.cycleOrder.size(); ++i) {
        if (i) out << ", ";
        out << "\"" << Escape(c.cycleOrder[i]) << "\"";
    }
    out << "],\n";

    // i2cSourceAddr
    out << "  \"i2cSourceAddr\": \"" << Escape(c.i2cSourceAddr) << "\",\n";

    // hotkeys
    out << "  \"hotkeys\": {\n";
    out << "    \"cycle\": \"" << Escape(c.hotkeys.cycle) << "\",\n";
    out << "    \"direct\": {";
    size_t j = 0, n = c.hotkeys.direct.size();
    for (const auto& kv : c.hotkeys.direct) {
        if (j++) out << ", ";
        out << "\"" << Escape(kv.first) << "\": \"" << Escape(kv.second) << "\"";
    }
    out << "}\n";
    out << "  },\n";

    // misc
    out << "  \"debounceMs\": " << c.debounceMs << ",\n";
    out << "  \"showNotifications\": " << (c.showNotifications ? "true" : "false") << ",\n";
    out << "  \"startWithWindows\": " << (c.startWithWindows ? "true" : "false") << "\n";

    out << "}\n";
    return out.str();
}

static bool ParseJsonToConfig(const json& j, AppConfig& out) {
    try {
        AppConfig c = Defaults();

        // targets
        if (j.contains("targets") && j["targets"].is_array() && !j["targets"].empty()) {
            c.targets.clear();
            for (auto& t : j["targets"]) {
                if (t.is_array() && t.size() == 2 && t[0].is_number_integer() && t[1].is_number_integer()) {
                    c.targets.emplace_back(t[0].get<int>(), t[1].get<int>());
                }
            }
            if (c.targets.empty()) c.targets = Defaults().targets;
        }

        // inputs
        if (j.contains("inputs") && j["inputs"].is_array() && !j["inputs"].empty()) {
            c.inputs.clear();
            for (auto& it : j["inputs"]) {
                if (it.contains("label") && it.contains("code") && it["label"].is_string() && it["code"].is_string()) {
                    c.inputs.push_back({ it["label"].get<std::string>(), it["code"].get<std::string>() });
                }
            }
            if (c.inputs.empty()) c.inputs = Defaults().inputs;
        }

        // cycleOrder
        if (j.contains("cycleOrder") && j["cycleOrder"].is_array()) {
            c.cycleOrder.clear();
            for (auto& s : j["cycleOrder"]) if (s.is_string()) c.cycleOrder.push_back(s.get<std::string>());
            if (c.cycleOrder.empty()) c.cycleOrder = Defaults().cycleOrder;
        }

        // i2cSourceAddr
        if (j.contains("i2cSourceAddr") && j["i2cSourceAddr"].is_string()) {
            c.i2cSourceAddr = j["i2cSourceAddr"].get<std::string>();
        }

        // hotkeys.cycle
        if (j.contains("hotkeys") && j["hotkeys"].is_object()) {
            auto& hk = j["hotkeys"];
            if (hk.contains("cycle") && hk["cycle"].is_string())
                c.hotkeys.cycle = hk["cycle"].get<std::string>();
            // hotkeys.direct
            if (hk.contains("direct") && hk["direct"].is_object()) {
                c.hotkeys.direct.clear();
                for (auto it = hk["direct"].begin(); it != hk["direct"].end(); ++it) {
                    if (it.value().is_string()) c.hotkeys.direct[it.key()] = it.value().get<std::string>();
                }
            }
        }

        // misc
        if (j.contains("debounceMs") && j["debounceMs"].is_number_integer())
            c.debounceMs = j["debounceMs"].get<int>();
        if (j.contains("showNotifications") && j["showNotifications"].is_boolean())
            c.showNotifications = j["showNotifications"].get<bool>();
        if (j.contains("startWithWindows") && j["startWithWindows"].is_boolean())
            c.startWithWindows = j["startWithWindows"].get<bool>();

        out = std::move(c);
        return true;
    }
    catch (...) {
        return false;
    }
}

// ---------- Public API ----------

bool LoadConfig(AppConfig& out) {
    const auto path = ConfigPath();
    if (!std::filesystem::exists(path)) {
        // First run: provide defaults in-memory, but signal "not loaded" so the app can
        // show Welcome + Settings before saving.
        out = Defaults();
        return false;
    }

    // Read file
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        out = Defaults();
        return false;
    }
    std::stringstream ss; ss << f.rdbuf();
    f.close();

    // Parse JSON
    try {
        nlohmann::json j = nlohmann::json::parse(ss.str());
        if (!ParseJsonToConfig(j, out)) {
            out = Defaults();
            return false;
        }
    }
    catch (...) {
        out = Defaults();
        return false;
    }
    return true;
}

bool SaveConfig(const AppConfig& cfg) {
    if (!EnsureConfigDir()) return false;
    std::ofstream f(ConfigPath(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::string json = ToJson(cfg);
    f.write(json.data(), (std::streamsize)json.size());
    return (bool)f;
}
