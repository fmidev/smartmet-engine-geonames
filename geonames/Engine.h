// ======================================================================
/*!
 * \brief Class Geo::Engine
 */
// ======================================================================

#pragma once

#include "WktGeometry.h"
#include <gis/OGR.h>
#include <spine/HTTP.h>
#include <spine/Location.h>
#include <spine/SmartMetEngine.h>
#include <spine/Table.h>
#include <spine/TableFormatter.h>
#include <spine/Thread.h>

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
}  // namespace Fmi

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
  const Spine::TaggedLocationList& locations() const { return itsLocations; }
  bool empty() const { return itsLocations.empty(); }
  Spine::TaggedLocationList::size_type size() const { return itsLocations.size(); }
  void setLocations(const Spine::TaggedLocationList& locations) { itsLocations = locations; }

 private:
  friend class Engine;
  Spine::TaggedLocationList itsLocations;

  void add(const std::string& theTag, Spine::LocationPtr theLoc)
  {
    itsLocations.push_back(Spine::TaggedLocation(theTag, theLoc));
  }

  void add(const std::string& theTag, std::unique_ptr<Spine::Location>& theLoc)
  {
    add(theTag, Spine::LocationPtr(theLoc.release()));
  }

};  // class LocationOptions

typedef std::pair<boost::shared_ptr<Spine::Table>, Spine::TableFormatter::Names> StatusReturnType;

class Engine : public Spine::SmartMetEngine
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
  mutable std::atomic<long> itsNameSearchCount;  // TODO: These should be made into std::atomics
  mutable std::atomic<long> itsLonLatSearchCount;
  mutable std::atomic<long> itsIdSearchCount;
  mutable std::atomic<long> itsKeywordSearchCount;
  mutable std::atomic<long> itsSuggestCount;
  std::string itsConfigFile;
  std::string itsErrorMessage;

 public:
  Engine(const std::string& theConfigFile);
  ~Engine() override;

  std::size_t hash_value() const;

  // Get timezone information
  const Fmi::TimeZones& getTimeZones() const { return itsTimeZones; }
  // Find location with default options

  Spine::LocationPtr nameSearch(const std::string& theName, const std::string& theLang) const;

  Spine::LocationPtr lonlatSearch(double theLongitude,
                                  double theLatitude,
                                  const std::string& theLang,
                                  double theMaxDistance = Locus::Query::default_radius) const;

  Spine::LocationPtr featureSearch(double theLongitude,
                                   double theLatitude,
                                   const std::string& theLang,
                                   const std::string& theFeatures,
                                   double theMaxDistance = Locus::Query::default_radius) const;

  Spine::LocationPtr idSearch(long theGeoID, const std::string& theLang) const;

  // Find locations with options

  Spine::LocationList nameSearch(const Locus::QueryOptions& theOptions,
                                 const std::string& theName) const;

  Spine::LocationList latlonSearch(const Locus::QueryOptions& theOptions,
                                   float theLatitude,
                                   float theLongitude,
                                   float theRadius = Locus::Query::default_radius) const;

  Spine::LocationList lonlatSearch(const Locus::QueryOptions& theOptions,
                                   float theLongitude,
                                   float theLatitude,
                                   float theRadius = Locus::Query::default_radius) const;

  Spine::LocationList idSearch(const Locus::QueryOptions& theOptions, int theId) const;

  Spine::LocationPtr keywordSearch(double theLongitude,
                                   double theLatitude,
                                   double theRadius = -1,
                                   const std::string& theLang = "fi",
                                   const std::string& theKeyword = FMINAMES_DEFAULT_KEYWORD) const;

  Spine::LocationList keywordSearch(const Locus::QueryOptions& theOptions,
                                    const std::string& theKeyword) const;

  Spine::LocationPtr wktSearch(const std::string& theWktString,
                               const std::string& theLanguage,
                               double theRadius = 0.0) const;

  // suggest alphabetical completions

  Spine::LocationList suggest(const std::string& thePattern,
                              const std::string& theLang = "fi",
                              const std::string& theKeyword = FMINAMES_DEFAULT_KEYWORD,
                              unsigned int thePage = 0,
                              unsigned int theMaxResults = fminames_default_maxresults) const;

  Spine::LocationList suggestDuplicates(
      const std::string& thePattern,
      const std::string& theLang = "fi",
      const std::string& theKeyword = FMINAMES_DEFAULT_KEYWORD,
      unsigned int thePage = 0,
      unsigned int theMaxResults = fminames_default_maxresults) const;

  std::vector<Spine::LocationList> suggest(
      const std::string& thePattern,
      const std::vector<std::string>& theLanguages,
      const std::string& theKeyword = FMINAMES_DEFAULT_KEYWORD,
      unsigned int thePage = 0,
      unsigned int theMaxResults = fminames_default_maxresults) const;

  // find name of country

  std::string countryName(const std::string& theIso2, const std::string& theLang = "fi") const;

  // Parse locations from FMISIDs,LPNNs,WMOs
  LocationOptions parseLocations(const std::vector<int>& fmisids,
                                 const std::vector<int>& lpnns,
                                 const std::vector<int>& wmos,
                                 const std::string& language) const;

  // Parse location-related HTTP options
  LocationOptions parseLocations(const Spine::HTTP::Request& theReq) const;

  // Get WKT geometries
  WktGeometries getWktGeometries(const LocationOptions& loptions,
                                 const std::string& language) const;

  // Reload all data

  bool reload();
  const std::string& errorMessage() const;

  // Return cache status info

  StatusReturnType cacheStatus() const;
  StatusReturnType metadataStatus() const;

  void sort(Spine::LocationList& theLocations) const;

  // DEM elevation for a coordinate
  boost::shared_ptr<Fmi::DEM> dem() const;

  // LandCover data
  boost::shared_ptr<Fmi::LandCover> landCover() const;

  // DEM height
  double demHeight(double theLongitude, double theLatitude) const;

  // Cover type
  Fmi::LandCover::Type coverType(double theLongitude, double theLatitude) const;

  // Has autocomplete data been initialized?
  bool isSuggestReady() const;

 protected:
  virtual void init() override;
  void shutdown() override;
  void shutdownRequestFlagSet() override;

 private:
  unsigned int maxDemResolution() const;
  void cache_cleaner();

};  // class Geo

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
