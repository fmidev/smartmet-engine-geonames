// ======================================================================
/*!
 * \brief Implementation details
 */
// ======================================================================

#pragma once

#include "Engine.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/locale.hpp>
#include <boost/locale/collator.hpp>
#include <boost/move/unique_ptr.hpp>
#include <boost/thread.hpp>
#include <gis/LandCover.h>
#include <macgyver/Cache.h>
#include <macgyver/Geometry.h>
#include <macgyver/NearTree.h>
#include <macgyver/TernarySearchTree.h>
#include <macgyver/TimedCache.h>
#include <cmath>
#include <libconfig.h++>
#include <list>
#include <map>
#include <memory>
#include <string>

namespace Fmi
{
class DEM;
}

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
// ----------------------------------------------------------------------
/*!
 * \brief NearTree distance calculations for Spine::LocationPtr
 */
// ----------------------------------------------------------------------

struct LocationPtrDistance
{
  double operator()(Spine::LocationPtr loc1, Spine::LocationPtr loc2) const
  {
    return Fmi::Geometry::GeoDistance(
               loc1->longitude, loc1->latitude, loc2->longitude, loc2->latitude) /
           1000.0;
  }
};

// ----------------------------------------------------------------------
/*!
 * \brief Implementation hiding details for Locus
 */
// ----------------------------------------------------------------------

class Engine::Impl : private boost::noncopyable
{
 public:
  typedef std::map<int, std::string> Municipalities;  // kunnat

  typedef std::map<std::string, std::string> Translations;

  typedef std::map<std::string, std::string> Countries;  // valtiot
  typedef std::map<std::string, Translations> AlternateCountries;

  typedef std::map<Spine::GeoId, Translations> AlternateNames;  // localized names
  typedef std::map<int, Translations> AlternateMunicipalities;

  typedef std::map<Spine::GeoId, Spine::LocationPtr*> GeoIdMap;   // map from
                                                                  // geoid to
                                                                  // location
  typedef std::map<std::string, Spine::LocationList> KeywordMap;  // geoids belonging to
                                                                  // keywords

  typedef Fmi::NearTree<Spine::LocationPtr, LocationPtrDistance> GeoTree;
  typedef GeoTree* GeoTreePtr;
  typedef std::map<std::string, GeoTreePtr> GeoTreeMap;  // nearest point searches

  // default name search trees per keyword
  typedef Fmi::TernarySearchTree<const Spine::Location> TernaryTree;
  typedef boost::shared_ptr<TernaryTree> TernaryTreePtr;
  typedef std::map<std::string, TernaryTreePtr> TernaryTreeMap;

  // alternate language search trees per language per keyword
  typedef boost::shared_ptr<TernaryTreeMap> TernaryTreeMapPtr;
  typedef std::map<std::string, TernaryTreeMapPtr> LangTernaryTreeMap;

  // From search hash key to result
  typedef Fmi::Cache::Cache<std::size_t, Spine::LocationList> NameSearchCache;

  // Suggest cache
  using SuggestCache = Fmi::TimedCache::Cache<std::size_t, Spine::LocationList>;
  boost::movelib::unique_ptr<SuggestCache> itsSuggestCache;

 public:
  Impl(std::string configfile, bool reloading);
  ~Impl();

  void init(bool first_construction);

  std::size_t hash_value() const;

  // DEM elevation for a coordinate
  boost::shared_ptr<Fmi::DEM> dem() const;
  boost::shared_ptr<Fmi::LandCover> landCover() const;

  double elevation(double lon, double lat) const;
  double elevation(double lon, double lat, unsigned int maxdemresolution) const;
  unsigned int maxDemResolution() const { return itsMaxDemResolution; }
  Fmi::LandCover::Type coverType(double lon, double lat) const;

  Spine::LocationList suggest(const std::string& pattern,
                              const std::string& lang,
                              const std::string& keyword,
                              unsigned int page,
                              unsigned int maxresults) const;

  Spine::LocationList name_search(const Locus::QueryOptions& theOptions,
                                  const std::string& theName);

  Spine::LocationList lonlat_search(const Locus::QueryOptions& theOptions,
                                    float theLongitude,
                                    float theLatitude,
                                    float theRadius);

  Spine::LocationList id_search(const Locus::QueryOptions& theOptions, int theId);

  Spine::LocationList keyword_search(const Locus::QueryOptions& theOptions,
                                     const std::string& theKeyword);

  // Priority sort of locations
  void sort(Spine::LocationList& theLocations) const;

  void translate(Spine::LocationPtr& loc, const std::string& lang) const;

  void translate(Spine::LocationList& locs, const std::string& lang) const;

  std::string translate_country(const std::string& iso2, const std::string& lang = "fi") const;

