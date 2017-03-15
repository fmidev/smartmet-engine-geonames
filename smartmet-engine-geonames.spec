%define DIRNAME geonames
%define LIBNAME smartmet-%{DIRNAME}
%define SPECNAME smartmet-engine-%{DIRNAME}
Summary: Smartmet geonames engine
Name: %{SPECNAME}
Version: 17.3.15
Release: 2%{?dist}.fmi
License: MIT
Group: SmartMet/Engines
URL: https://github.com/fmidev/smartmet-engine-geonames
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: libicu-devel
BuildRequires: boost-devel
BuildRequires: smartmet-library-locus-devel >= 17.3.14
BuildRequires: smartmet-library-spine-devel >= 17.3.15
BuildRequires: smartmet-library-macgyver-devel >= 17.3.14
BuildRequires: smartmet-library-gis-devel >= 17.3.14
BuildRequires: postgresql93-libs
BuildRequires: libpqxx-devel
BuildRequires: libpqxx
BuildRequires: mysql++-devel
Requires: smartmet-library-spine >= 17.3.15
Requires: smartmet-server >= 17.3.15
Requires: smartmet-library-locus >= 17.3.14
Requires: smartmet-library-macgyver >= 17.3.14
Requires: smartmet-library-gis >= 17.3.14
Requires: libatomic
Requires: libicu
%if 0%{rhel} >= 7
Requires: boost-date-time
Requires: boost-filesystem
Requires: boost-iostreams
Requires: boost-locale
Requires: boost-system
Requires: boost-thread
BuildRequires: mariadb-devel
Requires: mariadb-libs
%else
BuildRequires: mysql-devel
%endif
Provides: %{SPECNAME}
Obsoletes: smartmet-brainstorm-geoengine < 16.11.1
Obsoletes: smartmet-brainstorm-geoengine-debuginfo < 16.11.1

%description
SmartMet geonames engine


%package -n %{SPECNAME}-devel
Summary: SmartMet %{SPECNAME} development headers
Group: SmartMet/Development
Provides: %{SPECNAME}-devel
Obsoletes: smartmet-brainstorm-geoengine-devel < 16.11.1
%description -n %{SPECNAME}-devel
Smartmet %{SPECNAME} development headers.

%prep
rm -rf $RPM_BUILD_ROOT

%setup -q -n %{SPECNAME}
 
%build -q -n %{SPECNAME}
make %{_smp_mflags}

%install
%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%files -n %{SPECNAME}
%defattr(0775,root,root,0775)
%{_datadir}/smartmet/engines/%{DIRNAME}.so

%files -n %{SPECNAME}-devel
%defattr(0664,root,root,0775)
%{_includedir}/smartmet/engines/%{DIRNAME}

%changelog
* Wed Mar 15 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.3.15-2.fmi
- Removed dependency on jss::atomic_shared_ptr

* Wed Mar 15 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.3.15-1.fmi
- Recompiled since Spine::Exception changed

* Tue Mar 14 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.3.14-1.fmi
- Switched to use macgyver StringConversion tools 

* Fri Feb  3 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.2.3-1.fmi
- Added possibility to set the PostgreSQL port

* Fri Jan 27 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.27-1.fmi
- Improved initialization speed

* Tue Jan 24 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.24-1.fmi
- Added safety checks against NULL country isocodes and feature codes

* Wed Jan  4 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.4-1.fmi
- Updated to use renamed SmartMet base libraries

* Wed Nov 30 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.11.30-1.fmi
- Using test database in sample and test configuration
- Some tests and test results changed
- No installation for configuration

* Tue Nov 29 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.11.29-1.fmi
- Added option database.disable with default value false.

* Tue Nov  1 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.11.1-1.fmi
- Namespace changed. Pimple class renamed to Impl.
- Fixed code to check properly that the DEM and LandCover data are available

* Tue Sep  6 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.9.6-1.fmi
- New exception handler

* Mon Aug 15 2016 Markku Koskela <markku.koskela@fmi.fi> - 16.8.15-1.fmi
- The shutdown of this engine is a nightmare during the initialization 
- phase, because there are no way to to terminate long initialization
- processes. On the other hand, a succeful shutdown requires that also
- the Pimple object can be shutted down even it is not yet visible to
- the rest of the methods during its initialization phase.

