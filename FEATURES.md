# smartmet-engine-geonames — Feature List

A structured inventory of capabilities provided by the geonames
engine. Use as a checklist when drafting release notes. When new
functionality is added, append the new entry under the matching
section (and bump the *Last updated* line at the bottom).

`smartmet-engine-geonames` (output: `geonames.so`) is the SmartMet
Server engine that resolves place names ↔ coordinates and provides
location-based services to every plugin. It builds a fully in-memory
index from a PostGIS database of 11+ million places (sourced from the
[GeoNames](http://www.geonames.org) geographical database) plus
optional DEM and land-cover layers. Often called the **GeoEngine**.

---

## 1. Lookup APIs

Single-result lookups via `Engine`:

- **`nameSearch(name, lang)`** — resolve a single place name in a
  language to a `LocationPtr`.
- **`idSearch(geoid, lang)`** — by GeoNames ID.
- **`lonlatSearch(lon, lat, lang, radius)`** — nearest place within
  radius.
- **`featureSearch(lon, lat, country, featureCode, lang, radius)`** —
  nearest place of a feature class (e.g. populated place, mountain).
- **`keywordSearch(lon, lat, keyword, …)`** — nearest place inside a
  predefined keyword set.
- **`wktSearch(wkt, …)`** — resolve a WKT geometry to a representative
  location.

List-result lookups (multi-result `LocationList`):

- **`nameSearch(QueryOptions, name)`** — all matches for a name.
- **`latlonSearch(QueryOptions, lat, lon, radius)`** /
  **`lonlatSearch(...)`** — places inside a radius.
- **`idSearch(QueryOptions, id)`** — multi-row by id (alternate
  names).
- **`keywordSearch(QueryOptions, keyword)`** — every place in a
  named set.

## 2. Autocomplete / suggestion

- **`suggest(pattern, predicate, lang, keyword, maxResults, page)`**
  — prefix-matched autocomplete using a ternary search tree (one per
  language).
- **`suggestDuplicates(...)`** — returns name collisions explicitly.
- **Multi-language suggest** — `vector<LocationList>` overload that
  returns matches across multiple languages.
- **Predicate filtering** — callers pass a `std::function` predicate
  to filter at index-walk time (e.g. only Finnish municipalities).
- **Pagination** — results support page / page-size.

## 3. Ranking & priorities

- **`LocationPriorities`** composite scoring used by autocomplete:
  - **Population weight** — bigger cities first.
  - **Feature-code weight** — populated places vs administrative vs
    natural features.
  - **Area weight** — larger administrative areas first.
  - **Country weight** — local country preference.
- **Configurable weights** under `priorities` in the config.
- **`assign_priorities(locations)`** — explicit priority assignment
  for a list.
- **`sort(locations)`** — stable sort by priority.

## 4. Keyword groups

- **Named location sets** loaded from the database table.
- **Per-keyword in-memory indexes** — separate KD-tree (for spatial
  search) and ternary search trees (for autocomplete) per keyword.
- **`add(tag, location)`** — extension hook for plugin-supplied
  in-memory keywords.

## 5. WKT geometries

- **`WktGeometry`** — parses WKT strings into:
  - **OGR geometry** for spatial operations.
  - **SVG path string** for rendering.
  - **Lat/Lon coordinate lists** for sampling.
- **Used by timeseries / WMS / EDR** plugins to convert request
  parameters like `wkt=POLYGON(...)` into usable shapes.

## 6. Translations

- **`translateLocation(location, lang)`** — translate a location
  name into another language using GeoNames alternate names.
- **Locale-aware sorting** — autocomplete order respects the
  configured locale (e.g. Finnish `fi_FI.UTF-8` for ÄÖÅ
  collation).
- **Configurable display format** — `areas` config controls how
  area-qualified names render (e.g. `"%name, %area"`).

## 7. DEM and land-cover integration

- **Digital Elevation Model** — loaded from `demdir` at init; used by
  plugins for elevation lookup.
- **Land-cover layer** — loaded from `landcoverdir`; used together
  with DEM for landscape-aware temperature interpolation.
- **`maxdemresolution`** — caps the DEM resolution per request.

## 8. In-memory indexes

Built once at init / reload, immutable thereafter:

- **`GeoIdMap`** — O(1) hash from GeoNames ID to location.
- **`KeywordMap`** — keyword → `LocationList`.
- **`GeoTreeMap`** — one KD-tree (`Fmi::NearTree`) per keyword for
  nearest-neighbour spatial search.
- **`LangTernaryTreeMap`** — per-language, per-keyword ternary
  search trees for autocomplete.
- **`NameSearchCache`** — LRU cache of recent name searches
  (`Fmi::Cache::Cache`).

## 9. Atomic reload

- **`Fmi::AtomicSharedPtr<Impl>`** — a complete new `Impl` is built
  off-thread and swapped in atomically; readers never block.
- **`autoreload` interval** — periodic database-hash check
  (boost::asio timer).
- **`hash_value()`** — content-hash of the current snapshot used to
  detect database changes.
- **Manual reload** — supported as well, callable through the
  admin path.
- **`nextAutoreloadCheckTime(incr)`** — next planned check.
- **`is_autoreload_enabled()`** — reload toggle.
- **`itsReady` gating** — base lookup works as soon as the base data
  is loaded; autocomplete indexes may still be filling in.

## 10. Concurrency

- **Atomic counters** for in-flight requests.
- **Parallel init** — countries, municipalities, places, alternate
  names, and keywords loaded concurrently.
- **DEM and LandCover loaded in parallel** with the database reads.
- **Lock-free reads** — every published `Impl` is immutable.

## 11. Configuration

libconfig file with these sections:

- **`database`** — PostgreSQL/PostGIS connection (host, port, user,
  password, dbname).
- **`cache`** — LRU sizes (name-search cache, etc.).
- **`locale`** — e.g. `fi_FI.UTF-8`.
- **`remove_underscores`** — control name normalisation.
- **`priorities`** — population / feature / area / country weights.
- **`areas`** — display format like `"%name, %area"`.
- **`autoreload`** — period in minutes (0 = disabled).
- **`demdir`** / **`landcoverdir`** — paths to DEM / land-cover
  files.
- **`maxdemresolution`** — max DEM resolution for requests.
- **`security`** — patterns for denying suspicious queries.
- **SmartMet config extensions** — `@include`, `@ifdef`, `$(VAR)`,
  `%(DIR)`.

## 12. HTTP-parameter helpers

Plugins call into the engine to parse common location-related URL
parameters consistently:

- **`parse_place`** — `place=Helsinki`.
- **`parse_lonlat`** / **`parse_latlon`** — coordinate pairs.
- **`parse_geoid`** — GeoNames ID.
- **`parse_keyword`** — keyword sets.
- **`parse_wkt`** — WKT geometries.

These produce normalised `LocationOptions` that downstream
engines/plugins consume.

## 13. Engine factory and lifecycle

- **Subclasses `Spine::SmartMetEngine`** — standard `init()`,
  `shutdown()`.
- **Pimpl design** — `Engine` is the public face, `Impl` holds the
  data, swapped via `AtomicSharedPtr`.
- **Engine factory** — exposes the `engine_class_creator` C symbol
  for `dlopen`.

## 14. Testing

- **`tframe`** regression test framework (FMI's own, not Boost.Test).
- **`test/EngineTest.cpp`** — boots a `Spine::Reactor` with the
  engine loaded.
- **`test/cnf/geonames.conf.in`** — template config; the host is
  rewritten by CI to point at the local test database.
- **PostgreSQL/PostGIS required** for tests; CI provisions a local
  instance automatically.
- **Direct test run**: `cd test && make test`.

## 15. Documentation

- **`README.md`** — configuration overview.
- **`docs/docker.md`** — running the engine in Docker.
- **`Doxyfile`** — Doxygen config.

## 16. Build & integration

- **Output**: `geonames.so`.
- **Loaded at**: `$(prefix)/share/smartmet/engines/geonames.so`.
- **Build**: `make`.
- **Format**: `make format` runs clang-format.
- **Install**: `make install` (default `PREFIX=/usr`).
- **RPM**: `make rpm`.
- **pkg-config requirements**: `fmt`, `gdal`, `icu-i18n`,
  `configpp`, `libpqxx`.
- **SmartMet libraries**: `smartmet-library-locus` (query options),
  `smartmet-library-macgyver` (caching / datetime / atomics),
  `smartmet-library-gis` (OGR geometry), `smartmet-library-spine`
  (server framework, Location types, HTTP).
- **CI**: CircleCI on RHEL 8 / RHEL 10 (`fmidev/smartmet-cibase-{8,10}`
  Docker images) with a local PostGIS for tests.

---

*Last updated: 2026-06-01.*
