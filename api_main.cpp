
#include <iostream>
#include <string>
#include <algorithm>
#include <exception>
#include <cstdlib>
#include <ctime>
#include <map>
#include <tuple>
#include <sstream>
#include <cctype>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <atomic>
#include <mutex>
#include <set>
#include <unordered_map>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#endif

#include "core/home/SmartHome.h"
#include "third_party/httplib.h"
#include "third_party/json.hpp"

#include "core/devices/GenericAppliance.h"
#include "core/devices/Light.h"
#include "core/devices/Fan.h"
#include "core/devices/AC.h"
#include "core/devices/Heater.h"

using json = nlohmann::json;

namespace
{
    struct RoomAutomationSession
    {
        std::string roomName;
        std::string enrollmentId;
        std::string profileId;
        std::string observedEnrollmentId;
        std::string observationStatus = "starting";
        long long observedAt = 0;
        long long lastActivityAt = 0;
        UserPreferenceRecord preferences;
        json preview = json::array();
        std::set<std::string> appliedEnrollments;
    };

    struct RoomAutomationRegistry
    {
        std::mutex mutex;
        std::string activeSessionId;
        std::unordered_map<std::string, RoomAutomationSession> sessions;
        std::atomic<unsigned long long> nextId{1};
    };

    struct ModePreviewRecord
    {
        json selection;
        json actions;
        long long expiresAt = 0;
    };

    struct ModePreviewRegistry
    {
        std::mutex mutex;
        std::unordered_map<std::string, ModePreviewRecord> previews;
        std::atomic<unsigned long long> nextId{1};
    };

    struct ModePresetInfo
    {
        const char *id;
        const char *label;
        const char *description;
    };

    const std::vector<ModePresetInfo> &seasonPresetInfo()
    {
        static const std::vector<ModePresetInfo> presets = {
            {"summer", "Summer", "Suggested cooling and airflow policy: compatible ACs and fans ON, heaters OFF, lights unchanged."},
            {"monsoon", "Monsoon", "Suggested airflow policy: fans ON; ACs, heaters, and lights unchanged. No weather is measured."},
            {"winter", "Winter", "Suggested heating policy: heaters ON, ACs OFF, fans configured without changing power, lights unchanged."},
            {"spring", "Spring", "Suggested mild policy: ACs and heaters OFF, fans configured without changing power, lights unchanged."}};
        return presets;
    }

    const std::vector<ModePresetInfo> &moodPresetInfo()
    {
        static const std::vector<ModePresetInfo> presets = {
            {"focus", "Focus / Study", "Suggested study lights and fan ON, with supported lighting and fan settings."},
            {"relax", "Relax", "Suggested lights and bedroom/living fans ON with softer supported settings."},
            {"romantic", "Romantic", "Suggested ambient-capable lights ON with warm or colored settings where supported."},
            {"movie-night", "Movie Night", "Suggested dimmable lights ON at low brightness; other lights and devices stay unchanged."},
            {"sleep", "Sleep", "Suggested bedroom lights OFF and fan ON at a low setting; other devices stay unchanged."},
            {"energize", "Energize", "Suggested study/living lights and fans ON with supported settings."}};
        return presets;
    }

    const ModePresetInfo *findPresetInfo(
        const std::string &modeType, const std::string &presetId)
    {
        const auto &presets =
            modeType == "season" ? seasonPresetInfo() : moodPresetInfo();
        const auto found = std::find_if(
            presets.begin(), presets.end(),
            [&presetId](const ModePresetInfo &preset)
            { return preset.id == presetId; });
        return found == presets.end() ? nullptr : &*found;
    }