* Tue Jun 14 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.14-1.fmi
- Full recompile

* Thu Jun  2 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.2-1.fmi
- Full recompile

* Wed Jun  1 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.1-1.fmi
- Added graceful shutdown

* Wed May 18 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.5.18-1.fmi
- Fixed to use atomic_shared_ptr instead of shared_ptr

* Mon May 16 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.5.16-1.fmi
- Added TimeZones

* Wed May 11 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.5.11-1.fmi
- Added maxdemresolution configuration setting with default value zero
- Added possibility to specify desired maximum DEM resolution when requesting an elevation
- Removed mutexes, using shared_ptr atomics instead for thread safety

* Wed Apr 27 2016 Santeri Oksman <santeri.oksman@fmi.fi> - 16.4.27-1.fmi
- Handle latlon[s]:radius the same way as lonlat[s]:radius.

* Wed Apr 20 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.4.20-1.fmi
- More efficient cache keys both in size and speed

* Mon Jan 25 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.1.25-1.fmi
- Doubled cache size to 10M

* Sat Jan 23 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.1.23-1.fmi
- Fmi::TimeZoneFactory API changed

* Mon Jan 18 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.1.18-1.fmi
- newbase API changed, full recompile

* Mon Dec 21 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.12.21-1.fmi
- Increased cache size to combat request spikes

* Mon Oct 26 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.10.26-1.fmi
- Added proper debuginfo packaging

* Mon Oct  5 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.10.5-1.fmi
- Doubled cache size from 500k to 1M

* Mon Sep 21 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.9.21-1.fmi
- Increased the cache size from 100k to 500k, the former is not sufficient due to geolocation.

* Mon Aug 24 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.24-1.fmi
- Recompiled due to Convenience.h API changes

* Tue Aug 18 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.18-1.fmi
- Recompile forced by brainstorm API changes

* Mon Aug 17 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.17-1.fmi
- Use -fno-omit-frame-pointer to improve perf use

* Fri Aug 14 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.14-1.fmi
- Read dem and landcover from the database, use raster data only if values are NULL
- Sort country translations like normal translations to avoid results like Kingdom of Sweden
- Avoid boost::lexical_cast, Fmi::number_cast and std::ostringstream for speed

* Thu Aug  6 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.8.4-1.fmi
- Moved TaggedLocation to Spine

* Thu Jul 30 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.7.30-2.fmi
- The hash value of fminames now depends on the existing last_modified columns

* Thu Jul 30 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.7.30-1.fmi
- Added a method for retrieving the hash_value for the state of the fminames database

* Thu Jul 23 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.7.23-1.fmi
- Improved initialization speed by optimizing the SQL

* Wed Jul  1 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.7.1-1.fmi
- Safer handling of NULL fields (timezone, population) during initialization

* Fri Jun 26 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.6.26-1.fmi
- Recompiled due to spine changes

* Tue Jun 23 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.6.23-1.fmi
- Forced name search and autocomplete searches to be sorted similarly
- Use similar area selection logic for name and autocomplete searches (municipality vs country)

* Mon May 25 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.5.25-1.fmi
- Fixed DEM and land cover type for coordinate searches with no named location nearby

* Tue May 12 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.5.12-1.fmi
- Fixed race conditions in dem, landcover and sort methods

* Mon May  4 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.5.4-1.fmi
- Fixed DEM value extraction for coordinate searches

* Wed Apr 29 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.29-1.fmi
- Added global land cover data
- GeoEngine now handles request location option parsing

* Thu Apr  9 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.9-1.fmi
- newbase API changed

* Wed Apr  8 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.8-1.fmi
- Dynamic linking of smartmet libraries into use
- Enabled a mock-mode which skips the heavy initialization phase

* Mon Mar 23 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.3.23-2.fmi
- Updated smartmet-gis dependency

* Mon Mar 23 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.3.23-1.fmi
- Added DEM elevations

* Mon Jan 26 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.1.26-2.fmi
- Fixed translations for iso2 countries with multiple political entities (Finland + Åland)

* Mon Jan 26 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.1.26-1.fmi
- Added PCLF and PCLD as valid country types

* Wed Dec 17 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.12.17-1.fmi
- Recompiled due to spine API changes

* Thu Nov 13 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.11.13-1.fmi
- Recompiled due to newbase API changes

