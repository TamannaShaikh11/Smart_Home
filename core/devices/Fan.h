#ifndef FAN_H
#define FAN_H

#include "Appliance.h"

class Fan : public Appliance {
private:
    int speed;

public:
    Fan(
        const std::string& id,
        const std::string& name,
        const std::string& room,
        int powerWatts = 75
    );

    void setSpeed(int speed);
    int getSpeed() const;

    void showStatus() const override;
};

#endif