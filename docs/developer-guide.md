# Geonames engine developer guide

This guide is for developers who change `smartmet-engine-geonames`, or use it from a
plugin. The engine is the server's location service: it turns place names, coordinates,
geoids, keywords and WKT into `Spine::Location`s, answers autocomplete queries, and
provides the DEM and land cover data to other engines.

[CLAUDE.md](../CLAUDE.md) has an architecture summary; [docker.md](docker.md) the
container setup.

## Contents

1. [Building and testing](#1-building-and-testing)
2. [Structure](#2-structure)
3. [Start-up and reload](#3-start-up-and-reload)
4. [Searching](#4-searching)
5. [Parsing locations from a request](#5-parsing-locations-from-a-request)
6. [Autocomplete](#6-autocomplete)
7. [DEM and land cover](#7-dem-and-land-cover)
8. [Configuration](#8-configuration)
9. [Compatibility](#9-compatibility)
10. [Known pitfalls](#10-known-pitfalls)

---

## 1. Building and testing

```bash
make
make test     # needs a PostGIS database (test/cnf/geonames.conf, generated from geonames.conf.in)
```

In CI a local test database is created; locally the tests use the host configured in the
test configuration. The autocomplete and timeseries plugin tests exercise the engine too.

## 2. Structure

* **`Engine`** is the public class. It holds the data as `Fmi::AtomicSharedPtr<Impl>`, so
  requests read an immutable snapshot without locking, and a reload swaps in a complete
  new one.
* **`Impl`** holds everything read from the database: the locations, a geoid map, keyword
  lists, per-keyword k-d trees for nearest searches, the autocomplete indexes per
  language, the name-search cache, countries and municipalities, and the priorities.
* **`AutoCompleteIndex`** is the array-backed prefix index used for `suggest()` (it
  replaced the ternary search trees): keys are ICU collation keys, a prefix query is two
  binary searches over a sorted array, and many keys may share a value.
* **`LocationPriorities`** scores locations for ranking (population, feature code, area,
  country, exact-match bonus).
* **`WktGeometry`** parses `wkt=` values into OGR geometries and SVG paths.
* **`LanguageScript`** handles language-to-script rules for autocomplete.

## 3. Start-up and reload

`init()` loads the DEM and land cover in parallel with the database: countries,
municipalities, geonames, alternate names and keywords are read concurrently, then the
geoid map, the k-d trees, the priorities and the autocomplete indexes are built. The engine
is usable for searches before the autocomplete indexes are finished; `isSuggestReady()`
tells when they are.

**Auto-reload.** With `autoreload.period` (minutes) set, a timer checks whether the
database has changed: the "hash" is the latest modification time of the relevant tables
(`max(last_modified)`). On a change, `reload()` builds a complete new `Impl` and swaps it
in. `/admin?what=reload` triggers the same; it is registered as requiring
authentication, so it exists only when the server has `admin.user` / `admin.password`
(see the spine guide). `what=geonames` (public) reports the engine's state.

With `strict = true`, a failure to read the hash or to find expected data is an error;
otherwise it is logged and ignored.

## 4. Searching

| Call | Returns |
|------|---------|
| `nameSearch(name, lang)`, `nameSearch(options, name)` | The best match or all matches for a name. `options` (`Locus::QueryOptions`) sets language, countries, feature codes, result count, and so on. |
| `lonlatSearch(lon, lat, lang, maxdistance)`, `latlonSearch()` | The nearest place to a coordinate. |
| `featureSearch(lon, lat, lang, features)` | The nearest place with a given feature code. |
| `idSearch(geoid, lang)` | A place by geoid. |
| `keywordSearch(options, keyword)`, `keywordSearch(lon, lat, …)` | All places of a keyword (a named location list), or the nearest one of them. |
| `wktSearch(wkt, lang, radius)` | A location for a WKT geometry. |
| `countryName(iso2, lang)` | Country names. |

Name searches go through an LRU cache (`cache.max_size`). Results are sorted by
priority (`sort()`, `assign_priorities()`).

## 5. Parsing locations from a request

`parseLocations(request)` is what the plugins use to read the standard location options:

| Parameter | Meaning |
|-----------|---------|
| `place`, `places` | Names, searched with the request's `lang`. |
| `lonlat`, `lonlats`, `latlon`, `latlons` | Coordinates. |
| `geoid`, `geoids` | Geoids. |
| `keyword` | A named location list. |
| `area`, `areas`, `path`, `paths`, `bbox`, `bboxes`, `wkt` | Areas, paths and geometries. |
| `feature`, `maxdistance`, `type`, `meta` | Search options. |

A `:radius` suffix gives a radius, for example `area=Helsinki:10`. Each location is
tagged with the original request string, so results can be labelled as the user wrote
them. `parseLocations(fmisids, …)` resolves observation station ids the same way, and
`getWktGeometries()` returns the geometries for area queries.

## 6. Autocomplete

`suggest(pattern, predicate, lang, keyword, page, maxresults)` (and the variants for
duplicates and several languages) look the pattern up in the language's index, filter the
candidates with the predicate, rank them with `LocationPriorities`, and return one page.
Patterns are normalised with ICU collation, so case and accents do not matter
(`ascii_autocomplete` also folds to ASCII). `disable_autocomplete` (formerly `mock`)
skips building the indexes, which makes start-up much faster for servers that do not
serve autocomplete. `cache.suggest_max_size` sizes the suggest cache.

## 7. DEM and land cover

`demdir` and `landcoverdir` point at the elevation and land cover data. `dem()`,
`landCover()`, `demHeight(lon, lat)` and `coverType(lon, lat)` expose them; the
timeseries plugin passes `dem()` and `landCover()` on to the grid engine.
`maxdemresolution` limits the DEM resolution used.

The locations returned by database searches (name, coordinate, id and keyword
searches) get their elevation and cover type from a cache keyed by the coordinate
(`cache.terrain_max_size` entries, default 500000, roughly 130 bytes each). A large
keyword touches a different DEM tile for nearly every location, so computing the
values is slow when the tiles are not in the page cache. Coordinates do not change
when keywords do, so the cache needs no invalidation; a reload takes it over unless
`demdir`, `landcoverdir` or `maxdemresolution` changed.

## 8. Configuration

| Key | Meaning |
|-----|---------|
| `database` (`host`, `port`, `user`, `pass`, `database`, `where.geonames`, `where.alternate_geonames`, `overrides`) | The PostGIS database and filters on what is read. `database.disable` runs without a database. |
| `cache.max_size`, `cache.suggest_max_size`, `cache.terrain_max_size` | Cache sizes (number of entries). |
| `priorities` (`match`, population, feature and country weights) | Ranking. |
| `areas` | Display formats for area names. |
| `autoreload.period` | Reload check interval in minutes. |
| `demdir`, `landcoverdir`, `maxdemresolution` | DEM and land cover. |
| `security` (`disable`, `names.deny`) | Regex patterns of names that are rejected. A configured `security` block is in effect unless `disable = true`. |
| `disable_autocomplete`, `ascii_autocomplete`, `remove_underscores`, `language_scripts` | Autocomplete. |
| `strict`, `verbose` | Error handling and logging. |

## 9. Compatibility

The engine's methods are **non-virtual**; plugins link against their symbols when they
are loaded. Adding methods is safe; changing a signature makes old plugins fail to load
with an unresolved symbol. Changing the layout of `Spine::Location`, `LocationOptions` or
`Locus::QueryOptions` changes the ABI of every plugin that uses them.

## 10. Known pitfalls

* **Reload doubles memory use** while the new `Impl` is built next to the old one.
* **Suggest is not ready at start.** Autocomplete returns nothing useful until
  `isSuggestReady()`.
* **Update detection is by modification time.** Changes that do not update
  `last_modified` are not noticed by auto-reload.