* Tue Sep 30 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.9.30-1.fmi
- Fixed sorting to use iso2 instead of country since the latter is not always resolved yet

* Fri Sep 26 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.9.26-1.fmi
- geonames changed Finnish municipalities back to ADM3 class

* Wed Sep 10 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.9.10-1.fmi
- Improved the cache key for keyword searches for improved admin plugin reports

* Mon Sep  8 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.9.8-1.fmi
- Fixed priorities of ADM1 and ADM2 locations

* Fri Sep  5 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.9.5-1.fmi
- Made the API const correct

* Wed Aug 27 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.8.27-1.fmi
- Use ADM2 for Finnish municipalties, geonames.org changed the classifications

* Mon Jun 30 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.30-1.fmi
- Recompiled with latest spine API

* Wed May 14 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.14-1.fmi
- Use shared macgyver and locus libraries

* Tue May  6 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.6-2.fmi
- Use postgresql 9.0 until 9.3 installation is complete

* Tue May  6 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.6-1.fmi
- Added database.database configuration variable to a new recatored release

* Mon May  5 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.5-1.fmi
- Added database.database configuration variable

* Fri May  2 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.2-1.fmi
- Recompiled with locus library with much faster latlon searches

* Mon Apr 28 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.4.28-1.fmi
- Full recompile due to large changes in spine etc APIs

* Thu Mar 20 2014 Mikko Visa <mikko.visa@fmi.fi> - 14.3.20-1.fmi
- Open data 2014-04-01 release

* Mon Feb 3 2014 Mikko Visa <mikko.visa@fmi.fi> - 14.2.3-2.fmi
- Open data 2014-02-03 release
- Allow all alternate languages when searching for names, crashes no longer occur

* Thu Jan  9 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.1.9-1.fmi
- Recompiled with new Locus which fixes a problem in limiting keyword sizes

* Wed Jan  8 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.1.8-1.fmi
- Fixed suggest to find name,area patterns properly

* Tue Dec 10 2013 Tuomo Lauri <tuomo.lauri@fmi.fi> - 13.12.10-1.fmi
- Fixed geoengine reloading functionality

* Mon Dec  2 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.12.2-1.fmi
- Recompiled with latest Locus to disable historic names as translations

* Fri Nov 29 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.29-2.fmi
- Ternary trees now include the geoid as the part of the area to make sure all distinct features are stored

* Fri Nov 29 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.29-1.fmi
- Score PPLA2 and PPLA3 equally
- Removed open data configuration file as unnecessary

* Wed Nov 27 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.27-4.fmi
- Do not use historic alternate names at all

* Wed Nov 27 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.27-3.fmi
- Improved support in locus for ignoring locations with NULL timezone

* Wed Nov 27 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.27-2.fmi
- Locus now ignores locations with NULL timezone

* Wed Nov 27 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.27-1.fmi
- Order historic alternate names last in preference
- Prefer PPLA* in scoring over ADM3

* Tue Nov 26 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.26-1.fmi
- Recompiled with fixed locus library with corrected name comparisons (lower case)

* Mon Nov 25 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.25-1.fmi
- Added autocompletion on word boundaries
- Added possibility to force removal of underscores from geonames names

* Thu Nov 14 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.14-1.fmi
- Started using PostgreSQL database 
- Added a separate configuration file for open data machines

* Tue Nov  5 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.5-1.fmi
- Major release

* Wed Oct 9 2013 Tuomo Lauri     <tuomo.lauri@fmi.fi>    - 13.10.9-1.fmi
- Now conforming with the new Reactor initialization API

* Mon Sep 23 2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.9.23-1.fmi
- Now uses the new NearTree API

* Fri Sep 6  2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.9.6-1.fmi
- Recompiled due Spine changes

* Fri Aug 30 2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.8.30-1.fmi
- Rebuilt against the non-caching fminames

* Tue Jul 23 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.7.23-1.fmi
- Recompiled due to thread safety fixes in newbase & macgyver

* Wed Jul  3 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.7.3-1.fmi
- Update to boost 1.54
- Fixed searches with municipality names in other languages to work

* Thu Jun 27 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.6.27-3.fmi
- coordinate searches are now cached inside fminames

* Thu Jun 27 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.6.27-2.fmi
- geoengine reload now clears fminames internal caches too

