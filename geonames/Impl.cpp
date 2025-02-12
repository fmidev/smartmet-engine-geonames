// ======================================================================
/*!
 * SmartMet fminames database in memory
 */
// ======================================================================

#include "Impl.h"
#include "Engine.h"
#include <boost/algorithm/string/erase.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/bind/bind.hpp>
#include <boost/locale.hpp>
#include <boost/thread.hpp>
#include <gis/DEM.h>
#include <gis/LandCover.h>
#include <macgyver/CharsetTools.h>
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <macgyver/StringConversion.h>
#include <spine/ConfigTools.h>
#include <spine/Exceptions.h>
#include <spine/Location.h>
#include <sys/types.h>
#include <cassert>
#include <cerrno>  // iconv uses errno
#include <cmath>
#include <csignal>
#include <sstream>
#include <stdexcept>
#include <string>

// We want to allow empty databases in order to be able to build it one part a time while testing
// the engine too

const int default_port = 5432;

// We'd prefer priority to be a float, but that would require changing Spine::Location.
// We don't want to break the ABI, but to get finer control over population sort we
// scale all scores by this number.

const int priority_scale = 1000;

#ifndef NDEBUG

void print(const SmartMet::Spine::Location &loc)
{
  std::cout << "Geoid:\t" << loc.geoid << std::endl
            << "Name:\t" << loc.name << std::endl
            << "Feature:\t" << loc.feature << std::endl
            << "ISO2:\t" << loc.iso2 << std::endl
            << "Area:\t" << loc.area << std::endl
            << "Country:\t" << loc.country << std::endl
            << "Lon:\t" << loc.longitude << std::endl
            << "Lat:\t" << loc.latitude << std::endl
            << "TZ:\t" << loc.timezone << std::endl
            << "Popu:\t" << loc.population << std::endl
            << "Elev:\t" << loc.elevation << std::endl
            << "DEM:\t" << loc.dem << std::endl
            << "Priority:\t" << loc.priority << std::endl;
}

void print(const SmartMet::Spine::LocationPtr &ptr)
{
  try
  {
    if (!ptr)
      std::cout << "No location to print" << std::endl;
    else
      print(*ptr);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
#endif

namespace
{

// ----------------------------------------------------------------------
/*!
 * \brief Transform language id to normal form
 */
// ----------------------------------------------------------------------

std::string to_language(const std::string &lang)
{
  return Fmi::ascii_tolower_copy(lang);
}

// ----------------------------------------------------------------------
/*!
 * \brief Keep only desired features
 */
// ----------------------------------------------------------------------

void filter_features(SmartMet::Spine::LocationList &locs,
                     const std::function<bool(const SmartMet::Spine::LocationPtr &)> &predicate)
{
  locs.remove_if(predicate);
}

// ----------------------------------------------------------------------
/*!
 * \brief Keep only the desired page of suggest results
 */
// ----------------------------------------------------------------------

void keep_wanted_page(SmartMet::Spine::LocationList &locs,
                      unsigned int maxresults,
                      unsigned int page)
{
  if (maxresults == 0)
    return;

  // Erase the pages before the desired one
  unsigned int first = page * maxresults;
  auto pos1 = locs.begin();
  auto pos2 = pos1;
  std::advance(pos2, first);
  locs.erase(pos1, pos2);

  // Erase the remaining elements after the size of 'maxelements'.
  pos1 = locs.begin();
  auto npos2 = std::min(locs.size(), static_cast<std::size_t>(maxresults));
  std::advance(pos1, npos2);
  locs.erase(pos1, locs.end());
}

// ----------------------------------------------------------------------
/*!
 * \brief Throw for disallowed name searches
 */
// ----------------------------------------------------------------------

void check_forbidden_name_search(const std::string &name,
                                 const std::vector<boost::regex> &forbidden_rules)
{
  for (const auto &rule : forbidden_rules)
  {
    if (boost::regex_match(name, rule))
      throw Fmi::Exception(BCP, "Forbidden name search")
          .addParameter("Name", name)
          .disableLogging();
  }
}

}  // namespace

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
// ----------------------------------------------------------------------
/*!
 * \brief Characterset Conversion
 *
 * Note: boost::locale::from_utf does not do translitteration for ASCII
 * or ASCII//TRANSLIT
 */
// ----------------------------------------------------------------------

std::string Engine::Impl::iconvName(const std::string &name) const
{
  assert(utf8_to_latin1);
  return utf8_to_latin1->convert(name);
}
// ---------------------------------------------------------------------
/*!
 * \brief Impl destructor
 */
// ----------------------------------------------------------------------

Engine::Impl::~Impl() = default;

// ----------------------------------------------------------------------
/*!
 * \brief Impl constructor
 *
 * 1. Read configfile
 * 2. Read locations from DB
 * 3. Read keywords from DB
 * 4. For each keyword and the
 * full db a) Construct map of
 * geoids b) Construct neartree
 * of locations using geoid map
 *    c) Construct autocomplete
 * search tree using geoid map
 * 5. Construct map from
 * keywords to above constructs
 */
// ----------------------------------------------------------------------

Engine::Impl::Impl(std::string configfile, bool reloading)
    : itsReloading(reloading),
      itsConfigFile(std::move(configfile)),
      startTime(Fmi::SecondClock::universal_time())
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
      unsigned int suggestCacheSize = 666;
      itsConfig.lookupValue("cache.suggest_max_size", suggestCacheSize);
      if (suggestCacheSize != 666)
        std::cerr << "Warning: cache.suggest_max_size is deprecated" << std::endl;

      // Establish collator

      const libconfig::Setting &locale = itsConfig.lookup("locale");
      itsLocale = itsLocaleGenerator(locale);
      itsCollator = &std::use_facet<Collator>(itsLocale);

      // Optional second encoding for autocomplete, usually ASCII

      itsConfig.lookupValue("ascii_autocomplete", itsAsciiAutocomplete);

      if (itsAsciiAutocomplete)
      {
        try
        {
          utf8_to_latin1.reset(new Fmi::CharsetConverter("UTF-8", "ascii//translit", 256));
        }
        catch (Fmi::Exception &e)
        {
          e.addDetail(
              "You may try to set ascii_autocomplete=false"
              " to workaround problem");
          throw;
        }
      }

      setup_fallback_encodings();

      // FIXME: should we have limits in configuration instead of hardcoded here?
      if (!itsDatabaseDisabled)
      {
        query_worker_pool.reset(new Fmi::WorkerPool<Locus::Query>(
            [this]() -> std::shared_ptr<Locus::Query> {
              return std::make_shared<Locus::Query>(
                  itsHost, itsUser, itsPass, itsDatabase, itsPort);
            },
            30,
            100,
            5));

        std::shared_ptr<Locus::Query> lq = query_worker_pool->reserve();
        lq->load_iso639_table();
      }
    }
    catch (...)
    {
      Spine::Exceptions::handle("GeoNames engine");
    }
  }

  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Constructor failed!");
  }
}  // namespace Geonames

