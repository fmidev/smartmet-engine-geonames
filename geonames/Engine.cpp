// ======================================================================
/*!
 * \brief Class Engine::Geo::Engine
 */
// ======================================================================

#include "Engine.h"
#include "Impl.h"
#include <fmt/format.h>
#include <fmt/printf.h>
#include <gis/DEM.h>
#include <gis/LandCover.h>
#include <locus/Query.h>
#include <macgyver/DistanceParser.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <macgyver/TimeZoneFactory.h>
#include <spine/Convenience.h>
#include <spine/DebugFormatter.h>
#include <spine/Location.h>
#include <spine/Reactor.h>
#include <spine/TableFormatterOptions.h>
#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>
#include <ogr_geometry.h>
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
static const char* default_maxdistance = "15km";  // km

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
      output_string.resize(pos);
    }

    radius = Fmi::stod(radius_string);

    return output_string;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility function to resolve DEM height if it is available
 */
// ----------------------------------------------------------------------

double demheight(const std::shared_ptr<Fmi::DEM>& theDem,
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

Fmi::LandCover::Type covertype(const std::shared_ptr<Fmi::LandCover>& theLandCover,
                               double theLongitude,
                               double theLatitude)
{
  if (!theLandCover)
    return Fmi::LandCover::NoData;
  return theLandCover->coverType(theLongitude, theLatitude);
}

void parse_area(LocationOptions& theOptions, const Spine::HTTP::Request& theRequest)
{
  auto searchName = theRequest.getParameterList("area");
  if (searchName.empty())
    return;

  for (const std::string& area : searchName)
  {
    double radius = 0.0;
    std::string area_string = parse_radius(area, radius);
    std::unique_ptr<Spine::Location> loc(new Spine::Location(area_string, radius));
    loc->radius = radius;
    loc->type = Spine::Location::Area;
    theOptions.add(area, loc);
  }
}

void parse_areas(LocationOptions& theOptions, const Spine::HTTP::Request& theRequest)
{
  auto searchName = theRequest.getParameterList("areas");
  if (searchName.empty())
    return;

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
      theOptions.add(area, loc);
    }
  }
}

void parse_path(LocationOptions& theOptions, const Spine::HTTP::Request& theRequest)
{
  auto searchName = theRequest.getParameterList("path");
  if (searchName.empty())
    return;

  unsigned int path_counter = 1;  // number is added to the end of pathname
  for (const std::string& path : searchName)
  {
    if (path.find(' ') != std::string::npos)
      throw Fmi::Exception(BCP, "Invalid path parameter " + path + ", no spaces allowed!");

    std::string tag = "path" + Fmi::to_string(path_counter++);

    // radius handling added if we want to extend path to area
    double radius = 0.0;
    std::string path_name = parse_radius(path, radius);

    std::unique_ptr<Spine::Location> loc(new Spine::Location(path_name, radius));
    loc->type = Spine::Location::Path;
    theOptions.add(tag, loc);
  }
}

void parse_paths(LocationOptions& theOptions, const Spine::HTTP::Request& theRequest)
{
  auto searchName = theRequest.getParameterList("paths");
  if (searchName.empty())
    return;

  unsigned int paths_counter = 1;  // number is added to the end of pathname
  for (const std::string& paths : searchName)
  {
    std::list<std::string> path_list;
    boost::algorithm::split(path_list, paths, boost::algorithm::is_any_of(" "));

    for (const std::string& path : path_list)
    {
      if (path.find(':') != std::string::npos)
        throw Fmi::Exception(BCP, "Invalid path parameter " + path + ", no radius allowed!");

      std::string tag = "paths" + Fmi::to_string(paths_counter++);
      std::unique_ptr<Spine::Location> loc(new Spine::Location(path, 0.0));
      loc->type = Spine::Location::Path;
      theOptions.add(tag, loc);
    }
  }
}

void parse_bbox(LocationOptions& theOptions, const Spine::HTTP::Request& theRequest)
{
  // format bbox=lon,lat,lon,lat[:radius]
  auto searchName = theRequest.getParameterList("bbox");
  if (searchName.empty())
    return;

  for (const std::string& bbox : searchName)
  {
    std::list<std::string> parts;
    boost::algorithm::split(parts, bbox, boost::algorithm::is_any_of(","));
    if (parts.size() != 4)
      throw Fmi::Exception(
          BCP,
          "Invalid bbox parameter " + bbox + ", should be in format 'lon,lat,lon,lat[:radius]'!");

    double radius = 0.0;
    std::string bbox_string = parse_radius(bbox, radius);

    std::unique_ptr<Spine::Location> loc(new Spine::Location(bbox_string, radius));
    loc->type = Spine::Location::BoundingBox;
    theOptions.add(bbox, loc);
  }
}

void parse_bboxes(LocationOptions& theOptions, const Spine::HTTP::Request& theRequest)
{
  // format bboxes=lon,lat,lon,lat[:radius],lon,lat,lon,lat[:radius],...
  auto searchName = theRequest.getParameterList("bboxes");
  if (searchName.empty())
    return;

  for (const std::string& bboxes : searchName)
  {
    std::vector<std::string> coordinates;
    boost::algorithm::split(coordinates, bboxes, boost::algorithm::is_any_of(","));
    if (coordinates.size() % 4 != 0)
      throw Fmi::Exception(BCP,
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
      theOptions.add(bbox_name, loc);
    }
  }
}

void LocationOptions::add(const std::string& theTag, const Spine::LocationPtr& theLoc)
{
  itsLocations.push_back(Spine::TaggedLocation(theTag, theLoc));
}

void LocationOptions::add(const std::string& theTag, std::unique_ptr<Spine::Location>& theLoc)
{
  add(theTag, Spine::LocationPtr(theLoc.release()));
}

// ----------------------------------------------------------------------
/*!
 * \brief Constructor
 */
// ----------------------------------------------------------------------

Engine::Engine(std::string theConfigFile)
    : itsStartTime(Fmi::SecondClock::local_time()),
      itsReloading(false),
      itsNameSearchCount(0),
      itsLonLatSearchCount(0),
      itsIdSearchCount(0),
      itsKeywordSearchCount(0),
      itsSuggestCount(0),
      itsConfigFile(std::move(theConfigFile)),
      initFailed(false)
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
    Spine::Reactor *reactor = Spine::Reactor::instance;
    if (reactor)
    {
      using AdminRequestAccess = Spine::Reactor::AdminRequestAccess;

      reactor->addAdminCustomRequestHandler(
        this,
        "reload",
        AdminRequestAccess::RequiresAuthentication,
        std::bind(&Engine::requestReload, this, std::placeholders::_3), "Reload geoengine");

      reactor->addAdminTableRequestHandler(
        this,
        "geonames",
        AdminRequestAccess::Public,
        std::bind(&Engine::requestInfo, this, std::placeholders::_2),
         "Geoengine information");
    }

    tmpImpl = std::make_shared<Impl>(itsConfigFile, false);
    bool first_construction = true;
    tmpImpl->init(first_construction);
    impl.store(tmpImpl);
  }
  catch (...)
  {
    initFailed = true;
    throw Fmi::Exception::Trace(BCP, "Init failed!");
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
      if (tmpImpl)
      {
        tmpImpl->shutdown();
        return;
      }

      if (initFailed)
        break;

      // The is no Impl object available yet, so its initialization is probably still
      // running. There should be a way to terminate this initialization, because
      // now we have to wait its termination.
      boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
    }
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

std::size_t Engine::hash_value() const
{
  try
  {
    auto mycopy = impl.load();
    return mycopy->hash_value();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
      throw Fmi::Exception(BCP, "Unknown location: " + theName);

    return translateLocation(*result.front(), theLang);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
  std::string features;  // use defaults
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
        Spine::Location newloc(*result.front());
        newloc.longitude = theLongitude;
        newloc.latitude = theLatitude;
        newloc.dem = demheight(dem(), theLongitude, theLatitude, maxDemResolution());
        newloc.covertype = covertype(landCover(), theLongitude, theLatitude);

        return translateLocation(newloc, theLang);
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
      throw Fmi::Exception(BCP, "Unknown location ID: " + Fmi::to_string(theGeoID));

    return translateLocation(*result.front(), theLang);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    auto mycopy = impl.load();
    return mycopy->name_search(theOptions, theName);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    return lonlatSearch(theOptions, theLongitude, theLatitude, theRadius);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    auto mycopy = impl.load();
    return mycopy->lonlat_search(theOptions, theLongitude, theLatitude, theRadius);
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

Spine::LocationList Engine::idSearch(const Locus::QueryOptions& theOptions, int theId) const
{
  try
  {
    ++itsIdSearchCount;
    auto mycopy = impl.load();
    return mycopy->id_search(theOptions, theId);
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

Spine::LocationList Engine::keywordSearch(const Locus::QueryOptions& theOptions,
                                          const std::string& theKeyword) const
{
  try
  {
    ++itsKeywordSearchCount;
    auto mycopy = impl.load();
    return mycopy->keyword_search(theOptions, theKeyword);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Do a search for wkt object
 */
// ----------------------------------------------------------------------

Spine::LocationPtr Engine::wktSearch(const std::string& theWktString,
                                     const std::string& theLanguage,
                                     double theRadius /* = 0.0*/) const
{
  std::unique_ptr<OGRGeometry> geom;
  geom.reset(Fmi::OGR::createFromWkt(theWktString, 4326));

  if (geom)
  {
    if (theRadius > 0.0)
    {
      std::unique_ptr<OGRGeometry> poly;
      geom.reset(Fmi::OGR::expandGeometry(geom.get(), theRadius * 1000));
    }
  }

  OGREnvelope envelope;
  geom->getEnvelope(&envelope);
  double top = envelope.MaxY;
  double bottom = envelope.MinY;
  double left = envelope.MinX;
  double right = envelope.MaxX;

  double lon = (right + left) / 2.0;
  double lat = (top + bottom) / 2.0;

  return lonlatSearch(lon, lat, theLanguage);
}

// ----------------------------------------------------------------------
/*!
 * \brief Get wkt geometries
 */
// ----------------------------------------------------------------------

WktGeometries Engine::getWktGeometries(const LocationOptions& loptions,
                                       const std::string& language) const
{
  WktGeometries ret;

  // Store WKT-geometries
  for (const auto& tloc : loptions.locations())
  {
    if (tloc.loc->type == Spine::Location::Wkt)
    {
      WktGeometryPtr wktGeometry(new WktGeometry(tloc.loc, language, *this));
      ret.addWktGeometry(tloc.loc->name, wktGeometry);
    }
  }
  return ret;
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

    auto mycopy = impl.load();
    return mycopy->suggest(thePattern, theLang, theKeyword, thePage, theMaxResults, false);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

    auto mycopy = impl.load();
    return mycopy->suggest(thePattern, theLang, theKeyword, thePage, theMaxResults, true);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Find alphabetical complations for more than one language
 */
// ----------------------------------------------------------------------

std::vector<Spine::LocationList> Engine::suggest(const std::string& thePattern,
                                                 const std::vector<std::string>& theLanguages,
                                                 const std::string& theKeyword,
                                                 unsigned int thePage,
                                                 unsigned int theMaxResults) const
{
  try
  {
    Spine::LocationList ret;

    ++itsSuggestCount;

    bool duplicates = false;

    auto mycopy = impl.load();
    return mycopy->suggest(
        thePattern, theLanguages, theKeyword, thePage, theMaxResults, duplicates);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

    auto mycopy = impl.load();

    // We need suggest to be ready

    while (!mycopy->isSuggestReady())
    {
      boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
    }

    // return null if keyword is wrong

    const auto it = mycopy->itsGeoTrees.find(theKeyword);
    if (it == mycopy->itsGeoTrees.end())
      return {};

    // this is unfortunate - we must allocate new Location just to
    // get NearTree comparisons working

    Spine::LocationPtr dummy(new Spine::Location(theLongitude, theLatitude));

    // result will be here, if there is one

    std::optional<Spine::LocationPtr> ptr = it->second->nearest(dummy, theRadius);
    if (!ptr)
      return {};

    Spine::LocationPtr newptr(new Spine::Location(**ptr));
    mycopy->translate(newptr, theLang);
    return newptr;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

LocationOptions Engine::parseLocations(const std::vector<int>& fmisids,
                                       const std::vector<int>& lpnns,
                                       const std::vector<int>& wmos,
                                       const std::string& language) const
{
  try
  {
    LocationOptions options;

    Locus::QueryOptions opts;
    opts.SetCountries("all");
    opts.SetFullCountrySearch(true);
    opts.SetFeatures("SYNOP,FINAVIA,STUK");
    opts.SetSearchVariants(true);
    opts.SetLanguage(language);
    opts.SetResultLimit(1);

    opts.SetNameType("fmisid");
    for (const auto& id : fmisids)
    {
      Spine::LocationList ll = nameSearch(opts, Fmi::to_string(id));
      if (!ll.empty())
        options.add(Fmi::to_string(id), ll.front());
    }
    opts.SetNameType("lpnn");
    for (const auto& id : lpnns)
    {
      Spine::LocationList ll = nameSearch(opts, Fmi::to_string(id));
      if (!ll.empty())
        options.add(Fmi::to_string(id), ll.front());
    }
    opts.SetNameType("wmo");
    for (const auto& id : wmos)
    {
      Spine::LocationList ll = nameSearch(opts, Fmi::to_string(id));
      if (!ll.empty())
        options.add(Fmi::to_string(id), ll.front());
    }

    return options;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Map location-related HTTP query parameters to Locations
 */
// ----------------------------------------------------------------------

void Engine::parse_place(LocationOptions& theOptions,
                         const Spine::HTTP::Request& theRequest,
                         const std::string& theLanguage) const
{
  auto searchName = theRequest.getParameterList("place");
  if (searchName.empty())
    return;

  for (const std::string& city : searchName)
  {
    double radius = 0.0;
    std::string city_string = parse_radius(city, radius);
    Spine::LocationPtr loc = this->nameSearch(city_string, theLanguage);  // throws for empty result
    std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
    // in order to make difference between e.g. Helsinki, Helsinki:50
    loc2->radius = radius;
    loc2->type = Spine::Location::Place;
    theOptions.add(city_string, loc2);
  }
}

void Engine::parse_places(LocationOptions& theOptions,
                          const Spine::HTTP::Request& theRequest,
                          const std::string& theLanguage) const
{
  auto searchName = theRequest.getParameterList("places");
  if (searchName.empty())
    return;

  for (const std::string& places : searchName)
  {
    std::list<std::string> parts;
    boost::algorithm::split(parts, places, boost::algorithm::is_any_of(","));

    for (const std::string& city : parts)
    {
      double radius = 0.0;
      std::string city_string = parse_radius(city, radius);
      Spine::LocationPtr loc =
          this->nameSearch(city_string, theLanguage);  // throws for empty result
      // in order to make difference between e.g. Helsinki, Helsinki:50
      std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
      loc2->radius = radius;
      loc2->type = Spine::Location::Place;
      theOptions.add(city_string, loc2);
    }
  }
}

void Engine::parse_lonlat(LocationOptions& theOptions,
                          const Spine::HTTP::Request& theRequest,
                          const std::string& theLanguage,
                          const std::string& theFeatures,
                          double theMaxDistance) const
{
  auto searchName = theRequest.getParameterList("lonlat");
  if (searchName.empty())
    return;

  for (const std::string& coords : searchName)
  {
    std::vector<std::string> parts;
    boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
    if (parts.size() % 2 != 0)
      throw Fmi::Exception(BCP, "Invalid lonlat list: " + std::string(coords));

    for (unsigned int j = 0; j < parts.size(); j += 2)
    {
      // handle radius
      double radius = 0.0;
      std::string latstr = parse_radius(parts[j + 1], radius);
      double lon = Fmi::stod(parts[j]);
      double lat = Fmi::stod(latstr);
      std::string tag = parts[j] + ',' + parts[j + 1];
      Spine::LocationPtr loc =
          this->featureSearch(lon, lat, theLanguage, theFeatures, theMaxDistance);
      std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
      loc2->radius = radius;
      loc2->type = Spine::Location::CoordinatePoint;
      theOptions.add(tag, loc2);
    }
  }
}

void Engine::parse_lonlats(LocationOptions& theOptions,
                           const Spine::HTTP::Request& theRequest,
                           const std::string& theLanguage,
                           const std::string& theFeatures,
                           double theMaxDistance) const
{
  auto searchName = theRequest.getParameterList("lonlats");
  if (searchName.empty())
    return;

  for (const std::string& coords : searchName)
  {
    std::vector<std::string> parts;
    boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
    if (parts.size() % 2 != 0)
      throw Fmi::Exception(BCP, "Invalid lonlats list: " + std::string(coords));

    for (unsigned int j = 0; j < parts.size(); j += 2)
    {
      double radius = 0.0;
      std::string latstr = parse_radius(parts[j + 1], radius);
      double lon = Fmi::stod(parts[j]);
      double lat = Fmi::stod(latstr);
      std::string tag = parts[j] + ',' + parts[j + 1];
      Spine::LocationPtr loc =
          this->featureSearch(lon, lat, theLanguage, theFeatures, theMaxDistance);
      std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
      loc2->type = Spine::Location::CoordinatePoint;
      loc2->radius = radius;
      theOptions.add(tag, loc2);
    }
  }
}

void Engine::parse_latlon(LocationOptions& theOptions,
                          const Spine::HTTP::Request& theRequest,
                          const std::string& theLanguage,
                          const std::string& theFeatures,
                          double theMaxDistance) const
{
  auto searchName = theRequest.getParameterList("latlon");
  if (searchName.empty())
    return;

  for (const std::string& coords : searchName)
  {
    std::vector<std::string> parts;
    boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
    if (parts.size() % 2 != 0)
      throw Fmi::Exception(BCP, "Invalid latlon list: " + std::string(coords));

    for (unsigned int j = 0; j < parts.size(); j += 2)
    {
      // Handle radius
      double radius = 0.0;
      std::string latstr = parse_radius(parts[j + 1], radius);
      double lon = Fmi::stod(parts[j]);
      double lat = Fmi::stod(latstr);
      std::swap(lon, lat);
      std::string tag = parts[j] + ',' + parts[j + 1];
      Spine::LocationPtr loc =
          this->featureSearch(lon, lat, theLanguage, theFeatures, theMaxDistance);
      std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
      loc2->type = Spine::Location::CoordinatePoint;
      loc2->radius = radius;
      theOptions.add(tag, loc2);
    }
  }
}

void Engine::parse_latlons(LocationOptions& theOptions,
                           const Spine::HTTP::Request& theRequest,
                           const std::string& theLanguage,
                           const std::string& theFeatures,
                           double theMaxDistance) const
{
  auto searchName = theRequest.getParameterList("latlons");
  if (searchName.empty())
    return;

  for (const std::string& coords : searchName)
  {
    std::vector<std::string> parts;
    boost::algorithm::split(parts, coords, boost::algorithm::is_any_of(","));
    if (parts.size() % 2 != 0)
      throw Fmi::Exception(BCP, "Invalid latlons list: " + std::string(coords));

    for (unsigned int j = 0; j < parts.size(); j += 2)
    {
      double radius = 0.0;
      std::string latstr = parse_radius(parts[j + 1], radius);
      double lon = Fmi::stod(parts[j]);
      double lat = Fmi::stod(latstr);
      std::swap(lon, lat);
      std::string tag = parts[j] + ',' + parts[j + 1];
      Spine::LocationPtr loc =
          this->featureSearch(lon, lat, theLanguage, theFeatures, theMaxDistance);
      std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
      loc2->type = Spine::Location::CoordinatePoint;
      loc2->radius = radius;
      theOptions.add(tag, loc2);
    }
  }
}

void Engine::parse_geoid(LocationOptions& theOptions,
                         const Spine::HTTP::Request& theRequest,
                         const std::string& theLanguage) const
{
  auto searchName = theRequest.getParameterList("geoid");
  if (searchName.empty())
    return;

  for (const std::string& geoids : searchName)
  {
    std::list<std::string> parts;
    boost::algorithm::split(parts, geoids, boost::algorithm::is_any_of(","));
    for (const std::string& geoid : parts)
    {
      long number = Fmi::stol(geoid);
      Spine::LocationPtr loc = this->idSearch(number, theLanguage);
      theOptions.add(geoid, loc);
    }
  }
}

void Engine::parse_geoids(LocationOptions& theOptions,
                          const Spine::HTTP::Request& theRequest,
                          const std::string& theLanguage) const
{
  auto searchName = theRequest.getParameterList("geoids");
  if (searchName.empty())
    return;

  for (const std::string& geoids : searchName)
  {
    std::list<std::string> parts;
    boost::algorithm::split(parts, geoids, boost::algorithm::is_any_of(","));
    for (const std::string& geoid : parts)
    {
      long number = Fmi::stol(geoid);
      Spine::LocationPtr loc = this->idSearch(number, theLanguage);
      theOptions.add(geoid, loc);
    }
  }
}

void Engine::parse_keyword(LocationOptions& theOptions,
                           const Spine::HTTP::Request& theRequest,
                           const std::string& theLanguage) const
{
  auto searchName = theRequest.getParameterList("keyword");
  if (searchName.empty())
    return;

  for (const std::string& keyword : searchName)
  {
    std::list<std::string> parts;
    boost::algorithm::split(parts, keyword, boost::algorithm::is_any_of(","));
    for (const std::string& key : parts)
    {
      Locus::QueryOptions opts;
      opts.SetLanguage(theLanguage);
      Spine::LocationList places = this->keywordSearch(opts, key);
      if (places.empty())
        throw Fmi::Exception(BCP, "No locations for keyword " + std::string(key) + " found");

      for (Spine::LocationPtr& place : places)
        theOptions.add(place->name, place);
    }
  }
}

void Engine::parse_wkt(LocationOptions& theOptions,
                       const Spine::HTTP::Request& theRequest,
                       const std::string& theLanguage,
                       const std::string& theFeatures,
                       double theMaxDistance) const
{
  auto searchName = theRequest.getParameterList("wkt");
  if (searchName.empty())
    return;

  for (const std::string& wkt : searchName)
  {
    double radius = 0.0;
    size_t aliasPos = wkt.find(" as ");
    if (aliasPos != std::string::npos && wkt.size() - aliasPos < 5)
      throw Fmi::Exception(BCP, "Invalid WKT-parameter: " + wkt);
    std::string wktStr = wkt.substr(0, aliasPos);
    wktStr = parse_radius(wktStr, radius);
    // find first coordinate and do a lonlat search with it
    std::size_t firstNumberPos = wktStr.find_first_of("+-.0123456789");
    if (firstNumberPos == std::string::npos)
      throw Fmi::Exception(BCP, "Invalid WKT: " + wktStr);

    std::size_t firstCharacterAfterNumberPos = wktStr.find_first_of(",)");
    if (firstCharacterAfterNumberPos == std::string::npos)
      throw Fmi::Exception(BCP, "Invalid WKT: " + wktStr);
    std::string firstCoordinate =
        wktStr.substr(firstNumberPos, firstCharacterAfterNumberPos - firstNumberPos);
    std::size_t spacePos = firstCoordinate.find(' ');
    if (spacePos == std::string::npos)
      throw Fmi::Exception(BCP, "Invalid WKT: " + wktStr);

    std::string lonStr = firstCoordinate.substr(0, spacePos);
    std::string latStr = firstCoordinate.substr(spacePos + 1);
    double lon = Fmi::stod(lonStr);
    double lat = Fmi::stod(latStr);
    Spine::LocationPtr loc =
        this->featureSearch(lon, lat, theLanguage, theFeatures, theMaxDistance);
    std::unique_ptr<Spine::Location> loc2(new Spine::Location(*loc));
    loc2->type = Spine::Location::Wkt;
    loc2->name = wkt;
    loc2->radius = radius;
    theOptions.add(wktStr, loc2);
  }
}

LocationOptions Engine::parseLocations(const Spine::HTTP::Request& theReq) const
{
  try
  {
    // Language selection (default -> config -> querystring order)

    std::string language = default_language;
    language = Spine::optional_string(theReq.getParameter("lang"), language);

    // Default feature list is empty, meaning Locus defaults will be used
    std::string features = Spine::optional_string(theReq.getParameter("feature"), "");

    // Maximum distance for latlon searches

    std::string maxdistance_str =
        Spine::optional_string(theReq.getParameter("maxdistance"), default_maxdistance);
    double maxdistance = Fmi::DistanceParser::parse_kilometer(maxdistance_str);

    // Scan all options for location references

    LocationOptions options;
    parse_place(options, theReq, language);
    parse_places(options, theReq, language);
    parse_area(options, theReq);
    parse_areas(options, theReq);
    parse_path(options, theReq);
    parse_paths(options, theReq);
    parse_bbox(options, theReq);
    parse_bboxes(options, theReq);
    parse_lonlat(options, theReq, language, features, maxdistance);
    parse_lonlats(options, theReq, language, features, maxdistance);
    parse_latlon(options, theReq, language, features, maxdistance);
    parse_latlons(options, theReq, language, features, maxdistance);
    parse_geoid(options, theReq, language);
    parse_geoids(options, theReq, language);
    parse_keyword(options, theReq, language);
    parse_wkt(options, theReq, language, features, maxdistance);

    return options;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    auto mycopy = impl.load();
    return mycopy->translate_country(theIso2, theLang);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the DEM data handler
 */
// ----------------------------------------------------------------------

std::shared_ptr<Fmi::DEM> Engine::dem() const
{
  try
  {
    auto mycopy = impl.load();
    return mycopy->dem();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the LandCover data handler
 */
// ----------------------------------------------------------------------

std::shared_ptr<Fmi::LandCover> Engine::landCover() const
{
  try
  {
    auto mycopy = impl.load();
    return mycopy->landCover();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

void Engine::sort(Spine::LocationList& theLocations) const
{
  try
  {
    auto mycopy = impl.load();
    mycopy->sort(theLocations);  // uses Impl::itsCollator, hence lock is needed
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Reload the data from database
 *
 * Returns pair of values: first is true if reload was successful, second is output string
 * (potentialy an eror message)
 */
// ----------------------------------------------------------------------

std::pair<bool, std::string> Engine::reload()
{
  std::ostringstream output;
  try
  {
    if (itsReloading)
    {
      itsErrorMessage = "Geo reload was already in progress";
      return {false, itsErrorMessage};
    }

    itsReloading = true;

    const Fmi::DateTime begin = Fmi::MicrosecClock::local_time();
    const std::string m1 = begin.to_simple_string() + " Geonames reloading initiated";
    output << m1 << std::endl;
    std::cout << m1 << std::endl;

    auto p = std::make_shared<Impl>(itsConfigFile, true);  // reload=true
    bool first_construction = false;
    p->init(first_construction);

    if (!p->itsReloadOK)
    {
      itsErrorMessage = p->itsReloadError;
      const Fmi::DateTime end = Fmi::MicrosecClock::local_time();
      const std::string m2 = end.to_simple_string() + " Geonames reloading failed: " + itsErrorMessage;
      output << m2 << std::endl;
      std::cout << m2 << std::endl;
      itsReloading = false;
      return { false, output.str() };
    }

    impl.store(p);

    const Fmi::DateTime end = Fmi::MicrosecClock::local_time();
    itsLastReload = end;
    itsErrorMessage = "";
    itsReloading = false;

    const double secs = 0.000001 * (end - begin).total_microseconds();
    const std::string m3 = itsLastReload.to_simple_string() + " Geonames reloaded in "
                          + fmt::format("{:0.3f}", secs) + " seconds"
    ;
    output << m3 << std::endl;
    std::cout << m3 << std::endl;

    return { true, output.str() };
  }
  catch (...)
  {
    const auto error = Fmi::Exception::Trace(BCP, "Geonames Reload failed");
    std::ostringstream errmsg;
    errmsg << Fmi::SecondClock::local_time() << ": C++ exception while reloading geonames:" << '\n'
           << error << '\n';
    output << errmsg.str() << std::endl;
    std::cout << errmsg.str() << std::endl;
    itsReloading = false; // Do not leave the system in reloading state
    itsErrorMessage = errmsg.str();
    // Return false and error message, but do not throw and exception
    return { false, output.str() };
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    std::unique_ptr<Spine::Table> cacheTable(new Spine::Table());
    Spine::TableFormatter::Names cacheHeaders;

    Fmi::DateTime now = Fmi::SecondClock::local_time();
    auto duration = now - itsStartTime;
    long secs = duration.total_seconds();

    unsigned int row = 0;
    unsigned int column = 0;

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

    cacheTable->setNames(cacheHeaders);
    return cacheTable;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

StatusReturnType Engine::cacheStatus() const
{
  try
  {
    std::shared_ptr<Spine::Table> cacheTable(new Spine::Table());
    Spine::TableFormatter::Names cacheHeaders;

    auto mycopy = impl.load();
    return mycopy->name_cache_status();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

// DEM height
double Engine::demHeight(double theLongitude, double theLatitude) const
{
  return demheight(dem(), theLongitude, theLatitude, maxDemResolution());
}

// Cover type
Fmi::LandCover::Type Engine::coverType(double theLongitude, double theLatitude) const
{
  return covertype(landCover(), theLongitude, theLatitude);
}

Fmi::Cache::CacheStatistics Engine::getCacheStats() const
{
  auto mycopy = impl.load();

  return mycopy->getCacheStats();
}

Spine::LocationPtr Engine::translateLocation(const Spine::Location& theLocation,
                                             const std::string& theLang) const
{
  Spine::LocationPtr newptr(new Spine::Location(theLocation));
  auto mycopy = impl.load();
  mycopy->translate(newptr, theLang);
  return newptr;
}

void Engine::requestReload(SmartMet::Spine::HTTP::Response& theResponse)
{
  std::ostringstream out;
  try
  {
    out << "<html><head><title>SmartMet Admin</title></head><body>\n";

    // Make MIME header
    std::string mime("text/html; charset=UTF-8");
    theResponse.setHeader("Content-Type", mime);

    Fmi::DateTime now = Fmi::MicrosecClock::universal_time();
    const std::pair<bool, std::string> result = reload();

    out << "</body></html>\n";
    out << "<pre>\n";
    out << result.second << '\n';
    out << "</pre>\n";

    // Set return code and content
    theResponse.setStatus(result.first ? Spine::HTTP::Status::ok : Spine::HTTP::Status::internal_server_error);
    theResponse.setContent(out.str());
  }
  catch (...)
  {
    const auto error = Fmi::Exception::Trace(BCP, "Operation failed!");
    std::cout << error << std::endl;
    out << error;
    theResponse.setStatus(Spine::HTTP::Status::internal_server_error);
    theResponse.setContent(out.str());
  }
}


std::unique_ptr<Spine::Table> Engine::requestInfo(const SmartMet::Spine::HTTP::Request& request) const
try
{
  const std::string dataType = Spine::optional_string(request.getParameter("type"), "meta");
  if (dataType == "meta")
    {
      return metadataStatus();
    }
    else if (dataType == "cache")
    {
      return cacheStatus();
    }
    else
    {
      throw Fmi::Exception(BCP, "Unknown type '" + dataType + "'");
    }
}
catch (...)
{
  throw Fmi::Exception::Trace(BCP, "Operation failed!");
}

void Engine::assign_priorities(Spine::LocationList& locs) const
try
{
  auto mycopy = impl.load();
  mycopy->assign_priorities(locs);
}
catch (...)
{
  throw Fmi::Exception::Trace(BCP, "Operation failed!");
}

// ----------------------------------------------------------------------

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