    std::string modeRoomCategory(const std::string &roomName)
    {
        std::string lower = roomName;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char value)
                       { return static_cast<char>(std::tolower(value)); });
        if (lower.find("bed") != std::string::npos)
            return "bedroom";
        if (lower.find("study") != std::string::npos ||
            lower.find("office") != std::string::npos)
            return "study";
        if (lower.find("living") != std::string::npos ||
            lower.find("lounge") != std::string::npos)
            return "living";
        if (lower.find("kitchen") != std::string::npos)
            return "kitchen";
        return "other";
    }

    using ModeRule = std::pair<std::string, json>;

    std::string defaultModePolicy(
        const std::string &modeType,
        const std::string &presetId,
        const DeviceRecord &device)
    {
        const std::string category = modeRoomCategory(device.roomName);
        if (modeType == "season")
        {
            if (device.type == "AC")
            {
                if (presetId == "summer")
                    return "ON";
                if (presetId == "monsoon")
                    return "CONFIGURE_ONLY";
                if (presetId == "winter" || presetId == "spring")
                    return "OFF";
                return "KEEP_CURRENT";
            }
            if (device.type == "Fan")
            {
                if (presetId == "summer" || presetId == "monsoon")
                    return "ON";
                if (presetId == "winter" || presetId == "spring")
                    return "CONFIGURE_ONLY";
                return "KEEP_CURRENT";
            }
            if (device.type == "Heater")
            {
                if (presetId == "winter")
                    return "ON";
                if (presetId == "summer" || presetId == "spring")
                    return "OFF";
                return "KEEP_CURRENT";
            }
            if (device.type == "Light")
                return "KEEP_CURRENT";
            return "KEEP_CURRENT";
        }

        if (device.type == "Light")
        {
            if (presetId == "focus" && category == "study")
                return "ON";
            if (presetId == "relax" || presetId == "romantic")
                return presetId != "romantic" ||
                               device.supportsBrightness ||
                               device.supportsColor ||
                               device.supportsColorTemperature
                    ? "ON"
                    : "KEEP_CURRENT";
            if (presetId == "movie-night")
                return device.supportsBrightness ? "ON" : "KEEP_CURRENT";
            if (presetId == "sleep" && category == "bedroom")
                return "OFF";
            if (presetId == "energize" &&
                (category == "study" || category == "living"))
                return "ON";
        }
        if (device.type == "Fan")
        {
            if ((presetId == "focus" && category == "study") ||
                (presetId == "sleep" && category == "bedroom") ||
                (presetId == "relax" &&
                 (category == "bedroom" || category == "living")) ||
                (presetId == "energize" &&
                 (category == "study" || category == "living")))
                return "ON";
        }
        return "";
    }

    std::vector<ModeRule> defaultModeRules(
        const std::string &modeType,
        const std::string &presetId,
        const DeviceRecord &device)
    {
        std::vector<ModeRule> rules;
        const std::string category = modeRoomCategory(device.roomName);
        if (modeType == "season")
        {
            if (device.type == "AC")
            {
                const int temperature =
                    presetId == "summer" ? 24 :
                    presetId == "monsoon" ? 25 :
                    presetId == "winter" ? 26 : 24;
                rules.emplace_back("setting", temperature);
            }
            else if (device.type == "Fan")
            {
                int speed = 0;
                if (presetId == "summer")
                    speed = category == "living" ? 3 : 2;
                else if (presetId == "monsoon")
                    speed = category == "living" ? 2 : 1;
                else if (presetId == "winter")
                    speed = category == "bedroom" ? 1 : 2;
                else
                    speed = category == "bedroom" ? 1 :
                            category == "living" ? 3 : 2;
                rules.emplace_back("setting", speed);
            }
            else if (device.type == "Heater" && presetId == "winter")
            {
                const int temperature =
                    category == "bedroom" ? 21 :
                    category == "study" ? 20 : 22;
                rules.emplace_back("setting", temperature);
            }
            return rules;
        }

        if (device.type == "Light")
        {
            if (presetId == "focus" && category == "study")
            {
                rules.emplace_back("brightness", 85);
                rules.emplace_back("colorTemperature", 4500);
            }
            else if (presetId == "relax")
            {
                rules.emplace_back("brightness", 35);
                rules.emplace_back("colorTemperature", 3000);
            }
            else if (presetId == "romantic")
            {
                rules.emplace_back("brightness", 40);
                rules.emplace_back("color", "#D96B78");
                rules.emplace_back("colorTemperature", 2700);
            }
            else if (presetId == "movie-night")
            {
                rules.emplace_back("brightness", 15);
            }
            else if (presetId == "sleep" && category == "bedroom")
            {
                rules.emplace_back("brightness", 10);
            }
            else if (presetId == "energize" &&
                     (category == "study" || category == "living"))
            {
                rules.emplace_back("brightness", 100);
                rules.emplace_back("colorTemperature", 5000);
            }
        }
        else if (device.type == "Fan")
        {
            if ((presetId == "focus" && category == "study") ||
                (presetId == "sleep" && category == "bedroom"))
                rules.emplace_back("setting", presetId == "sleep" ? 1 : 2);
            else if (presetId == "relax" &&
                     (category == "bedroom" || category == "living"))
                rules.emplace_back("setting", 1);
            else if (presetId == "energize" &&
                     (category == "study" || category == "living"))
                rules.emplace_back("setting", 3);
        }
        return rules;
    }

    bool modePropertySupported(
        const DeviceRecord &device,
        const std::string &property,
        const json &value,
        std::string &reason)
    {
        if (property == "power")
        {
            if (!value.is_boolean())
            {
                reason = "Power values must be boolean.";
                return false;
            }
            return true;
        }
        if (property == "policy")
        {
            if (!value.is_string())
            {
                reason = "Device policy must be a string.";
                return false;
            }
            const std::string policy = value.get<std::string>();
            if (policy == "ON" || policy == "OFF" ||
                policy == "KEEP_CURRENT" || policy == "CONFIGURE_ONLY")
                return true;
            reason = "Device policy must be ON, OFF, KEEP_CURRENT, or CONFIGURE_ONLY.";
            return false;
        }
        if (property == "setting")
        {
            if (!value.is_number_integer())
            {
                reason = "Device settings must be integer values.";
                return false;
            }
            const int setting = value.get<int>();
            if (device.type == "AC" && setting >= 16 && setting <= 30)
                return true;
            if (device.type == "Fan" && setting >= 1 && setting <= 5)
                return true;
            if (device.type == "Heater" && setting >= 20 && setting <= 45)
                return true;
            reason = device.type == "AC"
                ? "AC temperature must be between 16 and 30 °C."
                : device.type == "Fan"
                    ? "Fan speed must be between 1 and 5."
                    : device.type == "Heater"
                        ? "Heater temperature must be between 20 and 45 °C."
                        : "This device does not support an adjustable setting.";
            return false;
        }
        if (property == "brightness")
        {
            if (!value.is_number_integer() ||
                value.get<int>() < 0 || value.get<int>() > 100)
            {
                reason = "Brightness must be an integer from 0 to 100.";
                return false;
            }
            if (device.type != "Light" || !device.supportsBrightness)
            {
                reason = "This light does not support brightness.";
                return false;
            }
            return true;
        }
        if (property == "color")
        {
            if (!value.is_string())
            {
                reason = "Color must be a #RRGGBB hexadecimal string.";
                return false;
            }
            const std::string color = value.get<std::string>();
            if (color.size() != 7 || color[0] != '#' ||
                !std::all_of(color.begin() + 1, color.end(),
                             [](unsigned char character)
                             { return std::isxdigit(character) != 0; }))
            {
                reason = "Color must be a #RRGGBB hexadecimal string.";
                return false;
            }
            if (device.type != "Light" || !device.supportsColor)
            {
                reason = "This light does not support RGB color.";
                return false;
            }
            return true;
        }
        if (property == "colorTemperature")
        {
            if (!value.is_number_integer() ||
                value.get<int>() < 2700 || value.get<int>() > 6500)
            {
                reason = "Color temperature must be an integer from 2700 to 6500 K.";
                return false;
            }
            if (device.type != "Light" || !device.supportsColorTemperature)
            {
                reason = "This light does not support adjustable color temperature.";
                return false;
            }
            return true;
        }
        reason = "Unsupported mode setting property.";
        return false;
    }

    json currentModeValue(const DeviceRecord &device, const std::string &property)
    {
        if (property == "power")
            return device.status == 1;
        if (property == "setting")
            return device.setting;
        if (property == "brightness")
            return device.brightness;
        if (property == "color")
            return device.color;
        if (property == "colorTemperature")
            return device.colorTemperature;
        return nullptr;
    }

    void applyModeValue(
        SmartHome &home,
        const std::string &deviceId,
        const std::string &property,
        const json &value)
    {
        if (property == "power")
        {
            if (value.get<bool>())
                home.turnOn(deviceId);
            else
                home.turnOff(deviceId);
        }
        else if (property == "setting")
        {
            if (!home.setDeviceSetting(deviceId, value.get<int>()))
                throw std::runtime_error("Could not persist the device setting.");
        }
        else if (property == "brightness")
        {
            if (!home.setLightBrightness(deviceId, value.get<int>()))
                throw std::runtime_error("Could not persist light brightness.");
        }
        else if (property == "color")
        {
            if (!home.setLightColor(deviceId, value.get<std::string>()))
                throw std::runtime_error("Could not persist light color.");
        }
        else if (property == "colorTemperature")
        {
            if (!home.setLightColorTemperature(deviceId, value.get<int>()))
                throw std::runtime_error("Could not persist light color temperature.");
        }
        else
        {
            throw std::invalid_argument("Unsupported mode setting property.");
        }
    }

    json serializeModeHistory(const ModeHistoryRecord &history)
    {
        json details = json::array();
        json selection = json::object();
        json restoreDetails = json::array();
        try
        {
            details = json::parse(history.detailsJson);
            restoreDetails = json::parse(history.restoreDetailsJson);
            if (details.is_object() && details.contains("actions"))
            {
                selection = details.value("selection", json::object());
                details = details["actions"];
            }
        }
        catch (const json::exception &)
        {
            throw std::runtime_error("Stored mode history contains invalid JSON.");
        }
        return {
            {"id", history.id},
            {"seasonId", history.seasonId},
            {"moodId", history.moodId},
            {"appliedAt", history.appliedAt},
            {"outcome", history.outcome},
            {"selection", selection},
            {"actions", details},
            {"restoredAt", history.restoredAt},
            {"restoreOutcome", history.restoredAt > 0 ? "restored" : ""},
            {"restoreActions", restoreDetails},
            {"canRestore", history.restoredAt == 0 &&
                               !history.snapshotJson.empty() &&
                               history.snapshotJson != "[]"}};
    }

    json buildModeActions(
        SmartHome &home,
        const json &selection,
        const std::vector<DeviceRecord> &devices)
    {
        const std::string seasonId = selection.value("seasonId", "");
        const std::string moodId = selection.value("moodId", "");
        std::set<std::string> roomNames;
        std::set<std::string> deviceIds;
        if (selection.contains("roomNames"))
            for (const auto &room : selection["roomNames"])
                roomNames.insert(room.get<std::string>());
        if (selection.contains("deviceIds"))
            for (const auto &device : selection["deviceIds"])
                deviceIds.insert(device.get<std::string>());

        auto inTarget = [&](const DeviceRecord &device)
        {
            return (roomNames.empty() || roomNames.count(device.roomName) > 0) &&
                   (deviceIds.empty() || deviceIds.count(device.id) > 0);
        };

        std::map<std::pair<std::string, std::string>, json> proposals;
        auto addProposal = [&](const DeviceRecord &device,
                               const std::string &property,
                               const json &value,
                               const std::string &source,
                               const std::string &reason)
        {
            const auto key = std::make_pair(device.id, property);
            const bool hasPrevious = proposals.find(key) != proposals.end();
            std::string finalReason = reason;
            if (hasPrevious && source == "mood")
                finalReason = "Mood overlay overrides the seasonal baseline for this device property.";
            else if (source == "custom")
                finalReason = "User-customized preset value.";
            proposals[key] = {
                {"deviceId", device.id},
                {"deviceName", device.name},
                {"deviceType", device.type},
                {"roomName", device.roomName},
                {"property", property},
                {"previousValue", currentModeValue(device, property)},
                {"value", value},
                {"source", source},
                {"reason", finalReason}};
        };

        std::map<std::string, std::string> policies;
        std::map<std::string, std::string> policySources;
        auto applyDefaultPolicies = [&](const std::string &modeType,
                                        const std::string &presetId,
                                        const std::string &source)
        {
            if (presetId.empty())
                return;
            for (const DeviceRecord &device : devices)
            {
                if (!inTarget(device))
                    continue;
                const std::string policy =
                    defaultModePolicy(modeType, presetId, device);
                if (!policy.empty())
                {
                    policies[device.id] = policy;
                    policySources[device.id] = source;
                }
            }
        };
        applyDefaultPolicies("season", seasonId, "season");
        applyDefaultPolicies("mood", moodId, "mood");

        auto loadPolicyOverrides = [&](const std::string &modeType,
                                       const std::string &presetId)
        {
            if (presetId.empty())
                return;
            const auto stored = home.getModePresets(modeType);
            const auto preset = std::find_if(
                stored.begin(), stored.end(),
                [&presetId](const ModePresetRecord &item)
                { return item.presetId == presetId; });
            if (preset == stored.end())
                return;
            const json overrides = json::parse(preset->overridesJson);
            for (const json &rule : overrides)
            {
                const std::string property = rule.value("property", "");
                if (property != "policy" && property != "power")
                    continue;
                const std::string id = rule.value("deviceId", "");
                const auto device = std::find_if(
                    devices.begin(), devices.end(),
                    [&id](const DeviceRecord &item)
                    { return item.id == id; });
                if (device == devices.end() || !inTarget(*device))
                    continue;
                const json value = rule.value("value", json(nullptr));
                policies[id] = property == "power"
                    ? (value.get<bool>() ? "ON" : "OFF")
                    : value.get<std::string>();
                policySources[id] = "custom";
            }
        };
        loadPolicyOverrides("season", seasonId);
        loadPolicyOverrides("mood", moodId);

        auto applyPresetRules = [&](const std::string &modeType,
                                    const std::string &presetId,
                                    const std::string &source)
        {
            if (presetId.empty())
                return;
            for (const DeviceRecord &device : devices)
            {
                if (!inTarget(device))
                    continue;
                const auto devicePolicy = policies.find(device.id);
                if (devicePolicy != policies.end() &&
                    (devicePolicy->second == "OFF" ||
                     devicePolicy->second == "KEEP_CURRENT"))
                    continue;
                for (const auto &rule : defaultModeRules(
                         modeType, presetId, device))
                {
                    addProposal(
                        device, rule.first, rule.second, source,
                        source == "mood"
                            ? "Suggested mood overlay on this compatible device."
                            : "Suggested seasonal baseline for this room and device type.");
                }
            }

            const auto stored = home.getModePresets(modeType);
            const auto preset = std::find_if(
                stored.begin(), stored.end(),
                [&presetId](const ModePresetRecord &item)
                { return item.presetId == presetId; });
            if (preset == stored.end())
                return;
            const json overrides = json::parse(preset->overridesJson);
            for (const json &overrideRule : overrides)
            {
                const std::string id = overrideRule.value("deviceId", "");
                const std::string property = overrideRule.value("property", "");
                if (property == "policy")
                {
                    const bool exists = std::any_of(
                        devices.begin(), devices.end(),
                        [&id](const DeviceRecord &item)
                        { return item.id == id; });
                    if (!exists)
                    {
                        const std::string ruleRoom =
                            overrideRule.value("roomName", "");
                        const bool selectedMissingDevice =
                            (roomNames.empty() && deviceIds.empty()) ||
                            (deviceIds.count(id) > 0) ||
                            (!ruleRoom.empty() && roomNames.count(ruleRoom) > 0);
                        if (selectedMissingDevice)
                        {
                            proposals[{id, property}] = {
                                {"deviceId", id},
                                {"deviceName", ""},
                                {"roomName", ruleRoom},
                                {"property", property},
                                {"previousValue", nullptr},
                                {"value", overrideRule.value("value", json(nullptr))},
                                {"source", "custom"},
                                {"status", "skipped"},
                                {"reason", "The customized preset device no longer exists."}};
                        }
                    }
                    continue;
                }
                const auto device = std::find_if(
                    devices.begin(), devices.end(),
                    [&id](const DeviceRecord &item)
                    { return item.id == id; });
                if (device == devices.end())
                {
                    const std::string ruleRoom =
                        overrideRule.value("roomName", "");
                    const bool selectedMissingDevice =
                        (roomNames.empty() && deviceIds.empty()) ||
                        (deviceIds.count(id) > 0) ||
                        (!ruleRoom.empty() && roomNames.count(ruleRoom) > 0);
                    if (!selectedMissingDevice)
                        continue;
                    proposals[{id, property}] = {
                        {"deviceId", id},
                        {"deviceName", ""},
                        {"roomName", ruleRoom},
                        {"property", property},
                        {"previousValue", nullptr},
                        {"value", overrideRule.value("value", json(nullptr))},
                        {"source", "custom"},
                        {"status", "skipped"},
                        {"reason", "The customized preset device no longer exists."}};
                    continue;
                }
                if (!inTarget(*device))
                    continue;
                addProposal(
                    *device, property,
                    overrideRule.value("value", json(nullptr)),
                    "custom", "User-customized preset value.");
            }
        };

        applyPresetRules("season", seasonId, "season");
        applyPresetRules("mood", moodId, "mood");

        json actions = json::array();
        std::set<std::string> devicesWithActions;
        for (auto &entry : proposals)
        {
            json action = entry.second;
            const std::string deviceId =
                action["deviceId"].get<std::string>();
            std::string policy = policies.count(deviceId)
                ? policies.at(deviceId)
                : "KEEP_CURRENT";
            const auto policySource = policySources.find(deviceId);
            action["policy"] = policy;
            if (!action.contains("status"))
            {
                const auto device = std::find_if(
                    devices.begin(), devices.end(),
                    [&deviceId](const DeviceRecord &item)
                    { return item.id == deviceId; });
                const std::string property =
                    action["property"].get<std::string>();
                std::string reason;
                if (device == devices.end() ||
                    !modePropertySupported(
                        *device, property,
                        action["value"], reason))
                {
                    action["status"] = "skipped";
                    action["reason"] = device == devices.end()
                        ? "The selected device no longer exists."
                        : reason;
                }
                else if (property != "power" &&
                         (policy == "KEEP_CURRENT" || policy == "OFF"))
                {
                    action["status"] = "skipped";
                    action["reason"] = policy == "OFF"
                        ? "The OFF policy does not apply additional settings."
                        : "The KEEP_CURRENT policy leaves power and settings unchanged.";
                }
                else if (action["previousValue"] == action["value"])
                {
                    action["status"] = "unchanged";
                    action["reason"] = "The device already has the proposed value.";
                }
                else
                {
                    action["status"] = "ready";
                }
            }
            devicesWithActions.insert(deviceId);
            actions.push_back(std::move(action));
        }

        for (const DeviceRecord &device : devices)
        {
            if (!inTarget(device))
                continue;
            const std::string policy = policies.count(device.id)
                ? policies.at(device.id)
                : "KEEP_CURRENT";
            if (policy == "ON" || policy == "OFF")
            {
                const auto key = std::make_pair(device.id, "power");
                if (!proposals.count(key))
                {
                    json action = {
                        {"deviceId", device.id},
                        {"deviceName", device.name},
                        {"deviceType", device.type},
                        {"roomName", device.roomName},
                        {"property", "power"},
                        {"previousValue", device.status == 1},
                        {"value", policy == "ON"},
                        {"policy", policy},
                        {"source", policySources.count(device.id)
                                       ? policySources.at(device.id)
                                       : "default"},
                        {"reason", "Explicit device power policy."}};
                    action["status"] = device.status == (policy == "ON" ? 1 : 0)
                        ? "unchanged"
                        : "ready";
                    actions.push_back(std::move(action));
                    devicesWithActions.insert(device.id);
                }
            }
            if (!devicesWithActions.count(device.id))
            {
                actions.push_back({
                    {"deviceId", device.id},
                    {"deviceName", device.name},
                    {"deviceType", device.type},
                    {"roomName", device.roomName},
                    {"property", "policy"},
                    {"previousValue", device.status == 1 ? "ON" : "OFF"},
                    {"value", policy},
                    {"policy", policy},
                    {"source", policySources.count(device.id)
                                   ? policySources.at(device.id)
                                   : "default"},
                    {"status", "unchanged"},
                    {"reason", policy == "KEEP_CURRENT"
                                   ? "This device is intentionally left unchanged."
                                   : "This policy requires no further setting changes."}});
            }
        }
        return actions;
    }

    json buildModeSnapshot(
        const json &selection,
        const std::vector<DeviceRecord> &devices)
    {
        std::set<std::string> roomNames;
        std::set<std::string> deviceIds;
        for (const json &room : selection.value("roomNames", json::array()))
            roomNames.insert(room.get<std::string>());
        for (const json &id : selection.value("deviceIds", json::array()))
            deviceIds.insert(id.get<std::string>());

        json snapshot = json::array();
        for (const DeviceRecord &device : devices)
        {
            if ((!roomNames.empty() &&
                 roomNames.count(device.roomName) == 0) ||
                (!deviceIds.empty() && deviceIds.count(device.id) == 0))
                continue;

            std::vector<std::string> properties = {"power"};
            if (device.type == "AC" || device.type == "Fan" ||
                device.type == "Heater")
                properties.push_back("setting");
            if (device.type == "Light")
            {
                if (device.supportsBrightness)
                    properties.push_back("brightness");
                if (device.supportsColor)
                    properties.push_back("color");
                if (device.supportsColorTemperature)
                    properties.push_back("colorTemperature");
            }
            for (const std::string &property : properties)
            {
                snapshot.push_back({
                    {"deviceId", device.id},
                    {"deviceName", device.name},
                    {"roomName", device.roomName},
                    {"property", property},
                    {"value", currentModeValue(device, property)}});
            }
        }
        return snapshot;
    }

    json serializeModePresetList(
        const SmartHome &home,
        const std::string &modeType)
    {
        const auto &presets =
            modeType == "season" ? seasonPresetInfo() : moodPresetInfo();
        const auto stored = home.getModePresets(modeType);
        json result = json::array();
        for (const ModePresetInfo &preset : presets)
        {
            json overrides = json::array();
            const auto found = std::find_if(
                stored.begin(), stored.end(),
                [&preset](const ModePresetRecord &item)
                { return item.presetId == preset.id; });
            if (found != stored.end())
                overrides = json::parse(found->overridesJson);
            result.push_back({
                {"id", preset.id},
                {"label", preset.label},
                {"description", preset.description},
                {"overrides", overrides}});
        }
        return result;
    }

    std::string environmentValue(const char *name)
    {
        const char *value = std::getenv(name);
        return value == nullptr ? std::string() : std::string(value);
    }

    std::string formatKwh(double value)
    {
        std::ostringstream formatted;
        formatted.precision(5);
        formatted << std::fixed << value;
        return formatted.str();
    }

    json serializeDevice(const DeviceRecord &device)
    {
        json result = {
            {"id", device.id},
            {"name", device.name},
            {"type", device.type},
            {"roomName", device.roomName},
            {"status", device.status == 1 ? "ON" : "OFF"},
            {"powerWatts", device.powerWatts},
            {"setting", device.setting},
            {"runtimeMinutes", device.runtimeMinutes},
            {"lifetime", device.lifetime},
            {"expiryTime", device.expiryTime},
            {"capabilities", json::array()}};

        if (device.type == "AC")
        {
            result["settingName"] = "temperature";
            result["settingUnit"] = "°C";
            result["settingMinimum"] = 16;
            result["settingMaximum"] = 30;
            result["capabilities"].push_back("temperature");
        }
        else if (device.type == "Heater")
        {
            result["settingName"] = "temperature";
            result["settingUnit"] = "°C";
            result["settingMinimum"] = 20;
            result["settingMaximum"] = 45;
            result["capabilities"].push_back("temperature");
        }
        else if (device.type == "Fan")
        {
            result["settingName"] = "speed";
            result["settingUnit"] = "of 5";
            result["settingMinimum"] = 1;
            result["settingMaximum"] = 5;
            result["capabilities"].push_back("speed");
        }
        else if (device.type == "Light")
        {
            result["lightType"] = device.lightType;
            if (device.supportsBrightness)
            {
                result["capabilities"].push_back("brightness");
                result["brightness"] = device.brightness;
            }
            if (device.supportsColor)
            {
                result["capabilities"].push_back("color");
                result["color"] = device.color;
            }
            if (device.supportsColorTemperature)
            {
                result["capabilities"].push_back("colorTemperature");
                result["colorTemperature"] = device.colorTemperature;
            }
        }

        return result;
    }

    json summarizeEnergyWindow(
        const std::vector<EnergySessionRecord> &sessions,
        long long startAt,
        long long endAt)
    {
        double totalKwh = 0.0;
        std::map<std::string, double> rooms;
        std::map<std::pair<std::string, std::string>, double> devices;

        for (const EnergySessionRecord &session : sessions)
        {
            const long long sessionEnd =
                session.endedAt == 0
                    ? endAt
                    : std::min(session.endedAt, endAt);
            const long long overlapStart =
                std::max(startAt, session.startedAt);
            const long long overlapEnd =
                std::min(endAt, sessionEnd);

            if (session.powerWatts <= 0 ||
                overlapEnd <= overlapStart)
            {
                continue;
            }

            const double energyKwh =
                static_cast<double>(session.powerWatts) *
                static_cast<double>(overlapEnd - overlapStart) /
                3600000.0;

            totalKwh += energyKwh;
            rooms[session.roomName] += energyKwh;
            devices[{session.deviceId, session.deviceName}] += energyKwh;
        }

        json roomBreakdown = json::array();
        for (const auto &room : rooms)
        {
            roomBreakdown.push_back({
                {"roomName", room.first},
                {"energyKwh", room.second}});
        }

        json deviceBreakdown = json::array();
        for (const auto &device : devices)
        {
            deviceBreakdown.push_back({
                {"deviceId", device.first.first},
                {"deviceName", device.first.second},
                {"energyKwh", device.second}});
        }

        return {
            {"startAt", startAt},
            {"endAt", endAt},
            {"energyKwh", totalKwh},
            {"byRoom", roomBreakdown},
            {"byAppliance", deviceBreakdown}};
    }

    bool validPreferences(const UserPreferenceRecord &preferences)
    {
        return (preferences.acTemperature == -1 ||
                (preferences.acTemperature >= 16 &&
                 preferences.acTemperature <= 30)) &&
               (preferences.heaterTemperature == -1 ||
                (preferences.heaterTemperature >= 20 &&
                 preferences.heaterTemperature <= 45)) &&
               (preferences.fanSpeed == -1 ||
                (preferences.fanSpeed >= 1 &&
                 preferences.fanSpeed <= 5)) &&
               preferences.lightOn >= -1 &&
               preferences.lightOn <= 1;
    }

    json makePreferenceActions(
        SmartHome &home,
        const std::string &roomName,
        const UserPreferenceRecord &preferences,
        bool apply)
    {
        const auto devices = home.getAllDeviceRecords();
        json actions = json::array();
        const std::string status = apply ? "applied" : "preview";

        auto addUnavailable = [&actions](
                                  const std::string &setting,
                                  const std::string &type)
        {
            actions.push_back({
                {"setting", setting},
                {"deviceType", type},
                {"status", "unsupported"},
                {"message", "No matching appliance exists in this room."}});
        };

        auto setType = [&](const std::string &setting,
                           const std::string &type,
                           int value)
        {
            if (value == -1)
                return;
            bool found = false;
            for (const DeviceRecord &device : devices)
            {
                if (device.roomName != roomName || device.type != type)
                    continue;
                found = true;
                const int previous = device.setting;
                if (!apply)
                {
                    actions.push_back({
                        {"setting", setting},
                        {"deviceId", device.id},
                        {"deviceName", device.name},
                        {"status", status},
                        {"previousValue", previous},
                        {"value", value}});
                    continue;
                }
                try
                {
                    if (!home.setDeviceSetting(device.id, value))
                        throw std::runtime_error(
                            "Could not persist the device setting.");
                    actions.push_back({
                        {"setting", setting},
                        {"deviceId", device.id},
                        {"deviceName", device.name},
                        {"status", status},
                        {"previousValue", previous},
                        {"value", value}});
                }
                catch (const std::exception &error)
                {
                    actions.push_back({
                        {"setting", setting},
                        {"deviceId", device.id},
                        {"deviceName", device.name},
                        {"status", "failed"},
                        {"message", error.what()}});
                }
            }
            if (!found)
                addUnavailable(setting, type);
        };

        setType("acTemperature", "AC", preferences.acTemperature);
        setType("heaterTemperature", "Heater", preferences.heaterTemperature);
        setType("fanSpeed", "Fan", preferences.fanSpeed);

        if (preferences.lightOn != -1)
        {
            bool foundLight = false;
            for (const DeviceRecord &device : devices)
            {
                if (device.roomName != roomName || device.type != "Light")
                    continue;
                foundLight = true;
                const int previous = device.status;
                if (!apply)
                {
                    actions.push_back({
                        {"setting", "lightOn"},
                        {"deviceId", device.id},
                        {"deviceName", device.name},
                        {"status", status},
                        {"previousValue", previous},
                        {"value", preferences.lightOn}});
                    continue;
                }
                try
                {
                    if (preferences.lightOn == 1)
                        home.turnOn(device.id);
                    else
                        home.turnOff(device.id);
                    actions.push_back({
                        {"setting", "lightOn"},
                        {"deviceId", device.id},
                        {"deviceName", device.name},
                        {"status", status},
                        {"previousValue", previous},
                        {"value", preferences.lightOn}});
                }
                catch (const std::exception &error)
                {
                    actions.push_back({
                        {"setting", "lightOn"},
                        {"deviceId", device.id},
                        {"deviceName", device.name},
                        {"status", "failed"},
                        {"message", error.what()}});
                }
            }
            if (!foundLight)
                addUnavailable("lightOn", "Light");
        }
        return actions;
    }

    std::string newAutomationId(
        RoomAutomationRegistry &registry,
        const std::string &prefix)
    {
        return prefix + "_" + std::to_string(std::time(nullptr)) + "_" +
               std::to_string(registry.nextId.fetch_add(1));
    }

