// ======================================================================
/*!
 * SmartMet fminames database in memory
 */
// ======================================================================

#include "Impl.h"
#include "Engine.h"
#include <boost/algorithm/string/erase.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/functional/hash.hpp>
#include <boost/thread.hpp>
#include <gis/DEM.h>
#include <gis/LandCover.h>
#include <macgyver/StringConversion.h>
#include <spine/Exception.h>
#include <spine/Location.h>
#include <sys/types.h>
#include <cassert>
#include <cmath>
#include <csignal>
#include <sstream>
#include <stdexcept>
#include <string>

const int default_port = 5432;

// We'd prefer priority to be a float, but that would require changing Spine::Location.
// We don't want to break the ABI, but to get finer control over population sort we
// scale all scores by this number.

const int priority_scale = 1000;

#ifndef NDEBUG

void print(const SmartMet::Spine::LocationPtr &ptr)
{
  try
  {
    if (!ptr)
      std::cout << "No location to print" << std::endl;
    else
    {
      std::cout << "Geoid:\t" << ptr->geoid << std::endl
                << "Name:\t" << ptr->name << std::endl
                << "Feature:\t" << ptr->feature << std::endl
                << "ISO2:\t" << ptr->iso2 << std::endl
                << "Area:\t" << ptr->area << std::endl
                << "Lon:\t" << ptr->longitude << std::endl
                << "Lat:\t" << ptr->latitude << std::endl
                << "TZ:\t" << ptr->timezone << std::endl
                << "Popu:\t" << ptr->population << std::endl
                << "Elev:\t" << ptr->elevation << std::endl
                << "DEM:\t" << ptr->dem << std::endl
                << "Priority:\t" << ptr->priority << std::endl;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

void print(const std::list<SmartMet::Spine::LocationPtr *> &ptrs)
{
  try
  {
    for (const SmartMet::Spine::LocationPtr *ptr : ptrs)
    {
      print(*ptr);
      std::cout << std::endl;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception::Trace(BCP, "Operation failed!");
  }
}
#endif

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
// ----------------------------------------------------------------------
/*!
 * \brief Impl destructor
 */
// ----------------------------------------------------------------------

Engine::Impl::~Impl()
{
  for (auto &pp : itsGeoTrees)
    delete pp.second;
}

// ----------------------------------------------------------------------
/*!
 * \brief Impl constructor
 *
 * 1. Read configfile
 * 2. Read locations from DB
 * 3. Read keywords from DB
 * 4. For each keyword and the full db
 *    a) Construct map of geoids
 *    b) Construct neartree of locations using geoid map
 *    c) Construct autocomplete search tree using geoid map
 * 5. Construct map from keywords to above constructs
 */
// ----------------------------------------------------------------------

Engine::Impl::Impl(std::string configfile, bool reloading)
    : itsReady(false),
      itsReloading(reloading),
      itsReloadOK(false),
      itsVerbose(false),
      itsMockEngine(false),
      itsRemoveUnderscores(false),
      itsConfigFile(std::move(configfile)),
      itsHashValue(0),
      itsShutdownRequested(false)
{
  try
  {
    // Configuration

    read_config();

    try
    {
      // Cache settings
      unsigned int cacheMaxSize = 1000;
      itsConfig.lookupValue("cache.max_size", cacheMaxSize);
      itsNameSearchCache.resize(cacheMaxSize);

      // Suggest cache settings
      unsigned int suggestCacheSize = 10000;
      itsConfig.lookupValue("cache.suggest_max_size", suggestCacheSize);
      itsSuggestCache = boost::movelib::make_unique<SuggestCache>(suggestCacheSize);

      // Establish collator

      const libconfig::Setting &locale = itsConfig.lookup("locale");
      itsLocale = itsLocaleGenerator(locale);
      itsCollator = &std::use_facet<Collator>(itsLocale);
    }
    catch (const libconfig::SettingException &e)
    {
      throw Spine::Exception(BCP, "Configuration file setting error!")
          .addParameter("Path", e.getPath())
          .addParameter("Configuration file", itsConfigFile)
          .addParameter("Error description", e.what());
    }
  }

  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Constructor failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the global DEM
 */
// ----------------------------------------------------------------------

boost::shared_ptr<Fmi::DEM> Engine::Impl::dem() const
{
  return itsDEM;
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the height at the given coordinate
 */
// ----------------------------------------------------------------------

double Engine::Impl::elevation(double lon, double lat) const
{
  try
  {
    if (!itsDEM)
      return std::numeric_limits<double>::quiet_NaN();

    return itsDEM->elevation(lon, lat, itsMaxDemResolution);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the height at the given coordinate
 */
// ----------------------------------------------------------------------

double Engine::Impl::elevation(double lon, double lat, unsigned int maxdemresolution) const
{
  try
  {
    if (!itsDEM)
      return std::numeric_limits<double>::quiet_NaN();

    return itsDEM->elevation(lon, lat, maxdemresolution);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the global land cover data
 */
// ----------------------------------------------------------------------

boost::shared_ptr<Fmi::LandCover> Engine::Impl::landCover() const
{
  return itsLandCover;
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the height at the given coordinate
 */
// ----------------------------------------------------------------------

Fmi::LandCover::Type Engine::Impl::coverType(double lon, double lat) const
{
  try
  {
    if (!itsLandCover)
      return Fmi::LandCover::NoData;

    return itsLandCover->coverType(lon, lat);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Preprocess a UTF-8 name with possible bad characters
 */
// ----------------------------------------------------------------------

std::string Engine::Impl::preprocess_name(const std::string &name) const
{
  try
  {
    auto ret = name;

    // Some road stations used to have bad names with underscores
    // which prevents proper splitting of names at word boundaries.
    // Replacing underscores with spaces fixes the problem.

    if (itsRemoveUnderscores)
    {
      boost::algorithm::replace_all(ret, "_", " ");
    }

    return ret;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Transform pattern to partial normal forms
 *
 * 1. Tranform to normal form
 * 2. Build partial matches by splitting from potential word boundaries
 *    For example: Ho Chi Minh City ==> Ho Chi Minh City, Chi Minh City,
 *                                      Minh City and City
 */
// ----------------------------------------------------------------------

std::list<std::string> Engine::Impl::to_treewords(const std::string &name,
                                                  const std::string &area) const
{
  try
  {
    namespace bb = boost::locale::boundary;

    std::list<std::string> ret;

    // Create a mapping

    bb::ssegment_index map(bb::word, name.begin(), name.end(), itsLocale);

    // Ignore white space
    map.rule(bb::word_any);

    // Extract the remaining name starting from all word boundaries

    for (const auto &p : map)
    {
      if (p.rule() != 0)
      {
        const auto &it = p.begin();
        if (it != name.end())
        {
          // From word beginning to end of the original input location name
          std::string subname(it, name.end());

          // Normalize for collation. Note that we collate area and comma too
          // just like when searching for a "name,area"
          ret.emplace_back(to_treeword(subname, area));
        }
      }
    }

    return ret;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Transform pattern to normal form
 */
// ----------------------------------------------------------------------

std::string Engine::Impl::to_treeword(const std::string &name) const
{
  try
  {
    std::string tmp = name;
    boost::algorithm::erase_all(tmp, " ");
    tmp = itsCollator->transform(boost::locale::collator_base::primary, tmp);

    // The standard library std::string provided in RHEL6 cannot handle
    // std::string comparisons if there are 0-bytes in the std::strings. The collator
    // in boost always ends the result in 0-byte.

    if (!tmp.empty() && tmp[tmp.size() - 1] == '\0')
      tmp.resize(tmp.size() - 1);

    return tmp;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*
 * \brief Transform search location to normal form
 */
// ----------------------------------------------------------------------

std::string Engine::Impl::to_treeword(const std::string &name, const std::string &area) const
{
  try
  {
    if (area.empty())
      return to_treeword(name);

    return to_treeword(name + "," + area);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Transform language id to normal form
 */
// ----------------------------------------------------------------------

std::string Engine::Impl::to_language(const std::string &lang) const
{
  return Fmi::ascii_tolower_copy(lang);
}

// ----------------------------------------------------------------------
/*!
 * \brief Hash value for the data read during initialization
 */
// ----------------------------------------------------------------------

std::size_t Engine::Impl::hash_value() const
{
  return itsHashValue;
}

// ----------------------------------------------------------------------
/*!
 * \brief Lookup configuration value for the database considering overrides
 */
// ----------------------------------------------------------------------

const libconfig::Setting &Engine::Impl::lookup_database(const std::string &setting,
                                                        const std::string &name) const
{
  try
  {
    const libconfig::Setting &default_value = itsConfig.lookup("database." + setting);
    if (itsConfig.exists("database.overrides"))
    {
      const libconfig::Setting &override = itsConfig.lookup("database.overrides");
      int count = override.getLength();
      for (int i = 0; i < count; ++i)
      {
        const libconfig::Setting &trial_hosts = override[i]["name"];
        int num = trial_hosts.getLength();
        for (int j = 0; j < num; ++j)
        {
          std::string trial_host = trial_hosts[j];
          // If the start of the suggested host name matches current host name, accept override if
          // it has been set
          if (boost::algorithm::istarts_with(name, trial_host) && override[i].exists(setting))
            return override[i][setting];
        }  // for int j
      }    // for int i
    }      // if
    return default_value;
  }
  catch (const libconfig::SettingNotFoundException &ex)
  {
    throw SmartMet::Spine::Exception(BCP, "Override configuration error: " + setting, nullptr);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Initialize autocomplete data
 */
// ----------------------------------------------------------------------

void Engine::Impl::initSuggest(bool threaded)
{
  try
  {
    try
    {
      itsMaxDemResolution = itsConfig.lookupValue("maxdemresolution", itsMaxDemResolution);

      itsConfig.lookupValue("database.disable", itsDatabaseDisabled);

      if (itsDatabaseDisabled)
        std::cerr << "Warning: Geonames database is disabled" << std::endl;
      else
      {
        Locus::Connection conn(itsHost, itsUser, itsPass, itsDatabase, "UTF8", itsPort, false);

        if (!conn.isConnected())
          throw Spine::Exception(BCP, "Failed to connect to fminames database");

        read_database_hash_value(conn);

        if (handleShutDownRequest())
          return;

        // These are needed in regression tests even in mock mode
        read_countries(conn);
        read_alternate_countries(conn);

        if (!itsMockEngine)
        {
          if (handleShutDownRequest())
            return;
          read_municipalities(conn);

          if (handleShutDownRequest())
            return;
          read_geonames(conn);  // requires read_municipalities, read_countries

          if (handleShutDownRequest())
            return;
          read_alternate_geonames(conn);

          if (handleShutDownRequest())
            return;
          read_alternate_municipalities(conn);

          if (handleShutDownRequest())
            return;
          build_geoid_map();  // requires read_geonames

          if (handleShutDownRequest())
            return;
          read_keywords(conn);  // requires build_geoid_map
        }
      }
    }
    catch (const libconfig::ParseException &e)
    {
      throw Spine::Exception::Trace(BCP, "Geo configuration error!")
          .addDetail(std::string(e.getError()) + "' on line " + std::to_string(e.getLine()));
    }
    catch (const libconfig::ConfigException &)
    {
      throw Spine::Exception::Trace(BCP, "Geo configuration error");
    }
    catch (...)
    {
      Spine::Exception exception(BCP, "Operation failed", nullptr);
      if (!itsReloading)
      {
        throw exception;
      }

      // Signal failed reload to the engine
      itsReloadError = exception.what();
      itsReloadOK = false;
      itsReloading = false;
      itsReady = true;
      return;
    }

    // SQL connection is no longer needed at this point,
    // hence these are done outside the try..catch block
    // to close the connection.

    if (handleShutDownRequest())
      return;
    build_geotrees();  // requires ?

    if (handleShutDownRequest())
      return;
    build_ternarytrees();  // requires ?

    if (handleShutDownRequest())
      return;
    build_lang_ternarytrees();  // requires ?

    if (handleShutDownRequest())
      return;
    assign_priorities(itsLocations);  // requires read_geonames

    // Ready
    itsReloadOK = true;
    itsSuggestReadyFlag = true;
  }
  catch (...)
  {
    Spine::Exception exception(BCP, "Geonames autocomplete data initialization failed", nullptr);

    if (!threaded)
      throw exception;

    std::cerr << exception.getStackTrace() << std::endl;
    kill(getpid(), SIGKILL);  // If we use exit() we might get a core dump.
                              // exit(-1);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Initialize DEM data
 */
// ----------------------------------------------------------------------

void Engine::Impl::initDEM()
{
  std::string demdir;
  itsConfig.lookupValue("demdir", demdir);
  if (!demdir.empty())
    itsDEM = boost::make_shared<Fmi::DEM>(demdir);
}

// ----------------------------------------------------------------------
/*!
 * \brief Initialize LandCover data
 */
// ----------------------------------------------------------------------

void Engine::Impl::initLandCover()
{
  std::string landcoverdir;
  itsConfig.lookupValue("landcoverdir", landcoverdir);
  if (!landcoverdir.empty())
    itsLandCover = boost::make_shared<Fmi::LandCover>(landcoverdir);
}

// ----------------------------------------------------------------------
/*!
 * \brief Initialize
 */
// ----------------------------------------------------------------------

void Engine::Impl::init(bool first_construction)
{
  try
  {
    if (handleShutDownRequest())
      return;

    // Read DEM and GlobCover data in parallel for speed

    std::string landcoverdir;
    itsConfig.lookupValue("landcoverdir", landcoverdir);

    boost::thread_group threads;
    threads.add_thread(new boost::thread(boost::bind(&Engine::Impl::initDEM, this)));  // NOLINT
    threads.add_thread(
        new boost::thread(boost::bind(&Engine::Impl::initLandCover, this)));  // NOLINT
    threads.join_all();

    // Early abort if so requested

    if (handleShutDownRequest())
      return;

    // If we're doing a reload, we must do full initialization in this thread.
    // Otherwise we'll initialize autocomplete in a separate thread
    if (!first_construction)
      initSuggest(false);
    else
      boost::thread(boost::bind(&Engine::Impl::initSuggest, this, true));

    // Done apart from autocomplete. Ready to shutdown now though.
    itsReady = true;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Shutdown
 */
// ----------------------------------------------------------------------

void Engine::Impl::shutdown()
{
  try
  {
    std::cout << "  -- Shutdown requested (Impl)\n";
    itsShutdownRequested = true;

    while (!itsReady)
      boost::this_thread::sleep(boost::posix_time::milliseconds(100));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

void Engine::Impl::shutdownRequestFlagSet()
{
  itsShutdownRequested = true;
}

// ----------------------------------------------------------------------
/*!
 * \brief Prepare for a possible shutdown
 */
// ----------------------------------------------------------------------

bool Engine::Impl::handleShutDownRequest()
{
  if (itsShutdownRequested)
    itsReady = true;
  return itsShutdownRequested;
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the configuration file.
 *
 * Expected format:
 * \code
 * verbose	= true; // optional
 *
 * database:
 * {
 *    host	= "database.host.com";
 *	  user	= "USERNAME";
 *    pass	= "PASSWORD";
 *    port  = 5432;
 *    where   = "countries_iso2='FI'";	// optional
 * };
 * \endcode
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_config()
{
  try
  {
    try
    {
      if (itsVerbose)
        std::cout << "Reading fminames configuration file '" << itsConfigFile << "'" << std::endl;

      itsConfig.readFile(itsConfigFile.c_str());

      if (!itsConfig.exists("database"))
      {
        Spine::Exception exception(BCP, "Configuration file must specify the database details!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }

      const libconfig::Setting &db = itsConfig.lookup("database");

      if (!db.isGroup())
      {
        Spine::Exception exception(BCP, "Configured value of 'database' must be a group!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }

      itsConfig.lookupValue("verbose", itsVerbose);
      itsConfig.lookupValue("mock", itsMockEngine);
      itsConfig.lookupValue("remove_underscores", itsRemoveUnderscores);

      read_config_priorities();

      const std::string &name = boost::asio::ip::host_name();
      itsUser = lookup_database("user", name).c_str();
      itsHost = lookup_database("host", name).c_str();
      itsPass = lookup_database("pass", name).c_str();
      itsDatabase = lookup_database("database", name).c_str();

      // port is optional
      int port = default_port;
      itsConfig.lookupValue("database.port", port);
      itsPort = Fmi::to_string(port);
    }
    catch (const libconfig::SettingException &e)
    {
      Spine::Exception exception(BCP, "Configuration file setting error!");
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Path", e.getPath());
      exception.addParameter("Error description", e.what());
      throw exception;
    }
    catch (const libconfig::ParseException &e)
    {
      Spine::Exception exception(BCP, "Configuration file parsing failed!");
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Error line", std::to_string(e.getLine()));
      exception.addParameter("Error description", e.getError());
      throw exception;
    }
    catch (const libconfig::ConfigException &e)
    {
      Spine::Exception exception(BCP, "Configuration exception!");
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Error description", e.what());
      throw exception;
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Configuration read failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the configuration file section on priorities
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_config_priorities()
{
  try
  {
    try
    {
      if (!itsConfig.exists("priorities"))
        return;

      itsConfig.lookupValue("priorities.match", itsNameMatchPriority);

      read_config_prioritymap("populations", itsPopulationPriorities);
      read_config_prioritymap("areas", itsAreaPriorities);
      read_config_prioritymap("countries", itsCountryPriorities);

      if (!itsConfig.exists("priorities.features"))
        return;

      const libconfig::Setting &tmp = itsConfig.lookup("priorities.features");

      if (!tmp.isGroup())
      {
        Spine::Exception exception(BCP,
                                   "Configured value of 'priorities.features' must be a group!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }
      for (int i = 0; i < tmp.getLength(); ++i)
      {
        std::string countryname = tmp[i].getName();
        std::string featurename = tmp[i];

        std::string mapname = "priorities." + featurename;

        if (!itsConfig.exists(mapname))
        {
          Spine::Exception exception(BCP, "Configuration of '" + mapname + "' is missing!");
          exception.addParameter("Configuration file", itsConfigFile);
          throw exception;
        }

        const libconfig::Setting &tmpmap = itsConfig.lookup(mapname);

        for (int j = 0; j < tmpmap.getLength(); ++j)
        {
          std::string name = tmpmap[j].getName();
          int value = tmpmap[j];
          itsFeaturePriorities[countryname][name] = value;
        }
      }
    }
    catch (const libconfig::SettingException &e)
    {
      Spine::Exception exception(BCP, "Configuration file setting error!");
      exception.addParameter("Path", e.getPath());
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Error description", e.what());
      throw exception;
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Reading config priorities failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read a single map of priorities
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_config_prioritymap(const std::string &partname, Priorities &priomap)
{
  try
  {
    try
    {
      std::string name = "priorities." + partname;

      if (!itsConfig.exists(name))
        return;

      const libconfig::Setting &tmp = itsConfig.lookup(name);

      if (!tmp.isGroup())
      {
        Spine::Exception exception(BCP, "Configured value of '" + name + "' must be a group!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }

      for (int i = 0; i < tmp.getLength(); ++i)
      {
        std::string varname = tmp[i].getName();
        int value = tmp[i];
        priomap[varname] = value;
      }
    }
    catch (const libconfig::SettingException &e)
    {
      Spine::Exception exception(BCP, "Configuration file setting error!");
      exception.addParameter("Config file", itsConfigFile);
      exception.addParameter("Path", e.getPath());
      exception.addParameter("Error description", e.what());
      throw exception;
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the database hash value
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_database_hash_value(Locus::Connection &conn)
{
  try
  {
    // Select maximum last_modified from tables which have it. Note that
    // the maximum mya have fractional seconds, hence the rounding to seconds

    std::string query =
        "SELECT EXTRACT(epoch FROM date_trunc('second',max(val))) AS max "
        "FROM ("
        "SELECT max(last_modified) AS val from geonames UNION "
        "SELECT max(last_modified) AS val from keywords_has_geonames UNION "
        "SELECT max(last_modified) AS val from alternate_geonames) x";

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw Spine::Exception(BCP, "FmiNames: Failed to read database hash value");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      itsHashValue = row["max"].as<std::size_t>();
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the countries table
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_countries(Locus::Connection &conn)
{
  try
  {
    // Note: PCLI overrides smaller political entities if there are multiple
    // for
    // the same iso2
    // country
    // code
    std::string query(
        "SELECT name, countries_iso2 as iso2 FROM geonames WHERE "
        "features_code in "
        "('PCLD','PCLF','PCLI') ORDER BY features_code ASC");

    if (itsVerbose)
      std::cout << "read_countries: " << query << std::endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw Spine::Exception(BCP, "FmiNames: Found no PCLI/PCLF/PCLD places from geonames table");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      std::string name = row["name"].as<std::string>();
      std::string iso2 = row["iso2"].as<std::string>();
      itsCountries[iso2] = name;
    }

    if (itsVerbose)
      std::cout << "read_countries: " << res.size() << " countries" << std::endl;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the alternate countries
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_alternate_countries(Locus::Connection &conn)
{
  try
  {
    std::string query(
        "SELECT language, g.name as gname,a.name as "
        "alt_gname,a.preferred,a.priority,length(a.name) "
        "as length FROM geonames g, alternate_geonames a WHERE "
        "g.features_code in "
        "('PCLI','PCLF','PCLD') AND g.id=a.geonames_id ORDER BY "
        "geonames_id, a.priority ASC, "
        "a.preferred DESC, length ASC, alt_gname ASC");

    if (itsVerbose)
      std::cout << "read_alternate_countries: " << query << std::endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw Spine::Exception(BCP, "FmiNames: Found no country translations");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      std::string lang = row["language"].as<std::string>();
      std::string name = row["gname"].as<std::string>();
      std::string translation = row["alt_gname"].as<std::string>();

      auto it = itsAlternateCountries.find(name);
      if (it == itsAlternateCountries.end())
      {
        it = itsAlternateCountries.insert(make_pair(name, Translations())).first;
      }

      Fmi::ascii_tolower(lang);

      auto &translations = it->second;
      // Note: Failure to insert is OK, we prefer the sorted order of the SQL
      // statements
      translations.insert(std::make_pair(lang, translation));
    }

    if (itsVerbose)
      std::cout << "read_alternate_countries: " << res.size() << " translations" << std::endl;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the municipalities table
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_municipalities(Locus::Connection &conn)
{
  try
  {
    std::string query("SELECT id, name FROM municipalities");

    if (itsVerbose)
      std::cout << "read_municipalities: " << query << std::endl;

    pqxx::result res = conn.executeNonTransaction(query);

    // We allow this to be empty since the table contains only Finnish information
    // if (res.empty()) throw Spine::Exception(BCP, "FmiNames: Found nothing from municipalities
    // table");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      int id = row["id"].as<int>();
      std::string name = row["name"].as<std::string>();
      itsMunicipalities[id] = name;
    }

    if (itsVerbose)
      std::cout << "read_municipalities: " << itsMunicipalities.size() << " municipalities"
                << std::endl;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the base geonames table
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_geonames(Locus::Connection &conn)
{
  try
  {
    std::string sql =
        "SELECT id, geonames.name AS name, countries_iso2 as iso2, "
        "features_code as feature, "
        "municipalities_id as munip, lon, lat, timezone, population, "
        "elevation, dem, landcover, admin1 "
        "FROM "
        "geonames WHERE EXISTS (SELECT * FROM keywords_has_geonames WHERE "
        "geonames.id=keywords_has_geonames.geonames_id)";

    if (itsConfig.exists("database.where.geonames"))
    {
      const libconfig::Setting &where_clause = itsConfig.lookup("database.where.geonames");
      sql.append(" AND ").append(static_cast<const char *>(where_clause));
    }

    if (itsVerbose)
      std::cout << "read_geonames: " << sql << std::endl;

    pqxx::result res = conn.executeNonTransaction(sql);

    if (res.empty())
      throw Spine::Exception(BCP, "FmiNames: Found nothing from fminames database");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      Spine::GeoId geoid = Fmi::stoi(row["id"].as<std::string>());
      std::string name = row["name"].as<std::string>();

      if (row["timezone"].is_null())
      {
        std::cerr << "Warning: " << geoid << " '" << name
                  << "' timezone is null, discarding the location" << std::endl;
      }
      else
      {
        std::string iso2 = (row["iso2"].is_null() ? "" : row["iso2"].as<std::string>());
        std::string feature = (row["feature"].is_null() ? "" : row["feature"].as<std::string>());
        int munip = row["munip"].as<int>();
        double lon = row["lon"].as<double>();
        double lat = row["lat"].as<double>();
        std::string tz = row["timezone"].as<std::string>();
        int pop = (!row["population"].is_null() ? row["population"].as<int>() : 0);
        double ele = (!row["elevation"].is_null() ? row["elevation"].as<double>()
                                                  : std::numeric_limits<float>::quiet_NaN());
        double dem = (!row["dem"].is_null() ? row["dem"].as<int>() : elevation(lon, lat));
        std::string admin = (!row["admin1"].is_null() ? row["admin1"].as<std::string>() : "");
        auto covertype = Fmi::LandCover::Type(
            (!row["landcover"].is_null() ? row["landcover"].as<int>() : coverType(lon, lat)));

        std::string area;
        if (munip != 0)
        {
          auto it = itsMunicipalities.find(munip);
          if (it != itsMunicipalities.end())
            area = it->second;
        }

        if (area.empty())
        {
          auto it = itsCountries.find(iso2);
          auto us = itsCountries.find("US");
          if (it != itsCountries.end())
            area = it->second;
          if (it == us)
            area = admin.append(", ").append(area);
#if 0
          else
            std::cerr << "Failed to find country " << key << " for geoid " << geoid << std::endl;
#endif
        }

        std::string country;  // country will be filled in upon request
        Spine::LocationPtr loc(new Spine::Location(geoid,
                                                   name,
                                                   iso2,
                                                   munip,
                                                   area,
                                                   feature,
                                                   country,
                                                   lon,
                                                   lat,
                                                   tz,
                                                   pop,
                                                   boost::numeric_cast<float>(ele),
                                                   boost::numeric_cast<float>(dem),
                                                   covertype));
        itsLocations.push_back(loc);
      }
    }

    if (itsVerbose)
      std::cout << "read_geonames: " << itsLocations.size() << " locations" << std::endl;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the alternate_geonames table
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_alternate_geonames(Locus::Connection &conn)
{
  try
  {
    std::string sql =
        "SELECT a.geonames_id, a.name, a.language, a.priority, a.preferred, "
        "length(a.name) as "
        "length "
        "FROM alternate_geonames a INNER JOIN keywords_has_geonames k ON "
        "a.geonames_id=k.geonames_id";

    if (itsConfig.exists("database.where.alternate_geonames"))
    {
      const auto &where_clause = itsConfig.lookup("database.where.alternate_geonames");
      sql.append(" WHERE ").append(static_cast<const char *>(where_clause));
    }

    // This makes sure preferred names come first, and longest names last.
    // Note that this leaves cases like Montreal vs Montr�al, hence we do a final
    // name sort to guarantee a fixed order. Using ASC prefers non-accented letters.

#if 0
    // Works only in MySQL
    sql.append(" GROUP BY a.id HAVING count(*) > 1 ORDER BY a.geonames_id, a.priority ASC, a.preferred DESC, length ASC, name ASC");
#else
    // PostGreSQL requires all the names to be mentioned
    sql.append(
        " GROUP BY "
        "a.id,a.geonames_id,a.name,a.language,a.priority,a.preferred "
        "HAVING count(*) > 0 "
        "ORDER BY a.geonames_id, a.priority ASC, a.preferred DESC, "
        "length ASC, name ASC");
#endif

    if (itsVerbose)
      std::cout << "read_alternate_geonames: " << sql << std::endl;

    pqxx::result res = conn.executeNonTransaction(sql);

    if (res.empty())
      throw Spine::Exception(BCP, "FmiNames: Found nothing from alternate_fminames database");

    if (itsVerbose)
      std::cout << "read_alternate_geonames: " << res.size() << " translations" << std::endl;

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      Spine::GeoId geoid = Fmi::stoi(row["geonames_id"].as<std::string>());
      std::string name = row["name"].as<std::string>();
      std::string lang = row["language"].as<std::string>();

      auto it = itsAlternateNames.find(geoid);
      if (it == itsAlternateNames.end())
      {
        it = itsAlternateNames.insert(make_pair(geoid, Translations())).first;
      }

      Fmi::ascii_tolower(lang);

      auto &translations = it->second;

      // Note that it is OK if this fails - the first translation found is
      // preferred
      translations.insert(std::make_pair(lang, name));
    }

    if (itsVerbose)
      std::cout << "read_alternate_geonames done" << std::endl;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the alternate_municipalities table
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_alternate_municipalities(Locus::Connection &conn)
{
  try
  {
    std::string query(
        "SELECT municipalities_id as id, name, language FROM "
        "alternate_municipalities");

    if (itsVerbose)
      std::cout << "read_alternate_municipalities: " << query << std::endl;

    pqxx::result res = conn.executeNonTransaction(query);

    // Permit the table to be empty since it contains only Finnish information
    // if (res.empty()) throw Spine::Exception(BCP, "FmiNames: Found nothing from
    // alternate_municipalities database");

    if (itsVerbose)
      std::cout << "read_alternate_geonames: " << res.size() << " translations" << std::endl;

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      int munip = row["id"].as<int>();
      std::string name = row["name"].as<std::string>();
      std::string lang = row["language"].as<std::string>();

      auto it = itsAlternateMunicipalities.find(munip);
      if (it == itsAlternateMunicipalities.end())
      {
        it = itsAlternateMunicipalities.insert(make_pair(munip, Translations())).first;
      }

      Fmi::ascii_tolower(lang);
      it->second.insert(make_pair(lang, name));
    }

    if (itsVerbose)
      std::cout << "read_alternate_municipalities: " << res.size() << " translations" << std::endl;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build a map of geoid numbers to location pointers
 *
 * Pointers are not owner, itsLocations variable manages them
 */
// ----------------------------------------------------------------------

void Engine::Impl::build_geoid_map()
{
  try
  {
    if (itsVerbose)
      std::cout << "build_geoid_map()" << std::endl;

    for (Spine::LocationPtr &v : itsLocations)
      itsGeoIdMap.emplace(GeoIdMap::value_type(v->geoid, &v));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Assign priorities
 */
// ----------------------------------------------------------------------

void Engine::Impl::assign_priorities(Spine::LocationList &locs) const
{
  try
  {
    if (itsVerbose)
      std::cout << "assign_priorities" << std::endl;

    for (Spine::LocationPtr &v : locs)
    {
      int score = population_priority(*v);
      score += area_priority(*v);
      score += country_priority(*v);
      score += feature_priority(*v);

      auto &myloc = const_cast<Spine::Location &>(*v);  // NOLINT
      myloc.priority = score;
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

int Engine::Impl::country_priority(const Spine::Location &loc) const
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

int Engine::Impl::area_priority(const Spine::Location &loc) const
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

int Engine::Impl::population_priority(const Spine::Location &loc) const
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

int Engine::Impl::feature_priority(const Spine::Location &loc) const
{
  try
  {
    auto it = itsFeaturePriorities.find(loc.iso2);
    if (it == itsFeaturePriorities.end())
      it = itsFeaturePriorities.find("default");

    if (it == itsFeaturePriorities.end())
      return 0;

    auto &priomap = it->second;

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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read keywords_has_geonames
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_keywords(Locus::Connection &conn)
{
  try
  {
    std::string query("SELECT keyword, geonames_id as id FROM keywords_has_geonames");

    if (itsVerbose)
      std::cout << "read_keywords: " << query << std::endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw Spine::Exception(BCP, "FmiNames: Found nothing from keywords_has_geonames database");

    int count_ok = 0;
    int count_bad = 0;

    bool limited_db = itsConfig.exists("database.where");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      std::string key = row["keyword"].as<std::string>();
      Spine::GeoId geoid = Fmi::stoi(row["id"].as<std::string>());

      auto it = itsGeoIdMap.find(geoid);
      if (it != itsGeoIdMap.end())
      {
        itsKeywords[key].push_back(*it->second);
        ++count_ok;
      }
      else
      {
        ++count_bad;
        if (!limited_db)
        {
          std::cerr << "  warning: keyword " << key << " uses nonexistent geoid " << geoid
                    << std::endl;
        }
      }
    }

    if (itsVerbose)
      std::cout << "read_keywords: attached " << count_ok << " keywords to locations succesfully"
                << std::endl
                << "read_keywords: found " << count_bad << " unknown locations" << std::endl;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief build trees for finding nearest points
 */
// ----------------------------------------------------------------------

void Engine::Impl::build_geotrees()
{
  try
  {
    for (const auto &name_locs : itsKeywords)
    {
      const std::string &keyword = name_locs.first;
      const Spine::LocationList &locs = name_locs.second;

      if (itsVerbose)
        std::cout << "build_geotrees:  keyword '" << keyword << "' of size " << locs.size()
                  << std::endl;

      auto it = itsGeoTrees.find(keyword);
      if (it == itsGeoTrees.end())
      {
        it = itsGeoTrees.insert(make_pair(keyword, new GeoTree())).first;
      }

      for (Spine::LocationPtr ptr : locs)
        it->second->insert(ptr);
    }

    // global tree

    if (itsVerbose)
      std::cout << "build_geotrees: keyword '" << FMINAMES_DEFAULT_KEYWORD << "' of size "
                << itsLocations.size() << std::endl;

    auto it = itsGeoTrees.insert(std::make_pair(FMINAMES_DEFAULT_KEYWORD, new GeoTree())).first;
    for (Spine::LocationPtr &ptr : itsLocations)
      it->second->insert(ptr);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief build ternary trees for finding name suggestions
 */
// ----------------------------------------------------------------------

void Engine::Impl::build_ternarytrees()
{
  try
  {
    // normal geonames for each keyword

    for (auto &name_locs : itsKeywords)
    {
      const std::string &keyword = name_locs.first;
      Spine::LocationList &locs = name_locs.second;

      if (itsVerbose)
        std::cout << "build_ternarytrees: keyword '" << keyword << "' of size " << locs.size()
                  << std::endl;

      auto it = itsTernaryTrees.find(keyword);
      if (it == itsTernaryTrees.end())
      {
        auto newtree = boost::make_shared<TernaryTree>();
        it = itsTernaryTrees.insert(make_pair(keyword, newtree)).first;
      }

      for (Spine::LocationPtr &ptr : locs)
      {
        std::string specifier = ptr->area + "," + Fmi::to_string(ptr->geoid);
        auto names = to_treewords(preprocess_name(ptr->name), specifier);
        for (const auto &name : names)
        {
          it->second->insert(name, ptr);
        }
      }
    }

    // all geonames

    if (itsVerbose)
      std::cout << "build_ternarytrees: keyword '" << FMINAMES_DEFAULT_KEYWORD << "' of size "
                << itsLocations.size() << std::endl;

    auto newtree = boost::make_shared<TernaryTree>();
    auto it = itsTernaryTrees.insert(std::make_pair(FMINAMES_DEFAULT_KEYWORD, newtree)).first;

    for (Spine::LocationPtr &ptr : itsLocations)
    {
      std::string specifier = ptr->area + "," + Fmi::to_string(ptr->geoid);
      auto names = to_treewords(preprocess_name(ptr->name), specifier);
      for (const auto &name : names)
      {
        it->second->insert(name, ptr);
      }
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief build language specific ternary trees for finding name suggestions
 */
// ----------------------------------------------------------------------

void Engine::Impl::build_lang_ternarytrees()
{
  try
  {
    if (itsVerbose)
      std::cout << "build_lang_ternarytrees" << std::endl;

    build_lang_ternarytrees_all();
    build_lang_ternarytrees_keywords();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief build language specific ternary tree for keyword "all"
 *
 * for each alternate name
 *  for each translation
 *   insert translation into language specific tree
 *
 * inserts are done to keyword specific tables using keyword "all"
 */
// ----------------------------------------------------------------------

void Engine::Impl::build_lang_ternarytrees_all()
{
  try
  {
    // traverse all alternate names

    if (itsVerbose)
      std::cout << "build_lang_ternarytrees_all: " << itsAlternateNames.size() << " names"
                << std::endl;

    for (const auto &gt : itsAlternateNames)
    {
      int geoid = gt.first;
      const Translations &translations = gt.second;

      // find the original info

      auto git = itsGeoIdMap.find(geoid);

      // safety check - should not happen if all data is present

      if (git == itsGeoIdMap.end())
        continue;

      const Spine::LocationPtr &loc = *git->second;

      // Now process all translations for the geoid

      for (const auto &tt : translations)
      {
        const std::string &lang = tt.first;
        const std::string &name = tt.second;

        // Find the language specific tree

        auto it = itsLangTernaryTreeMap.find(lang);

        // If there isn't one, create it now

        if (it == itsLangTernaryTreeMap.end())
          it = itsLangTernaryTreeMap
                   .insert(std::make_pair(lang, boost::make_shared<TernaryTreeMap>()))
                   .first;
        // Then find keyword specific map, keyword being "all"

        auto &tmap = *it->second;
        auto tit = tmap.find("all");

        if (tit == tmap.end())
          tit = tmap.insert(TernaryTreeMap::value_type("all", boost::make_shared<TernaryTree>()))
                    .first;

        // Insert the word "name, area" to the tree

        auto &tree = *tit->second;

        std::string specifier = loc->area + "," + Fmi::to_string(loc->geoid);
        auto treenames = to_treewords(preprocess_name(name), specifier);

        for (const auto &treename : treenames)
        {
          if (!tree.insert(treename, *git->second))
          {
            // std::cout << "Failed to insert " << treename << std::endl;
          }
        }
      }
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build language specific ternary trees for explicit keywords
 *
 * For each keyword
 *  For each geoid for keyword
 *   For each alternate translation
 *    Insert translation into language specific tree
 */
// ----------------------------------------------------------------------

void Engine::Impl::build_lang_ternarytrees_keywords()
{
  try
  {
    // Traverse all alternate names

    if (itsVerbose)
      std::cout << "build_lang_ternarytrees_keywords()" << std::endl;

    for (auto &kloc : itsKeywords)
    {
      const std::string &keyword = kloc.first;

      int ntranslations = 0;

      for (Spine::LocationPtr &loc : kloc.second)
      {
        int geoid = loc->geoid;

        // safety check against missing geoid
        auto git = itsGeoIdMap.find(geoid);
        if (git == itsGeoIdMap.end())
          continue;

        auto ait = itsAlternateNames.find(geoid);
        if (ait == itsAlternateNames.end())
          continue;

        // Process all the different language translations

        Spine::LocationPtr &ptr = *git->second;

        for (const auto &tt : ait->second)
        {
          const std::string &lang = tt.first;
          const std::string &translation = tt.second;

          // Find the language specific tree

          auto it = itsLangTernaryTreeMap.find(lang);

          // If there isn't one, create it now

          if (it == itsLangTernaryTreeMap.end())
            it = itsLangTernaryTreeMap
                     .insert(std::make_pair(lang, boost::make_shared<TernaryTreeMap>()))
                     .first;

          // Then find keyword specific map

          auto &tmap = *it->second;
          auto tit = tmap.find(keyword);

          if (tit == tmap.end())
            tit =
                tmap.insert(TernaryTreeMap::value_type(keyword, boost::make_shared<TernaryTree>()))
                    .first;

          // Insert the word "name, area" to the tree

          ++ntranslations;

          TernaryTree &tree = *tit->second;

          // TODO(mheiskan): translate area
          std::string specifier = ptr->area + "," + Fmi::to_string(ptr->geoid);
          auto names = to_treewords(preprocess_name(translation), specifier);

          for (const auto &name : names)
          {
            if (!tree.insert(name, loc))
            {
              // std::cout << "Failed to insert " << name << std::endl;
            }
          }
        }
      }

      if (itsVerbose)
        std::cout << "build_lang_ternarytrees_keywords: " << keyword << " with " << ntranslations
                  << " translations" << std::endl;
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate location name
 */
// ----------------------------------------------------------------------

void Engine::Impl::translate_name(Spine::Location &loc, const std::string &lang) const
{
  try
  {
    // are there any translations?

    auto trans = itsAlternateNames.find(loc.geoid);
    if (trans == itsAlternateNames.end())
      return;

    // is there a translation?

    std::string lg = to_language(lang);

    auto &translations = trans->second;
    auto pos = translations.find(lg);

    if (pos == translations.end())
      return;

    loc.name = pos->second;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate location area
 */
// ----------------------------------------------------------------------

void Engine::Impl::translate_area(Spine::Location &loc, const std::string &lang) const
{
  try
  {
    std::string lg = to_language(lang);

    // are there any municipality translations?

    auto trans = itsAlternateMunicipalities.find(loc.municipality);
    if (trans != itsAlternateMunicipalities.end())
    {
      const auto &translations = trans->second;
      auto pos = translations.find(lg);

      if (pos != translations.end())
        loc.area = pos->second;
    }

    if (!loc.area.empty())
    {
      // Try translating country name first, see if it is preceded by a state
      // designator and a comma
      auto comma = loc.area.find(", ");
      auto country = (comma == std::string::npos) ? loc.area : loc.area.substr(comma + 2);
      auto it = itsAlternateCountries.find(country);

      if (it != itsAlternateCountries.end())
      {
        const auto &translations = it->second;

        auto pos = translations.find(lg);
        if (pos == translations.end())
          return;
        loc.area = (comma == std::string::npos) ? pos->second
                                                : loc.area.substr(0, comma + 2).append(pos->second);
      }
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate a location
 */
// ----------------------------------------------------------------------

void Engine::Impl::translate(Spine::LocationPtr &loc, const std::string &lang) const
{
  try
  {
    std::unique_ptr<Spine::Location> newloc(new Spine::Location(*loc));
    translate_name(*newloc, lang);
    translate_area(*newloc, lang);
    newloc->country = translate_country(newloc->iso2, lang);
    loc.reset(newloc.release());
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate a location list
 */
// ----------------------------------------------------------------------

void Engine::Impl::translate(Spine::LocationList &locs, const std::string &lang) const
{
  try
  {
    for (Spine::LocationPtr &loc : locs)
      translate(loc, lang);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate country name
 */
// ----------------------------------------------------------------------

std::string Engine::Impl::translate_country(const std::string &iso2, const std::string &lang) const
{
  try
  {
    std::string lg = to_language(lang);

    // iso2 to official name

    auto country = itsCountries.find(iso2);
    if (country == itsCountries.end())
      return "";

    // translation

    auto pos = itsAlternateCountries.find(country->second);
    if (pos == itsAlternateCountries.end())
      return country->second;

    const auto &translations = pos->second;

    auto pos2 = translations.find(lg);
    if (pos2 == translations.end())
      return country->second;

    return pos2->second;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Priority sorter
 */
// ----------------------------------------------------------------------

bool Engine::Impl::prioritySort(const Spine::LocationPtr &a, const Spine::LocationPtr &b) const
{
  try
  {
    // First try priority
    if (a->priority != b->priority)
      return (a->priority > b->priority);

    // Last use alphabetical sort.

    std::string aname = to_treeword(a->name);
    std::string bname = to_treeword(b->name);

    if (aname != bname)
      return (aname < bname);

    return (a->area < b->area);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Trivial sort to find duplicates (doesn't care about localization)
 */
// ----------------------------------------------------------------------

bool basicSort(const Spine::LocationPtr &a, const Spine::LocationPtr &b)
{
  try
  {
    if (a->name != b->name)
      return (a->name < b->name);

    if (a->iso2 != b->iso2)
      return (a->iso2 < b->iso2);

    if (a->area != b->area)
      return (a->area < b->area);

    return (a->priority > b->priority);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Definition of unique for Spine::LocationPtr*
 */
// ----------------------------------------------------------------------

bool closeEnough(const Spine::LocationPtr &a, const Spine::LocationPtr &b)
{
  try
  {
    return (((a->name == b->name)) && (a->iso2 == b->iso2) && (a->area == b->area));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Priority sort a list of locations
 */
// ----------------------------------------------------------------------

void Engine::Impl::sort(Spine::LocationList &theLocations) const
{
  try
  {
    assign_priorities(theLocations);
    theLocations.sort(basicSort);
    theLocations.unique(closeEnough);  // needed because language specific trees
                                       // create duplicates
    // Sort based on priorities
    theLocations.sort(boost::bind(&Impl::prioritySort, this, _1, _2));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Suggest translations
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::Impl::suggest(const std::string &pattern,
                                          const std::string &lang,
                                          const std::string &keyword,
                                          unsigned int page,
                                          unsigned int maxresults) const
{
  if (!itsSuggestReadyFlag)
    throw Spine::Exception(BCP, "Attempt to use geonames suggest before it is ready!");

  try
  {
    // Try using the cache first
    auto key = cache_key(pattern, lang, keyword, page, maxresults);
    auto cached_result = itsSuggestCache->find(key);
    if (cached_result)
      return *cached_result;

    // return null if keyword is wrong

    Spine::LocationList ret;

    auto it = itsTernaryTrees.find(keyword);
    if (it == itsTernaryTrees.end())
      return ret;

    // transform to collated form

    std::string name = to_treeword(pattern);

    // find it

    ret = it->second->findprefix(name);

    // check if there are language specific translations

    std::string lg = to_language(lang);

    auto lt = itsLangTernaryTreeMap.find(lg);
    if (lt != itsLangTernaryTreeMap.end())
    {
      auto tit = lt->second->find(keyword);
      if (tit != lt->second->end())
      {
        std::list<Spine::LocationPtr> tmpx = tit->second->findprefix(name);
        for (const Spine::LocationPtr &ptr : tmpx)
        {
          ret.push_back(ptr);
        }
      }
    }

    // Translate the names

    translate(ret, lang);

    // Give an extra bonus for exact matches

    for (auto &loc : ret)
    {
      std::string tmpname = to_treeword(loc->name);
      if (tmpname == name)
      {
        std::unique_ptr<Spine::Location> newloc(new Spine::Location(*loc));
        newloc->priority += itsNameMatchPriority * priority_scale;
        loc.reset(newloc.release());
      }
    }

    // Sort duplicates away, language specific trees may create them

    ret.sort(basicSort);
    ret.unique(closeEnough);

    // Sort based on priorities

    ret.sort(boost::bind(&Impl::prioritySort, this, _1, _2));

    // Keep the desired part. We do this after moving exact matches to the front,
    // otherwise for example "Spa, Belgium" is not very high on the list of
    // matches for "Spa". Translating everything first is expensive, but the
    // results are cached.

    if (maxresults > 0)
    {
      // should do this using erase
      unsigned int first = page * maxresults;
      for (std::size_t i = 0; i < first; i++)
        ret.pop_front();
      while (ret.size() > maxresults)
        ret.pop_back();
    }

    itsSuggestCache->insert(key, ret);

    return ret;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Convert FmiNames location list to SmartMet location list
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::Impl::to_locationlist(const Locus::Query::return_type &theList)
{
  try
  {
    Spine::LocationList ret;
    for (const auto &loc : theList)
    {
      double dem = elevation(loc.lon, loc.lat);
      auto covertype = coverType(loc.lon, loc.lat);

      // Select administrative area. In particular, if the location is the
      // administrative
      // area itself, select the country instead.

      std::string area = loc.admin;
      if (area == loc.name || area.empty())
        area = loc.country;

      Spine::LocationPtr ptr(new Spine::Location(loc.id,
                                                 loc.name,
                                                 loc.iso2,
                                                 0,
                                                 area,
                                                 loc.feature,
                                                 loc.country,
                                                 loc.lon,
                                                 loc.lat,
                                                 loc.timezone,
                                                 boost::numeric_cast<int>(loc.population),
                                                 loc.elevation,
                                                 dem,
                                                 covertype));

      ret.push_back(ptr);
    }
    return ret;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Answer a name search
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::Impl::name_search(const Locus::QueryOptions &theOptions,
                                              const std::string &theName)
{
  if (itsDatabaseDisabled)
    return {};

  try
  {
    std::size_t key = boost::hash_value(theName);
    boost::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
      return *pos;

    Locus::Query lq(itsHost, itsUser, itsPass, itsDatabase, itsPort);
    Spine::LocationList ptrs = to_locationlist(lq.FetchByName(theOptions, theName));
    assign_priorities(ptrs);
    ptrs.sort(boost::bind(&Impl::prioritySort, this, _1, _2));

    // Do not cache empty results
    if (ptrs.empty())
      return ptrs;

    // Update the cache

    itsNameSearchCache.insert(key, ptrs);

    return ptrs;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Coordinate search
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::Impl::lonlat_search(const Locus::QueryOptions &theOptions,
                                                float theLongitude,
                                                float theLatitude,
                                                float theRadius)
{
  // Let the engine handle setting the timezone, dem and covertype
  if (itsDatabaseDisabled)
    return {};

  try
  {
    std::size_t key = boost::hash_value(theLongitude);
    boost::hash_combine(key, boost::hash_value(theLatitude));
    boost::hash_combine(key, boost::hash_value(theRadius));
    boost::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
      return *pos;

    Locus::Query lq(itsHost, itsUser, itsPass, itsDatabase, itsPort);

    Spine::LocationList ptrs =
        to_locationlist(lq.FetchByLonLat(theOptions, theLongitude, theLatitude, theRadius));

    // Do not cache empty results
    if (ptrs.empty())
      return ptrs;

    // Update the cache
    itsNameSearchCache.insert(key, ptrs);
    return ptrs;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a id search
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::Impl::id_search(const Locus::QueryOptions &theOptions, int theId)
{
  if (itsDatabaseDisabled)
    return {};

  try
  {
    std::size_t key = boost::hash_value(theId);
    boost::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
      return *pos;

    Locus::Query lq(itsHost, itsUser, itsPass, itsDatabase, itsPort);

    Spine::LocationList ptrs = to_locationlist(lq.FetchById(theOptions, theId));

    // Do not cache empty results
    if (ptrs.empty())
      return ptrs;

    // Update the cache

    itsNameSearchCache.insert(key, ptrs);

    return ptrs;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a keyword search
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::Impl::keyword_search(const Locus::QueryOptions &theOptions,
                                                 const std::string &theKeyword)
{
  if (itsDatabaseDisabled)
    return {};

  try
  {
    // Just in case there is a keyword equal to an actual location name
    // we do not start the start hashing directly from the keyword
    std::size_t key = 0x12345678;
    boost::hash_combine(key, boost::hash_value(theKeyword));
    boost::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
      return *pos;

    Locus::Query lq(itsHost, itsUser, itsPass, itsDatabase, itsPort);

    Spine::LocationList ptrs = to_locationlist(lq.FetchByKeyword(theOptions, theKeyword));

    // Do not cache empty results
    if (ptrs.empty())
      return ptrs;

    // Update the cache
    itsNameSearchCache.insert(key, ptrs);

    return ptrs;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Status report
 */
// ----------------------------------------------------------------------

void Engine::Impl::name_cache_status(boost::shared_ptr<Spine::Table> tablePtr,
                                     Spine::TableFormatter::Names &theNames)
{
  try
  {
    auto contentList = itsNameSearchCache.getContent();

    if (contentList.empty())
    {
      return;
    }

    theNames.push_back("Position");
    theNames.push_back("Hits");
    theNames.push_back("Key");
    theNames.push_back("Name");
    theNames.push_back("Geoid");

    unsigned int row = 0;
    for (const auto &ReportObject : contentList)
    {
      const std::size_t count = ReportObject.itsHits;
      const std::size_t key = ReportObject.itsKey;
      const Spine::LocationPtr &loc = ReportObject.itsValue.front();

      unsigned int column = 0;

      tablePtr->set(column, row, Fmi::to_string(row));
      ++column;
      tablePtr->set(column, row, Fmi::to_string(count));
      ++column;
      tablePtr->set(column, row, Fmi::to_string(key));
      ++column;
      tablePtr->set(column, row, loc->name);
      ++column;
      tablePtr->set(column, row, Fmi::to_string(loc->geoid));

      ++row;
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return true if autocomplete data has been initialized
 */
// ----------------------------------------------------------------------

bool Engine::Impl::isSuggestReady() const
{
  return itsSuggestReadyFlag;
}

// ----------------------------------------------------------------------
/*!
 * \brief Cache key for a suggestion
 */
// ----------------------------------------------------------------------

std::size_t Engine::Impl::cache_key(const std::string &pattern,
                                    const std::string &lang,
                                    const std::string &keyword,
                                    unsigned int page,
                                    unsigned int maxresults) const
{
  auto hash = boost::hash_value(pattern);
  boost::hash_combine(hash, boost::hash_value(lang));
  boost::hash_combine(hash, boost::hash_value(keyword));
  boost::hash_combine(hash, boost::hash_value(page));
  boost::hash_combine(hash, boost::hash_value(maxresults));
  return hash;
}

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
