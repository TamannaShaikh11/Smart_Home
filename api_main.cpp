
#include <iostream>
#include <string>
#include <algorithm>
#include <exception>

#include "core/home/SmartHome.h"
#include "third_party/httplib.h"
#include "third_party/json.hpp"

using json = nlohmann::json;

int main()
{
    try
    {
        // One SmartHome instance owns the live devices and database.
        SmartHome home("My Smart Home");

        httplib::Server server;

        // Allow requests from the React/Vite development server.
        server.set_default_headers({
            {"Access-Control-Allow-Origin", "http://localhost:5173"},
            {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
            {"Access-Control-Allow-Headers", "Content-Type"}
        });

        // Handle browser CORS preflight requests.
        server.Options(R"(/api/.*)",
            [](const httplib::Request&, httplib::Response& res)
            {
                res.status = 204;
            });

        // GET /api/rooms
        server.Get("/api/rooms",
            [&home](const httplib::Request&, httplib::Response& res)
            {
                json result = json::array();

                for (const std::string& roomName : home.getRoomNames())
                {
                    result.push_back({
                        {"name", roomName}
                    });
                }

                res.set_content(result.dump(), "application/json");
            });

        // GET /api/devices
        server.Get("/api/devices",
            [&home](const httplib::Request&, httplib::Response& res)
            {
                json result = json::array();

                for (const DeviceRecord& device :
                     home.getAllDeviceRecords())
                {
                    result.push_back({
                        {"id", device.id},
                        {"name", device.name},
                        {"type", device.type},
                        {"roomName", device.roomName},
                        {"status", device.status == 1 ? "ON" : "OFF"},
                        {"powerWatts", device.powerWatts},
                        {"setting", device.setting},
                        {"runtimeMinutes", device.runtimeMinutes},
                        {"lifetime", device.lifetime},
                        {"expiryTime", device.expiryTime}
                    });
                }

                res.set_content(result.dump(), "application/json");
            });

        // POST /api/devices/{id}/power
        // Request JSON: {"on": true} or {"on": false}
        server.Post(R"(/api/devices/([^/]+)/power)",
            [&home](const httplib::Request& req, httplib::Response& res)
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
                        [&deviceId](const DeviceRecord& device)
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

                    json result = {
                        {"id", it->id},
                        {"name", it->name},
                        {"roomName", it->roomName},
                        {"status", it->status == 1 ? "ON" : "OFF"},
                        {"powerWatts", it->powerWatts}
                    };

                    res.set_content(result.dump(), "application/json");
                }
                catch (const json::exception& error)
                {
                    res.status = 400;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
                catch (const std::exception& error)
                {
                    res.status = 500;
                    res.set_content(
                        json({{"error", error.what()}}).dump(),
                        "application/json");
                }
            });

        std::cout << "\nSmartHomeOS API is starting...\n";
        std::cout << "Rooms: " << home.getRoomCount() << '\n';
        std::cout << "Devices: " << home.getDeviceCount() << '\n';
        std::cout << "API address: http://localhost:8080\n";
        std::cout << "Press Ctrl+C to stop the server.\n\n";

        if (!server.listen("127.0.0.1", 8080))
        {
            std::cerr << "Could not start the API server on port 8080.\n";
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "SmartHomeOS API error: "
                  << error.what() << '\n';
        return 1;
    }

    return 0;
}