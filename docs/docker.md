# Tutorial

This tutorial explains how to configure the geonames engine of the SmartMet Server when using Docker.

## Prereqs

Docker software has been installed on some Linux server where you have access to and the smartmetserver docker container is up and running.
The geonames engine and the configuration file it uses have been defined in the main configuration file smartmet.conf already.

### File geonames.conf

The purpose of the file "geonames.conf" is to define several configuration attributes that are needed for specifying the location services. If you followed the “SmartMet Server Tutorial (Docker)” you have your configuration folders and files in the host machine at $HOME/docker-smartmetserver/smartmetconf but inside Docker they show up under /etc/smartmet. 

1. Go to the correct directory and enter command below to review the file:


```
$ less geonames.conf
```

You will see something like this:

![](https://github.com/fmidev/smartmet-plugin-wms/wiki/images/GeoNames.PNG)

2. Use Nano or some other editor to enable/disable configuration attributes or to change their values if needed.

## Configuration attributes

### verbose 
This is mostly used for debugging and is set to false by default.
```
verbose = false;
```

### remove_underscores
This is used to state whether the station names should be splittable into words or not.
```
remove_underscores = true;
```

### locale
This is used for defining the user's format for the specification of language and affects autocomplete too. For example using en_US would mean that Ä and A would be considered equivalent. Example of the locale for Finnish language below: 
```
locale = "fi_FI.UTF-8";
```

### demdir
This attribute is used to define demdata directory where global topographic data has been stored (DEM = Digital Elevation Model). 
```
#demdir = "/smartmet/share/gis/rasters/viewfinder";
```
DEM data is assumed to be in the format provided by the Viewfinder project (SRTM). For more details about that you can review:
```
http://viewfinderpanoramas.org/dem3.html
```
The Viewfinder offers data in 1 and 3 arc-second pieces. At FMI also 9 arc-second versions have been calculated.

### maxdemresolution
This attribute is used to set the precision limit in kilometers. No more accurate DEM data than the limit value is used. 
1 arc-second is about 30 meters, 3 arc-seconds is about 90 meters and 9 arc-seconds is about 270 meters. The 1 arc-second data covers only Scandinavia and the Alps, so it takes less than 10 gigabytes of memory. The 3 arc-second data takes about 90 gigabytes of memory and the 9 arc-second data takes 3*3 meaning 9 times less memory than the 3 arc second-data. Restricting the resolution may help to avoid page faults if the server does not have much memory.  

* 0 meters allows the highest possible resolution.
* 50 meters disables Scandinavian 30 m data.
* 100 meters disables global 90 meter data.
* If this attribute is omitted, the dem value will always be NaN.
```
maxdemresolution = 0;
```

### landcoverdir
This attribute is used to define directory where global land use data has been stored in SRTM format. 
```
#landcoverdir = "/smartmet/share/gis/rasters/globcover";
```

In practice the data is assumed to be GlobCover data. For more details about that you can review:
```
http://due.esrin.esa.int/page_globcover.php
```

**Note:** If attributes demdir and/or landcoverdir are not set, landscape corrections can not be made for temperature forecasts. In normal conditions, the temperature falls as it goes higher.

### database
This attribute is used to define database settings. **Do NOT** use the full name. Use the alias only because different networks use different full host names but the same aliases.
```
database:
{
        host     = "localhost";
        user     = "fminames_user";
        database = "fminames";
        pass     = "gamer";

        # For quick memory leak tests with valgrind:
        where:
        {
        }
}
```

### cache
This attribute is used to define the maximum cache size in bytes.
```
cache:
{
        # Cache maximum size
        max_size        = 10000000;
};
```

### priorities
This attribute is used to define how each place name is scored for the autocomplete plugin when using serach location. The higher the score, the higher the name will be raised when the first letters of the name are written to the autocomplete. Priorities are defined for:

* populations 
* features 
  - default_features (default feature priorities for particular regions)
  - FI_features (feature priorities for a specific country) 
* areas (priorities of areas within a country)
* countries (the country code from the [ISO 3166-1 aplha-2 codes](https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2) along with the priority index)

**priorities / populations**

Defines the divisor of the population of the place to get scoring based on population.
The default value at FMI is 100,000, but it does not work well for some of the most populous countries. For example smaller Chinese million cities get too high a score. Instead a greater weight has been given to Scandinavia at FMI.
```
{
   # Divide population to get score

   populations:
   {
      FI = 2000;        // Finland
      AX = 2000;        // Aland
      EE = 10000;       // Estonia
etc...
   };
```
**Priorities / features**

Features are used to define how the place name type is prioritized in each country. The list of types can be found at:
```
http://www.geonames.org/export/codes.html
```
Capitals, ski resorts, capitals of counties, etc. are emphasized under default_features and FI_features. It might be good to emphasize places like airports and ports too. Finland has its own setups because Åland is an autonomous region and has its own codes for place names.
```
   features:
   {
        default = "default_features";
        FI      = "FI_features";
   };
```
**Priorities / default_features**
```
   default_features:
   {
        PPLC    = 35;    // populated place
        SKI     = 28;    // skiing place
        PPLA    = 25;
        PPLG    = 25;
        PPLA2   = 25;
        PPLA3   = 25;
        PPL     = 20;
        ADM2    = 20;    // we do not know which ones are municipalities around the world
        ADM3    = 20;
        PPLX    = 19;
        ADMD    = 14;
        ISL     = 12;
        POST    = 10;
        default = 0;
   };
```
**Priorities / FI_features**
```
   FI_features:
   {
        PPLC    = 35;    // populated place
        SKI     = 28;    // skiing place
        PPLA    = 25;
        PPLG    = 25;
        PPL     = 20;
        PPLA2   = 20;
        PPLA3   = 20;
        PPLX    = 19;
        ISL     = 12;
        POST    = 10;
        ADM1    = 0;     // Åland municipalities
        ADM2    = 0;     // Obsolete category in Finland (only Åland is ADM2)
        ADM3    = 2;     // Finland municipalities
        ADMD    = 1;
        default = 0;
   };
```
**Priorities / areas**

This attribute is used to prioritize certain areas more than the others. The area here refers to the name of the area that is assigned to the place name in autocomplete. The area can be for example Kallio (district in Helsinki) or Berlin (town in Germany). Extra points are given to districts of the biggest cities, because they often have more inhabitants than the smallest municipalities.
```
   areas:
   {
        Helsinki = 2;
        Espoo    = 1;
        Vantaa   = 1;
        Turku    = 1;
        Tampere  = 1;
        Oulu     = 1;
        default  = 0;
   };
```
**Priorities / countries**

This attribute can be used to prioritize certain countries more than the other countries. Nordic countries and Estonia have been emphasized in the example below.
```
   countries:
   {
        FI = 15;
        AX = 15;
        SE = 12;
        NO = 10;
        DK = 10;
        EE = 9;
        default = 0;
   };

};
```

3. You can test the Geonames engine (geoengine) by the timeseries plugin with query below that can be used to fetch the temperature forecasted for the city of Turku:

```
http://hostname:8080/timeseries?format=debug&place=Turku&param=name,time,temperature
```

**Note:** Replace hostname with your host machine name, by localhost or by host-ip. This depends on where you have the container you are using.