#ifndef ROOM_H
#define ROOM_H

#include <string>
#include <vector>

class Room
{
private:
    std::string name;
    std::vector<std::string> deviceIds;

public:
    explicit Room(const std::string& roomName);

    std::string getName() const;

    bool addDeviceId(const std::string& deviceId);
    bool removeDeviceId(const std::string& deviceId);

    bool hasDevice(const std::string& deviceId) const;

    const std::vector<std::string>& getDeviceIds() const;

    int getDeviceCount() const;
};

#endif