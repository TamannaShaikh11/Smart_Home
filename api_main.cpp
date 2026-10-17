
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
                          {"lightOn", preferences.lightOn}})
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

                    res.set_content(
                        json({{"message", "Preferences saved successfully."},
                              {"userId", preferences.userId},
                              {"roomName", preferences.roomName},
                              {"acTemperature", preferences.acTemperature},
                              {"heaterTemperature", preferences.heaterTemperature},
                              {"fanSpeed", preferences.fanSpeed},
                              {"lightOn", preferences.lightOn}})
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
                        "pending", "matched", "unknown", "ambiguous",
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
                                        {"lightOn", found->second.preferences.lightOn}}},
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
                                {"lightOn", preferences.lightOn}}},
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