* Thu Jun 27 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.6.27-1.fmi
- Bigger alternate name caches via new fminames library

* Wed Jun 26 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.6.26-1.fmi
- Recompiled with new fminames which caches intermediate results

* Mon Jun  3 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.6.3-1.fmi
- Rebuilt against the new Spine

* Fri Apr 26 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.4.26-1.fmi
- Recompiled with latest fminames to get area comparisons to be case insensitive

* Mon Apr 22 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.4.22-1.fmi
- Brainstorm API changed

* Fri Apr 12 2013 lauri <tuomo.lauri@fmi.fi>    - 13.4.12-1.fmi
- Build against the new Spine

* Tue Mar 26 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.3.26-1.fmi
- Improved error messages from geoengine reload operation

* Tue Mar 19 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.3.19-1.fmi
- Improved error handling during the reload operation

* Thu Mar  7 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.3.7-1.fmi
- Database moves to mysql.fmi.fi

* Tue Feb 26 2013 oksman <santeri.oksman@fmi.fi> - 13.2.26-1.fmi
- Rebuild for Into machines.

* Fri Feb 15 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.2.15-1.fmi
- Fixed alternate geonames to be sorted in descending order of name length

* Thu Feb 14 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.2.14-1.fmi
- Improved choices for alternate geonames (preferred names first, short names first)

* Wed Feb  6 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.2.6-1.fmi
- Built against new Spine and Server

* Tue Oct 16  2012 lauri   <tuomo.lauri@fmi.fi>     - 12.10.16-1.fmi

* Tue Oct 16  2012 lauri   <tuomo.lauri@fmi.fi>    - 12.10.16-1.el6.fmi
- Moved GeoEngine cache reporting to Ilmanet Brainstorm plugin

* Thu Oct 11  2012 lauri   <tuomo.lauri@fmi.fi>    - 12.10.11-1.el6.fmi
- Uses now new cache content reporting

* Tue Oct 9  2012 lauri   <tuomo.lauri@fmi.fi>     - 12.10.9-1.el6.fmi
- Switched to use cache (LRU policy) from MacGyver

* Thu Aug 30 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.8.30-1.el6.fmi
- Fixed autocomplete to return exact match first
- Fixed autocomplete to use proper locale for sorting the results

* Tue Aug 28 2012 lauri    <tuomo.lauri@fmi.fi>    - 12.8.28-1.el6.fmi
- Better definition of uniqueness for LocationPtr* - type

* Thu Aug  9 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.8.9-1.el6.fmi
- Sputnik API update

* Tue Aug  7 2012 lauri    <tuomo.lauri@fmi.fi>    - 12.8.7-2.el6.fmi
- Added support for 'country' parameter

* Tue Aug  7 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.8.7-1.el6.fmi
- Linked with latest fminames library

* Mon Aug  6 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.8.6-1.el6.fmi
- Optimized FetchByKeyword

* Fri Aug  3 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.8.3-1.el6.fmi
- Refactored Pimple to be more thread safe in the construction phase

* Tue Jul 31 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.31-2.el6.fmi
- Fixed race condition issues

* Tue Jul 31 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.31-1.el6.fmi
- Fixed race condition issues

* Wed Jul 25 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.26-2.el6.fmi
- Fixed keyword search counting

* Wed Jul 25 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.26-1.el6.fmi
- Improved status report
- Added cache cleaning

* Tue Jul 24 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.25-2.el6.fmi
- Added geoid to cache status printout

* Tue Jul 24 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.25-1.el6.fmi
- Added a write lock to protect the caches when a reload is requested

* Mon Jul 23 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.23-1.el6.fmi
- Added a status query

* Thu Jul 19 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.19-1.el6.fmi
- Added a reload method

* Tue Jul 10 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.10-1.el6.fmi
- Recompiled since Table changed

* Mon Jul  9 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.9-1.el6.fmi
- Added scores for ADM3 features

* Thu Jul  5 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.5-1.el6.fmi
- Migrated to boost 1.50
- Fixed suggest to use Boost.Locale collation with the last 0-byte removed since std::string cannot handle it

* Tue Jul  3 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.3-1.el6.fmi
- Server API changed
- Added ADM3 to default features in fminames

* Thu May 31 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.5.31-1.el6.fmi
- Using Boost.Locale collator classes to get Istanbul to behave right in autocomplete

