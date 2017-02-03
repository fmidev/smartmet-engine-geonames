// ======================================================================
/*!
 * SmartMet fminames database in memory
 */
// ======================================================================

#include "Engine.h"
#include "Impl.h"

#include <spine/Location.h>
#include <spine/Exception.h>

#include <gis/DEM.h>
#include <gis/LandCover.h>

#include <macgyver/String.h>

#include <boost/algorithm/string/erase.hpp>
#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>
#include <boost/thread.hpp>

#include <cassert>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

#include <signal.h>
#include <sys/types.h>

using namespace std;

const int default_port = 5432;

#ifndef NDEBUG

void print(const SmartMet::Spine::LocationPtr &ptr)
{
  try
  {
    if (!ptr)
      cout << "No location to print" << endl;
    else
    {
      cout << "Geoid:\t" << ptr->geoid << endl
           << "Name:\t" << ptr->name << endl
           << "Feature:\t" << ptr->feature << endl
           << "ISO2:\t" << ptr->iso2 << endl
           << "Area:\t" << ptr->area << endl
           << "Lon:\t" << ptr->longitude << endl
           << "Lat:\t" << ptr->latitude << endl
           << "TZ:\t" << ptr->timezone << endl
           << "Popu:\t" << ptr->population << endl
           << "Elev:\t" << ptr->elevation << endl
           << "DEM:\t" << ptr->dem << endl
           << "Priority:\t" << ptr->priority << endl;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void print(const list<SmartMet::Spine::LocationPtr *> &ptrs)
{
  try
  {
    BOOST_FOREACH (const SmartMet::Spine::LocationPtr *ptr, ptrs)
    {
      print(*ptr);
      cout << endl;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
  try
  {
    BOOST_FOREACH (auto &pp, itsGeoTrees)
      delete pp.second;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
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

Engine::Impl::Impl(const string &configfile, bool reloading)
    : itsReady(false),
      itsReloading(reloading),
      itsReloadOK(false),
      itsReloadError(),
      itsGeoTrees(),
      itsVerbose(false),
      itsMockEngine(false),
      itsRemoveUnderscores(false),
      itsConfigFile(configfile),
      itsCollator(0),
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

      // Resize the cache according to the given size
      itsNameSearchCache.resize(cacheMaxSize);

      // Establish collator

      const libconfig::Setting &locale = itsConfig.lookup("locale");
      itsLocale = itsLocaleGenerator(locale);
      itsCollator = &use_facet<Collator>(itsLocale);
    }
    catch (const libconfig::SettingException &e)
    {
      SmartMet::Spine::Exception exception(BCP, "Configuration file setting error!");
      exception.addParameter("Path", e.getPath());
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Error description", e.what());
      throw exception;
    }
  }

  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Constructor failed!", NULL);
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Preprocess a UTF-8 name with possible bad characters
 */
// ----------------------------------------------------------------------

string Engine::Impl::preprocess_name(const string &name) const
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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

list<string> Engine::Impl::to_treewords(const string &name, const string &area) const
{
  try
  {
    namespace bb = boost::locale::boundary;

    list<string> ret;

    // Create a mapping

    bb::ssegment_index map(bb::word, name.begin(), name.end(), itsLocale);

    // Ignore white space
    map.rule(bb::word_any);

    // Extract the remaining name starting from all word boundaries

    for (bb::ssegment_index::iterator p = map.begin(), e = map.end(); p != e; ++p)
    {
      if (p->rule() != 0)
      {
        const auto &it = p->begin();
        if (it != name.end())
        {
          // From word beginning to end of the original input location name
          string subname(it, name.end());

          // Normalize for collation. Note that we collate area and comma too
          // just like when searching for a "name,area"
          ret.push_back(to_treeword(subname, area));
        }
      }
    }

    return ret;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Transform pattern to normal form
 */
// ----------------------------------------------------------------------

string Engine::Impl::to_treeword(const string &name) const
{
  try
  {
    string tmp = name;
    boost::algorithm::erase_all(tmp, " ");
    tmp = itsCollator->transform(boost::locale::collator_base::primary, tmp);

    // The standard library std::string provided in RHEL6 cannot handle
    // string comparisons if there are 0-bytes in the strings. The collator
    // in boost always ends the result in 0-byte.

    if (!tmp.empty() && tmp[tmp.size() - 1] == '\0')
      tmp.resize(tmp.size() - 1);

    return tmp;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*
 * \brief Transform search location to normal form
 */
// ----------------------------------------------------------------------

string Engine::Impl::to_treeword(const string &name, const string &area) const
{
  try
  {
    if (area.empty())
      return to_treeword(name);

    return to_treeword(name + "," + area);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Transform language id to normal form
 */
// ----------------------------------------------------------------------

string Engine::Impl::to_language(const string &lang) const
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
        std::string host = itsConfig.lookup("database.host");
        std::string user = itsConfig.lookup("database.user");
        std::string pass = itsConfig.lookup("database.pass");
        std::string base = itsConfig.lookup("database.database");
        int port = default_port;
        itsConfig.lookupValue("database.port", port);

        Locus::Connection conn(host, user, pass, base, "UTF8", Fmi::to_string(port));

        if (!conn.isConnected())
          throw SmartMet::Spine::Exception(BCP, "Failed to connect to fminames database");

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
    catch (libconfig::ParseException &e)
    {
      SmartMet::Spine::Exception exception(BCP, "Geo configuration error!", NULL);
      exception.addDetail(std::string(e.getError()) + "' on line " + std::to_string(e.getLine()));
      throw exception;
    }
    catch (libconfig::ConfigException &)
    {
      throw SmartMet::Spine::Exception(BCP, "Geo configuration error", NULL);
    }
    catch (...)
    {
      SmartMet::Spine::Exception exception(BCP, "Operation failed", NULL);
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
    SmartMet::Spine::Exception exception(
        BCP, "Geonames autocomplete data initialization failed", NULL);

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
    itsDEM.reset(new Fmi::DEM(demdir));
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
    itsLandCover.reset(new Fmi::LandCover(landcoverdir));
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
    threads.add_thread(new boost::thread(boost::bind(&Engine::Impl::initDEM, this)));
    threads.add_thread(new boost::thread(boost::bind(&Engine::Impl::initLandCover, this)));
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
        cout << "Reading fminames configuration file '" << itsConfigFile << "'" << endl;

      itsConfig.readFile(itsConfigFile.c_str());

      if (!itsConfig.exists("database"))
      {
        SmartMet::Spine::Exception exception(
            BCP, "Configuration file must specify the database details!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }

      const libconfig::Setting &db = itsConfig.lookup("database");

      if (!db.isGroup())
      {
        SmartMet::Spine::Exception exception(BCP,
                                             "Configured value of 'database' must be a group!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }

      itsConfig.lookupValue("verbose", itsVerbose);
      itsConfig.lookupValue("mock", itsMockEngine);
      itsConfig.lookupValue("remove_underscores", itsRemoveUnderscores);

      read_config_priorities();

      // Required settings to be used later on. Note that port is optional, the default is 5432
      // These will throw if the setting is not found.
      itsConfig.lookup("database.host");
      itsConfig.lookup("database.database");
      itsConfig.lookup("database.user");
      itsConfig.lookup("database.pass");
    }
    catch (const libconfig::SettingException &e)
    {
      SmartMet::Spine::Exception exception(BCP, "Configuration file setting error!");
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Path", e.getPath());
      exception.addParameter("Error description", e.what());
      throw exception;
    }
    catch (libconfig::ParseException &e)
    {
      SmartMet::Spine::Exception exception(BCP, "Configuration file parsing failed!");
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Error line", std::to_string(e.getLine()));
      exception.addParameter("Error description", e.getError());
      throw exception;
    }
    catch (libconfig::ConfigException &e)
    {
      SmartMet::Spine::Exception exception(BCP, "Configuration exception!");
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Error description", e.what());
      throw exception;
    }
  }
  catch (...)
  {
    SmartMet::Spine::Exception exception(BCP, "Configuration read failed!", NULL);
    throw exception;
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

      read_config_prioritymap("populations", itsPopulationPriorities);
      read_config_prioritymap("areas", itsAreaPriorities);
      read_config_prioritymap("countries", itsCountryPriorities);

      if (!itsConfig.exists("priorities.features"))
        return;

      const libconfig::Setting &tmp = itsConfig.lookup("priorities.features");

      if (!tmp.isGroup())
      {
        SmartMet::Spine::Exception exception(
            BCP, "Configured value of 'priorities.features' must be a group!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }
      for (int i = 0; i < tmp.getLength(); ++i)
      {
        string countryname = tmp[i].getName();
        string featurename = tmp[i];

        string mapname = "priorities." + featurename;

        if (!itsConfig.exists(mapname))
        {
          SmartMet::Spine::Exception exception(BCP,
                                               "Configuration of '" + mapname + "' is missing!");
          exception.addParameter("Configuration file", itsConfigFile);
          throw exception;
        }

        const libconfig::Setting &tmpmap = itsConfig.lookup(mapname);

        for (int j = 0; j < tmpmap.getLength(); ++j)
        {
          string name = tmpmap[j].getName();
          int value = tmpmap[j];
          itsFeaturePriorities[countryname][name] = value;
        }
      }
    }
    catch (const libconfig::SettingException &e)
    {
      SmartMet::Spine::Exception exception(BCP, "Configuration file setting error!");
      exception.addParameter("Path", e.getPath());
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Error description", e.what());
      throw exception;
    }
  }
  catch (...)
  {
    SmartMet::Spine::Exception exception(BCP, "Reading config priorities failed!", NULL);
    throw exception;
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read a single map of priorities
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_config_prioritymap(const string &partname, Priorities &priomap)
{
  try
  {
    try
    {
      string name = "priorities." + partname;

      if (!itsConfig.exists(name))
        return;

      const libconfig::Setting &tmp = itsConfig.lookup(name);

      if (!tmp.isGroup())
      {
        SmartMet::Spine::Exception exception(BCP,
                                             "Configured value of '" + name + "' must be a group!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }

      for (int i = 0; i < tmp.getLength(); ++i)
      {
        string varname = tmp[i].getName();
        int value = tmp[i];
        priomap[varname] = value;
      }
    }
    catch (const libconfig::SettingException &e)
    {
      SmartMet::Spine::Exception exception(BCP, "Configuration file setting error!");
      exception.addParameter("Config file", itsConfigFile);
      exception.addParameter("Path", e.getPath());
      exception.addParameter("Error description", e.what());
      throw exception;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
        "SELECT EXTRACT(epoch FROM date_trunc('second',max(val))) AS max FROM ("
        "SELECT max(last_modified) AS val from geonames UNION "
        "SELECT max(last_modified) AS val from keywords_has_geonames UNION "
        "SELECT max(last_modified) AS val from alternate_geonames) x";

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw SmartMet::Spine::Exception(BCP, "FmiNames: Failed to read database hash value");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      itsHashValue = row["max"].as<std::size_t>();
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
    // Note: PCLI overrides smaller political entities if there are multiple for the same iso2
    // country
    // code
    std::string query(
        "SELECT name, countries_iso2 as iso2 FROM geonames WHERE features_code in "
        "('PCLD','PCLF','PCLI') ORDER BY features_code ASC");

    if (itsVerbose)
      cout << "read_countries: " << query << endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw SmartMet::Spine::Exception(
          BCP, "FmiNames: Found no PCLI/PCLF/PCLD places from geonames table");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      string name = row["name"].as<std::string>();
      string iso2 = row["iso2"].as<std::string>();
      itsCountries[iso2] = name;
    }

    if (itsVerbose)
      cout << "read_countries: " << res.size() << " countries" << endl;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
        "as length FROM geonames g, alternate_geonames a WHERE g.features_code in "
        "('PCLI','PCLF','PCLD') AND g.id=a.geonames_id ORDER BY geonames_id, a.priority ASC, "
        "a.preferred DESC, length ASC, alt_gname ASC");

    if (itsVerbose)
      cout << "read_alternate_countries: " << query << endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw SmartMet::Spine::Exception(BCP, "FmiNames: Found no country translations");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      string lang = row["language"].as<std::string>();
      string name = row["gname"].as<std::string>();
      string translation = row["alt_gname"].as<std::string>();

      auto it = itsAlternateCountries.find(name);
      if (it == itsAlternateCountries.end())
      {
        it = itsAlternateCountries.insert(make_pair(name, Translations())).first;
      }

      Fmi::ascii_tolower(lang);

      auto &translations = it->second;
      // Note: Failure to insert is OK, we prefer the sorted order of the SQL statements
      translations.insert(std::make_pair(lang, translation));
    }

    if (itsVerbose)
      cout << "read_alternate_countries: " << res.size() << " translations" << endl;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
      cout << "read_municipalities: " << query << endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw SmartMet::Spine::Exception(BCP, "FmiNames: Found nothing from municipalities table");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      int id = row["id"].as<int>();
      string name = row["name"].as<string>();
      itsMunicipalities[id] = name;
    }

    if (itsVerbose)
      cout << "read_municipalities: " << itsMunicipalities.size() << " municipalities" << endl;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
        "SELECT id, geonames.name AS name, countries_iso2 as iso2, features_code as feature, "
        "municipalities_id as munip, lon, lat, timezone, population, elevation, dem, landcover "
        "FROM "
        "geonames WHERE EXISTS (SELECT * FROM keywords_has_geonames WHERE "
        "geonames.id=keywords_has_geonames.geonames_id)";

    if (itsConfig.exists("database.where.geonames"))
    {
      const libconfig::Setting &where_clause = itsConfig.lookup("database.where.geonames");
      sql.append(" AND ").append(static_cast<const char *>(where_clause));
    }

    if (itsVerbose)
      cout << "read_geonames: " << sql << endl;

    pqxx::result res = conn.executeNonTransaction(sql);

    if (res.empty())
      throw SmartMet::Spine::Exception(BCP, "FmiNames: Found nothing from fminames database");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      SmartMet::Spine::GeoId geoid = Fmi::stoi(row["id"].as<string>());
      string name = row["name"].as<string>();

      if (row["timezone"].is_null())
      {
        std::cerr << "Warning: " << geoid << " '" << name
                  << "' timezone is NULL, discarding the location" << std::endl;
      }
      else
      {
        string iso2 = (row["iso2"].is_null() ? "" : row["iso2"].as<string>());
        string feature = (row["feature"].is_null() ? "" : row["feature"].as<string>());
        int munip = row["munip"].as<int>();
        double lon = row["lon"].as<double>();
        double lat = row["lat"].as<double>();
        string tz = row["timezone"].as<string>();
        int pop = (!row["population"].is_null() ? row["population"].as<int>() : 0);
        double ele = (!row["elevation"].is_null() ? row["elevation"].as<double>()
                                                  : numeric_limits<float>::quiet_NaN());
        double dem = (!row["dem"].is_null() ? row["dem"].as<int>() : elevation(lon, lat));
        auto covertype = Fmi::LandCover::Type(
            (!row["landcover"].is_null() ? row["landcover"].as<int>() : coverType(lon, lat)));

        string area;
        if (munip != 0)
        {
          auto it = itsMunicipalities.find(munip);
          if (it != itsMunicipalities.end())
            area = it->second;
        }

        if (area.empty())
        {
          auto it = itsCountries.find(iso2);
          if (it != itsCountries.end())
            area = it->second;
#if 0
          else
            std::cerr << "Failed to find country " << key << " for geoid " << geoid << std::endl;
#endif
        }

        string country("");  // country will be filled in upon request
        SmartMet::Spine::LocationPtr loc(
            new SmartMet::Spine::Location(geoid,
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
      cout << "read_geonames: " << itsLocations.size() << " locations" << endl;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
        "SELECT a.geonames_id, a.name, a.language, a.priority, a.preferred, length(a.name) as "
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
        " GROUP BY a.id,a.geonames_id,a.name,a.language,a.priority,a.preferred HAVING count(*) > 0 "
        "ORDER BY a.geonames_id, a.priority ASC, a.preferred DESC, length ASC, name ASC");
#endif

    if (itsVerbose)
      cout << "read_alternate_geonames: " << sql << endl;

    pqxx::result res = conn.executeNonTransaction(sql);

    if (res.empty())
      throw SmartMet::Spine::Exception(BCP,
                                       "FmiNames: Found nothing from alternate_fminames database");

    if (itsVerbose)
      cout << "read_alternate_geonames: " << res.size() << " translations" << endl;

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      SmartMet::Spine::GeoId geoid = Fmi::stoi(row["geonames_id"].as<string>());
      string name = row["name"].as<string>();
      string lang = row["language"].as<string>();

      auto it = itsAlternateNames.find(geoid);
      if (it == itsAlternateNames.end())
      {
        it = itsAlternateNames.insert(make_pair(geoid, Translations())).first;
      }

      Fmi::ascii_tolower(lang);

      auto &translations = it->second;

      // Note that it is OK if this fails - the first translation found is preferred
      translations.insert(std::make_pair(lang, name));
    }

    if (itsVerbose)
      cout << "read_alternate_geonames done" << endl;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
        "SELECT municipalities_id as id, name, language FROM alternate_municipalities");

    if (itsVerbose)
      cout << "read_alternate_municipalities: " << query << endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw SmartMet::Spine::Exception(
          BCP, "FmiNames: Found nothing from alternate_municipalities database");

    if (itsVerbose)
      cout << "read_alternate_geonames: " << res.size() << " translations" << endl;

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      int munip = row["id"].as<int>();
      string name = row["name"].as<string>();
      string lang = row["language"].as<string>();

      auto it = itsAlternateMunicipalities.find(munip);
      if (it == itsAlternateMunicipalities.end())
      {
        it = itsAlternateMunicipalities.insert(make_pair(munip, Translations())).first;
      }

      Fmi::ascii_tolower(lang);
      it->second.insert(make_pair(lang, name));
    }

    if (itsVerbose)
      cout << "read_alternate_municipalities: " << res.size() << " translations" << endl;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
      cout << "build_geoid_map()" << endl;

    assert(itsGeoIdMap.size() == 0);
    BOOST_FOREACH (SmartMet::Spine::LocationPtr &v, itsLocations)
    {
      itsGeoIdMap.insert(GeoIdMap::value_type(v->geoid, &v));
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 *�\brief Assign priorities
 */
// ----------------------------------------------------------------------

void Engine::Impl::assign_priorities(SmartMet::Spine::LocationList &locs) const
{
  try
  {
    if (itsVerbose)
      cout << "assign_priorities" << endl;

    BOOST_FOREACH (SmartMet::Spine::LocationPtr &v, locs)
    {
      int score = population_priority(*v);
      score += area_priority(*v);
      score += country_priority(*v);
      score += feature_priority(*v);

      SmartMet::Spine::Location &myloc = const_cast<SmartMet::Spine::Location &>(*v);
      myloc.priority = score;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

int Engine::Impl::country_priority(const SmartMet::Spine::Location &loc) const
{
  try
  {
    auto it = itsCountryPriorities.find(loc.iso2);
    if (it != itsCountryPriorities.end())
      return it->second;

    it = itsCountryPriorities.find("default");
    if (it != itsCountryPriorities.end())
      return it->second;

    return 0;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

int Engine::Impl::area_priority(const SmartMet::Spine::Location &loc) const
{
  try
  {
    auto it = itsAreaPriorities.find(loc.area);
    if (it != itsAreaPriorities.end())
      return it->second;

    it = itsAreaPriorities.find("default");
    if (it != itsAreaPriorities.end())
      return it->second;

    return 0;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

int Engine::Impl::population_priority(const SmartMet::Spine::Location &loc) const
{
  try
  {
    auto it = itsPopulationPriorities.find(loc.iso2);
    if (it != itsPopulationPriorities.end())
      return static_cast<int>(round(static_cast<float>(loc.population) / it->second));

    it = itsPopulationPriorities.find("default");
    if (it != itsPopulationPriorities.end())
      return static_cast<int>(round(static_cast<float>(loc.population) / it->second));

    return 0;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

int Engine::Impl::feature_priority(const SmartMet::Spine::Location &loc) const
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
      return jt->second;

    jt = priomap.find("default");
    if (jt != priomap.end())
      return jt->second;

    return 0;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
      cout << "read_keywords: " << query << endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
      throw SmartMet::Spine::Exception(
          BCP, "FmiNames: Found nothing from keywords_has_geonames database");

    int count_ok = 0;
    int count_bad = 0;

    bool limited_db = itsConfig.exists("database.where");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      string key = row["keyword"].as<string>();
      SmartMet::Spine::GeoId geoid = Fmi::stoi(row["id"].as<string>());

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
          cerr << "  warning: keyword " << key << " uses nonexistent geoid " << geoid << endl;
        }
      }
    }

    if (itsVerbose)
      cout << "read_keywords: attached " << count_ok << " keywords to locations succesfully" << endl
           << "read_keywords: found " << count_bad << " unknown locations" << endl;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
    BOOST_FOREACH (const KeywordMap::value_type &name_locs, itsKeywords)
    {
      const string &keyword = name_locs.first;
      const SmartMet::Spine::LocationList &locs = name_locs.second;

      if (itsVerbose)
        cout << "build_geotrees:  keyword '" << keyword << "' of size " << locs.size() << endl;

      auto it = itsGeoTrees.find(keyword);
      if (it == itsGeoTrees.end())
      {
        it = itsGeoTrees.insert(make_pair(keyword, new GeoTree())).first;
      }

      BOOST_FOREACH (SmartMet::Spine::LocationPtr ptr, locs)
        it->second->insert(ptr);
    }

    // global tree

    if (itsVerbose)
      cout << "build_geotrees: keyword '" << FMINAMES_DEFAULT_KEYWORD << "' of size "
           << itsLocations.size() << endl;

    auto it = itsGeoTrees.insert(make_pair(FMINAMES_DEFAULT_KEYWORD, new GeoTree())).first;
    BOOST_FOREACH (SmartMet::Spine::LocationPtr &ptr, itsLocations)
      it->second->insert(ptr);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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

    BOOST_FOREACH (auto &name_locs, itsKeywords)
    {
      const string &keyword = name_locs.first;
      SmartMet::Spine::LocationList &locs = name_locs.second;

      if (itsVerbose)
        cout << "build_ternarytrees: keyword '" << keyword << "' of size " << locs.size() << endl;

      auto it = itsTernaryTrees.find(keyword);
      if (it == itsTernaryTrees.end())
      {
        auto newtree = boost::make_shared<TernaryTree>();
        it = itsTernaryTrees.insert(make_pair(keyword, newtree)).first;
      }

      BOOST_FOREACH (SmartMet::Spine::LocationPtr &ptr, locs)
      {
        std::string specifier = ptr->area + "," + Fmi::to_string(ptr->geoid);
        auto names = to_treewords(preprocess_name(ptr->name), specifier);
        BOOST_FOREACH (const auto &name, names)
        {
          it->second->insert(name, ptr);
        }
      }
    }

    // all geonames

    if (itsVerbose)
      cout << "build_ternarytrees: keyword '" << FMINAMES_DEFAULT_KEYWORD << "' of size "
           << itsLocations.size() << endl;

    auto newtree = boost::make_shared<TernaryTree>();
    auto it = itsTernaryTrees.insert(make_pair(FMINAMES_DEFAULT_KEYWORD, newtree)).first;

    BOOST_FOREACH (SmartMet::Spine::LocationPtr &ptr, itsLocations)
    {
      std::string specifier = ptr->area + "," + Fmi::to_string(ptr->geoid);
      auto names = to_treewords(preprocess_name(ptr->name), specifier);
      BOOST_FOREACH (const auto &name, names)
      {
        it->second->insert(name, ptr);
      }
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
      cout << "build_lang_ternarytrees" << endl;

    build_lang_ternarytrees_all();
    build_lang_ternarytrees_keywords();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
      cout << "build_lang_ternarytrees_all: " << itsAlternateNames.size() << " names" << endl;

    BOOST_FOREACH (const auto &gt, itsAlternateNames)
    {
      int geoid = gt.first;
      const Translations &translations = gt.second;

      // find the original info

      auto git = itsGeoIdMap.find(geoid);

      // safety check - should not happen if all data is present

      if (git == itsGeoIdMap.end())
        continue;

      const SmartMet::Spine::LocationPtr &loc = *git->second;

      // Now process all translations for the geoid

      BOOST_FOREACH (const auto &tt, translations)
      {
        const string &lang = tt.first;
        const string &name = tt.second;

        // Find the language specific tree

        auto it = itsLangTernaryTreeMap.find(lang);

        // If there isn't one, create it now

        if (it == itsLangTernaryTreeMap.end())
          it = itsLangTernaryTreeMap.insert(make_pair(lang, boost::make_shared<TernaryTreeMap>()))
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

        BOOST_FOREACH (const auto &treename, treenames)
        {
          if (!tree.insert(treename, *git->second))
          {
            // cout << "Failed to insert " << treename << endl;
          }
        }
      }
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
      cout << "build_lang_ternarytrees_keywords()" << endl;

    BOOST_FOREACH (auto &kloc, itsKeywords)
    {
      const string &keyword = kloc.first;

      int ntranslations = 0;

      BOOST_FOREACH (SmartMet::Spine::LocationPtr &loc, kloc.second)
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

        SmartMet::Spine::LocationPtr &ptr = *git->second;

        BOOST_FOREACH (const auto &tt, ait->second)
        {
          const string &lang = tt.first;
          const string &translation = tt.second;

          // Find the language specific tree

          auto it = itsLangTernaryTreeMap.find(lang);

          // If there isn't one, create it now

          if (it == itsLangTernaryTreeMap.end())
            it = itsLangTernaryTreeMap.insert(make_pair(lang, boost::make_shared<TernaryTreeMap>()))
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

          // TODO: translate area
          std::string specifier = ptr->area + "," + Fmi::to_string(ptr->geoid);
          auto names = to_treewords(preprocess_name(translation), specifier);

          BOOST_FOREACH (const auto &name, names)
          {
            if (!tree.insert(name, loc))
            {
              // cout << "Failed to insert " << name << endl;
            }
          }
        }
      }

      if (itsVerbose)
        cout << "build_lang_ternarytrees_keywords: " << keyword << " with " << ntranslations
             << " translations" << endl;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate location name
 */
// ----------------------------------------------------------------------

void Engine::Impl::translate_name(SmartMet::Spine::Location &loc, const string &lang) const
{
  try
  {
    // are there any translations?

    auto trans = itsAlternateNames.find(loc.geoid);
    if (trans == itsAlternateNames.end())
      return;

    // is there a translation?

    string lg = to_language(lang);

    auto &translations = trans->second;
    auto pos = translations.find(lg);

    if (pos == translations.end())
      return;

    loc.name = pos->second;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate location area
 */
// ----------------------------------------------------------------------

void Engine::Impl::translate_area(SmartMet::Spine::Location &loc, const string &lang) const
{
  try
  {
    string lg = to_language(lang);

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
      // Try translating country name first
      auto it = itsAlternateCountries.find(loc.area);

      if (it != itsAlternateCountries.end())
      {
        const auto &translations = it->second;

        auto pos = translations.find(lg);
        if (pos == translations.end())
          return;
        loc.area = pos->second;
      }
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate a location
 */
// ----------------------------------------------------------------------

void Engine::Impl::translate(SmartMet::Spine::LocationPtr &loc, const string &lang) const
{
  try
  {
    std::unique_ptr<SmartMet::Spine::Location> newloc(new SmartMet::Spine::Location(*loc));
    translate_name(*newloc, lang);
    translate_area(*newloc, lang);
    newloc->country = translate_country(newloc->iso2, lang);
    loc.reset(newloc.release());
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate a location list
 */
// ----------------------------------------------------------------------

void Engine::Impl::translate(SmartMet::Spine::LocationList &locs, const string &lang) const
{
  try
  {
    BOOST_FOREACH (SmartMet::Spine::LocationPtr &loc, locs)
      translate(loc, lang);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate country name
 */
// ----------------------------------------------------------------------

string Engine::Impl::translate_country(const string &iso2, const string &lang) const
{
  try
  {
    string lg = to_language(lang);

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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Priority sorter
 */
// ----------------------------------------------------------------------

bool Engine::Impl::prioritySort(const SmartMet::Spine::LocationPtr &a,
                                const SmartMet::Spine::LocationPtr &b) const
{
  try
  {
    // First try priority
    if (a->priority != b->priority)
      return (a->priority > b->priority);

    // Last use alphabetical sort.

    string aname = to_treeword(a->name);
    string bname = to_treeword(b->name);

    if (aname != bname)
      return (aname < bname);

    return (a->area < b->area);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Trivial sort to find duplicates (doesn't care about localization)
 */
// ----------------------------------------------------------------------

bool basicSort(SmartMet::Spine::LocationPtr a, SmartMet::Spine::LocationPtr b)
{
  try
  {
    if (a->name != b->name)
    {
      return (a->name < b->name);
    }
    else if (a->iso2 != b->iso2)
    {
      return (a->iso2 < b->iso2);
    }
    else if (a->area != b->area)
    {
      return (a->area < b->area);
    }
    else
    {
      return (a->priority > b->priority);
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Definition of unique for SmartMet::Spine::LocationPtr*
 */
// ----------------------------------------------------------------------

bool closeEnough(SmartMet::Spine::LocationPtr a, SmartMet::Spine::LocationPtr b)
{
  try
  {
    if (((a->name == b->name)) && (a->iso2 == b->iso2) && (a->area == b->area))
    {
      return true;
    }
    else
    {
      return false;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Priority sort a list of locations
 */
// ----------------------------------------------------------------------

void Engine::Impl::sort(SmartMet::Spine::LocationList &theLocations) const
{
  try
  {
    assign_priorities(theLocations);
    theLocations.sort(basicSort);
    theLocations.unique(closeEnough);  // needed because language specific trees create duplicates
    // Sort based on priorities
    theLocations.sort(boost::bind(&Impl::prioritySort, this, _1, _2));
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Suggest translations
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::Impl::suggest(const string &pattern,
                                                    const string &lang,
                                                    const string &keyword,
                                                    unsigned int page,
                                                    unsigned int maxresults) const
{
  if (!itsSuggestReadyFlag)
    throw SmartMet::Spine::Exception(
        BCP, "Attempt to use geonames suggest before it is ready!", NULL);

  try
  {
    SmartMet::Spine::LocationList ret;

    // return null if keyword is wrong

    auto it = itsTernaryTrees.find(keyword);
    if (it == itsTernaryTrees.end())
      return ret;

    // transform to collated form

    string name = to_treeword(pattern);

    // find it

    ret = it->second->findprefix(name);

    // check if there are language specific translations

    string lg = to_language(lang);

    auto lt = itsLangTernaryTreeMap.find(lg);
    if (lt != itsLangTernaryTreeMap.end())
    {
      auto tit = lt->second->find(keyword);
      if (tit != lt->second->end())
      {
        list<SmartMet::Spine::LocationPtr> tmpx = tit->second->findprefix(name);
        BOOST_FOREACH (const SmartMet::Spine::LocationPtr &ptr, tmpx)
        {
          ret.push_back(ptr);
        }
      }
    }

    ret.sort(basicSort);
    ret.unique(closeEnough);  // needed because language specific trees create duplicates

    // Sort based on priorities

    ret.sort(boost::bind(&Impl::prioritySort, this, _1, _2));

    // Keep the desired part
    if (maxresults > 0)
    {
      // should do this using erase
      unsigned int first = page * maxresults;
      for (std::size_t i = 0; i < first; i++)
        ret.pop_front();
      while (ret.size() > maxresults)
        ret.pop_back();
    }

    // Translate and finish up
    translate(ret, lang);

    // If there is an exact name match, move it to first place.
    // This will move only the first match though!

    for (auto iter = ret.begin(); iter != ret.end(); ++iter)
    {
      string tmpname = to_treeword((*iter)->name);

      if (tmpname == name)
      {
        SmartMet::Spine::LocationPtr ptr = *iter;
        ret.erase(iter);
        ret.push_front(ptr);
        break;
      }
    }

    return ret;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Convert FmiNames location list to SmartMet location list
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::Impl::to_locationlist(
    const Locus::Query::return_type &theList)
{
  try
  {
    SmartMet::Spine::LocationList ret;
    BOOST_FOREACH (const auto &loc, theList)
    {
      double dem = elevation(loc.lon, loc.lat);
      auto covertype = coverType(loc.lon, loc.lat);

      // Select administrative area. In particular, if the location is the administrative
      // area itself, select the country instead.

      string area = loc.admin;
      if (area == loc.name || area.empty())
        area = loc.country;

      SmartMet::Spine::LocationPtr ptr(
          new SmartMet::Spine::Location(loc.id,
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Answer a name search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::Impl::name_search(const Locus::QueryOptions &theOptions,
                                                        const string &theName)
{
  if (itsDatabaseDisabled)
    return {};

  try
  {
    std::size_t key = boost::hash_value(theName);
    boost::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
    {
      return *pos;
    }
    else
    {
      std::string host = itsConfig.lookup("database.host");
      std::string user = itsConfig.lookup("database.user");
      std::string pass = itsConfig.lookup("database.pass");
      std::string base = itsConfig.lookup("database.database");
      int port = default_port;
      itsConfig.lookupValue("database.port", port);

      Locus::Query lq(host, user, pass, base, Fmi::to_string(port));
      SmartMet::Spine::LocationList ptrs = to_locationlist(lq.FetchByName(theOptions, theName));
      assign_priorities(ptrs);
      ptrs.sort(boost::bind(&Impl::prioritySort, this, _1, _2));

      // Do not cache empty results
      if (ptrs.empty())
        return ptrs;

      // Update the cache

      itsNameSearchCache.insert(key, ptrs);

      return ptrs;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Coordinate search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::Impl::lonlat_search(const Locus::QueryOptions &theOptions,
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
    {
      return *pos;
    }
    else
    {
      std::string host = itsConfig.lookup("database.host");
      std::string user = itsConfig.lookup("database.user");
      std::string pass = itsConfig.lookup("database.pass");
      std::string base = itsConfig.lookup("database.database");
      int port = default_port;
      itsConfig.lookupValue("database.port", port);

      Locus::Query lq(host, user, pass, base, Fmi::to_string(port));

      SmartMet::Spine::LocationList ptrs =
          to_locationlist(lq.FetchByLonLat(theOptions, theLongitude, theLatitude, theRadius));

      // Do not cache empty results
      if (ptrs.empty())
        return ptrs;

      // Update the cache
      itsNameSearchCache.insert(key, ptrs);
      return ptrs;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a id search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::Impl::id_search(const Locus::QueryOptions &theOptions,
                                                      int theId)
{
  if (itsDatabaseDisabled)
    return {};

  try
  {
    std::size_t key = boost::hash_value(theId);
    boost::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
    {
      return *pos;
    }
    else
    {
      std::string host = itsConfig.lookup("database.host");
      std::string user = itsConfig.lookup("database.user");
      std::string pass = itsConfig.lookup("database.pass");
      std::string base = itsConfig.lookup("database.database");
      int port = default_port;
      itsConfig.lookupValue("database.port", port);

      Locus::Query lq(host, user, pass, base, Fmi::to_string(port));

      SmartMet::Spine::LocationList ptrs = to_locationlist(lq.FetchById(theOptions, theId));

      // Do not cache empty results
      if (ptrs.empty())
        return ptrs;

      // Update the cache

      itsNameSearchCache.insert(key, ptrs);

      return ptrs;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a keyword search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::Impl::keyword_search(const Locus::QueryOptions &theOptions,
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
    {
      return *pos;
    }
    else
    {
      std::string host = itsConfig.lookup("database.host");
      std::string user = itsConfig.lookup("database.user");
      std::string pass = itsConfig.lookup("database.pass");
      std::string base = itsConfig.lookup("database.database");
      int port = default_port;
      itsConfig.lookupValue("database.port", port);

      Locus::Query lq(host, user, pass, base, Fmi::to_string(port));

      SmartMet::Spine::LocationList ptrs =
          to_locationlist(lq.FetchByKeyword(theOptions, theKeyword));

      // Do not cache empty results
      if (ptrs.empty())
        return ptrs;

      // Update the cache
      itsNameSearchCache.insert(key, ptrs);

      return ptrs;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Status report
 */
// ----------------------------------------------------------------------

void Engine::Impl::name_cache_status(boost::shared_ptr<SmartMet::Spine::Table> tablePtr,
                                     SmartMet::Spine::TableFormatter::Names &theNames)
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
    BOOST_FOREACH (const auto &ReportObject, contentList)
    {
      const std::size_t count = ReportObject.itsHits;
      const std::size_t key = ReportObject.itsKey;
      const SmartMet::Spine::LocationPtr &loc = ReportObject.itsValue.front();

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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
