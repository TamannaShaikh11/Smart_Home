
#include <iostream>
#include <string>
#include <algorithm>
#include <exception>

#include "core/home/SmartHome.h"
#include "third_party/httplib.h"
#include "third_party/json.hpp"

#include "core/devices/GenericAppliance.h"
#include "core/devices/Light.h"
#include "core/devices/Fan.h"
#include "core/devices/AC.h"
#include "core/devices/Heater.h"

using json = nlohmann::json;

int main()
{
    try
    {
        // One SmartHome instance owns the live devices and database.
        SmartHome home("My Smart Home");

        httplib::Server server;

        // Allow requests from the React/Vite development server.
        server.set_default_headers({{"Access-Control-Allow-Origin", "http://localhost:5173"},
                                    {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
                                    {"Access-Control-Allow-Headers", "Content-Type"}});

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

                            if (powerWatts < 0)
                            {
                                fail(400, "Power consumption cannot be negative.");
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

                            std::unique_ptr<Appliance> device;

                            if (type == "Light")
                            {
                                device = std::make_unique<Light>(
                                    id, name, roomName, powerWatts);
                            }
                            else if (type == "Fan")
                            {
                                device = std::make_unique<Fan>(
                                    id, name, roomName, powerWatts);
                            }
                            else if (type == "AC")
                            {
                                device = std::make_unique<AC>(
                                    id, name, roomName, powerWatts);
                            }
                            else if (type == "Heater")
                            {
                                device = std::make_unique<Heater>(
                                    id, name, roomName, powerWatts);
                            }
                            else
                            {
                                device = std::make_unique<GenericAppliance>(
                                    id, name, roomName, powerWatts, type);
                            }

                            if (temporary)
                            {
                                home.addTemporaryDevice(device.get(), lifetimeSeconds);
                            }
                            else
                            {
                                home.addDevice(device.get());
                            }

                            // Ownership transfers to SmartHome after successful addition.
                            device.release();

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
                            res.set_content(
                                json({{"message", "Device created successfully."},
                                      {"id", created.id},
                                      {"name", created.name},
                                      {"type", created.type},
                                      {"roomName", created.roomName},
                                      {"powerWatts", created.powerWatts},
                                      {"status", created.status == 1 ? "ON" : "OFF"},
                                      {"lifetime", created.lifetime},
                                      {"expiryTime", created.expiryTime}})
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
                        catch (const std::exception &error)
                        {
                            res.status = 400;
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
                           result.push_back({{"id", device.id},
                                             {"name", device.name},
                                             {"type", device.type},
                                             {"roomName", device.roomName},
                                             {"status", device.status == 1 ? "ON" : "OFF"},
                                             {"powerWatts", device.powerWatts},
                                             {"setting", device.setting},
                                             {"runtimeMinutes", device.runtimeMinutes},
                                             {"lifetime", device.lifetime},
                                             {"expiryTime", device.expiryTime}});
                       }

                       res.set_content(result.dump(), "application/json");
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

                            json result = {
                                {"id", it->id},
                                {"name", it->name},
                                {"roomName", it->roomName},
                                {"status", it->status == 1 ? "ON" : "OFF"},
                                {"powerWatts", it->powerWatts}};

                            res.set_content(result.dump(), "application/json");
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

        std::cout << "\nSmartHomeOS API is starting...\n";
        std::cout << "Rooms: " << home.getRoomCount() << '\n';
        std::cout << "Devices: " << home.getDeviceCount() << '\n';
        std::cout << "Energy sessions: /api/energy/sessions\n";
        std::cout << "API address: http://localhost:8080\n";
        std::cout << "Press Ctrl+C to stop the server.\n\n";

        if (!server.listen("127.0.0.1", 8080))
        {
            std::cerr << "Could not start the API server on port 8080.\n";
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