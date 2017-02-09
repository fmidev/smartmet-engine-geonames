// ======================================================================
/*!
 * \brief Class SmartMet::Engine::Geo::Engine
 */
// ======================================================================

#include "Engine.h"
#include "Impl.h"

#include <spine/DebugFormatter.h>
#include <spine/TableFormatterOptions.h>
#include <spine/Location.h>
#include <spine/Convenience.h>
#include <spine/Exception.h>

#include <gis/DEM.h>
#include <gis/LandCover.h>
#include <macgyver/String.h>
#include <macgyver/TimeZoneFactory.h>
#include <locus/Query.h>

#include <boost/foreach.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>

#include <algorithm>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>

#include <unistd.h>

using namespace std;

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
// Default parameters for location option parsing
static const std::string default_language = "fi";
static const double default_maxdistance = 15.0;  // km

string parse_radius(const string& lat_string, double& radius)
{
  try
  {
    string radius_string("0.0");
    string latitude_string(lat_string);

    const string::size_type pos = latitude_string.find(':');
    if (pos != string::npos)
    {
      radius_string = latitude_string.substr(pos + 1);
      latitude_string = latitude_string.substr(0, pos);
    }

    radius = Fmi::stod(radius_string);

    return latitude_string;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility function to resolve DEM height if it is available
 */
// ----------------------------------------------------------------------

double demheight(const boost::shared_ptr<Fmi::DEM>& theDem,
                 double theLongitude,
                 double theLatitude,
                 double theMaxResolution)
{
  if (!theDem)
    return std::numeric_limits<double>::quiet_NaN();
  return theDem->elevation(theLongitude, theLatitude, theMaxResolution);
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility function to resolve cover type if it is available
 */
// ----------------------------------------------------------------------

Fmi::LandCover::Type covertype(const boost::shared_ptr<Fmi::LandCover> theLandCover,
                               double theLongitude,
                               double theLatitude)
{
  if (!theLandCover)
    return Fmi::LandCover::NoData;
  return theLandCover->coverType(theLongitude, theLatitude);
}

// ----------------------------------------------------------------------
/*!
 * \brief Constructor
 */
// ----------------------------------------------------------------------

Engine::Engine(const string& theConfigFile)
    : SmartMetEngine(),
      itsStartTime(boost::posix_time::second_clock::local_time()),
      itsReloading(false),
      itsNameSearchCount(0),
      itsLonLatSearchCount(0),
      itsIdSearchCount(0),
      itsKeywordSearchCount(0),
      itsSuggestCount(0),
      itsConfigFile(theConfigFile)
{
}

// ----------------------------------------------------------------------
/*!
 * \brief Destructor
 */
// ----------------------------------------------------------------------

Engine::~Engine()
{
}
// ----------------------------------------------------------------------
/*!
 * \brief Nontrivial construction happens here
 */
// ----------------------------------------------------------------------

void Engine::init()
{
  try
  {
    /*
    auto tmp = jss::make_shared<Impl>(itsConfigFile, false);
    tmp->init();
    impl = tmp;
    */
    tmpImpl = jss::make_shared<Impl>(itsConfigFile, false);
    bool first_construction = true;
    tmpImpl->init(first_construction);
    impl = tmpImpl;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Init failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Shutdown the engine
 */
// ----------------------------------------------------------------------

void Engine::shutdown()
{
  try
  {
    std::cout << "  -- Shutdown requested (geoengine)\n";

    while (true)
    {
      auto mycopy = impl.load();
      if (mycopy)
      {
        mycopy->shutdown();
        return;
      }
      else if (tmpImpl)
      {
        tmpImpl->shutdown();
        return;
      }
      else
      {
        // The is no Impl object available yet, so its initialization is probably still
        // running. There should be a way to terminate this initialization, because
        // now we have to wait its termination.

        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
      }
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void Engine::shutdownRequestFlagSet()
{
  try
  {
    // Unfortunately this does not work as it shoud when the initialization is still
    // running. That's because there is Impl object available before the initialization
    // is ready.

    auto mycopy = impl.load();
    if (mycopy)
    {
      mycopy->shutdownRequestFlagSet();
    }
    else
    {
      if (tmpImpl)
        tmpImpl->shutdownRequestFlagSet();
      ;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Hash value for the data read during initialization
 */
// ----------------------------------------------------------------------

std::size_t Engine::hash_value() const
{
  try
  {
    auto mycopy = impl.load();
    return mycopy->hash_value();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a simple name search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationPtr Engine::nameSearch(const string& theName, const string& theLang) const
{
  try
  {
    // ++itsNameSearchCount;

    // Search the name
    Locus::QueryOptions opts;
    opts.SetCountries("all");
    opts.SetSearchVariants(true);
    opts.SetLanguage(theLang);
    opts.SetResultLimit(1);

    SmartMet::Spine::LocationList result = nameSearch(opts, theName);

    if (result.empty())
      throw SmartMet::Spine::Exception(BCP, "Unknown location: " + theName);

    return result.front();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a simple lonlat search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationPtr Engine::lonlatSearch(double theLongitude,
                                                  double theLatitude,
                                                  const string& theLang,
                                                  double theMaxDistance) const
{
  try
  {
    // Search the location only if there is a search distance

    if (theMaxDistance > 0)
    {
      Locus::QueryOptions opts;
      opts.SetCountries("all");
      opts.SetSearchVariants(true);
      opts.SetLanguage(theLang);
      opts.SetResultLimit(1);

      SmartMet::Spine::LocationList result =
          lonlatSearch(opts,
                       boost::numeric_cast<float>(theLongitude),
                       boost::numeric_cast<float>(theLatitude),
                       boost::numeric_cast<float>(theMaxDistance));

      if (!result.empty())
      {
        // Keep original coordinates, dem and landcover for the named location we found
        SmartMet::Spine::LocationPtr loc = result.front();

        return SmartMet::Spine::LocationPtr(new SmartMet::Spine::Location(
            loc->geoid,
            loc->name,
            loc->iso2,
            loc->municipality,
            loc->area,
            loc->feature,
            loc->country,
            theLongitude,
            theLatitude,
            loc->timezone,
            loc->population,
            loc->elevation,
            demheight(dem(), theLongitude, theLatitude, maxDemResolution()),
            covertype(landCover(), theLongitude, theLatitude),
            loc->priority));
      }
    }

    std::string name = Fmi::to_string(theLongitude) + "," + Fmi::to_string(theLatitude);

    string timezone = Fmi::TimeZoneFactory::instance().zone_name_from_coordinate(
        boost::numeric_cast<float>(theLongitude), boost::numeric_cast<float>(theLatitude));

    return SmartMet::Spine::LocationPtr(new SmartMet::Spine::Location(
        0,
        name,
        "",  // iso2
        -1,
        "",  // area
        "",  // feature
        "",  // country
        theLongitude,
        theLatitude,
        timezone,
        -1,
        -1,
        demheight(dem(), theLongitude, theLatitude, maxDemResolution()),
        covertype(landCover(), theLongitude, theLatitude)));
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a simple ID search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationPtr Engine::idSearch(long theGeoID, const string& theLang) const
{
  try
  {
    // ++itsIdSearchCount;

    // Search the name

    Locus::QueryOptions opts;
    opts.SetCountries("all");
    opts.SetSearchVariants(true);
    opts.SetLanguage(theLang);
    opts.SetResultLimit(1);

    SmartMet::Spine::LocationList result = idSearch(opts, boost::numeric_cast<int>(theGeoID));

    if (result.empty())
      throw SmartMet::Spine::Exception(BCP, "Unknown location ID: " + Fmi::to_string(theGeoID));

    return result.front();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a name search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::nameSearch(const Locus::QueryOptions& theOptions,
                                                 const string& theName) const
{
  try
  {
    ++itsNameSearchCount;
    auto mycopy = impl.load();
    return mycopy->name_search(theOptions, theName);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a coordinate search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::latlonSearch(const Locus::QueryOptions& theOptions,
                                                   float theLatitude,
                                                   float theLongitude,
                                                   float theRadius) const
{
  try
  {
    // ++itsLonLatSearchCount;
    return lonlatSearch(theOptions, theLongitude, theLatitude, theRadius);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a coordinate search
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::lonlatSearch(const Locus::QueryOptions& theOptions,
                                                   float theLongitude,
                                                   float theLatitude,
                                                   float theRadius) const
{
  try
  {
    ++itsLonLatSearchCount;
    auto mycopy = impl.load();
    return mycopy->lonlat_search(theOptions, theLongitude, theLatitude, theRadius);
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

SmartMet::Spine::LocationList Engine::idSearch(const Locus::QueryOptions& theOptions,
                                               int theId) const
{
  try
  {
    ++itsIdSearchCount;
    auto mycopy = impl.load();
    return mycopy->id_search(theOptions, theId);
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

SmartMet::Spine::LocationList Engine::keywordSearch(const Locus::QueryOptions& theOptions,
                                                    const string& theKeyword) const
{
  try
  {
    ++itsKeywordSearchCount;
    auto mycopy = impl.load();
    return mycopy->keyword_search(theOptions, theKeyword);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Find alphabetical completions
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationList Engine::suggest(const string& thePattern,
                                              const string& theLang,
                                              const string& theKeyword,
                                              unsigned int thePage,
                                              unsigned int theMaxResults) const
{
  try
  {
    SmartMet::Spine::LocationList ret;

    ++itsSuggestCount;

    auto mycopy = impl.load();
    return mycopy->suggest(thePattern, theLang, theKeyword, thePage, theMaxResults);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Find nearest point from keyword
 *
 * Returns SmartMet::Spine::LocationPtr(0) if none is found.
 * A negative radius implies there are no distance restrictions.
 */
// ----------------------------------------------------------------------

SmartMet::Spine::LocationPtr Engine::keywordSearch(
    double lon, double lat, double radius, const string& lang, const string& keyword) const
{
  try
  {
    ++itsLonLatSearchCount;

    auto mycopy = impl.load();

    // We need suggest to be ready

    while (!mycopy->isSuggestReady())
    {
      boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    }

    // return null if keyword is wrong

    const auto it = mycopy->itsGeoTrees.find(keyword);
    if (it == mycopy->itsGeoTrees.end())
      return SmartMet::Spine::LocationPtr();

    // this is unfortunate - we must allocate new Location just to
    // get NearTree comparisons working

    SmartMet::Spine::LocationPtr dummy(new SmartMet::Spine::Location(lon, lat));

    // result will be here, if there is one

    boost::optional<SmartMet::Spine::LocationPtr> ptr = it->second->nearest(dummy, radius);
    if (!ptr)
      return SmartMet::Spine::LocationPtr();

    SmartMet::Spine::LocationPtr newptr(new SmartMet::Spine::Location(**ptr));
    mycopy->translate(newptr, lang);
    return newptr;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Map location-related HTTP query parameters to Locations
 */
// ----------------------------------------------------------------------

LocationOptions Engine::parseLocations(const SmartMet::Spine::HTTP::Request& theReq) const
{
  try
  {
    // Language selection (default -> config -> querystring order)

    string language = default_language;
    language = SmartMet::Spine::optional_string(theReq.getParameter("lang"), language);

    // Maximum distance for latlon searches

    double maxdistance = default_maxdistance;
    maxdistance = SmartMet::Spine::optional_double(theReq.getParameter("maxdistance"), maxdistance);

    // Scan all options for location references

    LocationOptions options;

    auto searchName = theReq.getParameterList("place");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& city, searchName)
      {
        double radius(0.0);
        string city_string = parse_radius(city, radius);
        SmartMet::Spine::LocationPtr loc =
            this->nameSearch(city_string, language);  // throws for empty result
        std::unique_ptr<SmartMet::Spine::Location> loc2(new SmartMet::Spine::Location(*loc));
        // in order to make difference between e.g. Helsinki, Helsinki:50
        loc2->radius = radius;
        loc2->type = SmartMet::Spine::Location::Place;
        options.add(city_string, loc2);
      }
    }

    searchName = theReq.getParameterList("places");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& places, searchName)
      {
        list<string> parts;
        boost::algorithm::split(parts, places, boost::algorithm::is_any_of(","));

        BOOST_FOREACH (const string& city, parts)
        {
          double radius(0.0);
          string city_string = parse_radius(city, radius);
          SmartMet::Spine::LocationPtr loc =
              this->nameSearch(city_string, language);  // throws for empty result
          // in order to make difference between e.g. Helsinki, Helsinki:50
          std::unique_ptr<SmartMet::Spine::Location> loc2(new SmartMet::Spine::Location(*loc));
          loc2->radius = radius;
          loc2->type = SmartMet::Spine::Location::Place;
          options.add(city_string, loc2);
        }
      }
    }

    searchName = theReq.getParameterList("area");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& area, searchName)
      {
        double radius(0.0);
        string area_string = parse_radius(area, radius);
        std::unique_ptr<SmartMet::Spine::Location> loc(
            new SmartMet::Spine::Location(area_string, radius));
        loc->radius = radius;
        loc->type = SmartMet::Spine::Location::Area;
        options.add(area, loc);
      }
    }

    searchName = theReq.getParameterList("areas");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& areas, searchName)
      {
        list<string> area_list;
        boost::algorithm::split(area_list, areas, boost::algorithm::is_any_of(","));

        BOOST_FOREACH (const string& area, area_list)
        {
          double radius(0.0);
          string area_string = parse_radius(area, radius);
          std::unique_ptr<SmartMet::Spine::Location> loc(
              new SmartMet::Spine::Location(area_string, radius));
          loc->radius = radius;
          loc->type = SmartMet::Spine::Location::Area;
          options.add(area, loc);
        }
      }
    }

    searchName = theReq.getParameterList("path");
    unsigned int path_counter(1);  // numer is added to the end of pathname
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& path, searchName)
      {
        if (path.find(' ') != std::string::npos)
          throw SmartMet::Spine::Exception(
              BCP, "Invalid path parameter " + path + ", no spaces allowed!");

        std::string tag = "path" + Fmi::to_string(path_counter++);

        // radius handling added if we want to extend path to area
        double radius(0.0);
        string path_name = parse_radius(path, radius);

        std::unique_ptr<SmartMet::Spine::Location> loc(
            new SmartMet::Spine::Location(path_name, radius));
        loc->type = SmartMet::Spine::Location::Path;
        options.add(tag, loc);
      }
    }

    searchName = theReq.getParameterList("paths");
    unsigned int paths_counter(1);  // numer is added to the end of pathname
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& paths, searchName)
      {
        list<string> path_list;
        boost::algorithm::split(path_list, paths, boost::algorithm::is_any_of(" "));

        BOOST_FOREACH (const string& path, path_list)
        {
          if (path.find(':') != std::string::npos)
            throw SmartMet::Spine::Exception(
                BCP, "Invalid path parameter " + path + ", no radius allowed!");

          std::string tag = "paths" + Fmi::to_string(paths_counter++);
          std::unique_ptr<SmartMet::Spine::Location> loc(new SmartMet::Spine::Location(path, 0.0));
          loc->type = SmartMet::Spine::Location::Path;
          options.add(tag, loc);
        }
      }
    }

    // format bbox=lon,lat,lon,lat[:radius]
    searchName = theReq.getParameterList("bbox");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& bbox, searchName)
      {
        list<string> parts;
        boost::algorithm::split(parts, bbox, boost::algorithm::is_any_of(","));
        if (parts.size() != 4)
          throw SmartMet::Spine::Exception(BCP,
                                           "Invalid bbox parameter " + bbox +
                                               ", should be in format 'lon,lat,lon,lat[:radius]'!");

        double radius(0.0);
        string bbox_string = parse_radius(bbox, radius);

        std::unique_ptr<SmartMet::Spine::Location> loc(
            new SmartMet::Spine::Location(bbox_string, radius));
        loc->type = SmartMet::Spine::Location::BoundingBox;
        options.add(bbox, loc);
      }
    }

    // format bboxes=lon,lat,lon,lat[:radius],lon,lat,lon,lat[:radius],...
    searchName = theReq.getParameterList("bboxes");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& bboxes, searchName)
      {
        vector<string> coordinates;
        boost::algorithm::split(coordinates, bboxes, boost::algorithm::is_any_of(","));
        if (coordinates.size() % 4 != 0)
          throw SmartMet::Spine::Exception(
              BCP,
              "Invalid bboxes parameter " + bboxes +
                  ", should be in format 'lon,lat,lon,lat[:radius],lon,lat,lon,lat[:radius],...'!");

        for (unsigned int i = 0; i < coordinates.size(); i += 4)
        {
          string lonstr1(coordinates[i]);
          string latstr1(coordinates[i + 1]);
          string lonstr2(coordinates[i + 2]);
          string latstr2(coordinates[i + 3]);
          string bbox_name(lonstr1 + "," + latstr1 + "," + lonstr2 + "," + latstr2);
          double radius(0.0);
          latstr2 = parse_radius(latstr2, radius);
          std::unique_ptr<SmartMet::Spine::Location> loc(
              new SmartMet::Spine::Location(bbox_name, radius));
          loc->type = SmartMet::Spine::Location::BoundingBox;
          options.add(bbox_name, loc);
        }
      }
    }

    searchName = theReq.getParameterList("lonlat");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& coords, searchName)
      {
        vector<string> parts;
        boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
        if (parts.size() % 2 != 0)
          throw SmartMet::Spine::Exception(BCP, "Invalid lonlat list: " + string(coords));

        for (unsigned int j = 0; j < parts.size(); j += 2)
        {
          // handle radius
          double radius(0.0);
          std::string latstr = parse_radius(parts[j + 1], radius);
          double lon = Fmi::stod(parts[j]);
          double lat = Fmi::stod(latstr);
          string tag = parts[j] + ',' + parts[j + 1];
          SmartMet::Spine::LocationPtr loc = this->lonlatSearch(lon, lat, language, maxdistance);
          std::unique_ptr<SmartMet::Spine::Location> loc2(new SmartMet::Spine::Location(*loc));
          loc2->radius = radius;
          loc2->type = SmartMet::Spine::Location::CoordinatePoint;
          options.add(tag, loc2);
        }
      }
    }
    searchName = theReq.getParameterList("lonlats");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& coords, searchName)
      {
        vector<string> parts;
        boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
        if (parts.size() % 2 != 0)
          throw SmartMet::Spine::Exception(BCP, "Invalid lonlats list: " + string(coords));

        for (unsigned int j = 0; j < parts.size(); j += 2)
        {
          double radius(0.0);
          std::string latstr = parse_radius(parts[j + 1], radius);
          double lon = Fmi::stod(parts[j]);
          double lat = Fmi::stod(latstr);
          string tag = parts[j] + ',' + parts[j + 1];
          SmartMet::Spine::LocationPtr loc = this->lonlatSearch(lon, lat, language, maxdistance);
          std::unique_ptr<SmartMet::Spine::Location> loc2(new SmartMet::Spine::Location(*loc));
          loc2->type = SmartMet::Spine::Location::CoordinatePoint;
          loc2->radius = radius;
          options.add(tag, loc2);
        }
      }
    }

    searchName = theReq.getParameterList("latlon");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& coords, searchName)
      {
        vector<string> parts;
        boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
        if (parts.size() % 2 != 0)
          throw SmartMet::Spine::Exception(BCP, "Invalid latlon list: " + string(coords));

        for (unsigned int j = 0; j < parts.size(); j += 2)
        {
          // Handle radius
          double radius(0.0);
          std::string latstr = parse_radius(parts[j + 1], radius);
          double lon = Fmi::stod(parts[j]);
          double lat = Fmi::stod(latstr);
          swap(lon, lat);
          string tag = parts[j] + ',' + parts[j + 1];
          SmartMet::Spine::LocationPtr loc = this->lonlatSearch(lon, lat, language, maxdistance);
          std::unique_ptr<SmartMet::Spine::Location> loc2(new SmartMet::Spine::Location(*loc));
          loc2->type = SmartMet::Spine::Location::CoordinatePoint;
          loc2->radius = radius;
          options.add(tag, loc2);
        }
      }
    }

    searchName = theReq.getParameterList("latlons");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& coords, searchName)
      {
        vector<string> parts;
        boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
        if (parts.size() % 2 != 0)
          throw SmartMet::Spine::Exception(BCP, "Invalid latlons list: " + string(coords));

        for (unsigned int j = 0; j < parts.size(); j += 2)
        {
          double radius(0.0);
          std::string latstr = parse_radius(parts[j + 1], radius);
          double lon = Fmi::stod(parts[j]);
          double lat = Fmi::stod(latstr);
          swap(lon, lat);
          string tag = parts[j] + ',' + parts[j + 1];
          SmartMet::Spine::LocationPtr loc = this->lonlatSearch(lon, lat, language, maxdistance);
          std::unique_ptr<SmartMet::Spine::Location> loc2(new SmartMet::Spine::Location(*loc));
          loc2->type = SmartMet::Spine::Location::CoordinatePoint;
          loc2->radius = radius;
          options.add(tag, loc2);
        }
      }
    }

    searchName = theReq.getParameterList("geoid");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& geoids, searchName)
      {
        list<string> parts;
        boost::algorithm::split(parts, geoids, boost::algorithm::is_any_of(","));
        BOOST_FOREACH (const string& geoid, parts)
        {
          long number = Fmi::stol(geoid);
          SmartMet::Spine::LocationPtr loc = this->idSearch(number, language);
          options.add(geoid, loc);
        }
      }
    }

    searchName = theReq.getParameterList("geoids");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& geoids, searchName)
      {
        list<string> parts;
        boost::algorithm::split(parts, geoids, boost::algorithm::is_any_of(","));
        BOOST_FOREACH (const string& geoid, parts)
        {
          long number = Fmi::stol(geoid);
          SmartMet::Spine::LocationPtr loc = this->idSearch(number, language);
          options.add(geoid, loc);
        }
      }
    }

    searchName = theReq.getParameterList("keyword");
    if (!searchName.empty())
    {
      BOOST_FOREACH (const string& keyword, searchName)
      {
        Locus::QueryOptions opts;
        opts.SetLanguage(language);
        SmartMet::Spine::LocationList places = this->keywordSearch(opts, keyword);
        if (places.empty())
          throw SmartMet::Spine::Exception(
              BCP, "No locations for keyword " + string(keyword) + " found");

        BOOST_FOREACH (SmartMet::Spine::LocationPtr& place, places)
        {
          options.add(place->name, place);
        }
      }
    }

    return options;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate ISO2 to country name
 */
// ----------------------------------------------------------------------

string Engine::countryName(const string& theIso2, const string& theLang) const
{
  try
  {
    auto mycopy = impl.load();
    return mycopy->translate_country(theIso2, theLang);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the DEM data handler
 */
// ----------------------------------------------------------------------

boost::shared_ptr<Fmi::DEM> Engine::dem() const
{
  try
  {
    auto mycopy = impl.load();
    return mycopy->dem();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the maximum allowed DEM resolution
 */
// ----------------------------------------------------------------------

unsigned int Engine::maxDemResolution() const
{
  try
  {
    auto mycopy = impl.load();
    return mycopy->maxDemResolution();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the LandCover data handler
 */
// ----------------------------------------------------------------------

boost::shared_ptr<Fmi::LandCover> Engine::landCover() const
{
  try
  {
    auto mycopy = impl.load();
    return mycopy->landCover();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return true if autocomplete data has been initialized
 *
 * Loading autocomplete data from the database is quite slow. Since the
 * data is only needed by the autocomplete plugin, the init method
 * will start a separate thread to load the data. Hence other plugins
 * will not have to wait for the autocomplete data in order to be able
 * to register themselves. The autocomplete plugin will have to poll
 * this method though before registering itself.
 */
// ----------------------------------------------------------------------

bool Engine::isSuggestReady() const
{
  auto mycopy = impl.load();
  return mycopy->isSuggestReady();
}

// ----------------------------------------------------------------------
/*!
 * \brief Priority sort a location list
 */
// ----------------------------------------------------------------------

void Engine::sort(SmartMet::Spine::LocationList& theLocations) const
{
  try
  {
    auto mycopy = impl.load();
    mycopy->sort(theLocations);  // uses Impl::itsCollator, hence lock is needed
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Reload the data from database
 */
// ----------------------------------------------------------------------

bool Engine::reload()
{
  try
  {
    if (itsReloading)
    {
      itsErrorMessage = "Geo reload was already in progress";
      return false;
    }

    itsReloading = true;

    cerr << boost::posix_time::second_clock::local_time() << " Geo reloading initiated" << endl;

    auto p = jss::make_shared<Impl>(itsConfigFile, true);  // reload=true
    bool first_construction = false;
    p->init(first_construction);

    if (!p->itsReloadOK)
    {
      itsErrorMessage = p->itsReloadError;
      cerr << boost::posix_time::second_clock::local_time()
           << " Geo reloading failed: " << p->itsReloadError << endl;
      itsReloading = false;
      return false;
    }

    impl = p;

    itsLastReload = boost::posix_time::second_clock::local_time();
    itsErrorMessage = "";
    itsReloading = false;

    cerr << itsLastReload << " Geo reloading finished" << endl;

    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Convenience function for status()
 */
// ----------------------------------------------------------------------

std::string printrate(long count, long secs)
{
  try
  {
    ostringstream out;
    if (secs > 0)
    {
      out << setprecision(6) << 1.0 * count / secs << "/sec, " << 60.0 * count / secs << "/min";
    }
    else
    {
      out << "Not available";
    }

    return out.str();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*
 * \brief Print out status information
 */
// ----------------------------------------------------------------------

StatusReturnType Engine::metadataStatus() const
{
  try
  {
    boost::shared_ptr<SmartMet::Spine::Table> cacheTable(new SmartMet::Spine::Table());
    SmartMet::Spine::TableFormatter::Names cacheHeaders;

    boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
    auto duration = now - itsStartTime;
    long secs = duration.total_seconds();

    unsigned int row = 0, column = 0;

    auto mycopy = impl.load();

    std::stringstream ss;

    ss << itsStartTime;
    cacheTable->set(column, row, ss.str());
    ss.str("");
    ++column;

    ss << duration;
    cacheTable->set(column, row, ss.str());
    ss.str("");
    ++column;

    ss << itsLastReload;
    cacheTable->set(column, row, ss.str());
    ss.str("");
    ++column;

    cacheTable->set(column, row, Fmi::to_string(mycopy->itsNameSearchCache.maxSize()));
    ++column;

    cacheTable->set(column, row, printrate(itsNameSearchCount, secs));
    ++column;

    cacheTable->set(column, row, Fmi::to_string(itsNameSearchCount));
    ++column;

    cacheTable->set(column, row, printrate(itsLonLatSearchCount, secs));
    ++column;

    cacheTable->set(column, row, Fmi::to_string(itsLonLatSearchCount));
    ++column;

    cacheTable->set(column, row, printrate(itsIdSearchCount, secs));
    ++column;

    cacheTable->set(column, row, Fmi::to_string(itsIdSearchCount));
    ++column;

    cacheTable->set(column, row, printrate(itsKeywordSearchCount, secs));
    ++column;

    cacheTable->set(column, row, Fmi::to_string(itsKeywordSearchCount));
    ++column;

    cacheTable->set(column, row, printrate(itsSuggestCount, secs));
    ++column;

    cacheTable->set(column, row, Fmi::to_string(itsSuggestCount));
    ++column;

    cacheHeaders.push_back("StartTime");
    cacheHeaders.push_back("Uptime");
    cacheHeaders.push_back("LastReload");
    cacheHeaders.push_back("CacheMaxSize");
    cacheHeaders.push_back("NameSearchRate");
    cacheHeaders.push_back("NameSearches");
    cacheHeaders.push_back("CoordinateSearchRate");
    cacheHeaders.push_back("CoordinateSearches");
    cacheHeaders.push_back("GeoidSearchRate");
    cacheHeaders.push_back("GeoidSearches");
    cacheHeaders.push_back("KeywordSearchRate");
    cacheHeaders.push_back("KeywordSearches");
    cacheHeaders.push_back("AutocompleteSearchRate");
    cacheHeaders.push_back("AutocompleteSearches");

    return std::make_pair(cacheTable, cacheHeaders);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

StatusReturnType Engine::cacheStatus() const
{
  try
  {
    boost::shared_ptr<SmartMet::Spine::Table> cacheTable(new SmartMet::Spine::Table());
    SmartMet::Spine::TableFormatter::Names cacheHeaders;

    auto mycopy = impl.load();
    mycopy->name_cache_status(cacheTable, cacheHeaders);

    return std::make_pair(cacheTable, cacheHeaders);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return error message from the reload operation
 */
// ----------------------------------------------------------------------

const std::string& Engine::errorMessage() const
{
  return itsErrorMessage;
}

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// DYNAMIC MODULE CREATION TOOLS

extern "C" void* engine_class_creator(const char* configfile, void* /* user_data */)
{
  return new SmartMet::Engine::Geonames::Engine(configfile);
}

extern "C" const char* engine_name()
{
  return "Geonames";
}
// ======================================================================
