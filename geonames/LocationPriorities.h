#pragma once

#include <spine/Location.h>
#include <libconfig.h++>
#include <map>

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{

/**
 * @brief Class for calculating location priorities to be used for autocomplete
 */
class LocationPriorities
{
 public:
  constexpr static int priority_scale = 1000;

  /**
   * @brief Constructor: creates empty object LocationPriorities object
   *
   * One can initialize the object with init() method or add priorities
   * using setPopulationPriorities(), setAreaPriorities(), setCountryPriorities()
   * and setFeaturePriorities() methods.
   */
  LocationPriorities();
  virtual ~LocationPriorities();

  void init(const libconfig::Config& config);

  /**
   * @brief Get priority for a location
   */
  int getPriority(const SmartMet::Spine::Location& loc) const;

  void setPopulationPriorities(const std::string& iso2, int div);
  void setAreaPriorities(const std::string& area, int prty);
  void setCountryPriorities(const std::string& iso2, int prty);
  void setFeaturePriorities(const std::string& iso2, const std::string& feature, int prty);
  void setFeaturePriorities(const std::string& iso2, std::map<std::string, int> prtyMap);

 private:
  int populationPriority(const SmartMet::Spine::Location& loc) const;
  int areaPriority(const SmartMet::Spine::Location& loc) const;
  int countryPriority(const SmartMet::Spine::Location& loc) const;
  int featurePriority(const SmartMet::Spine::Location& loc) const;

  std::map<std::string, int> itsPopulationPriorities;
  std::map<std::string, int> itsAreaPriorities;
  std::map<std::string, int> itsCountryPriorities;
  std::map<std::string, std::map<std::string, int>> itsFeaturePriorities;
};

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet
