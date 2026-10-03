# smartmet-engine-geonames

Part of [SmartMet Server](https://github.com/fmidev/smartmet-server). See the [SmartMet Server documentation](https://github.com/fmidev/smartmet-server) for a full overview of the ecosystem.

## Overview

The geonames engine (also called geoengine) provides shared location services to SmartMet Server. It resolves place names to coordinates and vice versa, using a PostGIS database based on the [GeoNames](http://www.geonames.org) geographical database with over eleven million place names.

## Configuration

The configuration file consists of the configuration of
several parameters that are needed for specifying the location
services. These parameter include the following:

* Station names

This configuration states whether  the station names should be splittable into words or not. For example,
<pre><code>
remove_underscores = true;
</code></pre>

* Locale

Locale defines the user's  format for the specification of language.  <a href="https://gcc.gnu.org/onlinedocs/libstdc++/manual/localization.html">This link</a> gives the GNU document on locale for C and C++. In the configuration file, we can specify for example the locale for  Finnish language   as 
<pre><code>
locale = "fi_FI.UTF-8";
</code></pre>
Using  en_US would mean the characters Ä and A would be considered equivalent. The language used affects the autocomplete feature.

* maxdemresolution for the data

<pre><code>
maxdemresolution = 0;
</code></pre> 
The setting of 0 meters allow highest possible resolution.  Do not use too high resolution data to avoid page faults

* LandCover data directory
<pre><code>
landcoverdir = "directory_name";
</code></pre> 

* Database settings
 
Do NOT use the full name, use the alias only
because different networks use different full host names but the same alias.

<pre><code>
database:
{
        host     = "localhost";
        user     = "username";
        database = "databasename";
        pass     = "password";

};

</code></pre>

* Cache Maximum size
<pre><code>
cache:
{
       max_size        = cache size in bytes;
};

</code></pre>

* Automatic enginen reload tietokannan muutosten case

<pre><code>
autoreload:
{
       period        = period in minutes;
};

</code></pre>

Default 0 means - autoreload is disabled


* Language writing scripts

geonames.org stores alternate names under a language code without guaranteeing
that the name is actually written in that language's script. Ukrainian places
for example carry `uk` names written in Latin, and since name selection breaks
ties by length and alphabetical order, a romanized form such as `Lutsk` wins
over `Луцьк`. Listing a language here discards its names that are not written in
the expected script.

<pre><code>
language_scripts:
{
      uk = "Cyrillic";
};
</code></pre>

The check is off entirely when the section is absent. Both long and short ICU
script names are accepted, for example `Cyrillic` and `Cyrl`, and an unknown
name is an error at startup. Only list languages written in a single script:
Serbian uses both Cyrillic and Latin, and Japanese and Chinese mix scripts
within a single name.

Note that a place whose only name in a language is rejected gets no translation
at all, and is then shown under its primary name.

* Priorities


Priorities specify the priorities of countries, priorities of areas within a country, priorities of features and priorities of country specific features.


Use some criteria to prioritize the countries. The priority index along with the country code from the  <a href="https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2">ISO 3166-1 alpha-2 codes</a> is given in the configuration file.
 
<pre><code>
priorities: 
{
      FI = priority index;        // Finland
      EE = priority index;       // Estonia
      SE = priority index;       // Sweden
      ...
      default = 100000;
   };
</code></pre>

* Feature priorities

Feature priority for a particular region or country
<pre><code>
 {
        default = "default_features";
        FI      = "FI_features"; // specific features for Finland
   };
   default_features:
   {
	PPLC    = priority index;  // populated place
        SKI     = priority index;  // skiing place
	...

   };
</code></pre>

* Country specific features

<pre><code>
FI_features:
   {
	PPLC    = priority index;  // populated place
        SKI     = priority index;  // skiing place
	...
    };
</code></pre>

* Areas

Priorities of areas within a country 
<pre><code>
   areas:
   {
        Area1 = 2;
        Area2    = 1;
	...

        default  = 0;
   };
</code></pre>

* Countries

<pre><code>
   countries:
   {
        FI = priority index;
        SE = priority index;
        NO = priority index;
        ...

	default = 0;
   };
</code></pre>

* Timezones

The timezone of a coordinate is resolved from timezone polygons with
`Fmi::TimeZoneFinder` from
[smartmet-library-gis](https://github.com/fmidev/smartmet-library-gis).
This is used for locations given as coordinates, including the case where
a named place is found near the coordinate: the location keeps the
timezone of the coordinate itself, which matters near borders such as
Tornio (Europe/Helsinki) and Haparanda (Europe/Stockholm).

The recommended data is the "with-oceans" release of
[timezone-boundary-builder](https://github.com/evansiroky/timezone-boundary-builder),
which post-processes OpenStreetMap boundary data into polygons for every
IANA timezone and covers the entire globe. **By default the polygons are
read from the shapefile installed by the smartmet-timezones RPM,
`/usr/share/smartmet/timezones/timezones-with-oceans.shp`,** so no
configuration is needed. The `timezones` setting can name another source:

<pre><code>
timezones:
{
    // Any GDAL/OGR vector source. A relative path is relative to this file.
    source = "/usr/share/smartmet/timezones/timezones-with-oceans.shp";
    layer  = "";              // optional layer name for multi-layer sources

    // Or a PostGIS table instead of source:
    // database:
    // {
    //     host     = "localhost";
    //     port     = 5432;
    //     database = "gis";
    //     user     = "gis_user";
    //     pass     = "secret";
    // };
    // table = "public.timezones";

    field        = "tzid";    // attribute holding the IANA timezone name
    max_vertices = 256;       // polygon piece size, affects speed and memory only
    preferred    = [];        // winners in disputed areas, e.g. ["Asia/Shanghai"]
    make_valid   = true;      // repair invalid geometries
    threads      = 0;         // build threads, 0 = all cores
};
</code></pre>

The polygons are mandatory: the engine fails to start if they cannot be
read. There is no fallback, since answers without polygons would be wrong
without anyone noticing. Building the search structure takes about a
second and about 90 MB of memory, and it is done in parallel with loading
the database. Database reloads keep the polygons, a change of the
`timezones` setting requires a restart.

To load the data into PostGIS, keep it as `geometry(MultiPolygon, 4326)`
(never `geography`) and do not simplify it:

<pre><code>
ogr2ogr -f PostgreSQL PG:"host=... dbname=... user=..." \
    /usr/share/smartmet/timezones/timezones-with-oceans.shp -nln public.timezones \
    -nlt PROMOTE_TO_MULTI -lco GEOMETRY_NAME=geom -lco FID=gid -lco PRECISION=NO
</code></pre>

Plugins can resolve coordinates with `getTimeZoneName(lon, lat)` and
`getTimeZone(lon, lat)`. See the
[timezone documentation](https://github.com/fmidev/smartmet-library-gis/blob/master/docs/gis-timezones.md)
of smartmet-library-gis for the data, overlapping zones and semantics at sea.

## Documentation

- [Developer guide](docs/developer-guide.md) — structure, reload, searching, request parsing, autocomplete, configuration

## Docker

SmartMet Server can be dockerized. This [tutorial](docs/docker.md)
explains how to explains how to configure the GeoNames engine of the
SmartMet Server when using Docker.

## License

MIT — see [LICENSE](LICENSE)

## Contributing

Bug reports and pull requests are welcome on [GitHub](../../issues).
