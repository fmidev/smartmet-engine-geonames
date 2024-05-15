PROG = $(patsubst %.cpp,%,$(wildcard *Test.cpp))

REQUIRES = gdal configpp

include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

CFLAGS = -DUNIX -O0 -g $(FLAGS)

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
	-lboost_thread \
	-lboost_filesystem \
	-lboost_regex \
	-lboost_iostreams \
	-lboost_chrono \
	-lboost_system \
	-lbz2 -lz -lpthread

TEST_PREPARE_TARGETS := cnf/geonames.conf
TEST_FINISH_TARGETS := dummy
TEST_DB_DIR := $(shell pwd)/tmp-geonames-db
TEST_RUNNER :=

ifdef CI
GEONAMES_HOST_EDIT := sed -e 's|"smartmet-test"|"$(TEST_DB_DIR)"|g'
TEST_PREPARE_TARGETS += start-geonames-db
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

test: $(TEST_PREPARE_TARGETS) $(PROG)
	@echo Running tests:
	@for prog in $(PROG); do $(TEST_RUNNER) ./$$prog; done
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

$(PROG) : % : %.cpp ../geonames.so $(TEST_PREPARE_TARGETS)
	$(CXX) $(CFLAGS) -o $@ $@.cpp $(INCLUDES) $(LIBS)

cnf/geonames.conf:
	$(GEONAMES_HOST_EDIT) $@.in >$@

dummy:

.PHONY: cnf/geonames.conf
