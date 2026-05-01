// ======================================================================
/*!
 * \brief Smoke test for the OSM backend in smartmet-engine-geonames.
 *
 * Boots the engine with backend = "osm" pointing at the TSV fixtures in
 * smartmet-library-osm/test/data, then exercises a handful of public
 * Engine methods to confirm the load_from_osm() path produced sane data
 * and that the in-memory indices work the same as in GeoNames mode.
 *
 * No PostgreSQL involved.
 */
// ======================================================================

#include "Engine.h"

#include <locus/Query.h>
#include <regression/tframe.h>
#include <spine/Location.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using SmartMet::Engine::Geonames::Engine;
using SmartMet::Spine::Location;
using SmartMet::Spine::LocationList;
using SmartMet::Spine::LocationPtr;

namespace
{
// SmartMetEngine::init() is protected (Reactor is the normal caller).
// Expose it so this smoke test can boot without a full Reactor stack.
class TestableEngine : public Engine
{
 public:
  using Engine::Engine;
  using Engine::init;
};

std::shared_ptr<TestableEngine> g_engine;

bool any(const LocationList& locs, const std::function<bool(const LocationPtr&)>& pred)
{
  return std::any_of(locs.begin(), locs.end(), pred);
}

// suggest()'s "predicate" REJECTS locations when it returns true (it's a
// filter for unwanted matches). We don't want to reject anything.
const auto accept_all = [](const LocationPtr&) { return false; };
}  // namespace

// The smoke test exercises only the in-memory code paths. nameSearch /
// lonlatSearch / idSearch / keywordSearch(QueryOptions, ...) all go
// through Locus into PostgreSQL and return empty when the database is
// disabled (which is unconditionally true in OSM mode). Bridging those
// to the in-memory data is a known follow-up.

namespace OsmBackendTests
{
void engine_is_ready()
{
  if (!g_engine->isSuggestReady())
    TEST_FAILED("engine not suggest-ready after init wait");
  TEST_PASSED();
}

void nearest_city_to_kyiv_coord()
{
  // Spatial nearest-neighbour against the in-memory 'cities' KD-tree.
  // Kyiv is at ~ 30.52, 50.45.
  auto ptr = g_engine->keywordSearch(30.52, 50.45, 5.0, "en", "cities");
  if (!ptr)
    TEST_FAILED("keywordSearch returned null near Kyiv coord");
  if (ptr->iso2 != "UA")
    TEST_FAILED("expected UA near Kyiv coord, got iso2='" + ptr->iso2 + "'");
  if (ptr->timezone != "Europe/Kyiv")
    TEST_FAILED("expected timezone Europe/Kyiv, got '" + ptr->timezone + "'");
  if (ptr->population < 1000000)
    TEST_FAILED("expected pop > 1M near Kyiv coord, got " +
                std::to_string(ptr->population));
  TEST_PASSED();
}

void nearest_city_to_helsinki_coord()
{
  // Helsinki is at ~ 24.94, 60.17.
  auto ptr = g_engine->keywordSearch(24.94, 60.17, 5.0, "en", "cities");
  if (!ptr)
    TEST_FAILED("keywordSearch returned null near Helsinki coord");
  if (ptr->iso2 != "FI")
    TEST_FAILED("expected FI near Helsinki coord, got iso2='" + ptr->iso2 + "'");
  if (ptr->timezone != "Europe/Helsinki")
    TEST_FAILED("expected timezone Europe/Helsinki, got '" + ptr->timezone + "'");
  TEST_PASSED();
}

void suggest_helsinki_prefix()
{
  // Autocomplete via the in-memory ternary tree. Latin-script Finnish
  // names go in cleanly, no translations needed.
  auto locs = g_engine->suggest("Hels", accept_all, "en", "cities", 0, 5);
  if (locs.empty())
    TEST_FAILED("suggest('Hels', cities) returned no results");
  bool got_helsinki = any(locs, [](const LocationPtr& p) { return p->name == "Helsinki"; });
  if (!got_helsinki)
    TEST_FAILED("Helsinki not in suggest('Hels', cities) results");
  TEST_PASSED();
}

void suggest_kyiv_prefix_cyrillic()
{
  // OSM's primary name for Kyiv is Cyrillic "Київ" — a prefix search
  // for "Ки" should return it. With no translation pass, autocomplete
  // works in the source language only.
  auto locs = g_engine->suggest("Ки", accept_all, "uk", "cities", 0, 10);
  if (locs.empty())
    TEST_FAILED("suggest('Ки', cities) returned no results");
  TEST_PASSED();
}

// --- in-memory QueryOptions-based search paths --------------------------

void name_search_kyiv_cyrillic()
{
  Locus::QueryOptions opts;
  opts.SetCountries("UA");
  opts.SetLanguage("en");
  opts.SetResultLimit(5);
  auto locs = g_engine->nameSearch(opts, "Київ");
  if (locs.empty())
    TEST_FAILED("nameSearch('Київ', UA) returned nothing");
  bool got_kyiv = any(locs, [](const LocationPtr& p) { return p->population > 1000000; });
  if (!got_kyiv)
    TEST_FAILED("Kyiv (pop>1M) not in nameSearch result");
  TEST_PASSED();
}

void name_search_helsinki_case_insensitive()
{
  Locus::QueryOptions opts;
  opts.SetCountries("FI");
  opts.SetLanguage("en");
  opts.SetResultLimit(5);
  auto locs = g_engine->nameSearch(opts, "helsinki");  // lowercase
  if (locs.empty())
    TEST_FAILED("nameSearch('helsinki', FI) returned nothing — case folding broken?");
  TEST_PASSED();
}

void name_search_country_filter()
{
  // "Tallinn" is in EE which we did not load. Should return empty.
  Locus::QueryOptions opts;
  opts.SetCountries("EE");
  opts.SetResultLimit(5);
  auto locs = g_engine->nameSearch(opts, "Tallinn");
  if (!locs.empty())
    TEST_FAILED("expected empty result for Tallinn (EE not loaded)");
  TEST_PASSED();
}

void lonlat_search_with_options()
{
  // Radius search around Kyiv with default options should pick up the
  // city itself plus several nearby populated places.
  Locus::QueryOptions opts;
  opts.SetCountries("all");
  opts.SetResultLimit(5);
  auto locs = g_engine->lonlatSearch(opts, 30.52, 50.45, 50.0);
  if (locs.empty())
    TEST_FAILED("lonlatSearch(opts) returned nothing near Kyiv");
  // The list comes back ordered by distance — first one should be in UA.
  if (locs.front()->iso2 != "UA")
    TEST_FAILED("nearest place to Kyiv coord should be UA, got " + locs.front()->iso2);
  TEST_PASSED();
}

void lonlat_search_feature_filter()
{
  // Restrict to PPLA only — should still find Kyiv (a city) but reject
  // any nearby villages/hamlets.
  Locus::QueryOptions opts;
  opts.SetCountries("UA");
  opts.SetFeatures("PPLA");
  opts.SetResultLimit(10);
  auto locs = g_engine->lonlatSearch(opts, 30.52, 50.45, 100.0);
  if (locs.empty())
    TEST_FAILED("lonlatSearch with feature=PPLA returned nothing near Kyiv");
  for (const auto& p : locs)
    if (p->feature != "PPLA")
      TEST_FAILED("feature filter leaked: got " + p->feature);
  TEST_PASSED();
}

void id_search_round_trip()
{
  // Pick up Kyiv via lonlatSearch first, then re-fetch by its id.
  Locus::QueryOptions opts;
  opts.SetCountries("UA");
  opts.SetResultLimit(1);
  auto locs = g_engine->lonlatSearch(opts, 30.52, 50.45, 5.0);
  if (locs.empty())
    TEST_FAILED("setup: could not fetch Kyiv via lonlatSearch");
  const long kyiv_id = locs.front()->geoid;

  // QueryOptions defaults to country=fi; idSearch is logically a global
  // lookup, so widen the country filter.
  Locus::QueryOptions opts2;
  opts2.SetCountries("all");
  auto by_id = g_engine->idSearch(opts2, kyiv_id);
  if (by_id.empty())
    TEST_FAILED("idSearch returned empty for known id " + std::to_string(kyiv_id));
  if (by_id.front()->geoid != kyiv_id)
    TEST_FAILED("idSearch returned wrong location");
  TEST_PASSED();
}

void keyword_search_with_options()
{
  // The "cities" keyword set is small and should be returnable in full.
  Locus::QueryOptions opts;
  opts.SetCountries("all");
  auto locs = g_engine->keywordSearch(opts, "cities");
  if (locs.size() < 100)
    TEST_FAILED("keywordSearch('cities') returned fewer than expected: " +
                std::to_string(locs.size()));
  TEST_PASSED();
}

void keyword_search_population_filter()
{
  // Restrict to populations > 100000. Should yield far fewer hits than
  // the unfiltered keyword set.
  Locus::QueryOptions opts;
  opts.SetCountries("all");
  opts.SetPopulationMin(100000);
  auto locs = g_engine->keywordSearch(opts, "cities");
  if (locs.empty())
    TEST_FAILED("keywordSearch('cities', pop>=100k) returned nothing");
  for (const auto& p : locs)
    if (p->population < 100000)
      TEST_FAILED("population filter leaked: got pop=" + std::to_string(p->population));
  TEST_PASSED();
}

class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }
  void test()
  {
    TEST(engine_is_ready);
    TEST(nearest_city_to_kyiv_coord);
    TEST(nearest_city_to_helsinki_coord);
    TEST(suggest_helsinki_prefix);
    TEST(suggest_kyiv_prefix_cyrillic);

    // QueryOptions-based searches that route through the new in-memory paths
    TEST(name_search_kyiv_cyrillic);
    TEST(name_search_helsinki_case_insensitive);
    TEST(name_search_country_filter);
    TEST(lonlat_search_with_options);
    TEST(lonlat_search_feature_filter);
    TEST(id_search_round_trip);
    TEST(keyword_search_with_options);
    TEST(keyword_search_population_filter);
  }
};

}  // namespace OsmBackendTests

int main()
{
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);

  std::cout << "\nGeonames OSM backend smoke test\n===============================\n";

  try
  {
    g_engine = std::make_shared<TestableEngine>("cnf/geonames-osm.conf");
    g_engine->init();

    // init() returns before background autocomplete loading completes when
    // first_construction is true. Wait for readiness before running tests.
    for (int i = 0; i < 300 && !g_engine->isSuggestReady(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!g_engine->isSuggestReady())
    {
      std::cerr << "Engine did not become suggest-ready within 30s\n";
      return 1;
    }
  }
  catch (const std::exception& e)
  {
    std::cerr << "Engine init failed: " << e.what() << '\n';
    return 1;
  }

  OsmBackendTests::tests t;
  int rc = t.run();
  g_engine.reset();
  return rc;
}