* Wed Apr  4 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.4.4-1.el6.fmi
- common lib changed

* Mon Apr  2 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.4.2-1.el6.fmi
- macgyver change forced recompile

* Sat Mar 31 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.3.31-1.el5.fmi
- Upgrade to boost 1.49

* Wed Jan 18 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.1.18-1.el5.fmi
- Added caching of latlon queries

* Tue Dec 27 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.12.27-3.el5.fmi
- Bug fix to common Table class

* Tue Dec 27 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.12.27-1.el5.fmi
- fminames recompiled

* Thu Dec 22 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.12.22-2.el6.fmi
- Added a cache for keyword searches

* Thu Dec 22 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.12.22-1.el6.fmi
- Compiled with latest fminames library which should speed up keywords searches

* Wed Dec 21 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.12.21-1.el6.fmi
- RHEL6 release

* Tue Oct  4 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.10.4-1.el5.fmi
- Added caching of geoid queries

* Tue Aug 16 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.8.16-1.el5.fmi
- Upgrade to boost 1.47

* Mon Aug  8 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.8.2-1.el5.fmi
- Alternate geonames preferred field is now obeyed

* Wed Jul 27 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.7.27-1.el5.fmi
- Made all mysql queries high priority ones

* Tue Mar  1 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.3.1-1.el5.fmi
- Fixed a memory leak which caused crashes when unknown geoids were searched in the geoengine

* Mon Feb 14 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.2.14-1.el5.fmi
- Thread safety fixes related to mysql

* Tue Jan 18 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.1.18-1.el5.fmi
- Moved Location to common

* Wed Dec 15 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.12.15-1.el5.fmi
- Fixed municipality translations

* Mon Dec 13 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.12.13-1.el5.fmi
- Fixed comma usage in autocomplete

* Wed Dec  1 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.12.1-1.el5.fmi
- Autocomplete now sets utf8 on when reading geonames data

* Thu Oct 28 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.10.28-1.el5.fmi
- Upgrade to fminames 10.10.25

* Tue Sep 14 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.9.14-1.el5.fmi
- Upgrade to boost 1.44

* Mon Aug  9 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.8.9-2.el5.fmi
- Added nameCacheStatus method

* Mon Aug  9 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.8.9-1.el5.fmi
- Merged with fminames engine

* Tue Aug  3 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.8.3-1.el5.fmi
- Refactored API

* Mon Jul  5 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.7.5-1.el5.fmi
- Recompile brainstorm due to newbase hessaa bugfix

* Tue May 18 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.5.18-1.el5.fmi
- Upgrade to RHEL 5.5

* Fri Jan 15 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.1.15-1.el5.fmi
- Upgrade to boost 1.41

* Wed Dec  2 2009 mheiskan <mika.heiskanen@fmi.fi> - 9.12.2-1.el5.fmi
- Linked statically with new fminames library

* Tue Jul 14 2009 mheiskan <mika.heiskanen@fmi.fi> - 9.7.14-1.el5.fmi
- Upgrade to boost 1.39

* Wed Mar 25 2009 mheiskan <mika.heiskanen@fmi.fi> - 9.3.25-1.el5.fmi
- Full Brainstorm recompile release

* Wed Jan 07 2009 westerba <antti.westerberg@fmi.fi> - 9.1.7-1.el5.fmi
- New release with updated fminames library

* Mon Dec 29 2008 westerba <antti.westerberg@fmi.fi> - 8.12.10-1.el5.fmi
- New release with updated fminames library (bug fixes)

* Wed Dec 10 2008 mheiskan <mika.heiskanen@fmi.fi> - 8.12.10-1.el5.fmi
- New release with updated fminames library

* Wed Nov 19 2008 mheiskan <mika.heiskanen@fmi.fi> - 8.11.19-1.el5.fmi
- New brainstorm release

* Thu Oct 23 2008 mheiskan <mika.heiskanen@fmi.fi> - 8.10.23-1.el5.fmi
- Linked with updated macgyver library

* Thu Oct  9 2008 westerba <antti.westerberg@fmi.fi> - 8.10.9-1.el5.fmi
- Packaged operational and development files into separate packages

* Mon Oct  6 2008 mheiskan <mika.heiskanen@fmi.fi> - 8.10.6-1.el5.fmi
- Initial build
