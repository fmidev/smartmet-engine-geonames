// ======================================================================
/*!
 * \brief Implementation details
 */
// ======================================================================

#pragma once

#include "Engine.h"

#include <boost/atomic.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/locale.hpp>
#include <boost/locale/collator.hpp>
#include <boost/move/unique_ptr.hpp>
#include <boost/thread.hpp>
#include <gis/LandCover.h>
#include <macgyver/AsyncTaskGroup.h>
#include <macgyver/Cache.h>
#include <macgyver/CharsetConverter.h>
#include <macgyver/Geometry.h>
#include <macgyver/NearTree.h>
#include <macgyver/PostgreSQLConnection.h>
#include <macgyver/TernarySearchTree.h>
#include <macgyver/TimedCache.h>

#include <cmath>
#include <iconv.h>
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
  using Municipalities = std::map<int, std::string>;

  using Translations = std::map<std::string, std::string>;

  using Countries = std::map<std::string, std::string>;
  using AlternateCountries = std::map<std::string, Translations>;

  using AlternateNames = std::map<Spine::GeoId, Translations>;
  using AlternateMunicipalities = std::map<int, Translations>;

  using GeoIdMap = std::map<Spine::GeoId, Spine::LocationPtr*>;   // map from
                                                                  // geoid to
                                                                  // location
  using KeywordMap = std::map<std::string, Spine::LocationList>;  // geoids belonging to
                                                                  // keywords

  using GeoTree = Fmi::NearTree<Spine::LocationPtr, LocationPtrDistance>;
  using GeoTreePtr = boost::movelib::unique_ptr<GeoTree>;
  using GeoTreeMap = std::map<std::string, GeoTreePtr>;  // nearest point searches

  // default name search trees per keyword
  using TernaryTree = Fmi::TernarySearchTree<const Spine::Location>;
  using TernaryTreePtr = boost::shared_ptr<TernaryTree>;
  using TernaryTreeMap = std::map<std::string, TernaryTreePtr>;

  // alternate language search trees per language per keyword
  using TernaryTreeMapPtr = boost::shared_ptr<TernaryTreeMap>;
  using LangTernaryTreeMap = std::map<std::string, TernaryTreeMapPtr>;

  // From search hash key to result
  using NameSearchCache = Fmi::Cache::Cache<std::size_t, Spine::LocationList>;

  // Suggest cache
  using SuggestCache = Fmi::TimedCache::Cache<std::size_t, Spine::LocationList>;
  boost::movelib::unique_ptr<SuggestCache> itsSuggestCache;

  using LanguagesSuggestCache =
      Fmi::TimedCache::Cache<std::size_t, std::vector<Spine::LocationList>>;
  boost::movelib::unique_ptr<LanguagesSuggestCache> itsLanguagesSuggestCache;

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
                              unsigned int maxresults,
                              bool duplicates) const;

  std::vector<Spine::LocationList> suggest(const std::string& pattern,
                                           const std::vector<std::string>& languages,
                                           const std::string& keyword,
                                           unsigned int page,
                                           unsigned int maxresults,
                                           bool duplicates) const;

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

  void name_cache_status(const boost::shared_ptr<Spine::Table>& tablePtr,
                         Spine::TableFormatter::Names& theNames);

  void shutdown();
  void shutdownRequestFlagSet();

  bool isSuggestReady() const;

  bool itsReady = false;
  bool itsReloading = false;
  bool itsReloadOK = false;
  std::string itsReloadError;
  GeoTreeMap itsGeoTrees;

 private:
  // Remove underscores if requested and so on
  std::string preprocess_name(const std::string& name) const;

  // Convert an autocomplete name to all possible partial unaccented matches
  std::set<std::string> to_treewords(const std::string& name, const std::string& area) const;
  void add_treewords(std::set<std::string>& words,
                     const std::string& name,
                     const std::string& area) const;
  std::string iconvName(const std::string& name) const;

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
  bool itsVerbose = false;
  bool itsDatabaseDisabled = false;
  bool itsAutocompleteDisabled = false;
  bool itsStrict = true;
  bool itsRemoveUnderscores = false;
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

  using Priorities = std::map<std::string, int>;
  using FeaturePriorities = std::map<std::string, Priorities>;

  int itsNameMatchPriority = 50;
  Priorities itsPopulationPriorities;
  Priorities itsAreaPriorities;
  Priorities itsCountryPriorities;
  FeaturePriorities itsFeaturePriorities;

  boost::atomic<bool> itsSuggestReadyFlag{false};

  // caches

 public:
  NameSearchCache itsNameSearchCache;

 private:
  // locale handling for autocomplete

  using Collator = boost::locale::collator<char>;

  boost::locale::generator itsLocaleGenerator;

  std::locale itsLocale;
  const Collator* itsCollator = nullptr;  // perhaps should delete in destructor?

  bool itsAsciiAutocomplete = false;
  std::unique_ptr<Fmi::CharsetConverter> utf8_to_latin1;

  /// Converters to UTF-8 from possible fallback charsets
  std::vector<std::shared_ptr<Fmi::CharsetConverter>> fallback_converters;

  // DEM data
  boost::shared_ptr<Fmi::DEM> itsDEM;
  unsigned int itsMaxDemResolution = 0;  // allow highest possible resolution

  // LandCover data
  boost::shared_ptr<Fmi::LandCover> itsLandCover;

  // Hash value
  std::size_t itsHashValue = 0;

  bool itsShutdownRequested = false;

  // We store these as data members to avoid calling libconfig which
  // uses exceptions for normal control flow

  std::string itsUser;
  std::string itsHost;
  std::string itsPass;
  std::string itsDatabase;
  std::string itsPort;

  Fmi::AsyncTaskGroup tg1;
  boost::shared_ptr<Fmi::AsyncTask> initSuggestTask;

  Impl();
  bool handleShutDownRequest();

  void read_config();
  void read_config_priorities();
  void read_config_prioritymap(const std::string& partname, Priorities& priomap);
  void setup_fallback_encodings();

  void read_database_hash_value(Fmi::Database::PostgreSQLConnection& conn);

  void read_countries(Fmi::Database::PostgreSQLConnection& conn);
  void read_alternate_countries(Fmi::Database::PostgreSQLConnection& conn);
  void read_municipalities(Fmi::Database::PostgreSQLConnection& conn);
  void read_alternate_geonames(Fmi::Database::PostgreSQLConnection& conn);
  void read_alternate_municipalities(Fmi::Database::PostgreSQLConnection& conn);
  void read_geonames(Fmi::Database::PostgreSQLConnection& conn);
  void assign_priorities(Spine::LocationList& locs) const;
  int population_priority(const Spine::Location& loc) const;
  int area_priority(const Spine::Location& loc) const;
  int country_priority(const Spine::Location& loc) const;
  int feature_priority(const Spine::Location& loc) const;

  void build_geoid_map();
  void read_keywords(Fmi::Database::PostgreSQLConnection& conn);

  void build_geotrees();
  void build_ternarytrees();
  void build_lang_ternarytrees();
  void build_lang_ternarytrees_all();
  void build_lang_ternarytrees_keywords();

};  // Impl

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
