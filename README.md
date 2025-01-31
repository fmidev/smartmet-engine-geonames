
Table of Contents
=================

  * [SmartMet Server](#SmartMet Server)
  * [Introduction](#introduction)
  * [Configuration](#configuration)
  * [Docker](#docker)


# SmartMet Server
[SmartMet Server](https://github.com/fmidev/smartmet-server) is a data
and procut server for MetOcean data. It provides high capacity and
high availability data and product server for MetOcean data. The
server is written in C++.

#Introduction

SmartMet engine geonames (geoengine) provides location services for
other services. It resolves the location information for coordinates
and vice versa.

Geoengine provides shared access to the location database in the
SmartMet server. The location database is a PostGIS database and it is
based on <a href="http://www.geonames.org">the Geonames</a>, a
geographical database that covers all countries and contains over
eleven million placenames that are available for download free of
charge. The location database has to be synced with the 
the Geonames database from  time to time.

#Configuration

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

# Docker

SmartMet Server can be dockerized. This [tutorial](docs/docker.md)
explains how to explains how to configure the GeoNames engine of the
SmartMet Server when using Docker.

