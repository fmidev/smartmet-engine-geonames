#include "LanguageScript.h"
#include <macgyver/Exception.h>
#include <regression/tframe.h>
#include <string>

using namespace SmartMet::Engine::Geonames;

namespace Tests
{
// ----------------------------------------------------------------------

void parse_script_names()
{
  if (parse_script_name("Cyrillic") != USCRIPT_CYRILLIC)
    TEST_FAILED("Failed to parse 'Cyrillic'");

  // ICU short names must work too
  if (parse_script_name("Cyrl") != USCRIPT_CYRILLIC)
    TEST_FAILED("Failed to parse 'Cyrl'");

  if (parse_script_name("Latin") != USCRIPT_LATIN)
    TEST_FAILED("Failed to parse 'Latin'");

  if (parse_script_name("Greek") != USCRIPT_GREEK)
    TEST_FAILED("Failed to parse 'Greek'");

  // Configuration typos must be caught at startup rather than silently ignored

  try
  {
    parse_script_name("Cyrillick");
    TEST_FAILED("Should throw for an unknown script name");
  }
  catch (const Fmi::Exception &)
  {
  }

  // Scripts that cannot describe a name are useless as settings

  for (const auto *name : {"Common", "Inherited", "Unknown"})
  {
    try
    {
      parse_script_name(name);
      TEST_FAILED(std::string("Should throw for unusable script name '") + name + "'");
    }
    catch (const Fmi::Exception &)
    {
    }
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void match_script()
{
  const auto cyrillic = parse_script_name("Cyrillic");
  const auto latin = parse_script_name("Latin");

  // The Ukrainian cases this setting exists for. The romanized forms are real
  // 'uk' rows in the database and win the length/alphabetical tiebreak.

  if (!name_matches_script("Луцьк", cyrillic))
    TEST_FAILED("Луцьк should match Cyrillic");
  if (name_matches_script("Lutsk", cyrillic))
    TEST_FAILED("Lutsk should not match Cyrillic");

  if (!name_matches_script("Слов'янськ", cyrillic))
    TEST_FAILED("Слов'янськ should match Cyrillic (apostrophe must be ignored)");
  if (!name_matches_script("Слов’янськ", cyrillic))
    TEST_FAILED("Слов’янськ should match Cyrillic (typographic apostrophe)");
  if (name_matches_script("Sloviansk", cyrillic))
    TEST_FAILED("Sloviansk should not match Cyrillic");

  if (name_matches_script("Alchevsk", cyrillic))
    TEST_FAILED("Alchevsk should not match Cyrillic");
  if (!name_matches_script("Алчевськ", cyrillic))
    TEST_FAILED("Алчевськ should match Cyrillic");

  // Hyphens, spaces and digits carry no script and must not decide the outcome

  if (!name_matches_script("Кам'янець-Подільський", cyrillic))
    TEST_FAILED("Hyphenated Cyrillic name should match Cyrillic");
  if (!name_matches_script("Івано-Франківськ", cyrillic))
    TEST_FAILED("Івано-Франківськ should match Cyrillic");

  // Mixed scripts are accepted when the expected script is not outnumbered

  if (!name_matches_script("Kyiv (Київ)", cyrillic))
    TEST_FAILED("Mixed 'Kyiv (Київ)' should match Cyrillic");

  // A name with nothing scripted cannot be judged, so it is accepted

  if (!name_matches_script("123", cyrillic))
    TEST_FAILED("Unscripted name should match any script");
  if (!name_matches_script("", cyrillic))
    TEST_FAILED("Empty name should match any script");

  // The reverse direction must work as well

  if (!name_matches_script("Helsinki", latin))
    TEST_FAILED("Helsinki should match Latin");
  if (name_matches_script("Луцьк", latin))
    TEST_FAILED("Луцьк should not match Latin");

  // Accented Latin is still Latin

  if (!name_matches_script("Åbo", latin))
    TEST_FAILED("Åbo should match Latin");
  if (!name_matches_script("Jyväskylä", latin))
    TEST_FAILED("Jyväskylä should match Latin");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

class tests : public tframe::tests
{
  virtual const char *error_message_prefix() const { return "\n\t"; }

  void test()
  {
    TEST(parse_script_names);
    TEST(match_script);
  }

};  // class tests

}  // namespace Tests

int main()
{
  std::cout << std::endl << "LanguageScript tester" << std::endl << "=====================" << std::endl;
  Tests::tests t;
  return t.run();
}
