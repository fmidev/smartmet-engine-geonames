// ======================================================================
/*!
 * \brief Class Geo::Engine
 */
// ======================================================================

#pragma once

#include "WktGeometry.h"
#include <boost/asio.hpp>
#include <boost/utility.hpp>
#include <gis/OGR.h>
#include <locus/Query.h>
#include <macgyver/AtomicSharedPtr.h>
#include <macgyver/CacheStats.h>
#include <macgyver/DateTime.h>
#include <macgyver/TimeZones.h>
#include <spine/HTTP.h>
#include <spine/Location.h>
#include <spine/SmartMetEngine.h>
#include <spine/Table.h>
#include <spine/TableFormatter.h>
#include <spine/Thread.h>
#include <atomic>
#include <functional>
#include <memory>
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

  void add(const std::string& theTag, const Spine::LocationPtr& theLoc);
  void add(const std::string& theTag, std::unique_ptr<Spine::Location>& theLoc);

 private:
  Spine::TaggedLocationList itsLocations;

};  // class LocationOptions

using StatusReturnType = std::unique_ptr<Spine::Table>;

class Engine : public Spine::SmartMetEngine
{
 private:
  class Impl;
  Fmi::AtomicSharedPtr<Impl> impl;
  std::shared_ptr<Impl> tmpImpl;
  Fmi::TimeZones itsTimeZones;
  Fmi::DateTime itsStartTime;
  Fmi::DateTime itsLastReload;
  std::atomic<bool> itsReloading;
  mutable std::atomic<long> itsNameSearchCount;  // TODO: These should be made into std::atomics
  mutable std::atomic<long> itsLonLatSearchCount;
  mutable std::atomic<long> itsIdSearchCount;
  mutable std::atomic<long> itsKeywordSearchCount;
  mutable std::atomic<long> itsSuggestCount;
  std::string itsConfigFile;
  std::string itsErrorMessage;
  std::atomic_bool initFailed;

 public:
  explicit Engine(std::string theConfigFile);
  ~Engine() override;

  Engine() = delete;
  Engine(const Engine& other) = delete;
  Engine& operator=(const Engine& other) = delete;
  Engine(Engine&& other) = delete;
  Engine& operator=(Engine&& other) = delete;

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
                              const std::function<bool(const Spine::LocationPtr&)>& thePredicate,
                              const std::string& theLang = "fi",
                              const std::string& theKeyword = FMINAMES_DEFAULT_KEYWORD,
                              unsigned int thePage = 0,
                              unsigned int theMaxResults = fminames_default_maxresults) const;

  Spine::LocationList suggestDuplicates(
      const std::string& thePattern,
      const std::function<bool(const Spine::LocationPtr&)>& thePredicate,
      const std::string& theLang = "fi",
      const std::string& theKeyword = FMINAMES_DEFAULT_KEYWORD,
      unsigned int thePage = 0,
      unsigned int theMaxResults = fminames_default_maxresults) const;

  std::vector<Spine::LocationList> suggest(
      const std::string& thePattern,
      const std::function<bool(const Spine::LocationPtr&)>& thePredicate,
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

  std::pair<bool, std::string> reload();

  const std::string& errorMessage() const;

  // Return cache status info

  StatusReturnType cacheStatus() const;
  StatusReturnType metadataStatus() const;

  void sort(Spine::LocationList& theLocations) const;

  // DEM elevation for a coordinate
  std::shared_ptr<Fmi::DEM> dem() const;

  // LandCover data
  std::shared_ptr<Fmi::LandCover> landCover() const;

  // DEM height
  double demHeight(double theLongitude, double theLatitude) const;

  // Cover type
  Fmi::LandCover::Type coverType(double theLongitude, double theLatitude) const;

  // Has autocomplete data been initialized?
  bool isSuggestReady() const;

  void assign_priorities(Spine::LocationList& locs) const;

 protected:
  void init() override;
  void shutdown() override;

 private:
  unsigned int maxDemResolution() const;
  void cache_cleaner();
  Fmi::Cache::CacheStatistics getCacheStats() const override;
  Spine::LocationPtr translateLocation(const Spine::Location& theLocation,
                                       const std::string& theLang) const;

  void parse_place(LocationOptions& theOptions,
                   const Spine::HTTP::Request& theRequest,
                   const std::string& theLanguage) const;
  void parse_places(LocationOptions& theOptions,
                    const Spine::HTTP::Request& theRequest,
                    const std::string& theLanguage) const;
  void parse_lonlat(LocationOptions& theOptions,
                    const Spine::HTTP::Request& theRequest,
                    const std::string& theLanguage,
                    const std::string& theFeatures,
                    double theMaxDistance) const;
  void parse_lonlats(LocationOptions& theOptions,
                     const Spine::HTTP::Request& theRequest,
                     const std::string& theLanguage,
                     const std::string& theFeatures,
                     double theMaxDistance) const;
  void parse_latlon(LocationOptions& theOptions,
                    const Spine::HTTP::Request& theRequest,
                    const std::string& theLanguage,
                    const std::string& theFeatures,
                    double theMaxDistance) const;
  void parse_latlons(LocationOptions& theOptions,
                     const Spine::HTTP::Request& theRequest,
                     const std::string& theLanguage,
                     const std::string& theFeatures,
                     double theMaxDistance) const;
  void parse_geoid(LocationOptions& theOptions,
                   const Spine::HTTP::Request& theRequest,
                   const std::string& theLanguage) const;
  void parse_geoids(LocationOptions& theOptions,
                    const Spine::HTTP::Request& theRequest,
                    const std::string& theLanguage) const;
  void parse_keyword(LocationOptions& theOptions,
                     const Spine::HTTP::Request& theRequest,
                     const std::string& theLanguage) const;
  void parse_wkt(LocationOptions& theOptions,
                 const Spine::HTTP::Request& theRequest,
                 const std::string& theLanguage,
                 const std::string& theFeatures,
                 double theMaxDistance) const;

  void requestReload(SmartMet::Spine::HTTP::Response& theResponse);

  std::unique_ptr<Spine::Table> requestInfo(const SmartMet::Spine::HTTP::Request& request) const;

  // Autoreload functionality related members
  boost::asio::io_context itsIoService;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> itsWork;
  boost::asio::basic_waitable_timer<std::chrono::steady_clock> itsTimer;

  void runIoService();  // Run the io_service event loop (separate method to see it in GDB backtrace
                        // when needed)

  void maybeScheduleAutoReloadCheck();

  void autoReloadCheck();

  boost::thread itsIoServiceThread;
};  // class Geo

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
