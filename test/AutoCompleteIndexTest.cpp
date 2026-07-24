// ======================================================================
/*!
 * \brief Unit tests for AutoCompleteIndex
 *
 * Self-contained (no database): builds a synthetic dataset that mimics the
 * geonames to_treewords expansion (many keys mapping to the same location,
 * high-byte collation-key-like bytes) and verifies that AutoCompleteIndex
 * returns exactly the same matches as the reference TernarySearchTree for
 * both storage modes.
 */
// ======================================================================

#include "AutoCompleteIndex.h"
#include <macgyver/TernarySearchTree.h>
#include <regression/tframe.h>

#include <algorithm>
#include <list>
#include <memory>
#include <random>
#include <string>
#include <vector>

using SmartMet::Engine::Geonames::AutoCompleteIndex;

namespace
{
struct Loc
{
  std::uint32_t geoid;
  std::int32_t score;
};
using LocPtr = std::shared_ptr<const Loc>;

// A vocabulary of word fragments, some containing bytes > 127 to exercise
// unsigned (memcmp) ordering just like ICU primary collation keys.
std::vector<std::string> make_vocab(std::mt19937& rng)
{
  std::vector<std::string> v = {"new",   "york", "san",   "santa", "port",  "lake",
                                "north", "cape", "mount", "city",  "ville", "helsinki",
                                "espoo", "turku", "oulu", "paris", "berlin"};
  for (int i = 0; i < 40; ++i)
  {
    std::string s;
    int len = 3 + static_cast<int>(rng() % 5);
    for (int j = 0; j < len; ++j)
      s.push_back(static_cast<char>(0x80 + (rng() % 0x60)));
    v.push_back(std::move(s));
  }
  return v;
}

// full joined name + each word-boundary suffix, with a ",<geoid>" specifier
std::vector<std::string> treewords(const std::vector<std::string>& tokens, std::uint32_t geoid)
{
  std::vector<std::string> out;
  std::string spec = "," + std::to_string(geoid);
  for (std::size_t start = 0; start < tokens.size(); ++start)
  {
    std::string joined;
    for (std::size_t k = start; k < tokens.size(); ++k)
      joined += tokens[k];
    out.push_back(joined + spec);
  }
  return out;
}

struct Dataset
{
  std::vector<LocPtr> locs;
  std::vector<std::pair<std::string, LocPtr>> entries;
  std::vector<std::string> prefixes;
};

Dataset make_dataset(std::size_t nloc)
{
  std::mt19937 rng(2024);
  auto vocab = make_vocab(rng);
  std::uniform_int_distribution<int> ntok(1, 3);
  std::uniform_int_distribution<std::size_t> vpick(0, vocab.size() - 1);
  std::uniform_int_distribution<int> scored(0, 1000000);

  Dataset d;
  d.locs.reserve(nloc);
  for (std::size_t i = 0; i < nloc; ++i)
  {
    auto geoid = static_cast<std::uint32_t>(1000000 + i);
    auto loc = std::make_shared<const Loc>(Loc{geoid, scored(rng)});
    d.locs.push_back(loc);

    int t = ntok(rng);
    std::vector<std::string> tokens;
    for (int j = 0; j < t; ++j)
      tokens.push_back(vocab[vpick(rng)]);
    for (auto& key : treewords(tokens, geoid))
      d.entries.emplace_back(std::move(key), loc);
  }

  // varied-length query prefixes drawn from real keys, plus a few misses
  std::uniform_int_distribution<std::size_t> dpick(0, d.entries.size() - 1);
  for (int i = 0; i < 3000; ++i)
  {
    const std::string& k = d.entries[dpick(rng)].first;
    std::size_t len = 1 + (rng() % std::min<std::size_t>(k.size(), 8));
    d.prefixes.push_back(k.substr(0, len));
  }
  d.prefixes.emplace_back("zzz-no-such-key");
  d.prefixes.emplace_back("");  // empty must return nothing
  return d;
}

std::vector<std::uint32_t> sorted_geoids(const std::list<LocPtr>& r)
{
  std::vector<std::uint32_t> g;
  g.reserve(r.size());
  for (const auto& p : r)
    g.push_back(p->geoid);
  std::sort(g.begin(), g.end());
  return g;
}

template <typename Index>
void fill(Index& idx, const Dataset& d)
{
  for (const auto& kv : d.entries)
    idx.insert(kv.first, kv.second);
}

// ----------------------------------------------------------------------

void parity_plain()
{
  auto d = make_dataset(20000);

  Fmi::TernarySearchTree<const Loc> tst;
  fill(tst, d);

  AutoCompleteIndex<const Loc> idx(AutoCompleteIndex<const Loc>::Mode::Plain);
  fill(idx, d);
  idx.freeze();

  for (const auto& p : d.prefixes)
  {
    auto a = sorted_geoids(tst.findprefix(p));
    auto b = sorted_geoids(idx.findprefix(p));
    if (a != b)
      TEST_FAILED("Plain mode result differs from TST for prefix of length " +
                  std::to_string(p.size()) + ": tst=" + std::to_string(a.size()) +
                  " idx=" + std::to_string(b.size()));
  }
  TEST_PASSED();
}

void parity_frontcoded()
{
  auto d = make_dataset(20000);

  Fmi::TernarySearchTree<const Loc> tst;
  fill(tst, d);

  // Try several block sizes to shake out front-coding boundary bugs.
  for (std::uint32_t bs : {1u, 2u, 4u, 16u, 64u})
  {
    AutoCompleteIndex<const Loc> idx(AutoCompleteIndex<const Loc>::Mode::FrontCoded, bs);
    fill(idx, d);
    idx.freeze();

    for (const auto& p : d.prefixes)
    {
      auto a = sorted_geoids(tst.findprefix(p));
      auto b = sorted_geoids(idx.findprefix(p));
      if (a != b)
        TEST_FAILED("FrontCoded(block=" + std::to_string(bs) +
                    ") result differs from TST for prefix of length " + std::to_string(p.size()));
    }
  }
  TEST_PASSED();
}

void exact_find_and_contains()
{
  auto d = make_dataset(5000);
  AutoCompleteIndex<const Loc> idx(AutoCompleteIndex<const Loc>::Mode::FrontCoded, 8);
  fill(idx, d);

  for (const auto& kv : d.entries)
  {
    if (!idx.contains(kv.first))
      TEST_FAILED("contains() returned false for an inserted key");
    auto v = idx.find(kv.first);
    if (!v)
      TEST_FAILED("find() returned null for an inserted key");
  }
  if (idx.find("definitely-not-present"))
    TEST_FAILED("find() returned a value for an absent key");
  if (idx.contains(""))
    TEST_FAILED("contains(empty) must be false");
  TEST_PASSED();
}

void visit_matches_findprefix()
{
  auto d = make_dataset(8000);
  AutoCompleteIndex<const Loc> idx(AutoCompleteIndex<const Loc>::Mode::FrontCoded, 16);
  fill(idx, d);
  idx.freeze();

  for (const auto& p : d.prefixes)
  {
    std::list<LocPtr> viaVisit;
    idx.visitprefix(p, [&](const LocPtr& v) { viaVisit.push_back(v); });
    if (sorted_geoids(viaVisit) != sorted_geoids(idx.findprefix(p)))
      TEST_FAILED("visitprefix disagrees with findprefix");
  }
  TEST_PASSED();
}

void empty_index()
{
  AutoCompleteIndex<const Loc> idx;
  if (!idx.empty())
    TEST_FAILED("fresh index should be empty");
  if (idx.size() != 0)
    TEST_FAILED("fresh index size should be 0");
  if (!idx.findprefix("anything").empty())
    TEST_FAILED("empty index must return no matches");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }

  void test()
  {
    TEST(empty_index);
    TEST(exact_find_and_contains);
    TEST(parity_plain);
    TEST(parity_frontcoded);
    TEST(visit_matches_findprefix);
  }
};

}  // namespace

int main()
{
  std::cout << std::endl << "AutoCompleteIndex tester" << std::endl << "========================" << std::endl;
  tests t;
  return t.run();
}
