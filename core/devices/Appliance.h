#ifndef APPLIANCE_H
#define APPLIANCE_H

#include <string>
#include "../state/DeviceState.h"

class Appliance {
protected:
    std::string id;
    std::string name;
    std::string room;
    DeviceState state;

public:
    Appliance(
        const std::string& id,
        const std::string& name,
        const std::string& room,
        int powerWatts
    );

    virtual ~Appliance();

    virtual void turnOn();
    virtual void turnOff();

    virtual void showStatus() const;

    const std::string& getId() const;
    const std::string& getName() const;
    const std::string& getRoom() const;

    DeviceStatus getStatus() const;
    int getPowerWatts() const;

    DeviceState getState() const;
};

#endif