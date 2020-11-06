PROG = $(patsubst %.cpp,%,$(wildcard *Test.cpp))

REQUIRES = gdal configpp

include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

CFLAGS = -DUNIX -O0 -g $(FLAGS)

ifneq "$(wildcard /usr/gdal30/include)" ""
  INCLUDES += -isystem /usr/gdal30/include
  LIBS += -L$(PREFIX)/gdal30/lib
else
  INCLUDES += -isystem /usr/include/gdal
endif

INCLUDES += \
	-I../geonames \
	-isystem /usr/include/mysql \
	-isystem /usr/include/smartmet

LIBS += \
	../geonames.so \
	-lsmartmet-spine \
	-lsmartmet-macgyver \
	-lsmartmet-locus \
	-lsmartmet-newbase \
	$(CONFIGPP_LIBS) \
	-lpqxx \
	-lboost_date_time \
	-lboost_thread \
	-lboost_filesystem \
	-lboost_regex \
	-lboost_iostreams \
	-lboost_system \
	-lbz2 -lz -lpthread

all: $(PROG)
clean:
	rm -f $(PROG) *~


ifdef CI
test: fminames $(PROG)
else
test: $(PROG)
endif
	@echo Running tests:
	@for prog in $(PROG); do ./$$prog; done

fminames:
	sed -i -e 's/"smartmet-test"/"localhost"/g' cnf/*.conf
	@/usr/share/smartmet/test/db/init-and-start.sh && /usr/share/smartmet/test/db/install-test-db.sh > /dev/null


$(PROG) : % : %.cpp ../geonames.so
	$(CXX) $(CFLAGS) -o $@ $@.cpp $(INCLUDES) $(LIBS)
