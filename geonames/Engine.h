// ======================================================================
/*!
 * \brief Class SmartMet::Geo::Engine
 */
// ======================================================================

#pragma once

#include <spine/Location.h>
#include <spine/HTTP.h>
#include <spine/Table.h>
#include <spine/TableFormatter.h>
#include <spine/Thread.h>
#include <spine/SmartMetEngine.h>

#include <macgyver/TimeZones.h>

#include <locus/Query.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>

#include <string>

#define FMINAMES_DEFAULT_KEYWORD "all"

namespace Fmi
{
class DEM;
class LandCover;
}

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
const int fminames_default_maxresults = 15;

class LocationOptions
{
 public:
  const SmartMet::Spine::TaggedLocationList& locations() const { return itsLocations; }
  bool empty() const { return itsLocations.empty(); }
  SmartMet::Spine::TaggedLocationList::size_type size() const { return itsLocations.size(); }
 private:
  friend class Engine;
  SmartMet::Spine::TaggedLocationList itsLocations;

  void add(const std::string& theTag, SmartMet::Spine::LocationPtr theLoc)
  {
    itsLocations.push_back(SmartMet::Spine::TaggedLocation(theTag, theLoc));
  }

  void add(const std::string& theTag, std::unique_ptr<SmartMet::Spine::Location>& theLoc)
  {
    add(theTag, SmartMet::Spine::LocationPtr(theLoc.release()));
  }

};  // class LocationOptions

typedef std::pair<boost::shared_ptr<SmartMet::Spine::Table>, SmartMet::Spine::TableFormatter::Names>
    StatusReturnType;

class Engine : public SmartMet::Spine::SmartMetEngine
{
 private:
  Engine();  // must give config file

  class Impl;
  boost::shared_ptr<Impl> impl;
  boost::shared_ptr<Impl> tmpImpl;
  Fmi::TimeZones itsTimeZones;
  boost::posix_time::ptime itsStartTime;
  boost::posix_time::ptime itsLastReload;
  bool itsReloading;
  mutable long itsNameSearchCount;  // TODO: These should be made into std::atomics
  mutable long itsLonLatSearchCount;
  mutable long itsIdSearchCount;
  mutable long itsKeywordSearchCount;
  mutable long itsSuggestCount;
  std::string itsConfigFile;
  std::string itsErrorMessage;

 public:
  Engine(const std::string& theConfigFile);
  ~Engine();

  std::size_t hash_value() const;

  // Get timezone information
  const Fmi::TimeZones& getTimeZones() const { return itsTimeZones; }
  // Find location with default options

  SmartMet::Spine::LocationPtr nameSearch(const std::string& theName,
                                          const std::string& theLang) const;

  SmartMet::Spine::LocationPtr lonlatSearch(
      double theLongitude,
      double theLatitude,
      const std::string& theLang,
      double theMaxDistance = Locus::Query::default_radius) const;

  SmartMet::Spine::LocationPtr idSearch(long theGeoID, const std::string& theLang) const;

  // Find locations with options

  SmartMet::Spine::LocationList nameSearch(const Locus::QueryOptions& theOptions,
                                           const std::string& theName) const;

  SmartMet::Spine::LocationList latlonSearch(const Locus::QueryOptions& theOptions,
                                             float theLatitude,
                                             float theLongitude,
                                             float theRadius = Locus::Query::default_radius) const;

  SmartMet::Spine::LocationList lonlatSearch(const Locus::QueryOptions& theOptions,
                                             float theLongitude,
                                             float theLatitude,
                                             float theRadius = Locus::Query::default_radius) const;

  SmartMet::Spine::LocationList idSearch(const Locus::QueryOptions& theOptions, int theId) const;

  SmartMet::Spine::LocationPtr keywordSearch(
      double theLongitude,
      double theLatitude,
      double theRadius = -1,
      const std::string& theLang = "fi",
      const std::string& theKeyword = FMINAMES_DEFAULT_KEYWORD) const;

  SmartMet::Spine::LocationList keywordSearch(const Locus::QueryOptions& theOptions,
                                              const std::string& theKeyword) const;

  // suggest alphabetical completions

  SmartMet::Spine::LocationList suggest(
      const std::string& thePattern,
      const std::string& theLang = "fi",
      const std::string& theKeyword = FMINAMES_DEFAULT_KEYWORD,
      unsigned int thePage = 0,
      unsigned int theMaxResults = fminames_default_maxresults) const;

  // find name of country

  std::string countryName(const std::string& theIso2, const std::string& theLang = "fi") const;

  // Parse location-related HTTP options
  LocationOptions parseLocations(const SmartMet::Spine::HTTP::Request& theReq) const;

  // reload all data

  bool reload();
  const std::string& errorMessage() const;

  // Return cache status info

  StatusReturnType cacheStatus() const;
  StatusReturnType metadataStatus() const;

  void sort(SmartMet::Spine::LocationList& theLocations) const;

  // DEM elevation for a coordinate
  boost::shared_ptr<Fmi::DEM> dem() const;

  // LandCover data
  boost::shared_ptr<Fmi::LandCover> landCover() const;

  // Has autocomplete data been initialized?
  bool isSuggestReady() const;

 protected:
  virtual void init();
  void shutdown();
  void shutdownRequestFlagSet();

 private:
  unsigned int maxDemResolution() const;
  void cache_cleaner();

};  // class Geo

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
