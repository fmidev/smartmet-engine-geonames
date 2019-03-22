// ======================================================================
/*!
 * \brief Class Engine::Geo::Engine
 */
// ======================================================================

#include "Engine.h"
#include "Impl.h"
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <gis/DEM.h>
#include <gis/LandCover.h>
#include <locus/Query.h>
#include <macgyver/StringConversion.h>
#include <macgyver/TimeZoneFactory.h>
#include <spine/Convenience.h>
#include <spine/DebugFormatter.h>
#include <spine/Exception.h>
#include <spine/Location.h>
#include <spine/TableFormatterOptions.h>
#include <algorithm>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <unistd.h>

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
// Default parameters for location option parsing
static const std::string default_language = "fi";
static const double default_maxdistance = 15.0;  // km

std::string parse_radius(const std::string& inputStr, double& radius)
{
  try
  {
    std::string radius_string("0.0");
    std::string output_string(inputStr);

    const std::string::size_type pos = output_string.find(':');
    if (pos != std::string::npos)
    {
      radius_string = output_string.substr(pos + 1);
      output_string = output_string.substr(0, pos);
    }

    radius = Fmi::stod(radius_string);

    return output_string;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

Fmi::LandCover::Type covertype(const boost::shared_ptr<Fmi::LandCover>& theLandCover,
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

Engine::Engine(const std::string& theConfigFile)
    : itsStartTime(boost::posix_time::second_clock::local_time()),
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

Engine::~Engine() = default;

// ----------------------------------------------------------------------
/*!
 * \brief Nontrivial construction happens here
 */
// ----------------------------------------------------------------------

void Engine::init()
{
  try
  {
    tmpImpl = boost::make_shared<Impl>(itsConfigFile, false);
    bool first_construction = true;
    tmpImpl->init(first_construction);
    boost::atomic_store(&impl, tmpImpl);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Init failed!");
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
      auto mycopy = boost::atomic_load(&impl);
      if (mycopy)
      {
        mycopy->shutdown();
        return;
      }
      if (tmpImpl)
      {
        tmpImpl->shutdown();
        return;
      }

      // The is no Impl object available yet, so its initialization is probably still
      // running. There should be a way to terminate this initialization, because
      // now we have to wait its termination.

      boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

void Engine::shutdownRequestFlagSet()
{
  try
  {
    // Unfortunately this does not work as it shoud when the initialization is still
    // running. That's because there is Impl object available before the initialization
    // is ready.

    auto mycopy = boost::atomic_load(&impl);
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    auto mycopy = boost::atomic_load(&impl);
    return mycopy->hash_value();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a simple name search
 */
// ----------------------------------------------------------------------

Spine::LocationPtr Engine::nameSearch(const std::string& theName, const std::string& theLang) const
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

    Spine::LocationList result = nameSearch(opts, theName);

    if (result.empty())
      throw Spine::Exception(BCP, "Unknown location: " + theName);

    return result.front();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a simple lonlat search
 */
// ----------------------------------------------------------------------

Spine::LocationPtr Engine::lonlatSearch(double theLongitude,
                                        double theLatitude,
                                        const std::string& theLang,
                                        double theMaxDistance) const
{
  std::string features = "";  // use defaults
  return featureSearch(theLongitude, theLatitude, theLang, features, theMaxDistance);
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a simple lonlat search
 */
// ----------------------------------------------------------------------

Spine::LocationPtr Engine::featureSearch(double theLongitude,
                                         double theLatitude,
                                         const std::string& theLang,
                                         const std::string& theFeatures,
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
      if (!theFeatures.empty())
        opts.SetFeatures(theFeatures);

      Spine::LocationList result = lonlatSearch(opts,
                                                boost::numeric_cast<float>(theLongitude),
                                                boost::numeric_cast<float>(theLatitude),
                                                boost::numeric_cast<float>(theMaxDistance));

      if (!result.empty())
      {
        // Keep original coordinates, dem and landcover for the named location we found
        Spine::LocationPtr loc = result.front();

        return Spine::LocationPtr(
            new Spine::Location(loc->geoid,
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

    std::string timezone = Fmi::TimeZoneFactory::instance().zone_name_from_coordinate(
        boost::numeric_cast<float>(theLongitude), boost::numeric_cast<float>(theLatitude));

    return Spine::LocationPtr(
        new Spine::Location(0,
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a simple ID search
 */
// ----------------------------------------------------------------------

Spine::LocationPtr Engine::idSearch(long theGeoID, const std::string& theLang) const
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

    Spine::LocationList result = idSearch(opts, boost::numeric_cast<int>(theGeoID));

    if (result.empty())
      throw Spine::Exception(BCP, "Unknown location ID: " + Fmi::to_string(theGeoID));

    return result.front();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a name search
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::nameSearch(const Locus::QueryOptions& theOptions,
                                       const std::string& theName) const
{
  try
  {
    ++itsNameSearchCount;
    auto mycopy = boost::atomic_load(&impl);
    return mycopy->name_search(theOptions, theName);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a coordinate search
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::latlonSearch(const Locus::QueryOptions& theOptions,
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a coordinate search
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::lonlatSearch(const Locus::QueryOptions& theOptions,
                                         float theLongitude,
                                         float theLatitude,
                                         float theRadius) const
{
  try
  {
    ++itsLonLatSearchCount;
    auto mycopy = boost::atomic_load(&impl);
    return mycopy->lonlat_search(theOptions, theLongitude, theLatitude, theRadius);
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

Spine::LocationList Engine::idSearch(const Locus::QueryOptions& theOptions, int theId) const
{
  try
  {
    ++itsIdSearchCount;
    auto mycopy = boost::atomic_load(&impl);
    return mycopy->id_search(theOptions, theId);
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

Spine::LocationList Engine::keywordSearch(const Locus::QueryOptions& theOptions,
                                          const std::string& theKeyword) const
{
  try
  {
    ++itsKeywordSearchCount;
    auto mycopy = boost::atomic_load(&impl);
    return mycopy->keyword_search(theOptions, theKeyword);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Find alphabetical completions
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::suggest(const std::string& thePattern,
                                    const std::string& theLang,
                                    const std::string& theKeyword,
                                    unsigned int thePage,
                                    unsigned int theMaxResults) const
{
  try
  {
    Spine::LocationList ret;

    ++itsSuggestCount;

    auto mycopy = boost::atomic_load(&impl);
    return mycopy->suggest(thePattern, theLang, theKeyword, thePage, theMaxResults, false);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Find alphabetical completions
 */
// ----------------------------------------------------------------------

Spine::LocationList Engine::suggestDuplicates(const std::string& thePattern,
                                              const std::string& theLang,
                                              const std::string& theKeyword,
                                              unsigned int thePage,
                                              unsigned int theMaxResults) const
{
  try
  {
    Spine::LocationList ret;

    ++itsSuggestCount;

    auto mycopy = boost::atomic_load(&impl);
    return mycopy->suggest(thePattern, theLang, theKeyword, thePage, theMaxResults, true);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Find nearest point from keyword
 *
 * Returns Spine::LocationPtr(0) if none is found.
 * A negative radius implies there are no distance restrictions.
 */
// ----------------------------------------------------------------------

Spine::LocationPtr Engine::keywordSearch(double theLongitude,
                                         double theLatitude,
                                         double theRadius,
                                         const std::string& theLang,
                                         const std::string& theKeyword) const
{
  try
  {
    ++itsLonLatSearchCount;

    auto mycopy = boost::atomic_load(&impl);

    // We need suggest to be ready

    while (!mycopy->isSuggestReady())
    {
      boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    }

    // return null if keyword is wrong

    const auto it = mycopy->itsGeoTrees.find(theKeyword);
    if (it == mycopy->itsGeoTrees.end())
      return Spine::LocationPtr();

    // this is unfortunate - we must allocate new Location just to
    // get NearTree comparisons working

    Spine::LocationPtr dummy(new Spine::Location(theLongitude, theLatitude));

    // result will be here, if there is one

    boost::optional<Spine::LocationPtr> ptr = it->second->nearest(dummy, theRadius);
    if (!ptr)
      return Spine::LocationPtr();

    Spine::LocationPtr newptr(new Spine::Location(**ptr));
    mycopy->translate(newptr, theLang);
    return newptr;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Map location-related HTTP query parameters to Locations
 */
// ----------------------------------------------------------------------

LocationOptions Engine::parseLocations(const Spine::HTTP::Request& theReq) const
{
  try
  {
    // Language selection (default -> config -> querystring order)

    std::string language = default_language;
    language = Spine::optional_string(theReq.getParameter("lang"), language);

    // Maximum distance for latlon searches

    double maxdistance = default_maxdistance;
    maxdistance = Spine::optional_double(theReq.getParameter("maxdistance"), maxdistance);

    // Scan all options for location references

    LocationOptions options;

    auto searchName = theReq.getParameterList("place");
    if (!searchName.empty())
    {
      for (const std::string& city : searchName)
      {
        double radius = 0.0;
        std::string city_string = parse_radius(city, radius);
        Spine::LocationPtr loc =
            this->nameSearch(city_string, language);  // throws for empty result
        std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
        // in order to make difference between e.g. Helsinki, Helsinki:50
        loc2->radius = radius;
        loc2->type = Spine::Location::Place;
        options.add(city_string, loc2);
      }
    }

    searchName = theReq.getParameterList("places");
    if (!searchName.empty())
    {
      for (const std::string& places : searchName)
      {
        std::list<std::string> parts;
        boost::algorithm::split(parts, places, boost::algorithm::is_any_of(","));

        for (const std::string& city : parts)
        {
          double radius = 0.0;
          std::string city_string = parse_radius(city, radius);
          Spine::LocationPtr loc =
              this->nameSearch(city_string, language);  // throws for empty result
          // in order to make difference between e.g. Helsinki, Helsinki:50
          std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
          loc2->radius = radius;
          loc2->type = Spine::Location::Place;
          options.add(city_string, loc2);
        }
      }
    }

    searchName = theReq.getParameterList("area");
    if (!searchName.empty())
    {
      for (const std::string& area : searchName)
      {
        double radius = 0.0;
        std::string area_string = parse_radius(area, radius);
        std::unique_ptr<Spine::Location> loc(new Spine::Location(area_string, radius));
        loc->radius = radius;
        loc->type = Spine::Location::Area;
        options.add(area, loc);
      }
    }

    searchName = theReq.getParameterList("areas");
    if (!searchName.empty())
    {
      for (const std::string& areas : searchName)
      {
        std::list<std::string> area_list;
        boost::algorithm::split(area_list, areas, boost::algorithm::is_any_of(","));

        for (const std::string& area : area_list)
        {
          double radius = 0.0;
          std::string area_string = parse_radius(area, radius);
          std::unique_ptr<Spine::Location> loc(new Spine::Location(area_string, radius));
          loc->radius = radius;
          loc->type = Spine::Location::Area;
          options.add(area, loc);
        }
      }
    }

    searchName = theReq.getParameterList("path");
    unsigned int path_counter(1);  // numer is added to the end of pathname
    if (!searchName.empty())
    {
      for (const std::string& path : searchName)
      {
        if (path.find(' ') != std::string::npos)
          throw Spine::Exception(BCP, "Invalid path parameter " + path + ", no spaces allowed!");

        std::string tag = "path" + Fmi::to_string(path_counter++);

        // radius handling added if we want to extend path to area
        double radius = 0.0;
        std::string path_name = parse_radius(path, radius);

        std::unique_ptr<Spine::Location> loc(new Spine::Location(path_name, radius));
        loc->type = Spine::Location::Path;
        options.add(tag, loc);
      }
    }

    searchName = theReq.getParameterList("paths");
    unsigned int paths_counter(1);  // numer is added to the end of pathname
    if (!searchName.empty())
    {
      for (const std::string& paths : searchName)
      {
        std::list<std::string> path_list;
        boost::algorithm::split(path_list, paths, boost::algorithm::is_any_of(" "));

        for (const std::string& path : path_list)
        {
          if (path.find(':') != std::string::npos)
            throw Spine::Exception(BCP, "Invalid path parameter " + path + ", no radius allowed!");

          std::string tag = "paths" + Fmi::to_string(paths_counter++);
          std::unique_ptr<Spine::Location> loc(new Spine::Location(path, 0.0));
          loc->type = Spine::Location::Path;
          options.add(tag, loc);
        }
      }
    }

    // format bbox=lon,lat,lon,lat[:radius]
    searchName = theReq.getParameterList("bbox");
    if (!searchName.empty())
    {
      for (const std::string& bbox : searchName)
      {
        std::list<std::string> parts;
        boost::algorithm::split(parts, bbox, boost::algorithm::is_any_of(","));
        if (parts.size() != 4)
          throw Spine::Exception(BCP,
                                 "Invalid bbox parameter " + bbox +
                                     ", should be in format 'lon,lat,lon,lat[:radius]'!");

        double radius = 0.0;
        std::string bbox_string = parse_radius(bbox, radius);

        std::unique_ptr<Spine::Location> loc(new Spine::Location(bbox_string, radius));
        loc->type = Spine::Location::BoundingBox;
        options.add(bbox, loc);
      }
    }

    // format bboxes=lon,lat,lon,lat[:radius],lon,lat,lon,lat[:radius],...
    searchName = theReq.getParameterList("bboxes");
    if (!searchName.empty())
    {
      for (const std::string& bboxes : searchName)
      {
        std::vector<std::string> coordinates;
        boost::algorithm::split(coordinates, bboxes, boost::algorithm::is_any_of(","));
        if (coordinates.size() % 4 != 0)
          throw Spine::Exception(BCP,
                                 "Invalid bboxes parameter " + bboxes +
                                     ", should be in format "
                                     "'lon,lat,lon,lat[:radius],lon,lat,lon,lat[:radius],...'!");

        for (unsigned int i = 0; i < coordinates.size(); i += 4)
        {
          std::string bbox_name = fmt::sprintf("{},{},{},{}",
                                               coordinates[i],
                                               coordinates[i + 1],
                                               coordinates[i + 2],
                                               coordinates[i + 3]);
          double radius = 0.0;
          parse_radius(coordinates[i + 3], radius);
          std::unique_ptr<Spine::Location> loc(new Spine::Location(bbox_name, radius));
          loc->type = Spine::Location::BoundingBox;
          options.add(bbox_name, loc);
        }
      }
    }

    searchName = theReq.getParameterList("lonlat");
    if (!searchName.empty())
    {
      for (const std::string& coords : searchName)
      {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
        if (parts.size() % 2 != 0)
          throw Spine::Exception(BCP, "Invalid lonlat list: " + std::string(coords));

        for (unsigned int j = 0; j < parts.size(); j += 2)
        {
          // handle radius
          double radius = 0.0;
          std::string latstr = parse_radius(parts[j + 1], radius);
          double lon = Fmi::stod(parts[j]);
          double lat = Fmi::stod(latstr);
          std::string tag = parts[j] + ',' + parts[j + 1];
          Spine::LocationPtr loc = this->lonlatSearch(lon, lat, language, maxdistance);
          std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
          loc2->radius = radius;
          loc2->type = Spine::Location::CoordinatePoint;
          options.add(tag, loc2);
        }
      }
    }
    searchName = theReq.getParameterList("lonlats");
    if (!searchName.empty())
    {
      for (const std::string& coords : searchName)
      {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
        if (parts.size() % 2 != 0)
          throw Spine::Exception(BCP, "Invalid lonlats list: " + std::string(coords));

        for (unsigned int j = 0; j < parts.size(); j += 2)
        {
          double radius = 0.0;
          std::string latstr = parse_radius(parts[j + 1], radius);
          double lon = Fmi::stod(parts[j]);
          double lat = Fmi::stod(latstr);
          std::string tag = parts[j] + ',' + parts[j + 1];
          Spine::LocationPtr loc = this->lonlatSearch(lon, lat, language, maxdistance);
          std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
          loc2->type = Spine::Location::CoordinatePoint;
          loc2->radius = radius;
          options.add(tag, loc2);
        }
      }
    }

    searchName = theReq.getParameterList("latlon");
    if (!searchName.empty())
    {
      for (const std::string& coords : searchName)
      {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
        if (parts.size() % 2 != 0)
          throw Spine::Exception(BCP, "Invalid latlon list: " + std::string(coords));

        for (unsigned int j = 0; j < parts.size(); j += 2)
        {
          // Handle radius
          double radius = 0.0;
          std::string latstr = parse_radius(parts[j + 1], radius);
          double lon = Fmi::stod(parts[j]);
          double lat = Fmi::stod(latstr);
          std::swap(lon, lat);
          std::string tag = parts[j] + ',' + parts[j + 1];
          Spine::LocationPtr loc = this->lonlatSearch(lon, lat, language, maxdistance);
          std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
          loc2->type = Spine::Location::CoordinatePoint;
          loc2->radius = radius;
          options.add(tag, loc2);
        }
      }
    }

    searchName = theReq.getParameterList("latlons");
    if (!searchName.empty())
    {
      for (const std::string& coords : searchName)
      {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
        if (parts.size() % 2 != 0)
          throw Spine::Exception(BCP, "Invalid latlons list: " + std::string(coords));

        for (unsigned int j = 0; j < parts.size(); j += 2)
        {
          double radius = 0.0;
          std::string latstr = parse_radius(parts[j + 1], radius);
          double lon = Fmi::stod(parts[j]);
          double lat = Fmi::stod(latstr);
          std::swap(lon, lat);
          std::string tag = parts[j] + ',' + parts[j + 1];
          Spine::LocationPtr loc = this->lonlatSearch(lon, lat, language, maxdistance);
          std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
          loc2->type = Spine::Location::CoordinatePoint;
          loc2->radius = radius;
          options.add(tag, loc2);
        }
      }
    }

    searchName = theReq.getParameterList("geoid");
    if (!searchName.empty())
    {
      for (const std::string& geoids : searchName)
      {
        std::list<std::string> parts;
        boost::algorithm::split(parts, geoids, boost::algorithm::is_any_of(","));
        for (const std::string& geoid : parts)
        {
          long number = Fmi::stol(geoid);
          Spine::LocationPtr loc = this->idSearch(number, language);
          options.add(geoid, loc);
        }
      }
    }

    searchName = theReq.getParameterList("geoids");
    if (!searchName.empty())
    {
      for (const std::string& geoids : searchName)
      {
        std::list<std::string> parts;
        boost::algorithm::split(parts, geoids, boost::algorithm::is_any_of(","));
        for (const std::string& geoid : parts)
        {
          long number = Fmi::stol(geoid);
          Spine::LocationPtr loc = this->idSearch(number, language);
          options.add(geoid, loc);
        }
      }
    }

    searchName = theReq.getParameterList("keyword");
    if (!searchName.empty())
    {
      for (const std::string& keyword : searchName)
      {
        Locus::QueryOptions opts;
        opts.SetLanguage(language);
        Spine::LocationList places = this->keywordSearch(opts, keyword);
        if (places.empty())
          throw Spine::Exception(BCP,
                                 "No locations for keyword " + std::string(keyword) + " found");

        for (Spine::LocationPtr& place : places)
        {
          options.add(place->name, place);
        }
      }
    }

    searchName = theReq.getParameterList("wkt");
    if (!searchName.empty())
    {
      for (const std::string& wkt : searchName)
      {
        double radius = 0.0;
        size_t aliasPos = wkt.find(" as ");
        if (aliasPos != std::string::npos && wkt.size() - aliasPos < 5)
          throw Spine::Exception(BCP, "Invalid WKT-parameter: " + wkt);
        std::string wktStr = wkt.substr(0, aliasPos);
        wktStr = parse_radius(wktStr, radius);
        // find first coordinate and do a lonlat search with it
        std::size_t firstNumberPos = wktStr.find_first_of("123456789");
        if (firstNumberPos == std::string::npos)
          throw Spine::Exception(BCP, "Invalid WKT: " + wktStr);

        std::size_t firstCharacterAfterNumberPos = wktStr.find_first_of(",)");
        if (firstCharacterAfterNumberPos == std::string::npos)
          throw Spine::Exception(BCP, "Invalid WKT: " + wktStr);
        std::string firstCoordinate =
            wktStr.substr(firstNumberPos, firstCharacterAfterNumberPos - firstNumberPos);
        std::size_t spacePos = firstCoordinate.find(' ');
        if (spacePos == std::string::npos)
          throw Spine::Exception(BCP, "Invalid WKT: " + wktStr);

        std::string lonStr = firstCoordinate.substr(0, spacePos);
        std::string latStr = firstCoordinate.substr(spacePos + 1);
        double lon = Fmi::stod(lonStr);
        double lat = Fmi::stod(latStr);

        Spine::LocationPtr loc = this->lonlatSearch(lon, lat, language, maxdistance);
        std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
        loc2->type = Spine::Location::Wkt;
        loc2->name = wkt;
        loc2->radius = radius;
        options.add(wktStr, loc2);
      }
    }

    return options;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate ISO2 to country name
 */
// ----------------------------------------------------------------------

std::string Engine::countryName(const std::string& theIso2, const std::string& theLang) const
{
  try
  {
    auto mycopy = boost::atomic_load(&impl);
    return mycopy->translate_country(theIso2, theLang);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    auto mycopy = boost::atomic_load(&impl);
    return mycopy->dem();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    auto mycopy = boost::atomic_load(&impl);
    return mycopy->maxDemResolution();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    auto mycopy = boost::atomic_load(&impl);
    return mycopy->landCover();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
  auto mycopy = boost::atomic_load(&impl);
  return mycopy->isSuggestReady();
}

// ----------------------------------------------------------------------
/*!
 * \brief Priority sort a location list
 */
// ----------------------------------------------------------------------

void Engine::sort(Spine::LocationList& theLocations) const
{
  try
  {
    auto mycopy = boost::atomic_load(&impl);
    mycopy->sort(theLocations);  // uses Impl::itsCollator, hence lock is needed
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

    std::cerr << boost::posix_time::second_clock::local_time() << " Geonames reloading initiated"
              << std::endl;

    auto p = boost::make_shared<Impl>(itsConfigFile, true);  // reload=true
    bool first_construction = false;
    p->init(first_construction);

    if (!p->itsReloadOK)
    {
      itsErrorMessage = p->itsReloadError;
      std::cerr << boost::posix_time::second_clock::local_time()
                << " Geonames reloading failed: " << p->itsReloadError << std::endl;
      itsReloading = false;
      return false;
    }

    boost::atomic_store(&impl, p);

    itsLastReload = boost::posix_time::second_clock::local_time();
    itsErrorMessage = "";
    itsReloading = false;

    std::cerr << itsLastReload << " Geonames reloading finished" << std::endl;

    return true;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    std::ostringstream out;
    if (secs > 0)
    {
      out << std::setprecision(6) << 1.0 * count / secs << "/sec, " << 60.0 * count / secs
          << "/min";
    }
    else
    {
      out << "Not available";
    }

    return out.str();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    boost::shared_ptr<Spine::Table> cacheTable(new Spine::Table());
    Spine::TableFormatter::Names cacheHeaders;

    boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
    auto duration = now - itsStartTime;
    long secs = duration.total_seconds();

    unsigned int row = 0, column = 0;

    auto mycopy = boost::atomic_load(&impl);

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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

StatusReturnType Engine::cacheStatus() const
{
  try
  {
    boost::shared_ptr<Spine::Table> cacheTable(new Spine::Table());
    Spine::TableFormatter::Names cacheHeaders;

    auto mycopy = boost::atomic_load(&impl);
    mycopy->name_cache_status(cacheTable, cacheHeaders);

    return std::make_pair(cacheTable, cacheHeaders);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
