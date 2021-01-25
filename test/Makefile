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

TEST_PREPARE_TARGETS := cnf/geonames.conf
TEST_FINISH_TARGETS := dummy
TEST_DB_DIR := $(shell pwd)/tmp-geonames-db
TEST_RUNNER :=

ifdef CI
GEONAMES_HOST_EDIT := sed -e 's/"smartmet-test"/"localhost"/g'
TEST_PREPARE_TARGETS += geonames-database start-geonames-db
TEST_FINISH_TARGETS += stop-test-db
else
GEONAMES_HOST_EDIT := cat
endif

all: $(PROG)

clean:
	rm -f $(PROG) *~
	rm -f cnf/geonames.conf
	-$(MAKE) stop-test-db
	rm -rf tmp-geonames-db

test: $(PROG) $(TEST_PREPARE_TARGETS)
	@echo Running tests:
	@for prog in $(PROG); do ./$$prog; done
	-$(MAKE) $(TEST_FINISH_TARGETS)

geonames-database:
	@-$(MAKE) stop-test-db
	rm -rf tmp-geonames-db
	if ! /usr/share/smartmet/test/db/create-local-db.sh $(TEST_DB_DIR) >tmp-geonames-db.log 2>&1 ; then \
	    cat tmp-geonames-db.log; \
	    false; \
	fi

start-geonames-db: geonames-database
	/usr/share/smartmet/test/db/test-db-ctl.sh $(TEST_DB_DIR) start -w

stop-test-db:
	-/usr/share/smartmet/test/db/test-db-ctl.sh $(TEST_DB_DIR) stop

$(PROG) : % : %.cpp ../geonames.so
	$(CXX) $(CFLAGS) -o $@ $@.cpp $(INCLUDES) $(LIBS)

cnf/geonames.conf:  dummy
	$(GEONAMES_HOST_EDIT) $@.in >$@

dummy:
