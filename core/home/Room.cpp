#include "Room.h"

#include <algorithm>
#include <stdexcept>

Room::Room(const std::string& roomName)
{
    if (roomName.empty())
    {
        throw std::invalid_argument(
            "Room name cannot be empty."
        );
    }

    name = roomName;
}

std::string Room::getName() const
{
    return name;
}

bool Room::addDeviceId(const std::string& deviceId)
{
    if (deviceId.empty() || hasDevice(deviceId))
    {
        return false;
    }

    deviceIds.push_back(deviceId);
    return true;
}

bool Room::removeDeviceId(const std::string& deviceId)
{
    auto it = std::find(
        deviceIds.begin(),
        deviceIds.end(),
        deviceId
    );

    if (it == deviceIds.end())
    {
        return false;
    }

    deviceIds.erase(it);
    return true;
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