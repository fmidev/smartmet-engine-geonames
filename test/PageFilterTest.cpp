// ======================================================================
/*!
 * \brief Unit tests for keep_wanted_page (suggest result paging)
 *
 * Self-contained (no database). Regression coverage for H-22: paging
 * must never advance an iterator past end() and must clamp the requested
 * page/maxresults to the container size, even for values that far exceed
 * the list size and for page==0.
 */
// ======================================================================

#include "PageFilter.h"
#include <regression/tframe.h>

#include <list>
#include <memory>
#include <string>

using SmartMet::Engine::Geonames::keep_wanted_page;
using SmartMet::Spine::Location;
using SmartMet::Spine::LocationList;
using SmartMet::Spine::LocationPtr;

namespace
{
LocationList make_list(std::size_t n)
{
  LocationList locs;
  for (std::size_t i = 0; i < n; ++i)
    locs.push_back(std::make_shared<const Location>(
        static_cast<double>(i), static_cast<double>(i), "loc" + std::to_string(i)));
  return locs;
}

// ----------------------------------------------------------------------

void first_page()
{
  auto locs = make_list(100);
  keep_wanted_page(locs, 10, 0);
  if (locs.size() != 10)
    TEST_FAILED("page 0 of 100 with max 10 should keep 10 results, got " +
                std::to_string(locs.size()));
  if (locs.front()->name != "loc0")
    TEST_FAILED("first result on page 0 should be loc0, got " + locs.front()->name);
  TEST_PASSED();
}

void second_page()
{
  auto locs = make_list(100);
  keep_wanted_page(locs, 10, 1);
  if (locs.size() != 10)
    TEST_FAILED("page 1 of 100 with max 10 should keep 10 results, got " +
                std::to_string(locs.size()));
  if (locs.front()->name != "loc10")
    TEST_FAILED("first result on page 1 should be loc10, got " + locs.front()->name);
  TEST_PASSED();
}

void last_partial_page()
{
  auto locs = make_list(25);
  keep_wanted_page(locs, 10, 2);  // offset 20, only 5 remain
  if (locs.size() != 5)
    TEST_FAILED("last partial page should keep 5 results, got " + std::to_string(locs.size()));
  if (locs.front()->name != "loc20")
    TEST_FAILED("first result on last page should be loc20, got " + locs.front()->name);
  TEST_PASSED();
}

void page_past_end()
{
  auto locs = make_list(50);
  keep_wanted_page(locs, 10, 100);  // offset 1000 >> size
  if (!locs.empty())
    TEST_FAILED("page past the end must yield an empty list, got " +
                std::to_string(locs.size()));
  TEST_PASSED();
}

void huge_maxresults()
{
  // Reproduces the reported attack: /autocomplete?page=1&max=100000
  auto locs = make_list(50);
  keep_wanted_page(locs, 100000, 1);  // first = 100000 > size -> empty
  if (!locs.empty())
    TEST_FAILED("page 1 with huge maxresults must yield an empty list, got " +
                std::to_string(locs.size()));
  TEST_PASSED();
}

void huge_maxresults_page_zero()
{
  auto locs = make_list(50);
  keep_wanted_page(locs, 100000, 0);  // keep everything (only 50 available)
  if (locs.size() != 50)
    TEST_FAILED("page 0 with huge maxresults should keep all 50 results, got " +
                std::to_string(locs.size()));
  TEST_PASSED();
}

void overflow_product()
{
  // page*maxresults overflows 32-bit unsigned; must not wrap and must not crash.
  auto locs = make_list(50);
  keep_wanted_page(locs, 0xFFFFFFFFu, 0xFFFFFFFFu);
  if (!locs.empty())
    TEST_FAILED("overflowing page*maxresults must yield an empty list, got " +
                std::to_string(locs.size()));
  TEST_PASSED();
}

void zero_maxresults()
{
  auto locs = make_list(50);
  keep_wanted_page(locs, 0, 0);  // maxresults==0 is a no-op
  if (locs.size() != 50)
    TEST_FAILED("maxresults==0 should leave the list untouched, got " +
                std::to_string(locs.size()));
  TEST_PASSED();
}

void empty_input()
{
  LocationList locs;
  keep_wanted_page(locs, 10, 5);
  if (!locs.empty())
    TEST_FAILED("empty input must stay empty");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }

  void test()
  {
    TEST(first_page);
    TEST(second_page);
    TEST(last_partial_page);
    TEST(page_past_end);
    TEST(huge_maxresults);
    TEST(huge_maxresults_page_zero);
    TEST(overflow_product);
    TEST(zero_maxresults);
    TEST(empty_input);
  }
};

}  // namespace

int main()
{
  std::cout << std::endl
            << "PageFilter tester" << std::endl
            << "=================" << std::endl;
  tests t;
  return t.run();
}