  bool prioritySortPtr(Spine::LocationPtr* a, Spine::LocationPtr* b) const;
  bool prioritySort(const Spine::LocationPtr& a, const Spine::LocationPtr& b) const;

  Spine::LocationList to_locationlist(const Locus::Query::return_type& theList);

  void name_cache_status(boost::shared_ptr<Spine::Table> tablePtr,
                         Spine::TableFormatter::Names& theNames);

  void shutdown();
  void shutdownRequestFlagSet();

  bool isSuggestReady() const;

  bool itsReady;
  bool itsReloading;
  bool itsReloadOK;
  std::string itsReloadError;
  GeoTreeMap itsGeoTrees;

 private:
  // Remove underscores if requested and so on
  std::string preprocess_name(const std::string& name) const;

  // Convert an autocomplete name to all possible partial unaccented matches
  std::list<std::string> to_treewords(const std::string& name, const std::string& area) const;

  // Convert an autocomplete name to all possible unaccented matches
  std::string to_treeword(const std::string& name) const;
  std::string to_treeword(const std::string& name, const std::string& area) const;
  std::string to_language(const std::string& lang) const;

  void translate_name(Spine::Location& loc, const std::string& lang) const;
  void translate_area(Spine::Location& loc, const std::string& lang) const;

  void initSuggest(bool threaded);
  void initDEM();
  void initLandCover();

  const libconfig::Setting& lookup_database(const std::string& setting,
                                            const std::string& name) const;
  bool itsVerbose;
  bool itsDatabaseDisabled = false;
  bool itsMockEngine;
  bool itsRemoveUnderscores;
  const std::string itsConfigFile;
  libconfig::Config itsConfig;

  Spine::LocationList itsLocations;

  Countries itsCountries;
  AlternateCountries itsAlternateCountries;
  Municipalities itsMunicipalities;
  AlternateNames itsAlternateNames;
  AlternateMunicipalities itsAlternateMunicipalities;
  GeoIdMap itsGeoIdMap;
  KeywordMap itsKeywords;
  TernaryTreeMap itsTernaryTrees;
  LangTernaryTreeMap itsLangTernaryTreeMap;

  // priority info

  typedef std::map<std::string, int> Priorities;
  typedef std::map<std::string, Priorities> FeaturePriorities;

  int itsNameMatchPriority = 50;
  Priorities itsPopulationPriorities;
  Priorities itsAreaPriorities;
  Priorities itsCountryPriorities;
  FeaturePriorities itsFeaturePriorities;

  bool itsSuggestReadyFlag = false;

  // caches

 public:
  NameSearchCache itsNameSearchCache;

 private:
  // locale handling for autocomplete

  typedef boost::locale::collator<char> Collator;

  boost::locale::generator itsLocaleGenerator;
  std::locale itsLocale;
  const Collator* itsCollator = nullptr;  // perhaps should delete in destructor?

  // DEM data
  boost::shared_ptr<Fmi::DEM> itsDEM;
  unsigned int itsMaxDemResolution = 0;  // allow highest possible resolution

  // LandCover data
  boost::shared_ptr<Fmi::LandCover> itsLandCover;

  // Hash value
  std::size_t itsHashValue;

  bool itsShutdownRequested;

  // We store these as data members to avoid calling libconfig which
  // uses exceptions for normal control flow

  std::string itsUser;
  std::string itsHost;
  std::string itsPass;
  std::string itsDatabase;
  std::string itsPort;

 private:
  Impl();
  bool handleShutDownRequest();

  void read_config();
  void read_config_priorities();
  void read_config_prioritymap(const std::string& partname, Priorities& priomap);

  void read_database_hash_value(Locus::Connection& conn);

  void read_countries(Locus::Connection& conn);
  void read_alternate_countries(Locus::Connection& conn);
  void read_municipalities(Locus::Connection& conn);
  void read_alternate_geonames(Locus::Connection& conn);
  void read_alternate_municipalities(Locus::Connection& conn);
  void read_geonames(Locus::Connection& conn);
  void assign_priorities(Spine::LocationList& locs) const;
  int population_priority(const Spine::Location& loc) const;
  int area_priority(const Spine::Location& loc) const;
  int country_priority(const Spine::Location& loc) const;
  int feature_priority(const Spine::Location& loc) const;

  void build_geoid_map();
  void read_keywords(Locus::Connection& conn);

  void build_geotrees();
  void build_ternarytrees();
  void build_lang_ternarytrees();
  void build_lang_ternarytrees_all();
  void build_lang_ternarytrees_keywords();

  std::size_t cache_key(const std::string& pattern,
                        const std::string& lang,
                        const std::string& keyword,
                        unsigned int page,
                        unsigned int maxresults) const;

};  // Impl

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