// ----------------------------------------------------------------------
/*!
 * \brief Return the global DEM
 */
// ----------------------------------------------------------------------

std::shared_ptr<Fmi::DEM> Engine::Impl::dem() const
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the global land cover data
 */
// ----------------------------------------------------------------------

std::shared_ptr<Fmi::LandCover> Engine::Impl::landCover() const
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ---------------------------------------------------------
/*!
 * \brief Return partial normal forms for to_treewords
 */
// ----------------------------------------------------------------------
//
void Engine::Impl::add_treewords(std::set<std::string> &words,
                                 const std::string &name,
                                 const std::string &area) const
{
  try
  {
    // Insert without whitespace removal first
    words.insert(to_treeword(name, area));

    // Create a mapping

    namespace bb = boost::locale::boundary;

    bb::ssegment_index map(bb::word, name.begin(), name.end(), itsLocale);

    // Ignore white space
    map.rule(bb::word_any);

    // Process only if there is more than one part

    int count = 0;
    for (auto it = map.begin(), end = map.end(); it != end; ++it)
      ++count;

    if (count < 2)
      return;

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
          words.insert(to_treeword(subname, area));
        }
      }
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Transform pattern to partial normal forms
 *
 * 1. Tranform to normal form
 * 2. Build partial matches by splitting from potential
 * word boundaries For example: Ho Chi Minh City ==> Ho
 * Chi Minh City, Chi Minh City, Minh City and City
 */
// ----------------------------------------------------------------------

std::set<std::string> Engine::Impl::to_treewords(const std::string &name,
                                                 const std::string &area) const
{
  try
  {
    std::set<std::string> ret;
    add_treewords(ret, name, area);

    if (!itsAsciiAutocomplete)
      return ret;

    // Try a second encoding

    auto name2 = iconvName(name);

    // Does not do proper transliteration:
    // auto name2 = boost::locale::conv::from_utf(name, itsExtraEncoding);

    if (name2 == name)
      return ret;

    // It differs, must have made translitterations then
    add_treewords(ret, name2, area);
    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    std::string tmp;
    std::remove_copy_if(
        name.begin(), name.end(), std::back_inserter(tmp), [](char c) { return std::isspace(c); });
    if (tmp.empty())
      return {};
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
            return override[i][setting.c_str()];
        }  // for int j
      }    // for int i
    }      // if
    return default_value;
  }
  catch (const libconfig::SettingNotFoundException &ex)
  {
    throw Fmi::Exception(BCP, "Override configuration error: " + setting, nullptr);
  }
}

void Engine::Impl::setup_fallback_encodings()
{
  std::vector<std::string> encodings;

  try
  {
    const char *s_name = "fallback_encodings";
    if (itsConfig.exists(s_name))
    {
      const libconfig::Setting &s_enc = itsConfig.lookup(s_name);
      if (s_enc.isArray())
      {
        for (const auto &enc : s_enc)
        {
          if (enc.getType() == libconfig::Setting::TypeString)
            encodings.emplace_back(enc.c_str());
          else
            throw Fmi::Exception(BCP, "Invalid value in fallback encoding array (string expected)");
        }
      }
      else if (s_enc.getType() == libconfig::Setting::TypeString)
        encodings.emplace_back(s_enc.c_str());
      else
        throw Fmi::Exception(
            BCP, "Invalid config setting fallback_encoding (string or string array expected)");
    }
    else
    {
      encodings.emplace_back("latin1");
    }
  }
  catch (const libconfig::ConfigException &ex)
  {
    throw Fmi::Exception(BCP, "Unexpected configuration error");
  }

  std::set<std::string> duplicate_check;
  for (const auto &encoding : encodings)
  {
    if (duplicate_check.insert(encoding).second)
    {
      fallback_converters.emplace_back(new Fmi::CharsetConverter(encoding, "UTF-8", 256));
      // if (itsVerbose)
      std::cout << "Geonames: Added fallback charset converter " << encoding << " --> UTF-8"
                << std::endl;
    }
    else
    {
      throw Fmi::Exception(BCP, "Duplicate fallback encoding '" + encoding + "'");
    }
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
      itsConfig.lookupValue("maxdemresolution", itsMaxDemResolution);

      if (itsDatabaseDisabled)
        std::cerr << "Warning: Geonames database is disabled" << std::endl;
      else
      {
        Fmi::Database::PostgreSQLConnectionOptions opt;
        opt.host = itsHost;
        opt.port = Fmi::stoul(itsPort);
        opt.database = itsDatabase;
        opt.username = itsUser;
        opt.password = itsPass;
        opt.encoding = "UTF8";
        Fmi::Database::PostgreSQLConnection conn;
        conn.open(opt);

        if (!conn.isConnected())
          throw Fmi::Exception(BCP, "Failed to connect to fminames database");

        const auto hash = read_database_hash_value(conn);
        if (hash)
          itsHashValue = *hash;

        Fmi::AsyncTask::interruption_point();

        // These are needed in regression tests even in mock mode
        read_countries(conn);
        read_alternate_countries(conn);

        if (!itsAutocompleteDisabled)
        {
          Fmi::AsyncTask::interruption_point();
          read_municipalities(conn);

          Fmi::AsyncTask::interruption_point();
          read_geonames(conn);  // requires read_municipalities, read_countries

          Fmi::AsyncTask::interruption_point();
          build_geoid_map();  // requires read_geonames

          Fmi::AsyncTask::interruption_point();
          read_alternate_geonames(conn);  // requires build_geoid_map

          Fmi::AsyncTask::interruption_point();
          read_alternate_municipalities(conn);

          Fmi::AsyncTask::interruption_point();
          read_keywords(conn);  // requires build_geoid_map
        }
      }
    }
    catch (const libconfig::ParseException &e)
    {
      throw Fmi::Exception::Trace(BCP, "Geo configuration error!")
          .addDetail(std::string(e.getError()) + "' on line " + std::to_string(e.getLine()));
    }
    catch (const libconfig::ConfigException &)
    {
      throw Fmi::Exception::Trace(BCP, "Geo configuration error");
    }
    catch (...)
    {
      Fmi::Exception exception(BCP, "Operation failed", nullptr);
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

    Fmi::AsyncTask::interruption_point();
    build_geotrees();  // requires ?

    Fmi::AsyncTask::interruption_point();
    build_ternarytrees();  // requires ?

    Fmi::AsyncTask::interruption_point();
    build_lang_ternarytrees();  // requires ?

    Fmi::AsyncTask::interruption_point();
    assign_priorities(itsLocations);  // requires read_geonames

    // Ready
    itsReloadOK = true;
    itsSuggestReadyFlag = true;
  }
  catch (const boost::thread_interrupted &)
  {
    throw;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Geonames autocomplete data initialization failed", nullptr);

    if (!threaded)
      throw exception;

    std::cerr << exception.getStackTrace() << std::endl;
    kill(getpid(), SIGKILL);  // If we use exit() we might get a core dump.
                              // exit(-1);
  }
}

