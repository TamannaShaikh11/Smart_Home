#ifndef APPLIANCE_H
#define APPLIANCE_H

#include <string>
#include <ctime>

#include "../state/DeviceState.h"
#include "../state/DeviceLifetime.h"

class Appliance
{
protected:
    std::string id;
    std::string name;
    std::string room;
    DeviceState state;

    DeviceLifetime lifetime;
    std::time_t expiryTime;

public:
    Appliance(
        std::string id,
        std::string name,
        std::string room,
        int powerWatts
    );

    virtual ~Appliance();

    virtual void turnOn();
    virtual void turnOff();
    virtual void showStatus() const;

    std::string getId() const;
    std::string getName() const;
    std::string getRoom() const;

    DeviceStatus getStatus() const;
    int getPowerWatts() const;
    int getRuntimeMinutes() const;

    const DeviceState& getState() const;
    virtual std::string getType() const;
    void restoreState(DeviceStatus savedStatus, int savedRuntimeMinutes);
    void setRoom(const std::string& newRoom);

    void setLifetime(
        DeviceLifetime deviceLifetime,
        std::time_t expiry
    );

    DeviceLifetime getLifetime() const;
    std::time_t getExpiryTime() const;
};

#endif