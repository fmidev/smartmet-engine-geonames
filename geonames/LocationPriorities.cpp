#include "LocationPriorities.h"
#include <macgyver/Exception.h>
#include <cmath>

using namespace SmartMet::Engine::Geonames;
using namespace SmartMet::Spine;

namespace
{
void readPriorityMap(const std::string& part_name,
                     const libconfig::Config& config,
                     std::map<std::string, int>& priomap)
try
{
  std::string name = "priorities." + part_name;

  if (!config.exists(name))
    return;

  const libconfig::Setting& tmp = config.lookup(name);

  if (!tmp.isGroup())
  {
    Fmi::Exception exception(BCP, "Configured value of '" + name + "' must be a group!");
    throw exception;
  }

  for (int i = 0; i < tmp.getLength(); ++i)
  {
    std::string varname = tmp[i].getName();
    int value = tmp[i];
    priomap[varname] = value;
  }
}
catch (const libconfig::SettingException& e)
{
  Fmi::Exception exception(BCP, "Configuration file setting error!");
  exception.addParameter("Path", e.getPath());
  exception.addParameter("Error description", e.what());
  throw exception;
}
catch (...)
{
  throw Fmi::Exception::Trace(BCP, "Operation failed!");
}
}  // namespace

LocationPriorities::LocationPriorities() = default;
LocationPriorities::~LocationPriorities() = default;

int LocationPriorities::getPriority(const Location& loc) const
{
  try
  {
    int priority = 0;
    priority += populationPriority(loc);
    priority += areaPriority(loc);
    priority += countryPriority(loc);
    priority += featurePriority(loc);
    return priority;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void LocationPriorities::setPopulationPriorities(const std::string& iso2, int div)
{
  try
  {
    itsPopulationPriorities[iso2] = div;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void LocationPriorities::setAreaPriorities(const std::string& area, int prty)
{
  try
  {
    itsAreaPriorities[area] = prty;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void LocationPriorities::setCountryPriorities(const std::string& iso2, int prty)
{
  try
  {
    itsCountryPriorities[iso2] = prty;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void LocationPriorities::setFeaturePriorities(const std::string& iso2,
                                              const std::string& feature,
                                              int prty)
{
  try
  {
    itsFeaturePriorities[iso2][feature] = prty;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void LocationPriorities::setFeaturePriorities(const std::string& iso2,
                                              std::map<std::string, int> prtyMap)
{
  try
  {
    itsFeaturePriorities[iso2] = std::move(prtyMap);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int LocationPriorities::populationPriority(const Location& loc) const
{
  try
  {
    auto it = itsPopulationPriorities.find(loc.iso2);
    if (it != itsPopulationPriorities.end())
      return lround(1.0 * priority_scale * loc.population / it->second);

    it = itsPopulationPriorities.find("default");
    if (it != itsPopulationPriorities.end())
      return lround(1.0 * priority_scale * loc.population / it->second);

    return 0;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int LocationPriorities::areaPriority(const Location& loc) const
{
  try
  {
    auto it = itsAreaPriorities.find(loc.area);
    if (it != itsAreaPriorities.end())
      return it->second * priority_scale;

    it = itsAreaPriorities.find("default");
    if (it != itsAreaPriorities.end())
      return it->second * priority_scale;

    return 0;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int LocationPriorities::countryPriority(const Location& loc) const
{
  try
  {
    auto it = itsCountryPriorities.find(loc.iso2);
    if (it != itsCountryPriorities.end())
      return it->second * priority_scale;

    it = itsCountryPriorities.find("default");
    if (it != itsCountryPriorities.end())
      return it->second * priority_scale;

    return 0;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int LocationPriorities::featurePriority(const Location& loc) const
{
  try
  {
    auto it = itsFeaturePriorities.find(loc.iso2);
    if (it == itsFeaturePriorities.end())
      it = itsFeaturePriorities.find("default");

    if (it == itsFeaturePriorities.end())
      return 0;

    const auto& priomap = it->second;

    auto jt = priomap.find(loc.feature);

    if (jt != priomap.end())
      return jt->second * priority_scale;

    jt = priomap.find("default");
    if (jt != priomap.end())
      return jt->second * priority_scale;

    return 0;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void LocationPriorities::init(const libconfig::Config& config)
try
{
  if (!config.exists("priorities"))
    return;

  readPriorityMap("populations", config, itsPopulationPriorities);
  readPriorityMap("areas", config, itsAreaPriorities);
  readPriorityMap("countries", config, itsCountryPriorities);

  if (!config.exists("priorities.features"))
    return;

  const libconfig::Setting& tmp = config.lookup("priorities.features");

  if (!tmp.isGroup())
  {
    Fmi::Exception exception(BCP, "Configured value of 'priorities.features' must be a group!");
    throw exception;
  }

  for (int i = 0; i < tmp.getLength(); ++i)
  {
    std::string countryname = tmp[i].getName();
    std::string featurename = tmp[i];

    std::string mapname = "priorities." + featurename;

    if (!config.exists(mapname))
    {
      Fmi::Exception exception(BCP, "Configuration of '" + mapname + "' is missing!");
      throw exception;
    }

    const libconfig::Setting& tmpmap = config.lookup(mapname);

    for (int j = 0; j < tmpmap.getLength(); ++j)
    {
      std::string name = tmpmap[j].getName();
      int value = tmpmap[j];
      itsFeaturePriorities[countryname][name] = value;
    }
  }
}
catch (const libconfig::SettingException& e)
{
  Fmi::Exception exception(BCP, "Configuration file setting error!");
  exception.addParameter("Path", e.getPath());
  exception.addParameter("Error description", e.what());
  throw exception;
}
catch (...)
{
  throw Fmi::Exception::Trace(BCP, "Operation failed!");
}