std::optional<Fmi::DateTime> Engine::Impl::nextAutoreloadCheckTime(unsigned incr) const
try
{
  if (itsAutoReloadInterval == 0)
    return std::nullopt;

  const Fmi::DateTime tmp =
      Fmi::SecondClock::local_time() + Fmi::Minutes(incr + itsAutoReloadInterval);
  // Round down to closest itsAutoReloadInterval minutes
  const Fmi::Date date = tmp.date();
  const int minutes = tmp.time_of_day().total_minutes();
  const int remainder = minutes % itsAutoReloadInterval;
  const Fmi::DateTime next(date, Fmi::Minutes(minutes - remainder));
  return next;
}
catch (...)
{
  throw Fmi::Exception::Trace(BCP, "Operation failed!");
}

bool Engine::Impl::isGeonamesUpdated()
try
{
  if (itsDatabaseDisabled)
  {
    // std::cerr << "Warning: Geonames database is disabled" << std::endl;
    return false;
  }
  else if (Fmi::SecondClock::universal_time() - startTime < Fmi::Minutes(itsAutoReloadLimit))
  {
    // Do not allow reload too soon after startup
    return false;
  }
  else
  {
    const Fmi::DateTime now = Fmi::MicrosecClock::universal_time();
    Fmi::Database::PostgreSQLConnectionOptions opt;
    opt.host = itsHost;
    opt.port = Fmi::stoul(itsPort);
    opt.database = itsDatabase;
    opt.username = itsUser;
    opt.password = itsPass;
    opt.encoding = "UTF8";
    Fmi::Database::PostgreSQLConnection conn;
    conn.open(opt);

    if (!conn.isConnected())
      throw Fmi::Exception(BCP, "Failed to connect to fminames database");

    const auto new_hash = read_database_hash_value(conn);
    const Fmi::DateTime check_done = Fmi::MicrosecClock::universal_time();
    if (new_hash)
    {
      const bool updated = *new_hash != itsHashValue;
      std::cout << "Geonames database update check done in "
                << 0.001 * (check_done - now).total_milliseconds()
                << " seconds: " << (updated ? "update detected" : "no changes") << std::endl;
      return updated;
    }

    // No hash value available, cannot check for updates
    std::cout << "Geonames database update check done in "
              << 0.001 * (check_done - now).total_milliseconds() << " seconds: failed to get hesh"
              << std::endl;
    return false;
  }
}
catch (const Fmi::Exception &error)
{
  // We do not want to fail here. Just log the error
  std::cerr << error << std::endl;
  return false;
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
    itsDEM = std::make_shared<Fmi::DEM>(demdir);
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
    itsLandCover = std::make_shared<Fmi::LandCover>(landcoverdir);
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
    // Read DEM and GlobCover data in parallel for speed

    std::string landcoverdir;
    itsConfig.lookupValue("landcoverdir", landcoverdir);

    tg1.stop_on_error(true);
    tg1.on_task_error([](const std::string &s)
                      { throw Fmi::Exception::Trace(BCP, "Operation failed: " + s); });
    tg1.add("initDEM", [this]() { initDEM(); });
    tg1.add("initLandCover", [this]() { initLandCover(); });
    tg1.wait();

    // If we're doing a reload, we must do full initialization in this thread.
    // Otherwise we'll initialize autocomplete in a separate thread
    if (!first_construction)
      initSuggest(false);
    else
      tg1.add("initSuggest", [this]() { initSuggest(true); });

    itsConfig.lookupValue("autoreload.period", itsAutoReloadInterval);

    // Done apart from autocomplete. Ready to shutdown now though.
    itsReady = true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    if (query_worker_pool)
    {
      query_worker_pool->cancel();
    }
    tg1.stop();
    tg1.wait();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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

      // Enable sensible relative include paths
      std::filesystem::path p = itsConfigFile;
      p.remove_filename();
      itsConfig.setIncludeDir(p.c_str());

      itsConfig.readFile(itsConfigFile.c_str());
      Spine::expandVariables(itsConfig);

      if (!itsConfig.exists("database"))
      {
        Fmi::Exception exception(BCP, "Configuration file must specify the database details!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }

      const libconfig::Setting &db = itsConfig.lookup("database");

      if (!db.isGroup())
      {
        Fmi::Exception exception(BCP, "Configured value of 'database' must be a group!");
        exception.addParameter("Configuration file", itsConfigFile);
        throw exception;
      }

      itsConfig.lookupValue("verbose", itsVerbose);
      itsConfig.lookupValue("strict", itsStrict);

      // "mock" is deprecated
      if (!itsConfig.lookupValue("disable_autocomplete", itsAutocompleteDisabled))
        itsConfig.lookupValue("mock", itsAutocompleteDisabled);

      itsConfig.lookupValue("remove_underscores", itsRemoveUnderscores);

      read_config_priorities();

      read_config_security();

      const std::string &name = boost::asio::ip::host_name();
      itsUser = lookup_database("user", name).c_str();
      itsHost = lookup_database("host", name).c_str();
      itsPass = lookup_database("pass", name).c_str();
      itsDatabase = lookup_database("database", name).c_str();

      itsConfig.lookupValue("database.disable", itsDatabaseDisabled);

      // port is optional
      int port = default_port;
      itsConfig.lookupValue("database.port", port);
      itsPort = Fmi::to_string(port);
    }
    catch (const libconfig::SettingException &e)
    {
      Fmi::Exception exception(BCP, "Configuration file setting error!");
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Path", e.getPath());
      exception.addParameter("Error description", e.what());
      throw exception;
    }
    catch (const libconfig::ParseException &e)
    {
      Fmi::Exception exception(BCP, "Configuration file parsing failed!");
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Error line", std::to_string(e.getLine()));
      exception.addParameter("Error description", e.getError());
      throw exception;
    }
    catch (const libconfig::ConfigException &e)
    {
      Fmi::Exception exception(BCP, "Configuration exception!");
      exception.addParameter("Configuration file", itsConfigFile);
      exception.addParameter("Error description", e.what());
      throw exception;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Configuration read failed!");
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
    if (!itsConfig.exists("priorities"))
      return;

    itsConfig.lookupValue("priorities.match", itsNameMatchPriority);

    itsLocationPriorities.init(itsConfig);
  }
  catch (...)
  {
    auto error = Fmi::Exception::Trace(BCP, "Reading config priorities failed!");
    error.addParameter("Configuration file", itsConfigFile);
    throw error;
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
        Fmi::Exception exception(BCP, "Configured value of '" + name + "' must be a group!");
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
      Fmi::Exception exception(BCP, "Configuration file setting error!");
      exception.addParameter("Config file", itsConfigFile);
      exception.addParameter("Path", e.getPath());
      exception.addParameter("Error description", e.what());
      throw exception;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the database hash value
 */
// ----------------------------------------------------------------------

std::optional<std::size_t> Engine::Impl::read_database_hash_value(
    Fmi::Database::PostgreSQLConnection &conn)
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
    {
      if (!itsStrict)
        return std::nullopt;
      throw Fmi::Exception(BCP, "FmiNames: Failed to read database hash value");
    }

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      auto tmp = row["max"].as<double>();
      return std::size_t(std::floor(tmp + 0.5));
    }

    return std::nullopt;
  }
  catch (...)
  {
    if (itsStrict)
      throw Fmi::Exception::Trace(BCP, "Operation failed!");
    return std::nullopt;
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the countries table
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_countries(Fmi::Database::PostgreSQLConnection &conn)
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
    {
      if (itsStrict)
        throw Fmi::Exception(BCP, "FmiNames: Found no PCLI/PCLF/PCLD places from geonames table");

      std::cerr << "Warning: FmiNames: Found no PCLI/PCLF/PCLD places from geonames table"
                << std::endl;
    }

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      auto name = row["name"].as<std::string>();
      auto iso2 = row["iso2"].as<std::string>();
      itsCountries[iso2] = name;
    }

    if (itsVerbose)
      std::cout << "read_countries: " << res.size() << " countries" << std::endl;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the alternate countries
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_alternate_countries(Fmi::Database::PostgreSQLConnection &conn)
{
  try
  {
    std::string query(
        "SELECT language, g.name as gname,a.name as "
        "alt_gname,a.preferred,a.priority,length(a.name) "
        "as length FROM geonames g, alternate_geonames a WHERE "
        "g.features_code in "
        "('PCLI','PCLF','PCLD','TERR') AND g.id=a.geonames_id ORDER BY "
        "geonames_id, a.priority ASC, "
        "a.preferred DESC, length ASC, alt_gname ASC");

    if (itsVerbose)
      std::cout << "read_alternate_countries: " << query << std::endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
    {
      if (itsStrict)
        throw Fmi::Exception(BCP, "Found no country translations");

      std::cerr << "Warning: Found no country translations" << std::endl;
    }

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      auto lang = row["language"].as<std::string>();
      auto name = row["gname"].as<std::string>();
      auto translation = row["alt_gname"].as<std::string>();

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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the municipalities table
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_municipalities(Fmi::Database::PostgreSQLConnection &conn)
{
  try
  {
    std::string query("SELECT id, name FROM municipalities");

    if (itsVerbose)
      std::cout << "read_municipalities: " << query << std::endl;

    pqxx::result res = conn.executeNonTransaction(query);

    // We allow this to be empty since the table contains only Finnish information
    // if (res.empty()) throw Fmi::Exception(BCP, "FmiNames: Found nothing from municipalities
    // table");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      int id = row["id"].as<int>();
      auto name = row["name"].as<std::string>();
      itsMunicipalities[id] = name;
    }

    if (itsVerbose)
      std::cout << "read_municipalities: " << itsMunicipalities.size() << " municipalities"
                << std::endl;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the base geonames table
 */
// ----------------------------------------------------------------------

Spine::LocationPtr Engine::Impl::extract_geoname(const pqxx::result::const_iterator &row) const
{
  Spine::GeoId geoid = Fmi::stoi(row["id"].as<std::string>());
  auto name = row["name"].as<std::string>();

  auto iso2 = (row["iso2"].is_null() ? "" : row["iso2"].as<std::string>());
  auto feature = (row["feature"].is_null() ? "" : row["feature"].as<std::string>());
  auto munip = row["munip"].as<int>();
  auto lon = row["lon"].as<double>();
  auto lat = row["lat"].as<double>();
  auto tz = row["timezone"].as<std::string>();
  auto pop = (!row["population"].is_null() ? row["population"].as<int>() : 0);
  auto ele = (!row["elevation"].is_null() ? row["elevation"].as<double>()
                                          : std::numeric_limits<float>::quiet_NaN());
  double dem = (!row["dem"].is_null() ? row["dem"].as<int>() : elevation(lon, lat));
  auto admin = (!row["admin1"].is_null() ? row["admin1"].as<std::string>() : "");
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
  Spine::LocationPtr loc = std::make_shared<Spine::Location>(geoid,
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
                                                             covertype);

  return loc;
}

void Engine::Impl::read_geonames(Fmi::Database::PostgreSQLConnection &conn)
{
  try
  {
    std::string sql =
        "SELECT\n"
        "  id, geonames.name AS name, countries_iso2 as iso2, features_code as feature, \n"
        "  municipalities_id as munip, lon, lat, timezone, population, elevation, dem, landcover, "
        "admin1\n"
        "FROM\n"
        "  geonames\n"
        "INNER JOIN\n"
        "  keywords_has_geonames\n"
        "ON\n"
        "  geonames.id=keywords_has_geonames.geonames_id\n";

    if (itsConfig.exists("database.where.geonames"))
    {
      const libconfig::Setting &where_clause = itsConfig.lookup("database.where.geonames");
      sql.append(" AND ").append(static_cast<const char *>(where_clause));
    }

    if (itsVerbose)
      std::cout << "read_geonames: " << sql << std::endl;

    pqxx::result res = conn.executeNonTransaction(sql);

    if (res.empty())
    {
      if (itsStrict)
        throw Fmi::Exception(BCP, "Found nothing from fminames database");

      std::cerr << "Warning: Found nothing from fminames database" << std::endl;
    }

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      if (row["timezone"].is_null())
      {
        std::cerr << "Warning: " << Fmi::stoi(row["id"].as<std::string>()) << " '"
                  << row["name"].as<std::string>() << "' timezone is null, discarding the location"
                  << std::endl;
      }
      else
      {
        auto loc = extract_geoname(row);
        itsLocations.push_back(loc);
      }
    }

    if (itsVerbose)
      std::cout << "read_geonames: " << itsLocations.size() << " locations" << std::endl;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the alternate_geonames table
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_alternate_geonames(Fmi::Database::PostgreSQLConnection &conn)
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
    sql.append(" GROUP BY a.id HAVING count(*) > 1 ORDER BY a.geonames_id, a.language, a.priority ASC, a.preferred DESC, length ASC, name ASC");
#else
    // PostGreSQL requires all the names to be mentioned
    sql.append(
        " GROUP BY "
        "a.id,a.geonames_id,a.name,a.language,a.priority,a.preferred "
        "HAVING count(*) > 0 "
        "ORDER BY a.geonames_id, a.language, a.priority ASC, a.preferred DESC, "
        "length ASC, name ASC");
#endif

    if (itsVerbose)
      std::cout << "read_alternate_geonames: " << sql << std::endl;

    pqxx::result res = conn.executeNonTransaction(sql);

    if (res.empty())
    {
      if (itsStrict)
        throw Fmi::Exception(BCP, "Found nothing from alternate_geonames database");

      std::cerr << "Warning: Found nothing from alternate_geonames database" << std::endl;
    }

    if (itsVerbose)
      std::cout << "read_alternate_geonames: " << res.size() << " translations" << std::endl;

    // We assume sort order is geoid,language for the ifs to work
    Spine::GeoId last_handled_geoid = 0;
    std::string last_lang;
    auto idinfo = itsGeoIdMap.end();

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      Spine::GeoId geoid = Fmi::stoi(row["geonames_id"].as<std::string>());
      auto name = row["name"].as<std::string>();
      auto lang = row["language"].as<std::string>();

      Fmi::ascii_tolower(lang);

      // Handle only the first translation for each place
      if (geoid == last_handled_geoid && lang == last_lang)
        continue;

      if (geoid != last_handled_geoid)
        idinfo = itsGeoIdMap.find(geoid);  // update only when geoid changes for speed

      last_handled_geoid = geoid;
      last_lang = lang;

      // Discard translations which do not change anything to save memory and to avoid
      // duplicates more easily

      if (idinfo != itsGeoIdMap.end())
      {
        auto locptr = *idinfo->second;
        if (locptr->name == name)
          continue;
      }

      auto it = itsAlternateNames.find(geoid);
      if (it == itsAlternateNames.end())
        it = itsAlternateNames.insert(make_pair(geoid, Translations())).first;

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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the alternate_municipalities table
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_alternate_municipalities(Fmi::Database::PostgreSQLConnection &conn)
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
    // if (res.empty()) throw Fmi::Exception(BCP, "FmiNames: Found nothing from
    // alternate_municipalities database");

    if (itsVerbose)
      std::cout << "read_alternate_geonames: " << res.size() << " translations" << std::endl;

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      int munip = row["id"].as<int>();
      auto name = row["name"].as<std::string>();
      auto lang = row["language"].as<std::string>();

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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
      int score = itsLocationPriorities.getPriority(*v);

      auto &myloc = const_cast<Spine::Location &>(*v);  // NOLINT
      myloc.priority = score;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Read keywords_has_geonames
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_keywords(Fmi::Database::PostgreSQLConnection &conn)
{
  try
  {
    std::string query("SELECT keyword, geonames_id as id FROM keywords_has_geonames");

    if (itsVerbose)
      std::cout << "read_keywords: " << query << std::endl;

    pqxx::result res = conn.executeNonTransaction(query);

    if (res.empty())
    {
      if (!itsStrict)
        return;
      throw Fmi::Exception(BCP, "GeoNames: Found nothing from keywords_has_geonames database");
    }

    int count_ok = 0;
    int count_bad = 0;

    bool limited_db = itsConfig.exists("database.where");

    for (pqxx::result::const_iterator row = res.begin(); row != res.end(); ++row)
    {
      auto key = row["keyword"].as<std::string>();
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
        it = itsGeoTrees.insert(make_pair(keyword, std::make_unique<GeoTree>())).first;

      for (const auto &ptr : locs)
        it->second->insert(ptr);
    }

    // global tree

    if (itsVerbose)
      std::cout << "build_geotrees: keyword '" << FMINAMES_DEFAULT_KEYWORD << "' of size "
                << itsLocations.size() << std::endl;

    auto it =
        itsGeoTrees.insert(std::make_pair(FMINAMES_DEFAULT_KEYWORD, std::make_unique<GeoTree>()))
            .first;
    for (const auto &ptr : itsLocations)
      it->second->insert(ptr);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
        auto newtree = std::make_shared<TernaryTree>();
        it = itsTernaryTrees.insert(make_pair(keyword, newtree)).first;
      }

      for (Spine::LocationPtr &ptr : locs)
      {
        std::string specifier = ptr->area + "," + Fmi::to_string(ptr->geoid);
        auto simple_name = preprocess_name(ptr->name);

        auto names = to_treewords(simple_name, specifier);
        for (const auto &name : names)
          it->second->insert(name, ptr);
      }
    }

    // all geonames

    if (itsVerbose)
      std::cout << "build_ternarytrees: keyword '" << FMINAMES_DEFAULT_KEYWORD << "' of size "
                << itsLocations.size() << std::endl;

    auto newtree = std::make_shared<TernaryTree>();
    auto it = itsTernaryTrees.insert(std::make_pair(FMINAMES_DEFAULT_KEYWORD, newtree)).first;

    for (Spine::LocationPtr &ptr : itsLocations)
    {
      std::string specifier = ptr->area + "," + Fmi::to_string(ptr->geoid);
      auto simple_name = preprocess_name(ptr->name);

      auto names = to_treewords(simple_name, specifier);

      for (const auto &name : names)
        it->second->insert(name, ptr);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
          it =
              itsLangTernaryTreeMap.insert(std::make_pair(lang, std::make_shared<TernaryTreeMap>()))
                  .first;
        // Then find keyword specific map, keyword being "all"

        auto &tmap = *it->second;
        auto tit = tmap.find("all");

        if (tit == tmap.end())
          tit =
              tmap.insert(TernaryTreeMap::value_type("all", std::make_shared<TernaryTree>())).first;

        // Insert the word "name, area" to the tree

        auto &tree = *tit->second;

        std::string specifier = loc->area + "," + Fmi::to_string(loc->geoid);
        auto simple_name = preprocess_name(name);

        auto names = to_treewords(simple_name, specifier);
        for (const auto &treename : names)
          tree.insert(treename, *git->second);
      }
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
      const auto &keyword = kloc.first;
      const auto &locs = kloc.second;

      build_lang_ternarytrees_one_keyword(keyword, locs);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Engine::Impl::build_lang_ternarytrees_one_keyword(const std::string &keyword,
                                                       const Spine::LocationList &locs)
{
  int ntranslations = 0;

  for (const Spine::LocationPtr &loc : locs)
  {
    int geoid = loc->geoid;

    auto git = itsGeoIdMap.find(geoid);
    auto ait = itsAlternateNames.find(geoid);

    // safety check against missing settings
    if (git == itsGeoIdMap.end() || ait == itsAlternateNames.end())
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
        it = itsLangTernaryTreeMap.insert(std::make_pair(lang, std::make_shared<TernaryTreeMap>()))
                 .first;

      // Then find keyword specific map

      auto &tmap = *it->second;
      auto tit = tmap.find(keyword);

      if (tit == tmap.end())
        tit =
            tmap.insert(TernaryTreeMap::value_type(keyword, std::make_shared<TernaryTree>())).first;

      // Insert the word "name, area" to the tree

      ++ntranslations;

      TernaryTree &tree = *tit->second;

      // TODO(mheiskan): translate area

      std::string specifier = ptr->area + "," + Fmi::to_string(ptr->geoid);
      auto simple_name = preprocess_name(translation);

      auto names = to_treewords(simple_name, specifier);
      for (const auto &name : names)
        tree.insert(name, loc);
    }
  }

  if (itsVerbose)
    std::cout << "build_lang_ternarytrees_keywords: " << keyword << " with " << ntranslations
              << " translations" << std::endl;
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

    const auto &translations = trans->second;
    auto pos = translations.find(lg);

    if (pos == translations.end())
      return;

    loc.name = pos->second;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

    // Prevent name==area after translation just like Spine::Location constructor does on
    // initialization
    if (loc.name == loc.area)
      loc.area.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    // testing a->geoid == b->geoid would be redundant here
    return (((a->name == b->name)) && (a->iso2 == b->iso2) && (a->area == b->area));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Definition of unique for Spine::LocationPtr*
 */
// ----------------------------------------------------------------------

bool reallyClose(const Spine::LocationPtr &a, const Spine::LocationPtr &b)
{
  try
  {
    return (a->geoid == b->geoid);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    theLocations.unique(closeEnough);  // needed because language specific trees create duplicates

    // Sort based on priorities
    theLocations.sort([this](const Spine::LocationPtr &a, const Spine::LocationPtr &b)
                      { return prioritySort(a, b); });
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Suggest translations
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::Impl::suggest_one_keyword(const std::string &pattern,
                                                      const std::string &lang,
                                                      const std::string &keyword,
                                                      const TernaryTreePtr &tree) const
{
  Spine::LocationList result;

  std::string name;

  const auto try_pattern =
      [this, &result, &name, &lang, &keyword, tree](const std::string &pattern) -> void
  {
    // find it
    name = to_treeword(pattern);

    result = tree->findprefix(name);

    // check if there are language specific translations

    std::string lg = to_language(lang);

    auto lt = itsLangTernaryTreeMap.find(lg);
    if (lt != itsLangTernaryTreeMap.end())
    {
      auto tit = lt->second->find(keyword);
      if (tit != lt->second->end())
      {
        std::list<Spine::LocationPtr> tmpx = tit->second->findprefix(name);
        std::copy(tmpx.begin(), tmpx.end(), std::back_inserter(result));
      }
    }
  };

  if (Fmi::is_utf8(pattern))
  {
    try_pattern(pattern);
  }
  else
  {
    for (const auto &converter : fallback_converters)
    {
      std::string tmp;

      try
      {
        tmp = converter->convert(pattern);
      }
      catch (const Fmi::Exception &e)
      {
        // Not interested in errors, just try the next converter.
        continue;
      }

      try_pattern(tmp);

      if (!result.empty())
        break;  // done if the encoding produced matches
    }
  }

  // Give an extra bonus for exact matches
  add_exact_match_bonus(result, name, itsNameMatchPriority * priority_scale);

  return result;
}

Spine::LocationList Engine::Impl::suggest(
    const std::string &pattern,
    const std::function<bool(const Spine::LocationPtr &)> &predicate,
    const std::string &lang,
    const std::string &keyword,
    unsigned int page,
    unsigned int maxresults,
    bool duplicates) const
{
  if (!itsSuggestReadyFlag)
    throw Fmi::Exception(BCP, "Attempt to use geonames suggest before it is ready!");

  try
  {
    Spine::LocationList ret;

    // return null if any keyword is wrong, this mimics previous behaviour

    std::vector<std::string> keywords;
    boost::algorithm::split(keywords, keyword, boost::algorithm::is_any_of(","));

    for (const auto &key : keywords)
      if (itsTernaryTrees.find(key) == itsTernaryTrees.end())
        return ret;

    // Process all keywords

    for (const auto &key : keywords)
    {
      auto it = itsTernaryTrees.find(key);
      if (it == itsTernaryTrees.end())
        continue;

      auto result = suggest_one_keyword(pattern, lang, key, it->second);

      // Append to result for all keywords (speed optimized for first keyword)
      if (ret.empty())
        std::swap(ret, result);
      else
        ret.insert(ret.end(), result.begin(), result.end());
    }

    filter_features(ret, predicate);

    // Translate the names

    translate(ret, lang);

    // Remove duplicates

    ret.sort(basicSort);
    if (!duplicates)
      ret.unique(closeEnough);  // remove duplicate name,area matches
    else
      ret.unique(reallyClose);  // remove duplicate geoids

    // Sort based on priorities

    ret.sort([this](const Spine::LocationPtr &a, const Spine::LocationPtr &b)
             { return prioritySort(a, b); });

    // Keep the desired part. We do this after moving exact matches to the front,
    // otherwise for example "Spa, Belgium" is not very high on the list of
    // matches for "Spa". Translating everything first is expensive, but the
    // results are cached.

    keep_wanted_page(ret, maxresults, page);

    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add bonus points for exact suggest matches
 */
// ----------------------------------------------------------------------

void Engine::Impl::add_exact_match_bonus(Spine::LocationList &locs,
                                         const std::string &name,
                                         int bonus) const
{
  for (auto &loc : locs)
  {
    std::string tmpname = to_treeword(loc->name);
    if (tmpname == name)
    {
      std::unique_ptr<Spine::Location> newloc(new Spine::Location(*loc));
      newloc->priority += bonus;
      loc.reset(newloc.release());
    }
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Suggest translations for several languages
 */
// ----------------------------------------------------------------------

std::vector<Spine::LocationList> Engine::Impl::suggest(
    const std::string &pattern,
    const std::function<bool(const Spine::LocationPtr &)> &predicate,
    const std::vector<std::string> &languages,
    const std::string &keyword,
    unsigned int page,
    unsigned int maxresults,
    bool duplicates) const
{
  try
  {
    if (!itsSuggestReadyFlag)
      throw Fmi::Exception(BCP, "Attempt to use geonames suggest before it is ready!");

    if (languages.empty())
      throw Fmi::Exception(BCP, "Must provide atleast one language for autocomplete");

    if (languages.size() < 2)
      throw Fmi::Exception(BCP, "Called autocomplete for N languages with less than 2 languages");

    // return null if keyword is wrong

    std::vector<Spine::LocationList> ret;

    auto it = itsTernaryTrees.find(keyword);
    if (it == itsTernaryTrees.end())
      return ret;

    // transform pattern to collated form and find it from the search tree

    std::string name = to_treeword(pattern);
    auto candidates = it->second->findprefix(name);

    // check if there are language specific translations

    for (const auto &lang : languages)
    {
      std::string lg = to_language(lang);

      auto lt = itsLangTernaryTreeMap.find(lg);
      if (lt != itsLangTernaryTreeMap.end())
      {
        auto tit = lt->second->find(keyword);
        if (tit != lt->second->end())
        {
          std::list<Spine::LocationPtr> tmpx = tit->second->findprefix(name);
          std::copy(tmpx.begin(), tmpx.end(), std::back_inserter(candidates));
        }
      }
    }

    for (auto &loc_list : ret)
      filter_features(loc_list, predicate);

    // Remove duplicates

    candidates.sort(basicSort);
    if (!duplicates)
      candidates.unique(closeEnough);  // remove duplicate name,area matches
    else
      candidates.unique(reallyClose);  // remove duplicate geoids

    // Sort based on priorities. Note that the multilanguage version does not
    // give extra scores to exact matches since the used algorithm sorts before
    // translating the candidates. This is something that perhaps should be
    // improved later on.

    candidates.sort([this](const Spine::LocationPtr &a, const Spine::LocationPtr &b)
                    { return prioritySort(a, b); });

    // Keep the desired part.

    if (maxresults > 0)
    {
      // Erase the pages before the desired one
      unsigned int first = page * maxresults;
      auto pos1 = candidates.begin();
      auto pos2 = pos1;
      std::advance(pos2, first);
      candidates.erase(pos1, pos2);

      // Erase the remaining elements after the size of 'maxelements'.
      pos1 = candidates.begin();
      auto npos2 = std::min(candidates.size(), static_cast<std::size_t>(maxresults));
      std::advance(pos1, npos2);
      candidates.erase(pos1, candidates.end());
    }

    // Build translated results

    for (const auto &lang : languages)
    {
      auto tmp = candidates;
      translate(tmp, lang);
      ret.push_back(tmp);
    }

    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Convert FmiNames location list to SmartMet location list
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::Impl::to_locationlist(const Locus::Query::return_type &theList) const
{
  try
  {
    Spine::LocationList ret;
    for (const auto &loc : theList)
    {
      double dem = elevation(loc.lon, loc.lat);
      auto covertype = coverType(loc.lon, loc.lat);

      // Select administrative area. In particular, if the location is the
      // administrative area itself, select the country instead.

      std::string area = loc.admin;
      if (area == loc.name || area.empty())
        area = loc.country;

      Spine::Location newloc(loc.id,
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
                             covertype);
      newloc.fmisid = loc.fmisid;

      ret.push_back(Spine::LocationPtr(new Spine::Location(newloc)));
    }
    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    std::size_t key = Fmi::hash_value(theName);
    Fmi::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
      return *pos;

    check_forbidden_name_search(theName, itsForbiddenNamePatterns);

    // Locus priority sort messes up GeoEngine priority sort, so we temporarily
    // increase the limit to at least 100 names.
    auto options = theOptions;
    if (options.GetResultLimit() > 0)
      options.SetResultLimit(std::max(theOptions.GetResultLimit(), 100U));

    std::shared_ptr<Locus::Query> lq = query_worker_pool->reserve();
    Spine::LocationList ptrs = to_locationlist(lq->FetchByName(options, theName));

    assign_priorities(ptrs);
    ptrs.sort([this](const Spine::LocationPtr &a, const Spine::LocationPtr &b)
              { return prioritySort(a, b); });

    // And finally keep only the desired number of matches
    if (theOptions.GetResultLimit() > 0 && ptrs.size() > theOptions.GetResultLimit())
      ptrs.resize(theOptions.GetResultLimit());

    // Update the cache, even with empty results since searching is slow

    itsNameSearchCache.insert(key, ptrs);

    return ptrs;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    std::size_t key = Fmi::hash_value(theLongitude);
    Fmi::hash_combine(key, Fmi::hash_value(theLatitude));
    Fmi::hash_combine(key, Fmi::hash_value(theRadius));
    Fmi::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
      return *pos;

    std::shared_ptr<Locus::Query> lq = query_worker_pool->reserve();

    Spine::LocationList ptrs =
        to_locationlist(lq->FetchByLonLat(theOptions, theLongitude, theLatitude, theRadius));

    // Do not cache empty results
    if (ptrs.empty())
      return ptrs;

    // Update the cache
    itsNameSearchCache.insert(key, ptrs);
    return ptrs;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    std::size_t key = Fmi::hash_value(theId);
    Fmi::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
      return *pos;

    std::shared_ptr<Locus::Query> lq = query_worker_pool->reserve();

    Spine::LocationList ptrs = to_locationlist(lq->FetchById(theOptions, theId));

    // Do not cache empty results
    if (ptrs.empty())
      return ptrs;

    // Update the cache

    itsNameSearchCache.insert(key, ptrs);

    return ptrs;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    Fmi::hash_combine(key, Fmi::hash_value(theKeyword));
    Fmi::hash_combine(key, theOptions.HashValue());

    auto pos = itsNameSearchCache.find(key);
    if (pos)
      return *pos;

    std::shared_ptr<Locus::Query> lq = query_worker_pool->reserve();

    Spine::LocationList ptrs = to_locationlist(lq->FetchByKeyword(theOptions, theKeyword));

    // Do not cache empty results
    if (ptrs.empty())
      return ptrs;

    // Update the cache
    itsNameSearchCache.insert(key, ptrs);

    return ptrs;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Status report
 */
// ----------------------------------------------------------------------

std::unique_ptr<Spine::Table> Engine::Impl::name_cache_status() const
{
  try
  {
    auto contentList = itsNameSearchCache.getContent();

    std::unique_ptr<Spine::Table> tablePtr(new Spine::Table);
    Spine::TableFormatter::Names theNames;
    theNames.push_back("Position");
    theNames.push_back("Hits");
    theNames.push_back("Key");
    theNames.push_back("Name");
    theNames.push_back("Geoid");
    tablePtr->setNames(theNames);

    if (contentList.empty())
      return tablePtr;

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
    return tablePtr;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

// Convert TimedCache statistics to regular statistics
template <typename T>
Fmi::Cache::CacheStats convert_stats(const T &cache)
{
  auto stats = cache.getCacheStatistics();
  const Fmi::DateTime time =
      Fmi::date_time::from_time_t(std::chrono::duration_cast<std::chrono::seconds>(
                                      stats.getConstructionTime().time_since_epoch())
                                      .count());
  return {time,
          cache.maxSize(),
          cache.size(),
          stats.getHits(),
          stats.getMisses(),
          stats.getInsertSuccesses()};
}

Fmi::Cache::CacheStatistics Engine::Impl::getCacheStats() const
{
  Fmi::Cache::CacheStatistics ret;

  ret["Geonames::name_search_cache"] = itsNameSearchCache.statistics();

  return ret;
}

// ----------------------------------------------------------------------
/*!
 * \brief Read security settings
 */
// ----------------------------------------------------------------------

void Engine::Impl::read_config_security()
{
  try
  {
    if (!itsConfig.exists("security"))
      return;

    bool disabled = true;
    itsConfig.lookupValue("security.disable", disabled);
    if (disabled)
    {
      if (itsVerbose)
        std::cout << "Geonames security settings disabled" << std::endl;
      return;
    }

    if (itsConfig.exists("security.names.deny"))
    {
      const std::string name = "security.names.deny";
      const auto &deny = itsConfig.lookup(name);
      if (!deny.isArray())
        throw Fmi::Exception(BCP, "Configured value of '" + name + "' must be an array");
      for (int i = 0; i < deny.getLength(); ++i)
      {
        for (const auto &rule : deny)
        {
          if (rule.getType() != libconfig::Setting::TypeString)
            throw Fmi::Exception(
                BCP, "Configured value of '" + name + "' must be an array of pattern strings");
          itsForbiddenNamePatterns.emplace_back(rule.c_str());
          ;
        }
      }
    }
  }
  catch (const libconfig::SettingException &e)
  {
    Fmi::Exception exception(BCP, "Configuration file security setting error!");
    exception.addParameter("Path", e.getPath());
    exception.addParameter("Configuration file", itsConfigFile);
    exception.addParameter("Error description", e.what());
    throw exception;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Reading config security settings failed!");
  }
}

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
