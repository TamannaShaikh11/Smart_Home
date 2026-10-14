#ifndef GENERICAPPLIANCE_H
#define GENERICAPPLIANCE_H

#include "Appliance.h"

#include <string>

class GenericAppliance : public Appliance
{
private:
    std::string customType;

public:
    GenericAppliance(
        std::string id,
        std::string name,
        std::string room,
        int powerWatts,
        std::string type
    );

    std::string getType() const override;
};

#endif