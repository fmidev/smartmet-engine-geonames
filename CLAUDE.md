# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

The GeoNames engine (`smartmet-engine-geonames`) provides shared location services for SmartMet Server. It resolves place names to coordinates and vice versa using a PostGIS database of 11+ million place names from the GeoNames geographical database. Other engines and plugins access it at runtime for geocoding, autocomplete suggestions, and spatial queries.

## Build commands

```bash
make                # Build geonames.so shared library
make test           # Build and run tests (requires PostGIS database)
make format         # Run clang-format on all source files
make clean          # Clean build artifacts
make rpm            # Build RPM package
make install        # Install library and headers (PREFIX=/usr)
```

Tests require a running PostgreSQL/PostGIS database. In CI, a local test database is created automatically. Locally, tests connect to the host configured in `test/cnf/geonames.conf` (generated from `geonames.conf.in`).

To run tests directly after building:
```bash
cd test && make test
```

## Architecture

The engine has a pimpl (pointer-to-implementation) design with two main layers:

- **`Engine`** (`Engine.h/cpp`) — the public API, subclasses `Spine::SmartMetEngine`. Holds an `Fmi::AtomicSharedPtr<Impl>` for lock-free reads during concurrent requests. Manages the auto-reload timer (boost::asio) and atomic request counters. Also handles HTTP request parameter parsing (`parse_place`, `parse_lonlat`, `parse_geoid`, `parse_keyword`, `parse_wkt`, etc.).

- **`Impl`** (`Impl.h/cpp`) — the core data store, rebuilt atomically on reload. Contains:
  - **GeoIdMap** — hash map for O(1) GeoID lookups
  - **KeywordMap** — maps keyword strings to location lists
  - **TernaryTrees / LangTernaryTreeMap** — autocomplete indexes, one per language
  - **GeoTreeMap** — KD-trees for nearest-neighbor spatial queries (one per keyword)
  - **NameSearchCache** — LRU cache for name searches

Supporting classes:
- **`LocationPriorities`** — composite scoring (population, feature type, area, country) used to rank autocomplete results
- **`WktGeometry`** — parses WKT geometry strings from URL parameters into OGR geometries and SVG paths

### Initialization flow

1. Constructor reads libconfig configuration, sets up locale and DB connection pool
2. `init()` loads DEM and LandCover data in parallel
3. Database reads happen concurrently: countries, municipalities, geonames, alternate names, keywords
4. Index structures built: geoid map, geo-trees, ternary trees, priorities
5. `itsReady = true` once base data is loaded; autocomplete indexes may still be building
6. Auto-reload timer periodically checks database hash and swaps in a new `Impl` if changed

### Key design decisions

- The `AtomicSharedPtr<Impl>` swap pattern means all data structures are immutable once published — readers never block, and reload builds a complete new `Impl` before swapping
- Autocomplete uses ternary search trees (one per language per keyword) for prefix matching
- Spatial search uses separate KD-trees per keyword to avoid filtering overhead
- The engine resolves `SmartMet::Engine::` symbols at runtime when loaded by the server, so unresolved reference checks in the Makefile explicitly exclude these

## Dependencies

pkg-config: `fmt`, `gdal`, `icu-i18n`, `configpp`, `libpqxx`

SmartMet libraries: `smartmet-locus` (query options/types), `smartmet-macgyver` (caching, datetime, atomics), `smartmet-gis` (OGR geometry), `smartmet-spine` (server framework, Location types, HTTP)

## Configuration

Uses libconfig++ (`.conf` files). Key sections: `database` (PostgreSQL connection), `cache` (LRU sizes), `locale`, `priorities` (population/feature/area/country weights for autocomplete ranking), `areas` (display format like "City, Country"), `autoreload` (period in minutes), `demdir`/`landcoverdir`, `security` (deny patterns).

Test configuration template: `test/cnf/geonames.conf.in` — in CI the host is rewritten to point at a local test database.
