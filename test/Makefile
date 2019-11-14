PROG = $(patsubst %.cpp,%,$(wildcard *Test.cpp))

FLAGS = -std=c++11 -Wall -W -Wno-unused-parameter

EXTRAFLAGS = -Werror -pedantic -Wpointer-arith -Wcast-qual \
	-Wcast-align -Wwrite-strings -Wconversion -Winline \
	-Wctor-dtor-privacy -Wnon-virtual-dtor -Wno-pmf-conversions \
	-Wsign-promo -Wchar-subscripts -Wold-style-cast \
	-Wshadow -Woverloaded-virtual

DIFFICULTFLAGS = -Weffc++ -Wunreachable-code -Wredundant-decls

ifeq ($(TSAN), yes)
  FLAGS += -fsanitize=thread
endif
ifeq ($(ASAN), yes)
  FLAGS += -fsanitize=address -fsanitize=pointer-compare -fsanitize=pointer-subtract -fsanitize=undefined -fsanitize-address-use-after-scope
endif


CC = g++
CFLAGS = -DUNIX -O0 -g $(FLAGS)

INCLUDES = \
	-I ../geonames \
	-I /usr/include/mysql \
	-I /usr/include/smartmet \
	`pkg-config --cflags libconfig++`

LIBS = \
	../geonames.so \
	-lsmartmet-spine \
	-lsmartmet-macgyver \
	-lsmartmet-locus \
	-lsmartmet-newbase \
	`pkg-config --libs libconfig++` \
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

.ciprep:
	test "$$CI" != "true" || ( /usr/share/smartmet/test/db/init-and-start.sh && \
		/usr/share/smartmet/test/db/install-test-db.sh drop && rm -f $(PROG) && touch .ciprep )

# Detect certain configuration parameters according to environment
DATABASE_HOST = $(shell test "$$CI" = "true" && echo localhost || echo smartmet-test)
DATABASE_PORT = $(shell test "$$CI" = "true" && echo 5432 || echo 5444)
TESTDATADIR =  $(shell test -d /usr/share/smartmet/test/data && echo "/usr/share/smartmet/test/data" || echo "/smartmet/share")

cnf/geonames.conf: cnf/geonames.tmpl.conf
	sed -e "s/DATABASE_PORT/$(DATABASE_PORT)/" \
		-e "s/DATABASE_HOST/$(DATABASE_HOST)/" \
		-e "s%TESTDATADIR%$(TESTDATADIR)%" < cnf/geonames.tmpl.conf > tmp
		mv tmp cnf/geonames.conf


test: .ciprep cnf/geonames.conf $(PROG)
	@echo Running tests:
	@for prog in $(PROG); do \
	./$$prog; \
	done

$(PROG) : % : %.cpp ../geonames.so
	$(CC) $(CFLAGS) -o $@ $@.cpp $(INCLUDES) $(LIBS)