#ifdef _WIN32
    std::wstring widenUtf8(const std::string &value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0)
        {
            throw std::runtime_error("AI configuration contains invalid UTF-8.");
        }

        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    class WinHttpHandle
    {
    public:
        explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
        ~WinHttpHandle()
        {
            if (handle_ != nullptr)
            {
                WinHttpCloseHandle(handle_);
            }
        }
        WinHttpHandle(const WinHttpHandle &) = delete;
        WinHttpHandle &operator=(const WinHttpHandle &) = delete;
        HINTERNET get() const { return handle_; }

    private:
        HINTERNET handle_;
    };
#endif

    bool requestAiExplanation(
        const std::string &question,
        const json &context,
        std::string &answer)
    {
#ifdef _WIN32
        const std::string apiKey = environmentValue("AI_API_KEY");
        const std::string apiUrl = environmentValue("AI_API_URL");
        const std::string model = environmentValue("AI_MODEL");
        if (apiKey.empty() || apiUrl.empty() || model.empty())
        {
            return false;
        }

        URL_COMPONENTSW components{};
        components.dwStructSize = sizeof(components);
        wchar_t host[512]{};
        wchar_t path[4096]{};
        wchar_t extra[2048]{};
        components.lpszHostName = host;
        components.dwHostNameLength = static_cast<DWORD>(std::size(host));
        components.lpszUrlPath = path;
        components.dwUrlPathLength = static_cast<DWORD>(std::size(path));
        components.lpszExtraInfo = extra;
        components.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));

        const std::wstring wideUrl = widenUtf8(apiUrl);
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components) ||
            components.nScheme != INTERNET_SCHEME_HTTPS ||
            components.dwHostNameLength == 0)
        {
            return false;
        }

        std::wstring requestPath(path, components.dwUrlPathLength);
        requestPath.append(extra, components.dwExtraInfoLength);
        if (requestPath.empty())
        {
            requestPath = L"/";
        }

        WinHttpHandle session(WinHttpOpen(
            L"SmartHomeOS/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (session.get() == nullptr ||
            !WinHttpSetTimeouts(session.get(), 3000, 3000, 5000, 12000))
        {
            return false;
        }

        WinHttpHandle connection(WinHttpConnect(
            session.get(), components.lpszHostName,
            components.nPort, 0));
        if (connection.get() == nullptr)
        {
            return false;
        }

        WinHttpHandle request(WinHttpOpenRequest(
            connection.get(), L"POST", requestPath.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE));
        if (request.get() == nullptr)
        {
            return false;
        }
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(
                request.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                &redirectPolicy, sizeof(redirectPolicy)))
        {
            return false;
        }

        const std::wstring headers =
            L"Authorization: Bearer " + widenUtf8(apiKey) +
            L"\r\nContent-Type: application/json\r\n";
        const std::string body = json({
            {"model", model},
            {"temperature", 0.2},
            {"max_tokens", 300},
            {"messages", json::array({
                {
                    {"role", "system"},
                    {"content",
                     "Explain the supplied SmartHomeOS energy context concisely. "
                     "It is estimated from rated power and recorded ON-session "
                     "durations, not measured by a meter. Use no numeric values, "
                     "percentages, costs, device-state claims, or unsupported "
                     "savings estimates. Give advisory suggestions only; never "
                     "claim to control an appliance. The UI displays authoritative "
                     "figures separately."}
                },
                {
                    {"role", "user"},
                    {"content",
                     "Question: " + question + "\nContext: " + context.dump()}
                }
            })}
        }).dump();

        if (body.size() > 8192 ||
            !WinHttpSendRequest(
                request.get(), headers.c_str(), static_cast<DWORD>(-1),
                const_cast<char *>(body.data()),
                static_cast<DWORD>(body.size()),
                static_cast<DWORD>(body.size()), 0) ||
            !WinHttpReceiveResponse(request.get(), nullptr))
        {
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(
                request.get(),
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                WINHTTP_NO_HEADER_INDEX) ||
            status < 200 || status >= 300)
        {
            return false;
        }

        std::string responseBody;
        DWORD available = 0;
        do
        {
            if (!WinHttpQueryDataAvailable(request.get(), &available))
            {
                return false;
            }
            if (available == 0)
            {
                break;
            }
            if (responseBody.size() + available > 32768)
            {
                return false;
            }
            const size_t offset = responseBody.size();
            responseBody.resize(offset + available);
            DWORD received = 0;
            if (!WinHttpReadData(
                    request.get(), responseBody.data() + offset,
                    available, &received))
            {
                return false;
            }
            responseBody.resize(offset + received);
        } while (available > 0);

        try
        {
            const json parsed = json::parse(responseBody);
            if (!parsed.contains("choices") ||
                !parsed["choices"].is_array() ||
                parsed["choices"].empty() ||
                !parsed["choices"][0].contains("message") ||
                !parsed["choices"][0]["message"].contains("content") ||
                !parsed["choices"][0]["message"]["content"].is_string())
            {
                return false;
            }

            answer = parsed["choices"][0]["message"]["content"]
                         .get<std::string>();
            if (answer.empty() || answer.size() > 4000 ||
                std::any_of(answer.begin(), answer.end(), [](unsigned char c)
                { return std::isdigit(c) != 0; }))
            {
                answer.clear();
                return false;
            }
            return true;
        }
        catch (const json::exception &)
        {
            return false;
        }
#else
        (void)question;
        (void)context;
        (void)answer;
        return false;
#endif
    }
}

int main()
{
    try
    {
        // One SmartHome instance owns the live devices and database.
        SmartHome home("My Smart Home");

        httplib::Server server;
        RoomAutomationRegistry automationRegistry;
        ModePreviewRegistry modePreviewRegistry;

        server.set_default_headers({
            {"Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS"},
            {"Access-Control-Allow-Headers", "Content-Type"}});

        server.set_pre_routing_handler(
            [](const httplib::Request &req, httplib::Response &res)
            {
                const std::string origin = req.get_header_value("Origin");
                const bool localhostOrigin =
                    origin.rfind("http://localhost:", 0) == 0;
                const bool loopbackOrigin =
                    origin.rfind("http://127.0.0.1:", 0) == 0;
                if (localhostOrigin || loopbackOrigin)
                {
                    const std::size_t separator = origin.rfind(':');
                    const std::string portText =
                        separator == std::string::npos
                            ? std::string()
                            : origin.substr(separator + 1);
                    try
                    {
                        const int port = std::stoi(portText);
                        if (port >= 5173 && port <= 5199)
                            res.set_header(
                                "Access-Control-Allow-Origin", origin);
                    }
                    catch (const std::exception &)
                    {
                    }
                }
                return httplib::Server::HandlerResponse::Unhandled;
            });

        // Handle browser CORS preflight requests.
        server.Options(R"(/api/.*)",
                       [](const httplib::Request &, httplib::Response &res)
                       {
                           res.status = 204;
                       });

        // GET /api/rooms
        server.Get("/api/rooms",
                   [&home](const httplib::Request &, httplib::Response &res)
                   {
                       json result = json::array();

                       for (const std::string &roomName : home.getRoomNames())
                       {
                           result.push_back({{"name", roomName}});
                       }

                       res.set_content(result.dump(), "application/json");
                   });

        // Create a room
        server.Post("/api/rooms", [&home](const httplib::Request &req,
                                          httplib::Response &res)
                    {
        try
        {
            json body = json::parse(req.body);

            if (!body.contains("name") || !body["name"].is_string())
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"Room name is required."})",
                    "application/json"
                );
                return;
            }

            std::string name = body["name"].get<std::string>();

            if (name.find_first_not_of(" \t\n\r") == std::string::npos)
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"Room name cannot be empty."})",
                    "application/json"
                );
                return;
            }

            if (home.roomExists(name))
            {
                res.status = 409;
                res.set_content(
                    R"({"error":"A room with this name already exists."})",
                    "application/json"
                );
                return;
            }

            if (!home.addRoom(name))
            {
                res.status = 500;
                res.set_content(
                    R"({"error":"Could not create room."})",
                    "application/json"
                );
                return;
            }

            res.status = 201;
            res.set_content(
                json({{"message", "Room created successfully."},
                      {"name", name}}).dump(),
                "application/json"
            );
        }
        catch (const std::exception&)
        {
            res.status = 400;
            res.set_content(
                R"({"error":"Invalid JSON request."})",
                "application/json"
            );
        } });

        // Rename a room
        server.Post("/api/rooms/rename",
                    [&home](const httplib::Request &req,
                            httplib::Response &res)
                    {
                        try
                        {
                            json body = json::parse(req.body);

                            if (!body.contains("oldName") ||
                                !body.contains("newName") ||
                                !body["oldName"].is_string() ||
                                !body["newName"].is_string())
                            {
                                res.status = 400;
                                res.set_content(
                                    R"({"error":"Both oldName and newName are required."})",
                                    "application/json");
                                return;
                            }

                            std::string oldName = body["oldName"].get<std::string>();
                            std::string newName = body["newName"].get<std::string>();

                            if (newName.find_first_not_of(" \t\n\r") == std::string::npos)
                            {
                                res.status = 400;
                                res.set_content(
                                    R"({"error":"New room name cannot be empty."})",
                                    "application/json");
                                return;
                            }

                            if (!home.roomExists(oldName))
                            {
                                res.status = 404;
                                res.set_content(
                                    R"({"error":"Room not found."})",
                                    "application/json");
                                return;
                            }

                            if (oldName != newName && home.roomExists(newName))
                            {
                                res.status = 409;
                                res.set_content(
                                    R"({"error":"A room with the new name already exists."})",
                                    "application/json");
                                return;
                            }

                            if (!home.renameRoom(oldName, newName))
                            {
                                res.status = 500;
                                res.set_content(
                                    R"({"error":"Could not rename room."})",
                                    "application/json");
                                return;
                            }

                            res.set_content(
                                json({{"message", "Room renamed successfully."},
                                      {"name", newName}})
                                    .dump(),
                                "application/json");
                        }
                        catch (const std::exception &)
                        {
                            res.status = 400;
                            res.set_content(
                                R"({"error":"Invalid JSON request."})",
                                "application/json");
                        }
                    });

        // Delete an empty room
        server.Post("/api/rooms/delete",
                    [&home](const httplib::Request &req,
                            httplib::Response &res)
                    {
                        try
                        {
                            json body = json::parse(req.body);

                            if (!body.contains("name") || !body["name"].is_string())
                            {
                                res.status = 400;
                                res.set_content(
                                    R"({"error":"Room name is required."})",
                                    "application/json");
                                return;
                            }

                            std::string name = body["name"].get<std::string>();

                            if (!home.roomExists(name))
                            {
                                res.status = 404;
                                res.set_content(
                                    R"({"error":"Room not found."})",
                                    "application/json");
                                return;
                            }

                            if (!home.deleteRoom(name))
                            {
                                res.status = 409;
                                res.set_content(
                                    R"({"error":"Room could not be deleted. Make sure it has no devices."})",
                                    "application/json");
                                return;
                            }

                            res.set_content(
                                json({{"message", "Room deleted successfully."}}).dump(),
                                "application/json");
                        }
                        catch (const std::exception &)
                        {
                            res.status = 400;
                            res.set_content(
                                R"({"error":"Invalid JSON request."})",
                                "application/json");
                        }
                    });

        // POST /api/devices
        // Permanent device example:
        // {
        //   "id": "AIR_PURIFIER_01",
        //   "name": "Air Purifier",
        //   "type": "Air Purifier",
        //   "roomName": "Living Room",
        //   "powerWatts": 45,
        //   "temporary": true,
        //   "lifetimeSeconds": 1800
        // }
        //
        // lifetimeSeconds = 0 means manual removal only.
        server.Post("/api/devices",
                    [&home](const httplib::Request &req, httplib::Response &res)
                    {
                        bool deviceCreated = false;
                        std::string createdDeviceId;
                        auto cleanUpCreatedDevice = [&]()
                        {
                            if (deviceCreated && home.hasDevice(createdDeviceId) &&
                                !home.removeDevice(createdDeviceId))
                            {
                                std::cerr << "Could not remove partially created device "
                                          << createdDeviceId << ".\n";
                            }
                        };
                        try
                        {
                            const json body = json::parse(req.body);

                            auto fail = [&res](int status, const std::string &message)
                            {
                                res.status = status;
                                res.set_content(
                                    json({{"error", message}}).dump(),
                                    "application/json");
                            };

                            if (!body.contains("id") || !body["id"].is_string() ||
                                !body.contains("name") || !body["name"].is_string() ||
                                !body.contains("type") || !body["type"].is_string() ||
                                !body.contains("roomName") || !body["roomName"].is_string() ||
                                !body.contains("powerWatts") || !body["powerWatts"].is_number_integer())
                            {
                                fail(400, "Required fields: id, name, type, roomName, powerWatts.");
                                return;
                            }

                            const std::string id = body["id"].get<std::string>();
                            createdDeviceId = id;
                            const std::string name = body["name"].get<std::string>();
                            const std::string type = body["type"].get<std::string>();
                            const std::string roomName = body["roomName"].get<std::string>();
                            const int powerWatts = body["powerWatts"].get<int>();

                            auto isBlank = [](const std::string &value)
                            {
                                return value.find_first_not_of(" \t\n\r") == std::string::npos;
                            };

                            if (isBlank(id) || isBlank(name) ||
                                isBlank(type) || isBlank(roomName))
                            {
                                fail(400, "ID, name, type, and room name cannot be empty.");
                                return;
                            }

                            if (id.size() > 100 || name.size() > 100 ||
                                type.size() > 60 || roomName.size() > 100)
                            {
                                fail(400, "ID, name, type, or room name exceeds its maximum length.");
                                return;
                            }

                            if (powerWatts <= 0 || powerWatts > 100000)
                            {
                                fail(400, "Power consumption must be between 1 and 100000 watts.");
                                return;
                            }

                            if (!home.roomExists(roomName))
                            {
                                fail(404, "Destination room does not exist.");
                                return;
                            }

                            if (home.hasDevice(id))
                            {
                                fail(409, "A device with this ID already exists.");
                                return;
                            }

                            bool temporary = false;
                            int lifetimeSeconds = 0;

                            if (body.contains("temporary"))
                            {
                                if (!body["temporary"].is_boolean())
                                {
                                    fail(400, "temporary must be a boolean.");
                                    return;
                                }

                                temporary = body["temporary"].get<bool>();
                            }

                            if (body.contains("lifetimeSeconds"))
                            {
                                if (!body["lifetimeSeconds"].is_number_integer())
                                {
                                    fail(400, "lifetimeSeconds must be an integer.");
                                    return;
                                }

                                lifetimeSeconds = body["lifetimeSeconds"].get<int>();
                            }

                            if (lifetimeSeconds < 0)
                            {
                                fail(400, "lifetimeSeconds cannot be negative.");
                                return;
                            }

                            if (!temporary && lifetimeSeconds != 0)
                            {
                                fail(400, "Set temporary to true to specify a lifetime.");
                                return;
                            }

                            bool initialOn = false;
                            if (body.contains("initialOn"))
                            {
                                if (!body["initialOn"].is_boolean())
                                {
                                    fail(400, "initialOn must be a boolean.");
                                    return;
                                }
                                initialOn = body["initialOn"].get<bool>();
                            }

                            int setting = -1;
                            if (body.contains("setting"))
                            {
                                if (!body["setting"].is_number_integer())
                                {
                                    fail(400, "setting must be an integer.");
                                    return;
                                }
                                setting = body["setting"].get<int>();
                            }

                            std::string lightType = "standard";
                            if (body.contains("lightType"))
                            {
                                if (!body["lightType"].is_string())
                                {
                                    fail(400, "lightType must be a string.");
                                    return;
                                }
                                lightType = body["lightType"].get<std::string>();
                            }

                            std::unique_ptr<Appliance> device;

                            if (type == "Light")
                            {
                                auto light = std::make_unique<Light>(
                                    id, name, roomName, powerWatts, lightType);
                                if (body.contains("brightness"))
                                {
                                    if (!body["brightness"].is_number_integer())
                                    {
                                        fail(400, "brightness must be an integer.");
                                        return;
                                    }
                                    light->setBrightness(body["brightness"].get<int>());
                                }
                                if (body.contains("color"))
                                {
                                    if (!body["color"].is_string())
                                    {
                                        fail(400, "color must be a hexadecimal string.");
                                        return;
                                    }
                                    light->setColor(body["color"].get<std::string>());
                                }
                                if (body.contains("colorTemperature"))
                                {
                                    if (!body["colorTemperature"].is_number_integer())
                                    {
                                        fail(400, "colorTemperature must be an integer.");
                                        return;
                                    }
                                    light->setColorTemperature(
                                        body["colorTemperature"].get<int>());
                                }
                                device = std::move(light);
                            }
                            else if (type == "Fan")
                            {
                                auto fan = std::make_unique<Fan>(
                                    id, name, roomName, powerWatts);
                                if (setting != -1)
                                    fan->setSpeed(setting);
                                device = std::move(fan);
                            }
                            else if (type == "AC")
                            {
                                auto ac = std::make_unique<AC>(
                                    id, name, roomName, powerWatts);
                                if (setting != -1)
                                    ac->setTemperature(setting);
                                device = std::move(ac);
                            }
                            else if (type == "Heater")
                            {
                                auto heater = std::make_unique<Heater>(
                                    id, name, roomName, powerWatts);
                                if (setting != -1)
                                    heater->setTemperature(setting);
                                device = std::move(heater);
                            }
                            else
                            {
                                if (setting != -1 || body.contains("lightType") ||
                                    body.contains("brightness") ||
                                    body.contains("color") ||
                                    body.contains("colorTemperature"))
                                {
                                    fail(400, "This generic appliance does not support device-specific settings.");
                                    return;
                                }
                                device = std::make_unique<GenericAppliance>(
                                    id, name, roomName, powerWatts, type);
                            }

                            // Transfer ownership before calling SmartHome.
                            // SmartHome must clean up the device if registration fails.
                            Appliance *rawDevice = device.release();

                            if (temporary)
                            {
                                home.addTemporaryDevice(rawDevice, lifetimeSeconds);
                            }
                            else
                            {
                                home.addDevice(rawDevice);
                            }
                            deviceCreated = true;
                            if (initialOn)
                                home.turnOn(id);

                            DeviceRecord created;
                            for (const DeviceRecord &record : home.getAllDeviceRecords())
                            {
                                if (record.id == id)
                                {
                                    created = record;
                                    break;
                                }
                            }

                            res.status = 201;
                            json createdResponse = serializeDevice(created);
                            createdResponse["message"] =
                                "Device created successfully.";
                            res.set_content(
                                createdResponse.dump(), "application/json");
                        }
                        catch (const json::exception &error)
                        {
                            cleanUpCreatedDevice();
                            res.status = 400;
                            res.set_content(
                                json({{"error", error.what()}}).dump(),
                                "application/json");
                        }
                        catch (const std::invalid_argument &error)
                        {
                            cleanUpCreatedDevice();
                            res.status = 400;
                            res.set_content(
                                json({{"error", error.what()}}).dump(),
                                "application/json");
                        }
                        catch (const std::exception &error)
                        {
                            cleanUpCreatedDevice();
                            res.status = 500;
                            res.set_content(
                                json({{"error", error.what()}}).dump(),
                                "application/json");
                        }
                    });

        // GET /api/devices
        server.Get("/api/devices",
                   [&home](const httplib::Request &, httplib::Response &res)
                   {
                       json result = json::array();

                       for (const DeviceRecord &device :
                            home.getAllDeviceRecords())
                       {
                           result.push_back(serializeDevice(device));
                       }

                       res.set_content(result.dump(), "application/json");
                   });

        // GET /api/devices/{id}
        server.Get(R"(/api/devices/([^/]+))",
                   [&home](const httplib::Request &req, httplib::Response &res)
                   {
                       const std::string id = req.matches[1].str();
                       const auto records = home.getAllDeviceRecords();
                       const auto it = std::find_if(
                           records.begin(), records.end(),
                           [&id](const DeviceRecord &device)
                           { return device.id == id; });
                       if (it == records.end())
                       {
                           res.status = 404;
                           res.set_content(
                               R"({"error":"Device not found."})",
                               "application/json");
                           return;
                       }
                       res.set_content(
                           serializeDevice(*it).dump(), "application/json");
                   });

        // PATCH /api/devices/{id} updates editable metadata.
        server.Patch(R"(/api/devices/([^/]+))",
                     [&home](const httplib::Request &req, httplib::Response &res)
                     {
                         auto fail = [&res](int status, const std::string &message)
                         {
                             res.status = status;
                             res.set_content(
                                 json({{"error", message}}).dump(),
                                 "application/json");
                         };
                         try
                         {
                             const std::string id = req.matches[1].str();
                             const auto records = home.getAllDeviceRecords();
                             const auto found = std::find_if(
                                 records.begin(), records.end(),
                                 [&id](const DeviceRecord &device)
                                 { return device.id == id; });
                             if (found == records.end())
                             {
                                 fail(404, "Device not found.");
                                 return;
                             }
                             const json body = json::parse(req.body);
                             if (!body.is_object() || body.empty())
                             {
                                 fail(400, "Provide name and/or powerWatts to update.");
                                 return;
                             }
                             for (auto field = body.begin(); field != body.end(); ++field)
                             {
                                 if (field.key() != "name" &&
                                     field.key() != "powerWatts")
                                 {
                                     fail(400, "Only name and powerWatts can be edited.");
                                     return;
                                 }
                             }

                             std::string name = found->name;
                             int powerWatts = found->powerWatts;
                             if (body.contains("name"))
                             {
                                 if (!body["name"].is_string())
                                 {
                                     fail(400, "name must be a string.");
                                     return;
                                 }
                                 name = body["name"].get<std::string>();
                             }
                             if (body.contains("powerWatts"))
                             {
                                 if (!body["powerWatts"].is_number_integer())
                                 {
                                     fail(400, "powerWatts must be an integer.");
                                     return;
                                 }
                                 powerWatts = body["powerWatts"].get<int>();
                             }
                             if (!home.updateDeviceDetails(id, name, powerWatts))
                             {
                                 fail(500, "Device details were not persisted.");
                                 return;
                             }
                             const auto updatedRecords = home.getAllDeviceRecords();
                             const auto updated = std::find_if(
                                 updatedRecords.begin(), updatedRecords.end(),
                                 [&id](const DeviceRecord &device)
                                 { return device.id == id; });
                             if (updated == updatedRecords.end())
                             {
                                 fail(404, "Device no longer exists.");
                                 return;
                             }
                             res.set_content(
                                 serializeDevice(*updated).dump(),
                                 "application/json");
                         }
                         catch (const json::parse_error &)
                         {
                             fail(400, "Request body must be valid JSON.");
                         }
                         catch (const std::invalid_argument &error)
                         {
                             fail(400, error.what());
                         }
                         catch (const std::exception &error)
                         {
                             fail(500, error.what());
                         }
                     });

        // PATCH /api/devices/{id}/settings
        // Each request changes one supported property, preserving the rest.
        server.Patch(R"(/api/devices/([^/]+)/settings)",
                     [&home](const httplib::Request &req, httplib::Response &res)
                     {
                         auto fail = [&res](int status, const std::string &message)
                         {
                             res.status = status;
                             res.set_content(
                                 json({{"error", message}}).dump(),
                                 "application/json");
                         };
                         try
                         {
                             const std::string id = req.matches[1].str();
                             const auto records = home.getAllDeviceRecords();
                             const auto found = std::find_if(
                                 records.begin(), records.end(),
                                 [&id](const DeviceRecord &device)
                                 { return device.id == id; });
                             if (found == records.end())
                             {
                                 fail(404, "Device not found.");
                                 return;
                             }

                             const json body = json::parse(req.body);
                             if (!body.is_object() || body.size() != 1)
                             {
                                 fail(400, "Provide exactly one setting to update.");
                                 return;
                             }

                             const std::string key = body.begin().key();
                             bool persisted = false;
                             if (key == "setting")
                             {
                                 if (!body[key].is_number_integer())
                                 {
                                     fail(400, "setting must be an integer.");
                                     return;
                                 }
                                 if (found->type != "AC" &&
                                     found->type != "Fan" &&
                                     found->type != "Heater")
                                 {
                                     fail(400, "This appliance does not support this setting.");
                                     return;
                                 }
                                 persisted = home.setDeviceSetting(
                                     id, body[key].get<int>());
                             }
                             else if (key == "brightness")
                             {
                                 if (!body[key].is_number_integer())
                                 {
                                     fail(400, "brightness must be an integer.");
                                     return;
                                 }
                                 persisted = home.setLightBrightness(
                                     id, body[key].get<int>());
                             }
                             else if (key == "color")
                             {
                                 if (!body[key].is_string())
                                 {
                                     fail(400, "color must be a hexadecimal string.");
                                     return;
                                 }
                                 persisted = home.setLightColor(
                                     id, body[key].get<std::string>());
                             }
                             else if (key == "colorTemperature")
                             {
                                 if (!body[key].is_number_integer())
                                 {
                                     fail(400, "colorTemperature must be an integer.");
                                     return;
                                 }
                                 persisted = home.setLightColorTemperature(
                                     id, body[key].get<int>());
                             }
                             else
                             {
                                 fail(400, "Unsupported setting name.");
                                 return;
                             }

                             if (!persisted)
                             {
                                 fail(500, "The setting could not be persisted.");
                                 return;
                             }

                             const auto updatedRecords =
                                 home.getAllDeviceRecords();
                             const auto updated = std::find_if(
                                 updatedRecords.begin(), updatedRecords.end(),
                                 [&id](const DeviceRecord &device)
                                 { return device.id == id; });
                             if (updated == updatedRecords.end())
                             {
                                 fail(404, "Device no longer exists.");
                                 return;
                             }
                             res.set_content(
                                 serializeDevice(*updated).dump(),
                                 "application/json");
                         }
                         catch (const json::parse_error &)
                         {
                             fail(400, "Request body must be valid JSON.");
                         }
                         catch (const std::invalid_argument &error)
                         {
                             fail(400, error.what());
                         }
                         catch (const std::exception &error)
                         {
                             fail(500, error.what());
                         }
                     });

        // DELETE /api/devices/{id}
        server.Delete(R"(/api/devices/([^/]+))",
                      [&home](const httplib::Request &req, httplib::Response &res)
                      {
                          const std::string deviceId = req.matches[1].str();

                          if (!home.hasDevice(deviceId))
                          {
                              res.status = 404;
                              res.set_content(
                                  R"({"error":"Device not found."})",
                                  "application/json");
                              return;
                          }

                          if (!home.removeDevice(deviceId))
                          {
                              res.status = 500;
                              res.set_content(
                                  R"({"error":"Could not remove device."})",
                                  "application/json");
                              return;
                          }

                          res.set_content(
                              json({{"message", "Device removed successfully."},
                                    {"id", deviceId}})
                                  .dump(),
                              "application/json");
                      });

        // POST /api/devices/{id}/power
        // Request JSON: {"on": true} or {"on": false}
        server.Post(R"(/api/devices/([^/]+)/power)",
                    [&home](const httplib::Request &req, httplib::Response &res)
                    {
                        try
                        {
                            const std::string deviceId = req.matches[1].str();

                            if (!home.hasDevice(deviceId))
                            {
                                res.status = 404;
                                res.set_content(
                                    R"({"error":"Device not found"})",
                                    "application/json");
                                return;
                            }

                            const json body = json::parse(req.body);

                            if (!body.contains("on") ||
                                !body["on"].is_boolean())
                            {
                                res.status = 400;
                                res.set_content(
                                    R"({"error":"Expected a boolean field named on"})",
                                    "application/json");
                                return;
                            }

                            if (body["on"].get<bool>())
                            {
                                home.turnOn(deviceId);
                            }
                            else
                            {
                                home.turnOff(deviceId);
                            }

                            const auto records = home.getAllDeviceRecords();

                            const auto it = std::find_if(
                                records.begin(),
                                records.end(),
                                [&deviceId](const DeviceRecord &device)
                                {
                                    return device.id == deviceId;
                                });

                            if (it == records.end())
                            {
                                res.status = 404;
                                res.set_content(
                                    R"({"error":"Device no longer exists"})",
                                    "application/json");
                                return;
                            }

                            res.set_content(
                                serializeDevice(*it).dump(),
                                "application/json");
                        }
                        catch (const json::exception &error)
                        {
                            res.status = 400;
                            res.set_content(
                                json({{"error", error.what()}}).dump(),
                                "application/json");
                        }
                        catch (const std::exception &error)
                        {
                            res.status = 500;
                            res.set_content(
                                json({{"error", error.what()}}).dump(),
                                "application/json");
                        }
                    });

        // GET /api/energy/sessions
        // Returns saved and currently active energy sessions.
        server.Get("/api/energy/sessions",
                   [&home](const httplib::Request &, httplib::Response &res)
                   {
                       try
                       {
                           json result = json::array();

                           for (const EnergySessionRecord &session :
                                home.getEnergySessions())
                           {
                               result.push_back({{"id", session.id},
                                                 {"deviceId", session.deviceId},
                                                 {"deviceName", session.deviceName},
                                                 {"roomName", session.roomName},
                                                 {"powerWatts", session.powerWatts},
                                                 {"startedAt", session.startedAt},
                                                 {"endedAt", session.endedAt},
                                                 {"active", session.endedAt == 0},
                                                 {"energyKwh", session.energyKwh}});
                           }

                           res.set_content(result.dump(), "application/json");
                       }
                       catch (const std::exception &error)
                       {
                           res.status = 500;
                           res.set_content(
                               json({{"error", error.what()}}).dump(),
                               "application/json");
                       }
                   });

        // Trailing windows use rated watts and session durations, not meter readings.
        server.Get("/api/energy/summary",
                   [&home](const httplib::Request &, httplib::Response &res)
                   {
                       try
                       {
                           const long long now =
                               static_cast<long long>(std::time(nullptr));
                           const long long daySeconds = 24 * 60 * 60;
                           const std::vector<EnergySessionRecord> sessions =
                               home.getEnergySessions();

                           auto makeWindow = [&sessions, now](
                                                 long long duration)
                           {
                               json current = summarizeEnergyWindow(
                                   sessions, now - duration, now);
                               const json previous = summarizeEnergyWindow(
                                   sessions, now - 2 * duration,
                                   now - duration);

                               current["previousEnergyKwh"] =
                                   previous["energyKwh"];
                               current["changeKwh"] =
                                   current["energyKwh"].get<double>() -
                                   previous["energyKwh"].get<double>();
                               return current;
                           };

                           res.set_content(
                               json({
                                   {"asOf", now},
                                   {"unit", "kWh"},
                                   {"estimated", true},
                                   {"windows", {
                                       {"last24Hours", makeWindow(daySeconds)},
                                       {"last7Days", makeWindow(7 * daySeconds)},
                                       {"last30Days", makeWindow(30 * daySeconds)}}}})
                                   .dump(),
                               "application/json");
                       }
                       catch (const std::exception &error)
                       {
                           res.status = 500;
                           res.set_content(
                               json({{"error", error.what()}}).dump(),
                               "application/json");
                       }
                   });

        // Ask an optional cloud model to explain bounded, calculated energy context.
        server.Post("/api/assistant/chat",
                    [&home](const httplib::Request &req, httplib::Response &res)
                    {
                        try
                        {
                            if (req.body.size() > 4096)
                            {
                                res.status = 413;
                                res.set_content(
                                    R"({"error":"Request body exceeds 4096 bytes."})",
                                    "application/json");
                                return;
                            }

                            const json body = json::parse(req.body);
                            if (!body.contains("question") ||
                                !body["question"].is_string())
                            {
                                res.status = 400;
                                res.set_content(
                                    R"({"error":"question must be a string."})",
                                    "application/json");
                                return;
                            }

                            const std::string question =
                                body["question"].get<std::string>();
                            const auto first = question.find_first_not_of(" \t\r\n");
                            if (first == std::string::npos ||
                                question.size() > 1000)
                            {
                                res.status = 400;
                                res.set_content(
                                    R"({"error":"question must contain 1 to 1000 characters."})",
                                    "application/json");
                                return;
                            }

                            const long long now =
                                static_cast<long long>(std::time(nullptr));
                            const std::vector<EnergySessionRecord> sessions =
                                home.getEnergySessions();
                            const long long daySeconds = 24 * 60 * 60;
                            auto makeWindow = [&sessions, now](
                                                  long long duration)
                            {
                                json current = summarizeEnergyWindow(
                                    sessions, now - duration, now);
                                const json previous = summarizeEnergyWindow(
                                    sessions, now - 2 * duration,
                                    now - duration);
                                current["previousEnergyKwh"] =
                                    previous["energyKwh"];
                                current["changeKwh"] =
                                    current["energyKwh"].get<double>() -
                                    previous["energyKwh"].get<double>();
                                return current;
                            };
                            json windows = {
                                {"last24Hours", makeWindow(daySeconds)},
                                {"last7Days", makeWindow(7 * daySeconds)},
                                {"last30Days", makeWindow(30 * daySeconds)}};

                            const json &daily = windows["last24Hours"];
                            json context = {
                                {"unit", "kWh"},
                                {"estimated", true},
                                {"windows", {
                                    {"last24Hours", daily["energyKwh"]},
                                    {"last7Days", windows["last7Days"]["energyKwh"]},
                                    {"last30Days", windows["last30Days"]["energyKwh"]}}},
                                {"previousWindows", {
                                    {"last24Hours",
                                     windows["last24Hours"]["previousEnergyKwh"]},
                                    {"last7Days",
                                     windows["last7Days"]["previousEnergyKwh"]},
                                    {"last30Days",
                                     windows["last30Days"]["previousEnergyKwh"]}}},
                                {"changes", {
                                    {"last24Hours",
                                     windows["last24Hours"]["changeKwh"]},
                                    {"last7Days",
                                     windows["last7Days"]["changeKwh"]},
                                    {"last30Days",
                                     windows["last30Days"]["changeKwh"]}}},
                                {"topApplianceLast24Hours", nullptr},
                                {"topRoomLast24Hours", nullptr}};

                            auto addLargest = [&context](
                                                  const json &items,
                                                  const char *key)
                            {
                                const json *largest = nullptr;
                                for (const json &item : items)
                                {
                                    if (largest == nullptr ||
                                        item["energyKwh"].get<double>() >
                                            (*largest)["energyKwh"].get<double>())
                                    {
                                        largest = &item;
                                    }
                                }
                                if (largest != nullptr)
                                {
                                    context[key] = *largest;
                                }
                            };
                            addLargest(daily["byAppliance"],
                                       "topApplianceLast24Hours");
                            addLargest(daily["byRoom"], "topRoomLast24Hours");

                            std::string answer;
                            const bool cloudConfigured =
                                !environmentValue("AI_API_KEY").empty() &&
                                !environmentValue("AI_API_URL").empty() &&
                                !environmentValue("AI_MODEL").empty();
                            const bool usedCloud =
                                requestAiExplanation(question, context, answer);
                            if (!usedCloud)
                            {
                                answer =
                                    "Recorded ON sessions estimate " +
                                    formatKwh(windows["last24Hours"]["energyKwh"]
                                                  .get<double>()) +
                                    " kWh over the last 24 hours, " +
                                    formatKwh(windows["last7Days"]["energyKwh"]
                                                  .get<double>()) +
                                    " kWh over the last 7 days, and " +
                                    formatKwh(windows["last30Days"]["energyKwh"]
                                                  .get<double>()) +
                                    " kWh over the last 30 days. These are "
                                    "estimates from device rated power and "
                                    "recorded ON-session durations, not meter readings.";
                                if (!context["topApplianceLast24Hours"].is_null())
                                {
                                    const json &top =
                                        context["topApplianceLast24Hours"];
                                    answer += " The largest estimated appliance "
                                              "usage in the last 24 hours is " +
                                              top["deviceName"].get<std::string>() +
                                              " at " +
                                              formatKwh(top["energyKwh"]
                                                            .get<double>()) +
                                              " kWh.";
                                }
                                const double previousWeek =
                                    windows["last7Days"]["previousEnergyKwh"]
                                        .get<double>();
                                if (previousWeek > 0.0)
                                {
                                    const double weeklyChange =
                                        windows["last7Days"]["changeKwh"]
                                            .get<double>();
                                    answer += " The trailing seven-day estimate is " +
                                              std::string(weeklyChange >= 0.0
                                                              ? "up by "
                                                              : "down by ") +
                                              formatKwh(std::abs(weeklyChange)) +
                                              " kWh compared with the previous "
                                              "seven-day window.";
                                }
                            }

                            json recommendations = json::array();
                            if (!context["topApplianceLast24Hours"].is_null())
                            {
                                const json &top =
                                    context["topApplianceLast24Hours"];
                                recommendations.push_back({
                                    {"title", "Review the highest estimated consumer"},
                                    {"explanation",
                                     "Check whether its recent operating schedule "
                                     "matches your needs."},
                                    {"observation",
                                     top["deviceName"].get<std::string>() +
                                         " accounts for an estimated " +
                                         formatKwh(top["energyKwh"].get<double>()) +
                                         " kWh in the last 24 hours."},
                                    {"recommendedAction",
                                     "Review the appliance schedule manually."},
                                    {"estimatedBenefit", nullptr},
                                    {"assumptions",
                                     "Usage is estimated from rated watts and "
                                     "recorded session duration."},
                                    {"confidence", "Estimated usage history"},
                                    {"requiresUserAction", true}});
                            }

                            res.set_content(
                                json({
                                    {"answer", answer},
                                    {"source", usedCloud ? "cloud" : "rule_based"},
                                    {"notice", usedCloud
                                                   ? ""
                                                   : (cloudConfigured
                                                          ? "Cloud AI is unavailable or returned an invalid response; showing a rule-based summary."
                                                          : "Cloud AI is not configured; showing a rule-based summary.")},
                                    {"estimated", true},
                                    {"unit", "kWh"},
                                    {"windows", windows},
                                    {"limitations", json::array({
                                        "Energy is estimated from rated watts and recorded ON-session durations, not meter readings.",
                                        "Tariff settings are held in the frontend and are not available to this assistant; it cannot calculate a bill.",
                                        "Saved profile preferences are not included in assistant context."})},
                                    {"recommendations", recommendations}})
                                    .dump(),
                                "application/json");
                        }
                        catch (const json::parse_error &)
                        {
                            res.status = 400;
                            res.set_content(
                                R"({"error":"Request body must be valid JSON."})",
                                "application/json");
                        }
                        catch (const std::exception &error)
                        {
                            res.status = 500;
                            res.set_content(
                                json({{"error", error.what()}}).dump(),
                                "application/json");
                        }
                    });

        // GET /api/profiles
        // Return all saved user profiles.
        server.Get("/api/profiles",
                   [&home](const httplib::Request &,
                           httplib::Response &res)
                   {
                       json result = json::array();

                       for (const UserProfile &profile :
                            home.getUserProfiles())
                       {
                           result.push_back({{"id", profile.id},
                                             {"displayName", profile.displayName}});
                       }

                       res.set_content(
                           result.dump(),
                           "application/json");
                   });

        // POST /api/profiles
        // Request: {"id":"user_001","displayName":"Tamanna"}
        server.Post("/api/profiles",
                    [&home](const httplib::Request &req,
                            httplib::Response &res)
                    {
                        try
                        {
                            const json body = json::parse(req.body);

                            if (!body.contains("id") ||
                                !body["id"].is_string() ||
                                !body.contains("displayName") ||
                                !body["displayName"].is_string())
                            {
                                res.status = 400;
                                res.set_content(
                                    R"({"error":"id and displayName are required strings."})",
                                    "application/json");
                                return;
                            }

                            UserProfile profile;
                            profile.id = body["id"].get<std::string>();
                            profile.displayName =
                                body["displayName"].get<std::string>();

                            auto isBlank = [](const std::string &value)
                            {
                                return value.find_first_not_of(
                                           " \t\n\r") == std::string::npos;
                            };

                            if (isBlank(profile.id) ||
                                isBlank(profile.displayName))
                            {
                                res.status = 400;
                                res.set_content(
                                    R"({"error":"Profile ID and display name cannot be empty."})",
                                    "application/json");
                                return;
                            }

                            const auto profiles = home.getUserProfiles();
                            const auto existing = std::find_if(
                                profiles.begin(),
                                profiles.end(),
                                [&profile](const UserProfile &item)
                                {
                                    return item.id == profile.id;
                                });

                            if (existing != profiles.end())
                            {
                                res.status = 409;
                                res.set_content(
                                    R"({"error":"A profile with this ID already exists."})",
                                    "application/json");
                                return;
                            }

                            if (!home.createUserProfile(profile))
                            {
                                res.status = 500;
                                res.set_content(
                                    R"({"error":"Could not create profile."})",
                                    "application/json");
                                return;
                            }

                            res.status = 201;
                            res.set_content(
                                json({{"message", "Profile created successfully."},
                                      {"id", profile.id},
                                      {"displayName", profile.displayName}})
                                    .dump(),
                                "application/json");
                        }
                        catch (const json::exception &error)
                        {
                            res.status = 400;
                            res.set_content(
                                json({{"error", error.what()}}).dump(),
                                "application/json");
                        }
                    });

        // GET /api/profiles/{id}/preferences?roomName=Bedroom
        server.Get(
            R"(/api/profiles/([^/]+)/preferences)",
            [&home](const httplib::Request &req,
                    httplib::Response &res)
            {
                const std::string userId = req.matches[1].str();

                if (!req.has_param("roomName"))
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"roomName query parameter is required."})",
                        "application/json");
                    return;
                }

                const std::string roomName =
                    req.get_param_value("roomName");

                if (!home.roomExists(roomName))
                {
                    res.status = 404;
                    res.set_content(
                        R"({"error":"Room not found."})",
                        "application/json");
                    return;
                }

                const auto profiles = home.getUserProfiles();
                const auto profile = std::find_if(
                    profiles.begin(),
                    profiles.end(),
                    [&userId](const UserProfile &item)
                    {
                        return item.id == userId;
                    });

                if (profile == profiles.end())
                {
                    res.status = 404;
                    res.set_content(
                        R"({"error":"Profile not found."})",
                        "application/json");
                    return;
                }

                UserPreferenceRecord preferences;

                if (!home.getUserPreferences(
                        userId, roomName, preferences))
                {
                    res.status = 404;
                    res.set_content(
                        R"({"error":"No saved preferences found for this profile and room."})",
                        "application/json");
                    return;
                }

                const bool validPreferences =
                    (preferences.acTemperature == -1 ||
                     (preferences.acTemperature >= 16 &&
                      preferences.acTemperature <= 30)) &&
                    (preferences.heaterTemperature == -1 ||
                     (preferences.heaterTemperature >= 20 &&
                      preferences.heaterTemperature <= 45)) &&
                    (preferences.fanSpeed == -1 ||
                     (preferences.fanSpeed >= 1 &&
                      preferences.fanSpeed <= 5)) &&
                    preferences.lightOn >= -1 &&
                    preferences.lightOn <= 1;

                if (!validPreferences)
                {
                    res.status = 500;
                    res.set_content(
                        R"({"error":"Stored preference values are outside supported appliance limits."})",
                        "application/json");
                    return;
                }

                res.set_content(
                    json({{"userId", preferences.userId},
                          {"roomName", preferences.roomName},
                          {"acTemperature", preferences.acTemperature},
                          {"heaterTemperature", preferences.heaterTemperature},
                          {"fanSpeed", preferences.fanSpeed},
                          {"lightOn", preferences.lightOn},
                          {"automaticApplyEnabled", preferences.automaticApplyEnabled},
                          {"firstApplicationConfirmed", preferences.firstApplicationConfirmed}})
                        .dump(),
                    "application/json");
            });

        // PUT /api/profiles/{id}/preferences
        // Request:
        // {
        //   "roomName": "Bedroom",
        //   "acTemperature": 24,
        //   "heaterTemperature": 22,
        //   "fanSpeed": 3,
        //   "lightOn": 1
        // }
        server.Put(
            R"(/api/profiles/([^/]+)/preferences)",
            [&home](const httplib::Request &req,
                    httplib::Response &res)
            {
                try
                {
                    const std::string userId =
                        req.matches[1].str();

                    const json body = json::parse(req.body);

                    if (!body.contains("roomName") ||
                        !body["roomName"].is_string())
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"roomName is required."})",
                            "application/json");
                        return;
                    }

                    UserPreferenceRecord preferences;
                    preferences.userId = userId;
                    preferences.roomName =
                        body["roomName"].get<std::string>();

                    auto readInteger = [&body](
                                           const char *key, int &target) -> bool
                    {
                        if (!body.contains(key))
                            return true;

                        if (!body[key].is_number_integer())
                            return false;

                        target = body[key].get<int>();
                        return true;
                    };

                    if (!readInteger("acTemperature",
                                     preferences.acTemperature) ||
                        !readInteger("heaterTemperature",
                                     preferences.heaterTemperature) ||
                        !readInteger("fanSpeed",
                                     preferences.fanSpeed) ||
                        !readInteger("lightOn",
                                     preferences.lightOn))
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"Preference values must be integers."})",
                            "application/json");
                        return;
                    }
                    if (body.contains("automaticApplyEnabled"))
                    {
                        if (!body["automaticApplyEnabled"].is_boolean())
                        {
                            res.status = 400;
                            res.set_content(
                                R"({"error":"automaticApplyEnabled must be a boolean."})",
                                "application/json");
                            return;
                        }
                        preferences.automaticApplyEnabled =
                            body["automaticApplyEnabled"].get<bool>();
                    }

                    if (preferences.roomName.empty() ||
                        (preferences.acTemperature != -1 &&
                         (preferences.acTemperature < 16 ||
                          preferences.acTemperature > 30)) ||
                        (preferences.heaterTemperature != -1 &&
                         (preferences.heaterTemperature < 20 ||
                          preferences.heaterTemperature > 45)) ||
                        (preferences.fanSpeed != -1 &&
                         (preferences.fanSpeed < 1 ||
                          preferences.fanSpeed > 5)) ||
                        preferences.lightOn < -1 ||
                        preferences.lightOn > 1)
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"AC temperature must be 16-30 C, heater temperature 20-45 C, fan speed 1-5, and light preference -1, 0, or 1."})",
                            "application/json");
                        return;
                    }

                    if (!home.roomExists(preferences.roomName))
                    {
                        res.status = 404;
                        res.set_content(
                            R"({"error":"Room not found."})",
                            "application/json");
                        return;
                    }

                    const auto profiles = home.getUserProfiles();
                    const auto profile = std::find_if(
                        profiles.begin(),
                        profiles.end(),
                        [&userId](const UserProfile &item)
                        {
                            return item.id == userId;
                        });

                    if (profile == profiles.end())
                    {
                        res.status = 404;
                        res.set_content(
                            R"({"error":"Profile not found."})",
                            "application/json");
                        return;
                    }

                    if (!home.saveUserPreferences(preferences))
                    {
                        res.status = 500;
                        res.set_content(
                            R"({"error":"Could not save preferences."})",
                            "application/json");
                        return;
                    }

                    UserPreferenceRecord savedPreferences;
                    if (!home.getUserPreferences(
                            userId, preferences.roomName, savedPreferences))
                    {
                        res.status = 500;
                        res.set_content(
                            R"({"error":"Preferences were saved but could not be reloaded."})",
                            "application/json");
                        return;
                    }

                    res.set_content(
                        json({{"message", "Preferences saved successfully."},
                              {"userId", preferences.userId},
                              {"roomName", preferences.roomName},
                              {"acTemperature", preferences.acTemperature},
                              {"heaterTemperature", preferences.heaterTemperature},
                              {"fanSpeed", preferences.fanSpeed},
                              {"lightOn", preferences.lightOn},
                              {"automaticApplyEnabled", savedPreferences.automaticApplyEnabled},
                              {"firstApplicationConfirmed", savedPreferences.firstApplicationConfirmed}})
                            .dump(),
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        // Apply only persisted preferences; the frontend asks for confirmation first.
        server.Post(
            R"(/api/profiles/([^/]+)/preferences/apply)",
            [&home, &automationRegistry](const httplib::Request &req,
            httplib::Response &res)
            {
                try
                {
            const std::string userId = req.matches[1].str();
            const json body = json::parse(req.body);

            if (!body.contains("roomName") ||
                !body["roomName"].is_string() ||
                body["roomName"].get<std::string>().empty())
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"roomName is required."})",
                    "application/json");
                return;
            }

            const std::string roomName =
                body["roomName"].get<std::string>();

            const bool automatic =
                body.contains("automatic") && body["automatic"].is_boolean()
                    ? body["automatic"].get<bool>()
                    : false;
            if (body.contains("automatic") &&
                !body["automatic"].is_boolean())
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"automatic must be a boolean."})",
                    "application/json");
                return;
            }

            if (!home.roomExists(roomName))
            {
                res.status = 404;
                res.set_content(
                    R"({"error":"Room not found."})",
                    "application/json");
                return;
            }

            UserPreferenceRecord preferences;
            if (!home.getUserPreferences(
                    userId, roomName, preferences))
            {
                res.status = 404;
                res.set_content(
                    R"({"error":"No saved preferences found for this profile and room."})",
                    "application/json");
                return;
            }

            const bool validPreferences =
                (preferences.acTemperature == -1 ||
                 (preferences.acTemperature >= 16 &&
                  preferences.acTemperature <= 30)) &&
                (preferences.heaterTemperature == -1 ||
                 (preferences.heaterTemperature >= 20 &&
                  preferences.heaterTemperature <= 45)) &&
                (preferences.fanSpeed == -1 ||
                 (preferences.fanSpeed >= 1 &&
                  preferences.fanSpeed <= 5)) &&
                preferences.lightOn >= -1 &&
                preferences.lightOn <= 1;

            if (!validPreferences)
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"Saved preference values are invalid and cannot be applied."})",
                    "application/json");
                return;
            }

            if (automatic &&
                (!preferences.automaticApplyEnabled ||
                 !preferences.firstApplicationConfirmed))
            {
                res.status = 403;
                res.set_content(
                    R"({"error":"Automatic application is not enabled and confirmed for this profile and room."})",
                    "application/json");
                return;
            }
            if (automatic && !body.contains("automationSessionId"))
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"Automatic application requires a current room recognition session."})",
                    "application/json");
                return;
            }
            const bool needsFirstApplicationConfirmation =
                preferences.automaticApplyEnabled &&
                !preferences.firstApplicationConfirmed;
            if (needsFirstApplicationConfirmation &&
                (!body.contains("confirmed") ||
                 !body["confirmed"].is_boolean() ||
                 !body["confirmed"].get<bool>()))
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"Explicit confirmation is required for the first automatic-application setup."})",
                    "application/json");
                return;
            }

            if (preferences.acTemperature == -1 &&
                preferences.heaterTemperature == -1 &&
                preferences.fanSpeed == -1 &&
                preferences.lightOn == -1)
            {
                res.status = 400;
                res.set_content(
                    R"({"error":"There are no specified preferences to apply."})",
                    "application/json");
                return;
            }

            std::string automationEnrollmentId;
            std::unique_lock<std::mutex> automationSessionLock;
            if (body.contains("automationSessionId"))
            {
                if (!body.contains("confirmed") ||
                    !body["confirmed"].is_boolean() ||
                    !body["confirmed"].get<bool>() ||
                    !body["automationSessionId"].is_string())
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"Explicit confirmation and a valid automation session are required."})",
                        "application/json");
                    return;
                }
                const std::string sessionId =
                    body["automationSessionId"].get<std::string>();
                automationSessionLock =
                    std::unique_lock<std::mutex>(automationRegistry.mutex);
                auto session = automationRegistry.sessions.find(sessionId);
                if (!home.getAutomationEnabled() ||
                    session == automationRegistry.sessions.end() ||
                    automationRegistry.activeSessionId != sessionId ||
                    session->second.roomName != roomName ||
                    session->second.profileId != userId ||
                    session->second.enrollmentId.empty() ||
                    session->second.observationStatus != "matched" ||
                    session->second.observedEnrollmentId !=
                        session->second.enrollmentId ||
                    std::time(nullptr) - session->second.observedAt > 8)
                {
                    res.status = 409;
                    res.set_content(
                        R"({"error":"The confirmed identity or selected room no longer matches the active automation session."})",
                        "application/json");
                    return;
                }
                const UserPreferenceRecord &previewed =
                    session->second.preferences;
                if (previewed.acTemperature != preferences.acTemperature ||
                    previewed.heaterTemperature != preferences.heaterTemperature ||
                    previewed.fanSpeed != preferences.fanSpeed ||
                    previewed.lightOn != preferences.lightOn)
                {
                    res.status = 409;
                    res.set_content(
                        R"({"error":"Saved preferences changed after preview. Resolve the identity again before applying."})",
                        "application/json");
                    return;
                }
                automationEnrollmentId = session->second.enrollmentId;
                if (!session->second.appliedEnrollments
                         .insert(automationEnrollmentId).second)
                {
                    res.status = 409;
                    res.set_content(
                        R"({"error":"Preferences for this identity were already applied in this session."})",
                        "application/json");
                    return;
                }
            }

            const json actions = makePreferenceActions(
                home, roomName, preferences, true);

            const bool allApplied = std::all_of(
                actions.begin(),
                actions.end(),
                [](const json &action)
                {
                    return action.value("status", "") == "applied";
                });

            if (allApplied &&
                needsFirstApplicationConfirmation &&
                !home.confirmUserPreferenceAutomation(userId, roomName))
            {
                res.status = 500;
                res.set_content(
                    json({
                        {"error", "Devices were applied, but the first-application confirmation could not be persisted."},
                        {"success", true},
                        {"actions", actions},
                        {"eventPersisted", false}}).dump(),
                    "application/json");
                return;
            }

            if (!automationEnrollmentId.empty())
            {
                json changed = json::array();
                for (const json &action : actions)
                {
                    if (action.value("status", "") == "applied")
                        changed.push_back(action);
                }
                const AutomationEventRecord event{
                    0,
                    roomName,
                    userId,
                    static_cast<long long>(std::time(nullptr)),
                    allApplied ? "applied" :
                        (changed.empty() ? "failed" : "partial"),
                    changed.dump()};
                if (!home.recordAutomationEvent(event))
                {
                    res.status = 500;
                    res.set_content(
                        json({
                            {"error", "Device settings were processed, but the automation event could not be persisted."},
                            {"success", allApplied},
                            {"actions", actions},
                            {"eventPersisted", false}})
                            .dump(),
                        "application/json");
                    return;
                }
            }

            json response = {
                {"success", allApplied},
                {"actions", actions}};
            if (!automationEnrollmentId.empty())
            {
                response["eventPersisted"] = true;
                response["eventTime"] =
                    static_cast<long long>(std::time(nullptr));
            }
            res.set_content(response.dump(), "application/json");
                }
                catch (const json::exception &error)
                {
            res.status = 400;
            res.set_content(
                json({{"error", error.what()}}).dump(),
                "application/json");
                }
            });

        server.Get(
            "/api/automation/status",
            [&home, &automationRegistry](
                const httplib::Request &, httplib::Response &res)
            {
                std::lock_guard<std::mutex> lock(automationRegistry.mutex);
                json active = nullptr;
                if (!automationRegistry.activeSessionId.empty())
                {
                    const auto session = automationRegistry.sessions.find(
                        automationRegistry.activeSessionId);
                    if (session != automationRegistry.sessions.end())
                    {
                        active = {
                            {"sessionId", automationRegistry.activeSessionId},
                            {"roomName", session->second.roomName}};
                    }
                }
                res.set_content(
                    json({
                        {"enabled", home.getAutomationEnabled()},
                        {"activeSession", active}})
                        .dump(),
                    "application/json");
            });

        server.Put(
            "/api/automation/settings",
            [&home, &automationRegistry](
                const httplib::Request &req, httplib::Response &res)
            {
                try
                {
                    const json body = json::parse(req.body);
                    if (!body.contains("enabled") ||
                        !body["enabled"].is_boolean())
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"enabled must be a boolean."})",
                            "application/json");
                        return;
                    }
                    const bool enabled = body["enabled"].get<bool>();
                    std::lock_guard<std::mutex> lock(
                        automationRegistry.mutex);
                    if (!home.setAutomationEnabled(enabled))
                    {
                        res.status = 500;
                        res.set_content(
                            R"({"error":"Could not persist automation setting."})",
                            "application/json");
                        return;
                    }
                    if (!enabled)
                    {
                        automationRegistry.sessions.clear();
                        automationRegistry.activeSessionId.clear();
                    }
                    res.set_content(
                        json({{"enabled", enabled}}).dump(),
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        server.Get(
            "/api/automation/room-settings",
            [&home](const httplib::Request &req, httplib::Response &res)
            {
                if (!req.has_param("roomName"))
                {
                    json result = json::array();
                    for (const std::string &roomName : home.getRoomNames())
                    {
                        RoomAutomationSettings settings;
                        if (!home.getRoomAutomationSettings(roomName, settings))
                        {
                            res.status = 500;
                            res.set_content(
                                R"({"error":"Could not load room automation settings."})",
                                "application/json");
                            return;
                        }
                        result.push_back({
                            {"roomName", settings.roomName},
                            {"cameraEnabled", settings.cameraEnabled}});
                    }
                    res.set_content(result.dump(), "application/json");
                    return;
                }
                if (req.get_param_value("roomName").empty())
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"roomName query parameter cannot be empty."})",
                        "application/json");
                    return;
                }
                const std::string roomName = req.get_param_value("roomName");
                if (!home.roomExists(roomName))
                {
                    res.status = 404;
                    res.set_content(
                        R"({"error":"Room not found."})",
                        "application/json");
                    return;
                }
                RoomAutomationSettings settings;
                if (!home.getRoomAutomationSettings(
                        roomName, settings))
                {
                    res.status = 500;
                    res.set_content(
                        R"({"error":"Could not load room automation settings."})",
                        "application/json");
                    return;
                }
                res.set_content(
                    json({
                        {"roomName", settings.roomName},
                        {"cameraEnabled", settings.cameraEnabled}})
                        .dump(),
                    "application/json");
            });

        server.Put(
            "/api/automation/room-settings",
            [&home, &automationRegistry](
                const httplib::Request &req, httplib::Response &res)
            {
                try
                {
                    const json body = json::parse(req.body);
                    if (!body.contains("roomName") ||
                        !body["roomName"].is_string() ||
                        body["roomName"].get<std::string>().empty() ||
                        !body.contains("cameraEnabled") ||
                        !body["cameraEnabled"].is_boolean())
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"roomName and boolean cameraEnabled are required."})",
                            "application/json");
                        return;
                    }
                    RoomAutomationSettings settings{
                        body["roomName"].get<std::string>(),
                        body["cameraEnabled"].get<bool>()};
                    if (!home.roomExists(settings.roomName))
                    {
                        res.status = 404;
                        res.set_content(
                            R"({"error":"Room not found."})",
                            "application/json");
                        return;
                    }
                    std::lock_guard<std::mutex> lock(
                        automationRegistry.mutex);
                    if (!home.saveRoomAutomationSettings(settings))
                    {
                        res.status = 500;
                        res.set_content(
                            R"({"error":"Could not persist room automation settings."})",
                            "application/json");
                        return;
                    }
                    if (!settings.cameraEnabled &&
                        !automationRegistry.activeSessionId.empty())
                    {
                        const auto session = automationRegistry.sessions.find(
                            automationRegistry.activeSessionId);
                        if (session != automationRegistry.sessions.end() &&
                            session->second.roomName == settings.roomName)
                        {
                            automationRegistry.sessions.erase(session);
                            automationRegistry.activeSessionId.clear();
                        }
                    }
                    res.set_content(
                        json({
                            {"roomName", settings.roomName},
                            {"cameraEnabled", settings.cameraEnabled}})
                            .dump(),
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        server.Get(
            "/api/automation/enrollments",
            [&home](const httplib::Request &, httplib::Response &res)
            {
                json result = json::array();
                for (const AutomationEnrollmentRecord &enrollment :
                     home.getAutomationEnrollments())
                {
                    result.push_back({
                        {"id", enrollment.id},
                        {"profileId", enrollment.profileId},
                        {"profileName", enrollment.profileName},
                        {"createdAt", enrollment.createdAt}});
                }
                res.set_content(result.dump(), "application/json");
            });

        server.Post(
            "/api/automation/enrollments",
            [&home, &automationRegistry](
                const httplib::Request &req, httplib::Response &res)
            {
                try
                {
                    const json body = json::parse(req.body);
                    if (!body.contains("profileId") ||
                        !body["profileId"].is_string() ||
                        !body.contains("consent") ||
                        !body["consent"].is_boolean() ||
                        !body["consent"].get<bool>())
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"An existing profile ID and explicit biometric consent are required."})",
                            "application/json");
                        return;
                    }
                    const std::string profileId =
                        body["profileId"].get<std::string>();
                    const auto profiles = home.getUserProfiles();
                    const auto profile = std::find_if(
                        profiles.begin(), profiles.end(),
                        [&profileId](const UserProfile &item)
                        {
                            return item.id == profileId;
                        });
                    if (profile == profiles.end())
                    {
                        res.status = 404;
                        res.set_content(
                            R"({"error":"Profile not found."})",
                            "application/json");
                        return;
                    }

                    std::lock_guard<std::mutex> lock(
                        automationRegistry.mutex);
                    if (!automationRegistry.activeSessionId.empty())
                    {
                        res.status = 409;
                        res.set_content(
                            R"({"error":"Stop room recognition before changing enrollments."})",
                            "application/json");
                        return;
                    }
                    const auto existingEnrollments =
                        home.getAutomationEnrollments();
                    if (std::any_of(
                            existingEnrollments.begin(),
                            existingEnrollments.end(),
                            [&profileId](const AutomationEnrollmentRecord &item)
                            {
                                return item.profileId == profileId;
                            }))
                    {
                        res.status = 409;
                        res.set_content(
                            R"({"error":"This profile already has an enrollment. Delete it before re-enrolling."})",
                            "application/json");
                        return;
                    }
                    AutomationEnrollmentRecord enrollment{
                        newAutomationId(automationRegistry, "face"),
                        profileId,
                        profile->displayName,
                        static_cast<long long>(std::time(nullptr))};
                    if (!home.addAutomationEnrollment(enrollment))
                    {
                        res.status = 409;
                        res.set_content(
                            R"({"error":"Could not save the profile enrollment."})",
                            "application/json");
                        return;
                    }
                    res.status = 201;
                    res.set_content(
                        json({
                            {"id", enrollment.id},
                            {"profileId", enrollment.profileId},
                            {"profileName", enrollment.profileName},
                            {"createdAt", enrollment.createdAt}})
                            .dump(),
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        server.Delete(
            R"(/api/automation/enrollments/([^/]+))",
            [&home, &automationRegistry](
                const httplib::Request &req, httplib::Response &res)
            {
                const std::string enrollmentId = req.matches[1].str();
                std::lock_guard<std::mutex> lock(automationRegistry.mutex);
                if (!automationRegistry.activeSessionId.empty())
                {
                    res.status = 409;
                    res.set_content(
                        R"({"error":"Stop room recognition before deleting an enrollment."})",
                        "application/json");
                    return;
                }
                if (!home.deleteAutomationEnrollment(enrollmentId))
                {
                    res.status = 404;
                    res.set_content(
                        R"({"error":"Enrollment was not found."})",
                        "application/json");
                    return;
                }
                res.set_content(
                    json({{"id", enrollmentId}, {"deleted", true}}).dump(),
                    "application/json");
            });

        server.Get(
            "/api/automation/events",
            [&home](const httplib::Request &req, httplib::Response &res)
            {
                int limit = 50;
                try
                {
                    if (req.has_param("limit"))
                        limit = std::stoi(req.get_param_value("limit"));
                }
                catch (const std::exception &)
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"limit must be an integer from 1 to 500."})",
                        "application/json");
                    return;
                }
                if (limit < 1 || limit > 500)
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"limit must be an integer from 1 to 500."})",
                        "application/json");
                    return;
                }
                json result = json::array();
                for (const AutomationEventRecord &event :
                     home.getAutomationEvents(limit))
                {
                    json settings = json::array();
                    try
                    {
                        settings = json::parse(event.settingsChanged);
                    }
                    catch (const json::exception &)
                    {
                    }
                    result.push_back({
                        {"id", event.id},
                        {"roomName", event.roomName},
                        {"profileId", event.profileId},
                        {"eventTime", event.eventTime},
                        {"outcome", event.outcome},
                        {"settingsChanged", settings}});
                }
                res.set_content(result.dump(), "application/json");
            });

        server.Post(
            "/api/automation/sessions",
            [&home, &automationRegistry](
                const httplib::Request &req, httplib::Response &res)
            {
                try
                {
                    const json body = json::parse(req.body);
                    if (!body.contains("roomName") ||
                        !body["roomName"].is_string() ||
                        body["roomName"].get<std::string>().empty())
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"roomName is required."})",
                            "application/json");
                        return;
                    }
                    const std::string roomName =
                        body["roomName"].get<std::string>();
                    if (!home.roomExists(roomName))
                    {
                        res.status = 404;
                        res.set_content(
                            R"({"error":"Room not found."})",
                            "application/json");
                        return;
                    }
                    if (!home.getAutomationEnabled())
                    {
                        res.status = 403;
                        res.set_content(
                            R"({"error":"Camera automation is globally disabled."})",
                            "application/json");
                        return;
                    }
                    std::lock_guard<std::mutex> lock(
                        automationRegistry.mutex);
                    if (!home.getAutomationEnabled())
                    {
                        res.status = 403;
                        res.set_content(
                            R"({"error":"Camera automation is globally disabled."})",
                            "application/json");
                        return;
                    }
                    RoomAutomationSettings roomSettings;
                    if (!home.getRoomAutomationSettings(
                            roomName, roomSettings))
                    {
                        res.status = 500;
                        res.set_content(
                            R"({"error":"Could not load room automation settings."})",
                            "application/json");
                        return;
                    }
                    if (!roomSettings.cameraEnabled)
                    {
                        res.status = 403;
                        res.set_content(
                            R"({"error":"Camera monitoring is disabled for this room."})",
                            "application/json");
                        return;
                    }
                    if (!automationRegistry.activeSessionId.empty())
                    {
                        auto active = automationRegistry.sessions.find(
                            automationRegistry.activeSessionId);
                        const long long now =
                            static_cast<long long>(std::time(nullptr));
                        if (active != automationRegistry.sessions.end() &&
                            now - active->second.lastActivityAt <= 20)
                        {
                            res.status = 409;
                            res.set_content(
                                R"({"error":"Stop the active room session before starting another."})",
                                "application/json");
                            return;
                        }
                        automationRegistry.sessions.erase(
                            automationRegistry.activeSessionId);
                        automationRegistry.activeSessionId.clear();
                    }
                    const std::string sessionId =
                        newAutomationId(automationRegistry, "room");
                    automationRegistry.sessions.emplace(
                        sessionId, RoomAutomationSession{});
                    automationRegistry.sessions[sessionId].roomName =
                        roomName;
                    automationRegistry.sessions[sessionId].lastActivityAt =
                        static_cast<long long>(std::time(nullptr));
                    automationRegistry.activeSessionId = sessionId;
                    res.status = 201;
                    res.set_content(
                        json({
                            {"sessionId", sessionId},
                            {"roomName", roomName},
                            {"status", "starting"}})
                            .dump(),
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        server.Get(
            R"(/api/automation/sessions/([^/]+))",
            [&home, &automationRegistry](
                const httplib::Request &req, httplib::Response &res)
            {
                const std::string sessionId = req.matches[1].str();
                std::lock_guard<std::mutex> lock(automationRegistry.mutex);
                const auto session =
                    automationRegistry.sessions.find(sessionId);
                if (session == automationRegistry.sessions.end() ||
                    automationRegistry.activeSessionId != sessionId)
                {
                    res.status = 404;
                    res.set_content(
                        R"({"error":"Room recognition session is not active."})",
                        "application/json");
                    return;
                }
                session->second.lastActivityAt =
                    static_cast<long long>(std::time(nullptr));
                res.set_content(
                    json({
                        {"sessionId", sessionId},
                        {"roomName", session->second.roomName},
                        {"enabled", home.getAutomationEnabled()},
                        {"resolved", !session->second.enrollmentId.empty()}})
                        .dump(),
                    "application/json");
            });

        server.Delete(
            R"(/api/automation/sessions/([^/]+))",
            [&automationRegistry](
                const httplib::Request &req, httplib::Response &res)
            {
                const std::string sessionId = req.matches[1].str();
                std::lock_guard<std::mutex> lock(automationRegistry.mutex);
                if (automationRegistry.sessions.erase(sessionId) == 0)
                {
                    res.status = 404;
                    res.set_content(
                        R"({"error":"Room recognition session is not active."})",
                        "application/json");
                    return;
                }
                if (automationRegistry.activeSessionId == sessionId)
                    automationRegistry.activeSessionId.clear();
                res.set_content(
                    json({{"sessionId", sessionId}, {"status", "stopped"}})
                        .dump(),
                    "application/json");
            });

        server.Post(
            R"(/api/automation/sessions/([^/]+)/observation)",
            [&home, &automationRegistry](
                const httplib::Request &req, httplib::Response &res)
            {
                try
                {
                    const std::string sessionId = req.matches[1].str();
                    const json body = json::parse(req.body);
                    if (!body.contains("roomName") ||
                        !body["roomName"].is_string() ||
                        !body.contains("status") ||
                        !body["status"].is_string())
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"roomName and status are required."})",
                            "application/json");
                        return;
                    }
                    const std::string roomName =
                        body["roomName"].get<std::string>();
                    const std::string status =
                        body["status"].get<std::string>();
                    const std::set<std::string> allowed{
                        "pending", "matched", "unknown", "uncertain", "ambiguous",
                        "no_person", "failed"};
                    if (allowed.count(status) == 0)
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"Unsupported recognition status."})",
                            "application/json");
                        return;
                    }
                    std::string enrollmentId;
                    if (status == "matched")
                    {
                        if (!body.contains("enrollmentId") ||
                            !body["enrollmentId"].is_string())
                        {
                            res.status = 400;
                            res.set_content(
                                R"({"error":"A matched observation requires enrollmentId."})",
                                "application/json");
                            return;
                        }
                        enrollmentId =
                            body["enrollmentId"].get<std::string>();
                        AutomationEnrollmentRecord enrollment;
                        if (!home.getAutomationEnrollment(
                                enrollmentId, enrollment))
                        {
                            res.status = 404;
                            res.set_content(
                                R"({"error":"Recognized identity is not enrolled."})",
                                "application/json");
                            return;
                        }
                    }
                    std::lock_guard<std::mutex> lock(
                        automationRegistry.mutex);
                    auto session =
                        automationRegistry.sessions.find(sessionId);
                    if (session == automationRegistry.sessions.end() ||
                        automationRegistry.activeSessionId != sessionId)
                    {
                        res.status = 404;
                        res.set_content(
                            R"({"error":"Room recognition session is not active."})",
                            "application/json");
                        return;
                    }
                    if (session->second.roomName != roomName)
                    {
                        res.status = 409;
                        res.set_content(
                            R"({"error":"The selected room does not match this camera session."})",
                            "application/json");
                        return;
                    }
                    session->second.observationStatus = status;
                    session->second.observedEnrollmentId = enrollmentId;
                    session->second.observedAt =
                        static_cast<long long>(std::time(nullptr));
                    session->second.lastActivityAt =
                        session->second.observedAt;
                    res.set_content(
                        json({
                            {"sessionId", sessionId},
                            {"roomName", roomName},
                            {"status", status},
                            {"observedAt", session->second.observedAt}})
                            .dump(),
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        server.Post(
            R"(/api/automation/sessions/([^/]+)/resolve)",
            [&home, &automationRegistry](
                const httplib::Request &req, httplib::Response &res)
            {
                try
                {
                    const std::string sessionId = req.matches[1].str();
                    const json body = json::parse(req.body);
                    if (!body.contains("roomName") ||
                        !body["roomName"].is_string() ||
                        !body.contains("enrollmentId") ||
                        !body["enrollmentId"].is_string())
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"roomName and a matched enrollmentId are required."})",
                            "application/json");
                        return;
                    }
                    const std::string roomName =
                        body["roomName"].get<std::string>();
                    const std::string enrollmentId =
                        body["enrollmentId"].get<std::string>();
                    if (!home.getAutomationEnabled())
                    {
                        res.status = 403;
                        res.set_content(
                            R"({"error":"Camera automation is globally disabled."})",
                            "application/json");
                        return;
                    }
                    {
                        std::lock_guard<std::mutex> lock(
                            automationRegistry.mutex);
                        auto found = automationRegistry.sessions.find(sessionId);
                        if (found == automationRegistry.sessions.end() ||
                            automationRegistry.activeSessionId != sessionId)
                        {
                            res.status = 404;
                            res.set_content(
                                R"({"error":"Room recognition session is not active."})",
                                "application/json");
                            return;
                        }
                        if (found->second.roomName != roomName)
                        {
                            res.status = 409;
                            res.set_content(
                                R"({"error":"The selected room does not match this camera session."})",
                                "application/json");
                            return;
                        }
                        if (found->second.observationStatus != "matched" ||
                            found->second.observedEnrollmentId != enrollmentId ||
                            std::time(nullptr) - found->second.observedAt > 8)
                        {
                            res.status = 409;
                            res.set_content(
                                R"({"error":"Identity match is no longer current. Wait for a stable match before previewing."})",
                                "application/json");
                            return;
                        }
                        if (found->second.enrollmentId == enrollmentId &&
                            !found->second.preview.empty())
                        {
                            res.set_content(
                                json({
                                    {"sessionId", sessionId},
                                    {"roomName", roomName},
                                    {"enrollmentId", enrollmentId},
                                    {"profileId", found->second.profileId},
                                    {"preferences", {
                                        {"acTemperature", found->second.preferences.acTemperature},
                                        {"heaterTemperature", found->second.preferences.heaterTemperature},
                                        {"fanSpeed", found->second.preferences.fanSpeed},
                                        {"lightOn", found->second.preferences.lightOn},
                                        {"automaticApplyEnabled", found->second.preferences.automaticApplyEnabled},
                                        {"firstApplicationConfirmed", found->second.preferences.firstApplicationConfirmed}}},
                                    {"automaticApplyEnabled", found->second.preferences.automaticApplyEnabled},
                                    {"firstApplicationConfirmed", found->second.preferences.firstApplicationConfirmed},
                                    {"preview", found->second.preview},
                                    {"alreadyApplied", found->second.appliedEnrollments.count(enrollmentId) != 0}})
                                    .dump(),
                                "application/json");
                            return;
                        }
                        if (found->second.appliedEnrollments.count(enrollmentId) != 0)
                        {
                            res.status = 409;
                            res.set_content(
                                R"({"error":"Preferences for this identity were already applied in this session."})",
                                "application/json");
                            return;
                        }
                    }

                    AutomationEnrollmentRecord enrollment;
                    if (!home.getAutomationEnrollment(
                            enrollmentId, enrollment))
                    {
                        res.status = 404;
                        res.set_content(
                            R"({"error":"Recognized identity is not enrolled."})",
                            "application/json");
                        return;
                    }
                    UserPreferenceRecord preferences;
                    if (!home.getUserPreferences(
                            enrollment.profileId, roomName, preferences))
                    {
                        const AutomationEventRecord event{
                            0, roomName, enrollment.profileId,
                            static_cast<long long>(std::time(nullptr)),
                            "missing_preferences", "[]"};
                        if (!home.recordAutomationEvent(event))
                        {
                            res.status = 500;
                            res.set_content(
                                R"({"error":"No saved preferences exist and the outcome could not be recorded."})",
                                "application/json");
                            return;
                        }
                        res.status = 404;
                        res.set_content(
                            R"({"error":"No saved preferences found for this profile and room."})",
                            "application/json");
                        return;
                    }
                    if (!validPreferences(preferences))
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"Saved preference values are outside supported device limits."})",
                            "application/json");
                        return;
                    }
                    if (preferences.acTemperature == -1 &&
                        preferences.heaterTemperature == -1 &&
                        preferences.fanSpeed == -1 &&
                        preferences.lightOn == -1)
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"There are no specified preferences to preview."})",
                            "application/json");
                        return;
                    }
                    const json preview = makePreferenceActions(
                        home, roomName, preferences, false);
                    {
                        std::lock_guard<std::mutex> lock(
                            automationRegistry.mutex);
                        auto found = automationRegistry.sessions.find(sessionId);
                        if (found == automationRegistry.sessions.end() ||
                            automationRegistry.activeSessionId != sessionId ||
                            found->second.roomName != roomName)
                        {
                            res.status = 409;
                            res.set_content(
                                R"({"error":"Recognition session changed while resolving this identity."})",
                                "application/json");
                            return;
                        }
                        found->second.enrollmentId = enrollmentId;
                        found->second.profileId = enrollment.profileId;
                        found->second.preferences = preferences;
                        found->second.preview = preview;
                    }
                    res.set_content(
                        json({
                            {"sessionId", sessionId},
                            {"roomName", roomName},
                            {"enrollmentId", enrollment.id},
                            {"profileId", enrollment.profileId},
                            {"profileName", enrollment.profileName},
                            {"preferences", {
                                {"acTemperature", preferences.acTemperature},
                                {"heaterTemperature", preferences.heaterTemperature},
                                {"fanSpeed", preferences.fanSpeed},
                                {"lightOn", preferences.lightOn},
                                {"automaticApplyEnabled", preferences.automaticApplyEnabled},
                                {"firstApplicationConfirmed", preferences.firstApplicationConfirmed}}},
                            {"automaticApplyEnabled", preferences.automaticApplyEnabled},
                            {"firstApplicationConfirmed", preferences.firstApplicationConfirmed},
                            {"preview", preview},
                            {"alreadyApplied", false}})
                            .dump(),
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        server.Get(
            "/api/modes/seasons",
            [&home](const httplib::Request &, httplib::Response &res)
            {
                res.set_content(
                    serializeModePresetList(home, "season").dump(),
                    "application/json");
            });

        server.Get(
            "/api/modes/moods",
            [&home](const httplib::Request &, httplib::Response &res)
            {
                res.set_content(
                    serializeModePresetList(home, "mood").dump(),
                    "application/json");
            });

        server.Put(
            R"(/api/modes/presets/(season|mood)/([^/]+))",
            [&home](const httplib::Request &req, httplib::Response &res)
            {
                auto fail = [&res](int status, const std::string &message)
                {
                    res.status = status;
                    res.set_content(
                        json({{"error", message}}).dump(),
                        "application/json");
                };
                try
                {
                    const std::string modeType = req.matches[1].str();
                    const std::string presetId = req.matches[2].str();
                    const ModePresetInfo *preset =
                        findPresetInfo(modeType, presetId);
                    if (preset == nullptr)
                    {
                        fail(404, "Built-in mode preset not found.");
                        return;
                    }
                    const json body = json::parse(req.body);
                    if (!body.is_object() || body.size() != 1 ||
                        !body.contains("overrides") ||
                        !body["overrides"].is_array() ||
                        body["overrides"].size() > 100)
                    {
                        fail(400, "Provide an overrides array with at most 100 device rules.");
                        return;
                    }

                    const auto devices = home.getAllDeviceRecords();
                    std::set<std::pair<std::string, std::string>> seen;
                    std::set<std::string> devicesWithPolicy;
                    json canonicalOverrides = json::array();
                    for (const json &rule : body["overrides"])
                    {
                        if (!rule.is_object() ||
                            (rule.size() != 3 && rule.size() != 4) ||
                            !rule.contains("deviceId") ||
                            !rule["deviceId"].is_string() ||
                            !rule.contains("property") ||
                            !rule["property"].is_string() ||
                            !rule.contains("value"))
                        {
                            fail(400, "Each override requires deviceId, property, and value.");
                            return;
                        }
                        const std::string deviceId =
                            rule["deviceId"].get<std::string>();
                        const std::string property =
                            rule["property"].get<std::string>();
                        const auto device = std::find_if(
                            devices.begin(), devices.end(),
                            [&deviceId](const DeviceRecord &item)
                            { return item.id == deviceId; });
                        if (device == devices.end())
                        {
                            fail(404, "Override references a device that does not exist.");
                            return;
                        }
                        if (rule.contains("roomName") &&
                            (!rule["roomName"].is_string() ||
                             rule["roomName"].get<std::string>() != device->roomName))
                        {
                            fail(400, "Override roomName must match the selected device's current room.");
                            return;
                        }
                        if (!seen.emplace(deviceId, property).second)
                        {
                            fail(400, "A preset can define only one override per device property.");
                            return;
                        }
                        if (property == "policy" || property == "power")
                        {
                            if (!devicesWithPolicy.insert(deviceId).second)
                            {
                                fail(400, "A device may have only one power-policy override per preset.");
                                return;
                            }
                        }
                        std::string reason;
                        if (!modePropertySupported(
                                *device, property, rule["value"], reason))
                        {
                            fail(400, reason);
                            return;
                        }
                        canonicalOverrides.push_back({
                            {"deviceId", deviceId},
                            {"roomName", device->roomName},
                            {"property", property},
                            {"value", rule["value"]}});
                    }

                    const ModePresetRecord stored{
                        modeType,
                        presetId,
                        canonicalOverrides.dump(),
                        static_cast<long long>(std::time(nullptr))};
                    if (!home.saveModePreset(stored))
                    {
                        fail(500, "Could not persist the customized preset.");
                        return;
                    }
                    res.set_content(
                        json({
                            {"id", presetId},
                            {"modeType", modeType},
                            {"overrides", canonicalOverrides},
                            {"updatedAt", stored.updatedAt}})
                            .dump(),
                        "application/json");
                }
                catch (const json::parse_error &)
                {
                    fail(400, "Request body must be valid JSON.");
                }
                catch (const json::exception &error)
                {
                    fail(400, error.what());
                }
            });

        auto validateModeSelection =
            [&home](json &selection, std::string &error) -> bool
        {
            if (!selection.is_object())
            {
                error = "Request body must be a JSON object.";
                return false;
            }
            if (!selection.contains("seasonId") ||
                !selection["seasonId"].is_string() ||
                !selection.contains("moodId") ||
                !selection["moodId"].is_string())
            {
                error = "seasonId and moodId must be strings; use an empty string when not selected.";
                return false;
            }
            const std::string seasonId =
                selection["seasonId"].get<std::string>();
            const std::string moodId =
                selection["moodId"].get<std::string>();
            if (seasonId.empty() && moodId.empty())
            {
                error = "Select at least one season or mood preset.";
                return false;
            }
            if ((!seasonId.empty() &&
                 findPresetInfo("season", seasonId) == nullptr) ||
                (!moodId.empty() &&
                 findPresetInfo("mood", moodId) == nullptr))
            {
                error = "Unknown season or mood preset.";
                return false;
            }
            for (const std::string &key : {"roomNames", "deviceIds"})
            {
                if (!selection.contains(key))
                    selection[key] = json::array();
                if (!selection[key].is_array() || selection[key].size() > 500)
                {
                    error = key + " must be an array with at most 500 entries.";
                    return false;
                }
                std::set<std::string> unique;
                for (const json &item : selection[key])
                {
                    if (!item.is_string() || item.get<std::string>().empty())
                    {
                        error = key + " must contain non-empty strings.";
                        return false;
                    }
                    unique.insert(item.get<std::string>());
                }
                selection[key] = json::array();
                for (const std::string &item : unique)
                    selection[key].push_back(item);
            }

            const auto rooms = home.getRoomNames();
            for (const json &room : selection["roomNames"])
            {
                const std::string roomName = room.get<std::string>();
                if (std::find(rooms.begin(), rooms.end(), roomName) == rooms.end())
                {
                    error = "Selected room does not exist: " + roomName;
                    return false;
                }
            }
            const auto devices = home.getAllDeviceRecords();
            for (const json &id : selection["deviceIds"])
            {
                const std::string deviceId = id.get<std::string>();
                const auto device = std::find_if(
                    devices.begin(), devices.end(),
                    [&deviceId](const DeviceRecord &item)
                    { return item.id == deviceId; });
                if (device == devices.end())
                {
                    error = "Selected device does not exist: " + deviceId;
                    return false;
                }
                if (!selection["roomNames"].empty() &&
                    std::find(
                        selection["roomNames"].begin(),
                        selection["roomNames"].end(),
                        device->roomName) == selection["roomNames"].end())
                {
                    error = "Selected device is not in one of the selected rooms.";
                    return false;
                }
            }
            return true;
        };

        server.Post(
            "/api/modes/preview",
            [&home, &modePreviewRegistry, &validateModeSelection](
                const httplib::Request &req, httplib::Response &res)
            {
                try
                {
                    json selection = json::parse(req.body);
                    std::string error;
                    if (!validateModeSelection(selection, error))
                    {
                        res.status = 400;
                        res.set_content(
                            json({{"error", error}}).dump(),
                            "application/json");
                        return;
                    }
                    const json actions = buildModeActions(
                        home, selection, home.getAllDeviceRecords());
                    const long long now =
                        static_cast<long long>(std::time(nullptr));
                    std::string previewId;
                    {
                        std::lock_guard<std::mutex> lock(
                            modePreviewRegistry.mutex);
                        for (auto iterator =
                                 modePreviewRegistry.previews.begin();
                             iterator != modePreviewRegistry.previews.end();)
                        {
                            if (iterator->second.expiresAt < now)
                                iterator = modePreviewRegistry.previews.erase(iterator);
                            else
                                ++iterator;
                        }
                        previewId =
                            "mode-preview-" +
                            std::to_string(modePreviewRegistry.nextId++);
                        modePreviewRegistry.previews.emplace(
                            previewId,
                            ModePreviewRecord{selection, actions, now + 300});
                    }
                    int ready = 0;
                    int skipped = 0;
                    int unchanged = 0;
                    for (const json &action : actions)
                    {
                        const std::string status =
                            action.value("status", "");
                        ready += status == "ready" ? 1 : 0;
                        skipped += status == "skipped" ? 1 : 0;
                        unchanged += status == "unchanged" ? 1 : 0;
                    }
                    const std::string seasonId =
                        selection["seasonId"].get<std::string>();
                    const std::string moodId =
                        selection["moodId"].get<std::string>();
                    const ModePresetInfo *season =
                        seasonId.empty() ? nullptr :
                            findPresetInfo("season", seasonId);
                    const ModePresetInfo *mood =
                        moodId.empty() ? nullptr :
                            findPresetInfo("mood", moodId);
                    res.set_content(
                        json({
                            {"previewId", previewId},
                            {"seasonId", seasonId},
                            {"seasonLabel", season == nullptr ? "" : season->label},
                            {"moodId", moodId},
                            {"moodLabel", mood == nullptr ? "" : mood->label},
                            {"roomNames", selection["roomNames"]},
                            {"deviceIds", selection["deviceIds"]},
                            {"actions", actions},
                            {"summary", {
                                {"proposed", ready},
                                {"skipped", skipped},
                                {"unchanged", unchanged},
                                {"message", actions.empty()
                                    ? "No compatible settings apply to the selected target."
                                    : ""}}},
                            {"expiresInSeconds", 300}})
                            .dump(),
                        "application/json");
                }
                catch (const json::parse_error &)
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"Request body must be valid JSON."})",
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        server.Post(
            "/api/modes/apply",
            [&home, &modePreviewRegistry, &validateModeSelection](
                const httplib::Request &req, httplib::Response &res)
            {
                try
                {
                    const json body = json::parse(req.body);
                    if (!body.is_object() ||
                        !body.contains("previewId") ||
                        !body["previewId"].is_string() ||
                        !body.contains("confirmed") ||
                        !body["confirmed"].is_boolean() ||
                        !body["confirmed"].get<bool>())
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"A previewId and explicit confirmed:true are required."})",
                            "application/json");
                        return;
                    }

                    ModePreviewRecord preview;
                    {
                        std::lock_guard<std::mutex> lock(
                            modePreviewRegistry.mutex);
                        const std::string previewId =
                            body["previewId"].get<std::string>();
                        const auto found =
                            modePreviewRegistry.previews.find(previewId);
                        if (found == modePreviewRegistry.previews.end() ||
                            found->second.expiresAt <
                                static_cast<long long>(std::time(nullptr)))
                        {
                            res.status = 409;
                            res.set_content(
                                R"({"error":"Mode preview expired or was already used. Generate a new preview."})",
                                "application/json");
                            return;
                        }
                        preview = found->second;
                        modePreviewRegistry.previews.erase(found);
                    }

                    std::string error;
                    if (!validateModeSelection(preview.selection, error))
                    {
                        res.status = 409;
                        res.set_content(
                            json({{"error", "Mode preview is stale: " + error}})
                                .dump(),
                            "application/json");
                        return;
                    }
                    const json currentActions = buildModeActions(
                        home, preview.selection, home.getAllDeviceRecords());
                    if (currentActions != preview.actions)
                    {
                        res.status = 409;
                        res.set_content(
                            R"({"error":"Device state or preset values changed after preview. Generate and review a new preview."})",
                            "application/json");
                        return;
                    }

                    json actions = currentActions;
                    json snapshot = buildModeSnapshot(
                        preview.selection, home.getAllDeviceRecords());
                    int applied = 0;
                    int skipped = 0;
                    int failed = 0;
                    for (json &action : actions)
                    {
                        if (action.value("status", "") != "ready")
                        {
                            skipped += action.value("status", "") == "skipped";
                            continue;
                        }
                        const std::string deviceId =
                            action["deviceId"].get<std::string>();
                        const std::string property =
                            action["property"].get<std::string>();
                        const std::string roomName =
                            action["roomName"].get<std::string>();
                        const auto records = home.getAllDeviceRecords();
                        const auto device = std::find_if(
                            records.begin(), records.end(),
                            [&deviceId](const DeviceRecord &item)
                            { return item.id == deviceId; });
                        if (device == records.end() ||
                            device->roomName != roomName ||
                            currentModeValue(*device, property) !=
                                action["previousValue"])
                        {
                            action["status"] = "failed";
                            action["reason"] = "Device state changed after preview; no change was applied.";
                            ++failed;
                            continue;
                        }
                        try
                        {
                            applyModeValue(
                                home, deviceId, property, action["value"]);
                            const auto persistedRecords =
                                home.getAllDeviceRecords();
                            const auto persistedDevice = std::find_if(
                                persistedRecords.begin(),
                                persistedRecords.end(),
                                [&deviceId](const DeviceRecord &item)
                                { return item.id == deviceId; });
                            if (persistedDevice == persistedRecords.end() ||
                                currentModeValue(*persistedDevice, property) !=
                                    action["value"])
                            {
                                throw std::runtime_error(
                                    "The requested state was not persisted.");
                            }
                            action["status"] = "applied";
                            ++applied;
                        }
                        catch (const std::exception &applyError)
                        {
                            action["status"] = "failed";
                            action["reason"] = applyError.what();
                            ++failed;
                        }
                    }

                    if (applied == 0)
                    {
                        const std::string seasonId =
                            preview.selection["seasonId"].get<std::string>();
                        const std::string moodId =
                            preview.selection["moodId"].get<std::string>();
                        const auto recentHistory = home.getModeHistory(1);
                        long long historyId = 0;
                        bool historyEntryCreated = false;
                        const std::string historyDetails =
                            json({
                                {"selection", preview.selection},
                                {"actions", actions}})
                                .dump();
                        if (!recentHistory.empty() &&
                            recentHistory.front().outcome == "no_changes" &&
                            recentHistory.front().seasonId == seasonId &&
                            recentHistory.front().moodId == moodId &&
                            recentHistory.front().detailsJson == historyDetails)
                        {
                            historyId = recentHistory.front().id;
                        }
                        else
                        {
                            ModeHistoryRecord history{
                                0,
                                seasonId,
                                moodId,
                                static_cast<long long>(std::time(nullptr)),
                                "no_changes",
                                historyDetails,
                                "[]",
                                "[]",
                                0};
                            if (!home.recordModeHistory(history))
                            {
                                res.status = 500;
                                res.set_content(
                                    json({
                                        {"error", "No device changes were made, but the mode result could not be persisted."},
                                        {"success", false},
                                        {"outcome", "no_changes"},
                                        {"actions", actions},
                                        {"historyPersisted", false}})
                                        .dump(),
                                    "application/json");
                                return;
                            }
                            historyId = history.id;
                            historyEntryCreated = true;
                        }
                        res.set_content(
                            json({
                                {"success", false},
                                {"outcome", "no_changes"},
                                {"actions", actions},
                                {"historyId", historyId},
                                {"historyPersisted", true},
                                {"historyEntryCreated", historyEntryCreated},
                                {"message", skipped > 0
                                    ? "All applicable settings were skipped or already unchanged."
                                    : "No device changes were needed."}})
                                .dump(),
                            "application/json");
                        return;
                    }

                    const bool partial = skipped > 0 || failed > 0;
                    const std::string seasonId =
                        preview.selection["seasonId"].get<std::string>();
                    const std::string moodId =
                        preview.selection["moodId"].get<std::string>();
                    ModeHistoryRecord history{
                        0,
                        seasonId,
                        moodId,
                        static_cast<long long>(std::time(nullptr)),
                        partial ? "partial" : "applied",
                        json({
                            {"selection", preview.selection},
                            {"actions", actions}})
                            .dump(),
                        snapshot.dump(),
                        "[]",
                        0};
                    if (!home.recordModeHistory(history))
                    {
                        res.status = 500;
                        res.set_content(
                            json({
                                {"error", "Device changes were made, but mode history could not be persisted."},
                                {"success", false},
                                {"outcome", partial ? "partial" : "applied"},
                                {"actions", actions},
                                {"historyPersisted", false}})
                                .dump(),
                            "application/json");
                        return;
                    }
                    res.set_content(
                        json({
                            {"success", !partial},
                            {"outcome", partial ? "partial" : "applied"},
                            {"actions", actions},
                            {"historyId", history.id},
                            {"historyPersisted", true},
                            {"summary", {
                                {"applied", applied},
                                {"skipped", skipped},
                                {"failed", failed}}}})
                            .dump(),
                        "application/json");
                }
                catch (const json::parse_error &)
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"Request body must be valid JSON."})",
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        server.Post(
            "/api/modes/restore",
            [&home](const httplib::Request &req, httplib::Response &res)
            {
                try
                {
                    const json body = json::parse(req.body);
                    if (!body.is_object() || body.size() < 1 ||
                        body.size() > 2 ||
                        !body.contains("confirmed") ||
                        !body["confirmed"].is_boolean() ||
                        !body["confirmed"].get<bool>())
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"Explicit confirmed:true is required to restore a mode snapshot."})",
                            "application/json");
                        return;
                    }
                    if (body.contains("roomName") &&
                        (!body["roomName"].is_string() ||
                         body["roomName"].get<std::string>().empty() ||
                         !home.roomExists(body["roomName"].get<std::string>())))
                    {
                        res.status = 400;
                        res.set_content(
                            R"({"error":"roomName must identify an existing room."})",
                            "application/json");
                        return;
                    }
                    ModeHistoryRecord history;
                    bool hasHistory = false;
                    if (body.contains("roomName"))
                    {
                        const std::string roomName =
                            body["roomName"].get<std::string>();
                        for (const ModeHistoryRecord &candidate :
                             home.getModeHistory(200))
                        {
                            if (candidate.restoredAt != 0 ||
                                candidate.snapshotJson.empty() ||
                                candidate.snapshotJson == "[]")
                                continue;
                            const json details =
                                json::parse(candidate.detailsJson);
                            const json selectedRooms =
                                details.is_object() &&
                                        details.contains("selection")
                                    ? details["selection"].value(
                                          "roomNames", json::array())
                                    : json::array();
                            if (selectedRooms.is_array() &&
                                std::find(
                                    selectedRooms.begin(),
                                    selectedRooms.end(),
                                    roomName) != selectedRooms.end())
                            {
                                history = candidate;
                                hasHistory = true;
                                break;
                            }
                        }
                    }
                    else
                    {
                        hasHistory =
                            home.getLatestRestorableModeHistory(history);
                    }
                    if (!hasHistory)
                    {
                        res.status = 404;
                        res.set_content(
                            R"({"error":"There is no unapplied mode snapshot available for this room."})",
                            "application/json");
                        return;
                    }
                    const json snapshot = json::parse(history.snapshotJson);
                    json actions = json::array();
                    bool complete = true;
                    for (const json &entry : snapshot)
                    {
                        const std::string deviceId =
                            entry.value("deviceId", "");
                        const std::string property =
                            entry.value("property", "");
                        const json value =
                            entry.value("value", json(nullptr));
                        json action = {
                            {"deviceId", deviceId},
                            {"property", property},
                            {"value", value}};
                        const auto records = home.getAllDeviceRecords();
                        const auto device = std::find_if(
                            records.begin(), records.end(),
                            [&deviceId](const DeviceRecord &item)
                            { return item.id == deviceId; });
                        if (device == records.end())
                        {
                            action["status"] = "skipped";
                            action["reason"] = "Device no longer exists.";
                            complete = false;
                        }
                        else if (currentModeValue(*device, property) == value)
                        {
                            action["status"] = "unchanged";
                        }
                        else
                        {
                            std::string reason;
                            if (!modePropertySupported(
                                    *device, property, value, reason))
                            {
                                action["status"] = "skipped";
                                action["reason"] = reason;
                                complete = false;
                            }
                            else
                            {
                                try
                                {
                                    applyModeValue(
                                        home, deviceId, property, value);
                                    const auto persistedRecords =
                                        home.getAllDeviceRecords();
                                    const auto persistedDevice = std::find_if(
                                        persistedRecords.begin(),
                                        persistedRecords.end(),
                                        [&deviceId](const DeviceRecord &item)
                                        { return item.id == deviceId; });
                                    if (persistedDevice == persistedRecords.end() ||
                                        currentModeValue(*persistedDevice, property) != value)
                                    {
                                        throw std::runtime_error(
                                            "The restored state was not persisted.");
                                    }
                                    action["status"] = "restored";
                                }
                                catch (const std::exception &restoreError)
                                {
                                    action["status"] = "failed";
                                    action["reason"] = restoreError.what();
                                    complete = false;
                                }
                            }
                        }
                        actions.push_back(std::move(action));
                    }
                    const long long restoredAt = complete
                        ? static_cast<long long>(std::time(nullptr))
                        : 0;
                    if (!home.updateModeHistoryRestore(
                            history.id, actions.dump(), restoredAt))
                    {
                        res.status = 500;
                        res.set_content(
                            json({
                                {"error", "Restore was processed, but the restore result could not be persisted."},
                                {"success", false},
                                {"actions", actions}})
                                .dump(),
                            "application/json");
                        return;
                    }
                    res.set_content(
                        json({
                            {"success", complete},
                            {"outcome", complete ? "restored" : "partial"},
                            {"historyId", history.id},
                            {"actions", actions}})
                            .dump(),
                        "application/json");
                }
                catch (const json::parse_error &)
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"Request body must be valid JSON."})",
                        "application/json");
                }
                catch (const json::exception &error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        server.Get(
            "/api/modes/history",
            [&home](const httplib::Request &req, httplib::Response &res)
            {
                int limit = 50;
                try
                {
                    if (req.has_param("limit"))
                        limit = std::stoi(req.get_param_value("limit"));
                }
                catch (const std::exception &)
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"limit must be an integer from 1 to 200."})",
                        "application/json");
                    return;
                }
                if (limit < 1 || limit > 200)
                {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"limit must be an integer from 1 to 200."})",
                        "application/json");
                    return;
                }
                json result = json::array();
                try
                {
                    for (const ModeHistoryRecord &history :
                         home.getModeHistory(limit))
                        result.push_back(serializeModeHistory(history));
                }
                catch (const std::exception &error)
                {
                    res.status = 500;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                    return;
                }
                res.set_content(result.dump(), "application/json");
            });

        std::cout << "\nSmartHomeOS API is starting...\n";
        std::cout << "Rooms: " << home.getRoomCount() << '\n';
        std::cout << "Devices: " << home.getDeviceCount() << '\n';
        std::cout << "Energy sessions: /api/energy/sessions\n";
        std::cout << "API address: http://localhost:8080\n";
        std::cout << "Press Ctrl+C to stop the server.\n\n";

        int port = 8080;
        if (const char *configuredPort =
                std::getenv("SMARTHOME_API_PORT"))
        {
            try
            {
                std::size_t parsedLength = 0;
                const int parsedPort =
                    std::stoi(configuredPort, &parsedLength);

                if (parsedLength != std::string(configuredPort).size() ||
                    parsedPort < 1 || parsedPort > 65535)
                {
                    std::cerr << "SMARTHOME_API_PORT must be between 1 and 65535.\n";
                    return 1;
                }

                port = parsedPort;
            }
            catch (const std::exception &)
            {
                std::cerr << "SMARTHOME_API_PORT must be a valid integer.\n";
                return 1;
            }
        }

        if (!server.listen("127.0.0.1", port))
        {
            std::cerr << "Could not start the API server on port "
                      << port << ".\n";
            return 1;
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "SmartHomeOS API error: "
                  << error.what() << '\n';
        return 1;
    }

    return 0;
}