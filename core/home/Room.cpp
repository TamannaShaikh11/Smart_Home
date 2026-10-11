
#include "Room.h"

#include <algorithm>
#include <stdexcept>

Room::Room(const std::string& roomName)
    : name(roomName)
{
    if (name.empty()) {
        throw std::invalid_argument(
            "Room name cannot be empty."
        );
    }
}

std::string Room::getName() const
{
    return name;
}

void Room::addDevice(const std::string& deviceId)
{
    if (deviceId.empty() || hasDevice(deviceId)) {
        return;
    }

    deviceIds.push_back(deviceId);
}

void Room::removeDevice(const std::string& deviceId)
{
    deviceIds.erase(
        std::remove(
            deviceIds.begin(),
            deviceIds.end(),
            deviceId
        ),
        deviceIds.end()
    );
}

bool Room::hasDevice(const std::string& deviceId) const
{
    return std::find(
        deviceIds.begin(),
        deviceIds.end(),
        deviceId
    ) != deviceIds.end();
}

const std::vector<std::string>& Room::getDeviceIds() const
{
    return deviceIds;
}

int Room::getDeviceCount() const
{
    return static_cast<int>(deviceIds.size());
}