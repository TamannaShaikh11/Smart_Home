#include "Appliance.h"

#include <iostream>
#include <utility>

Appliance::Appliance(
    std::string deviceId,
    std::string deviceName,
    std::string deviceRoom,
    int powerWatts
)
    : id(std::move(deviceId)),
      name(std::move(deviceName)),
      room(std::move(deviceRoom)),
      lifetime(DeviceLifetime::PERMANENT),
      expiryTime(0)
{
    state.status = DeviceStatus::OFF;
    state.powerWatts = powerWatts;
    state.setting = 0;
    state.runtimeMinutes = 0;
}

Appliance::~Appliance()
{
}

void Appliance::turnOn()
{
    state.status = DeviceStatus::ON;
}

void Appliance::turnOff()
{
    state.status = DeviceStatus::OFF;
}

void Appliance::showStatus() const
{
    std::cout << "Device ID: " << id << '\n';
    std::cout << "Name: " << name << '\n';
    std::cout << "Room: " << room << '\n';

    std::cout << "Status: "
              << (state.status == DeviceStatus::ON ? "ON" : "OFF")
              << '\n';

    std::cout << "Power: " << state.powerWatts << " W\n";
    std::cout << "Runtime: " << state.runtimeMinutes << " minutes\n";

    std::cout << "Lifetime: "
              << (lifetime == DeviceLifetime::PERMANENT
                      ? "PERMANENT"
                      : "TEMPORARY")
              << '\n';

    if (lifetime == DeviceLifetime::TEMPORARY && expiryTime != 0)
    {
        std::time_t remaining = expiryTime - std::time(nullptr);

        if (remaining < 0)
            remaining = 0;

        std::cout << "Time remaining: "
                  << remaining << " seconds\n";
    }
}

std::string Appliance::getId() const
{
    return id;
}

std::string Appliance::getName() const
{
    return name;
}

std::string Appliance::getRoom() const
{
    return room;
}

DeviceStatus Appliance::getStatus() const
{
    return state.status;
}

int Appliance::getPowerWatts() const
{
    return state.powerWatts;
}

int Appliance::getRuntimeMinutes() const
{
    return state.runtimeMinutes;
}

const DeviceState& Appliance::getState() const
{
    return state;
}

void Appliance::setRoom(const std::string& newRoom)
{
    room = newRoom;
}

void Appliance::setLifetime(
    DeviceLifetime deviceLifetime,
    std::time_t expiry
)
{
    lifetime = deviceLifetime;
    expiryTime = expiry;
}

DeviceLifetime Appliance::getLifetime() const
{
    return lifetime;
}

std::time_t Appliance::getExpiryTime() const
{
    return expiryTime;
}

