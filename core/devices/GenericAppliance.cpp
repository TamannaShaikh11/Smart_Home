#include "GenericAppliance.h"

#include <utility>

GenericAppliance::GenericAppliance(
    std::string id,
    std::string name,
    std::string room,
    int powerWatts,
    std::string type
)
    : Appliance(
          std::move(id),
          std::move(name),
          std::move(room),
          powerWatts
      ),
      customType(std::move(type))
{
}

std::string GenericAppliance::getType() const
{
    return customType;
}
