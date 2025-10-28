#include "Engine.h"
#include <locus/Query.h>
#include <macgyver/StringConversion.h>
#include <regression/tframe.h>
#include <spine/Location.h>
#include <spine/Options.h>
#include <spine/Reactor.h>
#include <iterator>
#include <libconfig.h++>
#include <unistd.h>

using namespace std;

SmartMet::Spine::Reactor *reactor;
std::shared_ptr<SmartMet::Engine::Geonames::Engine> names;

auto accept_all = [](const SmartMet::Spine::LocationPtr &loc) { return false; };

void print(const SmartMet::Spine::LocationPtr &ptr)
{
  if (!ptr)
    cout << "No location to print" << endl;
  else
  {
    cout << "Geoid:\t" << ptr->geoid << endl
         << "Name:\t" << ptr->name << endl
         << "Feature:\t" << ptr->feature << endl
         << "ISO2:\t" << ptr->iso2 << endl
         << "Area:\t" << ptr->area << endl
         << "Country:\t" << ptr->country << endl
         << "Lon:\t" << ptr->longitude << endl
         << "Lat:\t" << ptr->latitude << endl
         << "TZ:\t" << ptr->timezone << endl
         << "Popu:\t" << ptr->population << endl
         << "Elev:\t" << ptr->elevation << endl
         << "DEM:\t" << ptr->dem << endl
         << "Priority:\t" << ptr->priority << endl;
  }
}

void print(const SmartMet::Spine::LocationList &ptrs)
{
  for (const SmartMet::Spine::LocationPtr &ptr : ptrs)
  {
    print(ptr);
    cout << '\n';
  }
}

namespace Tests
{
// ----------------------------------------------------------------------

void countryName()
{
  std::string name = names->countryName("FI", "fi");
  if (name != "Suomi")
    TEST_FAILED("Failed to resolve country name for FI (fi) is Suomi");

  name = names->countryName("FI", "en");
  if (name != "Finland")
    TEST_FAILED("Failed to resolve country name for FI (en) is Finland");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void nearest()
{
  using SmartMet::Spine::LocationPtr;

  LocationPtr ptr;

  ptr = names->keywordSearch(28.76, 61.17);
  if (!ptr)
    TEST_FAILED("Found no near place for coord 28.76,61.17");
  if (ptr->name != "Imatrankoski")
    TEST_FAILED("Should find Imatrankoski, not " + ptr->name);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void nearestplaces()
{
  SmartMet::Spine::LocationList ptrs;

  Locus::QueryOptions opts;
  opts.SetCountries("fi");
  opts.SetResultLimit(300);
  opts.SetFeatures("PPLX");  // polulated places
  opts.SetLanguage("fi");

  try
  {
    // from Helsinki onwards:
    //		ptrs = names->lonlatSearch(opts,25,60,50);
    ptrs = names->lonlatSearch(opts, 24.96, 60.17, 10);
  }
  catch (const libconfig::SettingException &e)
  {
    TEST_FAILED(string("Setting not found: ") + e.getPath());
  }

#if 0
		// Kruununhaka, Katajanokka, Kluuvi, ...
		int pos(0);
		for(SmartMet::Spine::LocationPtr & ptr : ptrs)
		  cout << ++pos << ": " << ptr->name << " -> " << ptr->longitude << ", " << ptr->latitude << ", " << ptr->feature << endl;
#endif

  if (ptrs.size() < 91)
    TEST_FAILED("Should find at least 91 places (PPLX) within 50km of Helsinki, not " +
                Fmi::to_string(ptrs.size()));

  SmartMet::Spine::LocationList::iterator it = ptrs.begin();

  ++it;  // skip Kruununhaka

  if ((*it)->name != "Katajanokka")
    TEST_FAILED(
        "Katajanokka should be second name in the list of populated "
        "places in southern Finland, "
        "not " +
        (*it)->name);

  ++it;
  ++it;  // skip Kuuvi
  if ((*it)->name != "Kaartinkaupunki")
    TEST_FAILED(
        "Should find Kaartinkaupunki 5rd name in the list of populated "
        "places in southern Finland, "
        "not " +
        (*it)->name);

  ++it;
  ++it;  // skip Merihaka
  if ((*it)->name != "Siltasaari")
    TEST_FAILED(
        "Should find Siltasaari 7th name in the list of populated "
        "places in southern Finland, "
        "not " +
        (*it)->name);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void suggest()
{
  // Wait for autocomplete data to be loaded
  // Technically this is not needed as long as we run the "nearest" test first,
  // since internally the engine will then wait for autocomplete to be ready.

  while (!names->isSuggestReady())
  {
    boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
  }

  SmartMet::Spine::LocationList ptrs;

  // Match he

  ptrs = names->suggest("he", accept_all);

  if (ptrs.size() != 15)
    TEST_FAILED("Should find 15 places starting with 'he', not " + Fmi::to_string(ptrs.size()));

  if (ptrs.front()->name != "Helsinki")
    TEST_FAILED("First match for 'he' should be Helsinki, not " + ptrs.front()->name);
  if (ptrs.front()->area != "")
    TEST_FAILED("Helsinki area should be '', not " + ptrs.front()->area);
  if (ptrs.front()->country != "Suomi")
    TEST_FAILED("Helsinki country should be 'Finland'");

  ptrs.pop_front();
  if (ptrs.front()->name != "Heinola")
    TEST_FAILED("Second match for 'he' should be Heinola, not " + ptrs.front()->name);

  // Match hAm

  ptrs = names->suggest("hAm", accept_all);
  if (ptrs.size() != 15)
    TEST_FAILED("Should find 15 places starting with 'hAm', not " + Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Hamina")
    TEST_FAILED("First match for 'hAm' should be Hamina, not " + ptrs.front()->name);

  // Match Äänekoski

  ptrs = names->suggest("Äänekoski", accept_all);
  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 2 places starting with 'Äänekoski', not " +
                Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Äänekoski")
    TEST_FAILED("First match for 'Äänekoski' should be Äänekoski, not " + ptrs.front()->name);

  ptrs = names->suggest("Ääne", accept_all);
  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 2 places starting with 'Ääne', not " +
                Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Äänekoski")
    TEST_FAILED("First match for 'Ääne' should be Äänekoski, not " + ptrs.front()->name);

  // Match helsinki in swedish

  ptrs = names->suggest("helsinki", accept_all, "sv");

  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 2 places starting with 'helsinki', not " +
                Fmi::to_string(ptrs.size()));

  // Match Åbo in Swedish

  ptrs = names->suggest("Åb", accept_all, "sv");

  if (ptrs.size() < 7)
    TEST_FAILED("Should find at least 7 places starting with 'Åbo', not " +
                Fmi::to_string(ptrs.size()));

  if (ptrs.front()->name != "Åbo")
    TEST_FAILED("Should find Åbo with lang=sv for 'Åbo'");

  // Match Helsingfors in Swedish

  ptrs = names->suggest("helsi", accept_all, "sv");

  if (ptrs.size() != 15)
    TEST_FAILED("Should find 15 place starting with 'helsi' in lang=sv, not " +
                Fmi::to_string(ptrs.size()));

  if (ptrs.front()->name != "Helsingfors")
    TEST_FAILED("Should find Helsingfors with lang=sv for 'helsi");

  // Test paging

  ptrs = names->suggest("h", accept_all, "fi", "ajax_fi_all", 0, 5);
  if (ptrs.size() != 5)
    TEST_FAILED("Should find 5 places starting with 'h' in first page, not " +
                Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Helsinki")
    TEST_FAILED("Should find Helsinki in page 1 at pos 1");

  ptrs = names->suggest("h", accept_all, "fi", "ajax_fi_all", 1, 5);
  if (ptrs.size() != 5)
    TEST_FAILED("Should find 5 places starting with 'h' in 2nd page, not " +
                Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name[0] != 'H')
    TEST_FAILED("Should find H... in page 2 at pos 1");

  ptrs = names->suggest("h", accept_all, "fi", "ajax_fi_all", 2, 5);
  if (ptrs.size() != 5)
    TEST_FAILED("Should find 5 places starting with 'h' in 3rd page, not " +
                Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name[0] != 'H')
    TEST_FAILED("Should find H... in page 3 at pos 1, not " + ptrs.front()->name);

  // Ii, Iisalmi, Iitti

  ptrs = names->suggest("ii", accept_all);
  if (ptrs.size() < 15)
    TEST_FAILED("Should find at least 15 places starting with 'ii'");
  if (ptrs.front()->name != "Ii")
    TEST_FAILED("Should find exact match 'Ii' in 1st place, not '" + ptrs.front()->name + "'");
  ptrs.pop_front();
  if (ptrs.front()->name != "Iisalmi")
    TEST_FAILED("Should find Iisalmi for 'Ii' in 2nd place, not '" + ptrs.front()->name + "'");

  // Vaasa is the preferred name over Nikolainkaupunki

  ptrs = names->suggest("vaasa", accept_all);
  if (ptrs.size() < 1)
    TEST_FAILED("Should find at least 1 place starting with 'vaasa'");
  if (ptrs.front()->name != "Vaasa")
    TEST_FAILED("Should find exact match 'Vaasa' in 1st place, not '" + ptrs.front()->name + "'");

  // Test words within location names

  ptrs = names->suggest("York", accept_all);
  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 1 York and New York");
  if (ptrs.front()->name != "York")
    TEST_FAILED("First match should be York exactly, not '" + ptrs.front()->name + "'");
  ptrs.pop_front();
  if (ptrs.front()->name != "New York")
    TEST_FAILED("Second match should be New York, not '" + ptrs.front()->name + "'");

  ptrs = names->suggest("Orlea", accept_all);
  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 2 *Orlea* matches");
  if (ptrs.front()->name != "New Orleans")
    TEST_FAILED("First match should be New Orleans, not '" + ptrs.front()->name + "'");
  ptrs.pop_front();
  if (ptrs.front()->name != "Orléans")
    TEST_FAILED("Second match should be Orléans, not '" + ptrs.front()->name + "'");

  // Test comma

  ptrs = names->suggest("Kumpula,Helsinki", accept_all);
  if (ptrs.size() < 1)
    TEST_FAILED("Should find Kumpula,Helsinki");
  if (ptrs.front()->area != "Helsinki")
    TEST_FAILED("Kumpula,Helsinki should be in Helsinki, not " + ptrs.front()->area);

  ptrs = names->suggest("Kumpula, Helsinki", accept_all);
  if (ptrs.size() < 1)
    TEST_FAILED("Should find Kumpula, Helsinki with a space after comma");
  if (ptrs.front()->area != "Helsinki")
    TEST_FAILED("Kumpula, Helsinki should be in Helsinki, not " + ptrs.front()->area);

  // Test fmisid

  ptrs = names->suggest("100539", accept_all, "fmisid");
  if (ptrs.size() < 1)
    TEST_FAILED("Should find Kemi Ajos");
  if (ptrs.front()->geoid != -100539)
    TEST_FAILED("GeoId of Kemi Ajos mareograph should be -100539, not " +
                std::to_string(ptrs.front()->geoid));

  ptrs = names->suggest("100540", accept_all, "fmisid");
  if (ptrs.size() < 1)
    TEST_FAILED("Should find Raahe Lapaluoto");
  if (ptrs.front()->geoid != -100540)
    TEST_FAILED("GeoId of Raahe Lapaluoto mareograph should be -100540, not " +
                std::to_string(ptrs.front()->geoid));

  // Test special political entities

  ptrs = names->suggest("the", accept_all);
  if (ptrs.size() < 1)
    TEST_FAILED("Should find 'The Settlement' and 'The Valley' for 'the'");

  if (ptrs.front()->name != "The Valley")
    TEST_FAILED("Name of the first match for 'The' should be 'The Valley', not " +
                ptrs.front()->name);

  if (ptrs.front()->iso2 != "AI")
    TEST_FAILED("Iso2-code of the first match for 'The' should be 'AI', not " + ptrs.front()->iso2);

  if (ptrs.front()->area != "Anguilla")
    TEST_FAILED("Country of the first match for 'The' should be 'Anguilla', not " +
                ptrs.front()->area);

  // nameSearch and suggest should get similar results

  ptrs = names->suggest("noumea", accept_all);
  if (ptrs.size() < 1)
    TEST_FAILED("Failed to find 'Nouméa' with pattern 'noumea'");
  if (ptrs.front()->name != "Nouméa")
    TEST_FAILED("Name of the first match for 'noumea' should be 'Nouméa', not " +
                ptrs.front()->name);

  ptrs = names->suggest("liege", accept_all);
  if (ptrs.size() < 1)
    TEST_FAILED("Failed to find 'Liege' with pattern 'liege'");
  if (ptrs.front()->name != "Liege")
    TEST_FAILED("Name of the first match for 'liege' should be 'Liege', not " + ptrs.front()->name);

  ptrs = names->suggest("montreal", accept_all);
  if (ptrs.size() < 1)
    TEST_FAILED("Failed to find 'Montreal' with pattern 'Montreal'");
  if (ptrs.front()->name != "Montreal")
    TEST_FAILED("Name of the first match for 'montreal' should be 'Montreal', not " +
                ptrs.front()->name);
  if (ptrs.front()->area != "Kanada")
    TEST_FAILED("Area of first match for 'montreal' should be 'Kanada', not " + ptrs.front()->area);

  ptrs = names->suggest("pristina", accept_all);
  if (ptrs.size() < 1)
    TEST_FAILED("Failed to find 'Pristina' with pattern 'Pristina'");
  if (ptrs.front()->name != "Pristina")
    TEST_FAILED("Name of the first match for 'pristina' should be 'Pristina', not " +
                ptrs.front()->name);

  ptrs = names->suggest("malakka", accept_all);
  if (ptrs.size() < 1)
    TEST_FAILED("Failed to find 'Malakka' with pattern 'Malakka'");
  if (ptrs.front()->name != "Malakka")
    TEST_FAILED("Name of the first match for 'malakka' should be 'Malakka', not " +
                ptrs.front()->name);

  // Test English country names

  ptrs = names->suggest("oslo", accept_all, "en");
  if (ptrs.size() < 1)
    TEST_FAILED("Failed to find 'Oslo' with lang=en");
  if (ptrs.front()->country != "Norway")
    TEST_FAILED("Country of first match for 'oslo' should be 'Norway', not " +
                ptrs.front()->country);

  ptrs = names->suggest("stockholm", accept_all, "en");
  if (ptrs.size() < 1)
    TEST_FAILED("Failed to find 'Stockholm' with lang=en");
  if (ptrs.front()->country != "Sweden")
    TEST_FAILED("Country of first match for 'stockholm' should be 'Sweden', not " +
                ptrs.front()->country);

  // BRAINSTORM-2113: SIGABRT (must handle case when non UTF-8 text entered)
  ptrs = names->suggest("\344\344", accept_all, "fi");  // ää in latin1
  if (ptrs.size() < 1)
    TEST_FAILED("Failed to find suggestions by provided 'ää' in Latin1 encoding");
  if (ptrs.front()->name != "Äänekoski")
    TEST_FAILED(
        "Name of the first match for 'ää' in Latin1 encoding should be"
        " 'Äänekoski ', not '" +
        ptrs.front()->name + "'");

  // BRAINSTORM-2113: SIGABRT (must handle case when empty string or whitespace only entered)
  ptrs = names->suggest("", accept_all, "fi");
  if (ptrs.size() != 0)
    TEST_FAILED("No suggestions expected when empty string or whitespace only provided. Got " +
                std::to_string(int(ptrs.size())) + " suggestions");

  TEST_PASSED();
}

void suggest_duplicates()
{
  // Wait for autocomplete data to be loaded
  // Technically this is not needed as long as we run the "nearest" test first,
  // since internally the engine will then wait for autocomplete to be ready.

  while (!names->isSuggestReady())
  {
    boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
  }

  SmartMet::Spine::LocationList ptrs;

  // Match he

  ptrs = names->suggestDuplicates("he", accept_all);

  if (ptrs.size() != 15)
    TEST_FAILED("Should find 15 places starting with 'he', not " + Fmi::to_string(ptrs.size()));

  if (ptrs.front()->name != "Helsinki")
    TEST_FAILED("First match for 'he' should be Helsinki PPLC, not " + ptrs.front()->name);
  if (ptrs.front()->area != "")
    TEST_FAILED("Helsinki area should be '', not " + ptrs.front()->area);
  if (ptrs.front()->country != "Suomi")
    TEST_FAILED("Helsinki country should be 'Finland'");

  ptrs.pop_front();
  if (ptrs.front()->name != "Helsinki")
    TEST_FAILED("Second match for 'he' should be Helsinki ADM3, not " + ptrs.front()->name);

  // Match hAm

  ptrs = names->suggestDuplicates("hAm", accept_all);
  if (ptrs.size() != 15)
    TEST_FAILED("Should find 15 places starting with 'hAm', not " + Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Hamina")
    TEST_FAILED("First match for 'hAm' should be Hamina, not " + ptrs.front()->name);

  // Match Äänekoski

  ptrs = names->suggestDuplicates("Äänekoski", accept_all);
  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 2 places starting with 'Äänekoski', not " +
                Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Äänekoski")
    TEST_FAILED("First match for 'Äänekoski' should be Äänekoski, not " + ptrs.front()->name);

  ptrs = names->suggestDuplicates("Ääne", accept_all);
  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 2 places starting with 'Ääne', not " +
                Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Äänekoski")
    TEST_FAILED("First match for 'Ääne' should be Äänekoski, not " + ptrs.front()->name);

  // Match helsinki in swedish

  ptrs = names->suggestDuplicates("helsinki", accept_all, "sv");

  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 2 places starting with 'helsinki', not " +
                Fmi::to_string(ptrs.size()));

  // Match Åbo in Swedish

  ptrs = names->suggestDuplicates("Åb", accept_all, "sv");

  if (ptrs.size() < 7)
    TEST_FAILED("Should find at least 7 places starting with 'Åbo', not " +
                Fmi::to_string(ptrs.size()));

  if (ptrs.front()->name != "Åbo")
    TEST_FAILED("Should find Åbo with lang=sv for 'Åbo'");

  // Match Helsingfors in Swedish

  ptrs = names->suggestDuplicates("helsi", accept_all, "sv");

  if (ptrs.size() != 15)
    TEST_FAILED("Should find 15 place starting with 'helsi' in lang=sv, not " +
                Fmi::to_string(ptrs.size()));

  if (ptrs.front()->name != "Helsingfors")
    TEST_FAILED("Should find Helsingfors with lang=sv for 'helsi");

  // Test words within location names

  ptrs = names->suggestDuplicates("York", accept_all);
  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 1 York and New York");
  if (ptrs.front()->name != "York")
    TEST_FAILED("First match should be York exactly, not '" + ptrs.front()->name + "'");
  ptrs.pop_front();
  if (ptrs.front()->name != "New York")
    TEST_FAILED("Second match should be New York, not '" + ptrs.front()->name + "'");

  ptrs = names->suggestDuplicates("Orlea", accept_all);
  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 2 *Orlea* matches");
  if (ptrs.front()->name != "New Orleans")
    TEST_FAILED("First match should be New Orleans, not '" + ptrs.front()->name + "'");
  ptrs.pop_front();
  if (ptrs.front()->name != "Orléans")
    TEST_FAILED("Second match should be Orléans, not '" + ptrs.front()->name + "'");

  // Kaisaniemi station

  ptrs = names->suggestDuplicates("Kaisaniemi", accept_all, "fi", "all");
  if (ptrs.size() < 2)
    TEST_FAILED("Should find at least 2 matches for Kaisaniemi with keyword 'all'");
  if (ptrs.front()->feature != "PPL")
    TEST_FAILED("First match should PPL, not '" + ptrs.front()->feature + "'");
  ptrs.pop_front();
  if (ptrs.front()->feature != "SYNOP")
    TEST_FAILED("Second match should be SYNOP, not '" + ptrs.front()->feature + "'");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void suggest_languages()
{
  // Wait for autocomplete data to be loaded
  // Technically this is not needed as long as we run the "nearest" test first,
  // since internally the engine will then wait for autocomplete to be ready.

  while (!names->isSuggestReady())
  {
    boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
  }

  std::vector<std::string> languages{"fi", "sv", "en"};

  // Match he

  auto ptrs = names->suggest("he", accept_all, languages);

  if (ptrs.size() != 3)
    TEST_FAILED("Should find 3 translation lists with 'he'");

  if (ptrs[0].size() != 15)
    TEST_FAILED("Should find 15 places starting with 'he', not " + Fmi::to_string(ptrs[0].size()));

  if (ptrs[0].front()->name != "Helsinki")
    TEST_FAILED("First match for 'he' should be Helsinki, not " + ptrs[0].front()->name);
  if (ptrs[0].front()->area != "")
    TEST_FAILED("Helsinki area in 'fi' should be '', not " + ptrs[0].front()->area);
  if (ptrs[0].front()->country != "Suomi")
    TEST_FAILED("Helsinki country should be 'Suomi'");

  if (ptrs[1].front()->name != "Helsingfors")
    TEST_FAILED("First match for 'he' should be Helsingfors, not " + ptrs[1].front()->name);
  if (ptrs[1].front()->area != "")
    TEST_FAILED("Helsinki area in 'sv' should be '', not " + ptrs[1].front()->area);
  if (ptrs[1].front()->country != "Finland")
    TEST_FAILED("Helsinki country should be 'Finland'");

  if (ptrs[2].front()->name != "Helsinki")
    TEST_FAILED("First match for 'he' should be Helsinki, not " + ptrs[2].front()->name);
  if (ptrs[2].front()->area != "")
    TEST_FAILED("Helsinki area in 'en' should be '', not " + ptrs[2].front()->area);
  if (ptrs[2].front()->country != "Finland")
    TEST_FAILED("Helsinki country should be 'Finland'");

  // Match Åbo

  ptrs = names->suggest("Åb", accept_all, languages);

  if (ptrs.size() != 3)
    TEST_FAILED("Should find 3 translation lists with 'Åb'");

  if (ptrs[0].size() < 7)
    TEST_FAILED("Should find at least 7 places starting with 'Åbo', not " +
                Fmi::to_string(ptrs.size()));

  if (ptrs[0].front()->name != "Turku")
    TEST_FAILED("Should find Turku with lang=fi for 'Åbo'");

  if (ptrs[1].front()->name != "Åbo")
    TEST_FAILED("Should find Åbo with lang=sv for 'Åbo'");

  if (ptrs[0].front()->name != "Turku")
    TEST_FAILED("Should find Turku with lang=en for 'Åbo'");

  TEST_PASSED();
}

void nameIdSearch()
{
  SmartMet::Spine::LocationList ptrs;

  Locus::QueryOptions opts;
  opts.SetCountries("all");
  opts.SetFeatures("SYNOP,FINAVIA,STUK");
  opts.SetSearchVariants(true);
  opts.SetResultLimit(1);
  SmartMet::Spine::LocationList ll;

  // FMISID
  opts.SetNameType("fmisid");
  opts.SetLanguage("fi");
  std::string id = "101004";
  ll = names->nameSearch(opts, id);

  if (ll.size() > 0)
    if (ll.front()->name != "Kumpula")
      TEST_FAILED("Name of FMISID 101004 should be Kumpula, not " + ll.front()->name);

  opts.SetLanguage("sv");
  ll = names->nameSearch(opts, id);

  if (ll.size() > 0)
    if (ll.front()->name != "Gumtäkt")
      TEST_FAILED("Name of FMISID 101004 should be Gumtäkt, not " + ll.front()->name);

  // WMO
  opts.SetNameType("wmo");
  opts.SetLanguage("fi");
  id = "2998";
  ll = names->nameSearch(opts, id);

  if (ll.size() > 0)
    if (ll.front()->name != "Kumpula")
      TEST_FAILED("Name of WMO 2998 should be Kumpula, not " + ll.front()->name);

  opts.SetLanguage("sv");
  ll = names->nameSearch(opts, id);

  if (ll.size() > 0)
    if (ll.front()->name != "Gumtäkt")
      TEST_FAILED("Name of WMO 2998 should be Gumtäkt, not " + ll.front()->name);

  // LPNN
  opts.SetNameType("lpnn");
  opts.SetLanguage("fi");
  id = "339";
  ll = names->nameSearch(opts, id);

  if (ll.size() > 0)
    if (ll.front()->name != "Kumpula")
      TEST_FAILED("Name of LPNN 339 should be Kumpula, not " + ll.front()->name);

  opts.SetLanguage("sv");
  ll = names->nameSearch(opts, id);

  if (ll.size() > 0)
    if (ll.front()->name != "Gumtäkt")
      TEST_FAILED("Name of LPNN 339 should be Gumtäkt, not " + ll.front()->name);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void nameSearch()
{
  SmartMet::Spine::LocationList ptrs;

  Locus::QueryOptions opts;
  opts.SetCountries("all");
  opts.SetSearchVariants(true);
  opts.SetLanguage("fi");

  // Helsinki

  try
  {
    ptrs = names->nameSearch(opts, "Helsinki");
  }
  catch (const libconfig::SettingException &e)
  {
    TEST_FAILED(string("Setting not found: ") + e.getPath());
  }

  if (ptrs.size() < 1)
    TEST_FAILED("Should find at least 1 place when searching for Helsinki, not " +
                Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Helsinki")
    TEST_FAILED("Did not find Helsinki but " + ptrs.front()->name);

  // Rome

  ptrs = names->nameSearch(opts, "Rome");

  if (ptrs.size() != 1)
    TEST_FAILED("Should find 1 Rome, found " + Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Rooma")
    TEST_FAILED("First match for Rome should be Rooma, not " + ptrs.front()->name);

  // Kumpula

  ptrs = names->nameSearch(opts, "Kumpula");

  if (ptrs.size() < 8)
    TEST_FAILED(
        "Should find at least 8 places when searching for Kumpula, "
        "found only " +
        Fmi::to_string(ptrs.size()));
  if (ptrs.front()->area != "Helsinki")
    TEST_FAILED("First match for Kumpula should be in Helsinki, not " + ptrs.front()->area);

  // Tallinna

  ptrs = names->nameSearch(opts, "Tallinna");
  if (ptrs.size() < 1)
    TEST_FAILED("Should find at least 1 place when searching for Tallinna, found none");
  if (ptrs.front()->name != "Tallinna")
    TEST_FAILED("First match for Tallinna should be in Tallinna, not " + ptrs.front()->name);

  // Kumpula in English

  opts.SetLanguage("en");

  ptrs = names->nameSearch(opts, "Kumpula");

  if (ptrs.size() < 8)
    TEST_FAILED(
        "Should find at least 8 places when searching for Kumpula in "
        "English, found only " +
        Fmi::to_string(ptrs.size()));

  ptrs = names->nameSearch(opts, "Kumpula,Helsinki");

  if (ptrs.size() != 1)
    TEST_FAILED(
        "Should find 1 places when searching for Kumpula,Helsinki in "
        "English, found " +
        Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Kumpula")
    TEST_FAILED(
        "First match for Kumpula,Helsinki in English should be in "
        "Helsinki, not " +
        ptrs.front()->area);

  // Should find Alanya PPLA2, not ADM2

  ptrs = names->nameSearch(opts, "Alanya");
  if (ptrs.size() < 1)
    TEST_FAILED("Found no matches for Analya");
  if (ptrs.front()->feature != "PPLA2")
    TEST_FAILED("Should get Alanya PPLA2 as the most important match");

  // Find one Sepänkylä

  opts.SetResultLimit(1);  // Setting the limit must not break nameSearch

  ptrs = names->nameSearch(opts, "Sepänkylä,Espoo");
  if (ptrs.size() != 1)
    TEST_FAILED(
        "Should find 1 places when searching for Sepänkylä,Espoo in "
        "English, found " +
        Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Sepänkylä")
    TEST_FAILED("First match for Sepänkylä,Espoo in English should be in Espoo, not " +
                ptrs.front()->area);

  SmartMet::Spine::LocationPtr loc;
  loc = names->nameSearch("Sepänkylä,Espoo", "eng");

  if (loc == 0)
    TEST_FAILED(
        "Should find 1 places when searching for Sepänkylä,Espoo in "
        "English, found " +
        Fmi::to_string(ptrs.size()));
  if (loc.get()->area != "Espoo")
    TEST_FAILED("First match for Sepänkylä,Espoo in English should be in Espoo, not " +
                ptrs.front()->area);

  opts.SetLanguage("fi");

  ptrs = names->nameSearch(opts, "Noumea");
  if (ptrs.empty())
    TEST_FAILED("Failed to find Noumea");
  if (ptrs.front()->name != "Nouméa")
    TEST_FAILED("First match for Noumea should be Nouméa, not " + ptrs.front()->name);

  ptrs = names->nameSearch(opts, "Liege");
  if (ptrs.empty())
    TEST_FAILED("Failed to find Liege");
  if (ptrs.front()->name != "Liege")
    TEST_FAILED("First match for Liege should be Liege, not " + ptrs.front()->name);

  ptrs = names->nameSearch(opts, "Montreal");
  if (ptrs.empty())
    TEST_FAILED("Failed to find Montreal");
  if (ptrs.front()->name != "Montreal")
    TEST_FAILED("First match for Montreal should be Montreal, not " + ptrs.front()->name);
  if (ptrs.front()->area != "Kanada")
    TEST_FAILED("Area of 'Montreal' should be 'Kanada', not " + ptrs.front()->area);

  ptrs = names->nameSearch(opts, "Pristina");
  if (ptrs.empty())
    TEST_FAILED("Failed to find Pristina");
  if (ptrs.front()->name != "Pristina")
    TEST_FAILED("First match for Pristina should be Pristina, not " + ptrs.front()->name);

  ptrs = names->nameSearch(opts, "Malakka");
  if (ptrs.empty())
    TEST_FAILED("Failed to find Malakka");
  if (ptrs.front()->name != "Malakka")
    TEST_FAILED("First match for Malakka should be Malakka, not " + ptrs.front()->name);

  // We should get Kallio, Helsinki as the best match due to its large population

  ptrs = names->nameSearch(opts, "Kallio");
  if (ptrs.empty())
    TEST_FAILED("Failed to find Kallio");
  if (ptrs.front()->area != "Helsinki")
    TEST_FAILED("First match for Kallio should be in Helsinki, not in " + ptrs.front()->area);

  opts.SetLanguage("sv");
  ptrs = names->nameSearch(opts, "Åbo,Åbo");
  if (ptrs.empty())
    TEST_FAILED("Failed to find Åbo,Åbo");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void idSearch()
{
  SmartMet::Spine::LocationList ptrs;

  Locus::QueryOptions opts;
  opts.SetCountries("all");
  opts.SetSearchVariants(true);
  opts.SetLanguage("fi");

  // Helsinki

  try
  {
    ptrs = names->idSearch(opts, 658225);
  }
  catch (const libconfig::SettingException &e)
  {
    TEST_FAILED(string("Setting not found: ") + e.getPath());
  }

  if (ptrs.size() != 1)
    TEST_FAILED("Should find 1 place when searching for Helsinki, not " +
                Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Helsinki")
    TEST_FAILED("Did not find Helsinki but " + ptrs.front()->name);

  // Rome

  ptrs = names->idSearch(opts, 3169070);

  if (ptrs.size() != 1)
    TEST_FAILED("Should find 1 place when searching for Rooma, not " + Fmi::to_string(ptrs.size()));
  if (ptrs.front()->name != "Rooma")
    TEST_FAILED("Did not find Rooma but " + ptrs.front()->name);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void lonlatSearch()
{
  SmartMet::Spine::LocationList ptrs;

  Locus::QueryOptions opts;
  opts.SetCountries("all");
  opts.SetSearchVariants(true);
  opts.SetLanguage("fi");

  // Kumpula, Helsinki

  try
  {
    ptrs = names->lonlatSearch(opts, 24.9642, 60.2089);
  }
  catch (const libconfig::SettingException &e)
  {
    TEST_FAILED(string("Setting not found: ") + e.getPath());
  }

  if (ptrs.size() < 1)
    TEST_FAILED("Should find at least 1 place when searching for 60.2089,24.9642");
  if (ptrs.front()->name != "Kumpula")
    TEST_FAILED("Did not find Kumpula, Helsinki but " + ptrs.front()->name);

  if (ptrs.front()->elevation != 11)
    TEST_FAILED("Elevation for Kumpula should be 11, not " +
                Fmi::to_string(ptrs.front()->elevation));

  if (ptrs.front()->dem != 24)
    TEST_FAILED("DEM for Kumpula should be 24, not " + Fmi::to_string(ptrs.front()->dem));

  // Rooma

  ptrs = names->lonlatSearch(opts, 12.4833, 41.9);

  if (ptrs.size() < 1)
    TEST_FAILED("Should find at least 1 place when searching for 41.9,12.4833");
  if (ptrs.front()->name != "Rooma")
    TEST_FAILED("Did not find Rooma but " + ptrs.front()->name);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void keywordSearch()
{
  SmartMet::Spine::LocationList ptrs;

  Locus::QueryOptions opts;
  opts.SetCountries("all");
  opts.SetSearchVariants(true);
  opts.SetLanguage("fi");

  // ylewww_fi

  try
  {
    ptrs = names->keywordSearch(opts, "mareografit");
  }
  catch (const libconfig::SettingException &e)
  {
    TEST_FAILED(string("Setting not found: ") + e.getPath());
  }

  if (ptrs.size() < 14)
    TEST_FAILED(string("mareografit keyword should have at least 14 locations: found ") +
                Fmi::to_string(ptrs.size()));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void reload()
{
  SmartMet::Spine::LocationList ptrs;

  Locus::QueryOptions opts;
  opts.SetCountries("all");
  opts.SetSearchVariants(true);
  opts.SetLanguage("fi");

  ptrs = names->keywordSearch(opts, "press_europe");

  const std::pair<bool, std::string> result = names->reload();
  if (!result.first)
    TEST_FAILED("Failed to reload geonames data: " + result.second);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void featureSearch()
{
  // Kumpula, Helsinki

  auto ptr = names->featureSearch(24.95, 60.175, "fi", "SYNOP");
  if (ptr->feature != "SYNOP")
    TEST_FAILED("Did not find a SYNOP station");
  if (ptr->name != "KAISANIEMI")
    TEST_FAILED("Did not find KAISANIE I, Helsinki but " + ptr->name);

  ptr = names->featureSearch(24.9642, 60.2089, "fi", "PPL");
  if (ptr->name != "Hermanni")
    TEST_FAILED("Did not find Hermanni PPL, Helsinki but " + ptr->name);
  if (ptr->feature != "PPL")
    TEST_FAILED("Did not find hermanni PPL, Helsinki but " + ptr->name);

  ptr = names->featureSearch(24.9642, 60.2089, "fi", "PPL,PPLX");
  if (ptr->name != "Kumpula")
    TEST_FAILED("Did not find Kumpula, Helsinki but " + ptr->name);
  if (ptr->feature != "PPLX")
    TEST_FAILED("Did not find Kumpula PPLX, Helsinki but " + ptr->name);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void security()
{
  Locus::QueryOptions opts;
  opts.SetCountries("all");
  opts.SetSearchVariants(true);
  opts.SetLanguage("fi");

  // Helsinki

  std::vector<std::string> tests{
      "Helsinki.png", "style.css", "Persepolis.png", "admin.js", "Helsinki"};

  for (const auto &name : tests)
  {
    try
    {
      auto ptrs = names->nameSearch(opts, name);
      if (name != "Helsinki")
        TEST_FAILED("Search should throw for " + name);
    }
    catch (Fmi::Exception &e)
    {
      // std::cerr << "\t" << name << " " << e.what() << std::endl;
    }
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

// The actual test driver
class tests : public tframe::tests
{
  //! Overridden message separator
  virtual const char *error_message_prefix() const { return "\n\t"; }
  //! Main test suite
  void test()
  {
    TEST(nameSearch);
    TEST(nameIdSearch);
    TEST(idSearch);
    TEST(lonlatSearch);
    TEST(nearest);
    TEST(nearestplaces);
    TEST(countryName);
    TEST(featureSearch);

    // Test the next last since they require autocomplete to be initialized
    TEST(keywordSearch);
    TEST(suggest);
    TEST(suggest_duplicates);
    TEST(suggest_languages);

    TEST(security);

    // TEST(reload);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  SmartMet::Spine::Options opts;
  opts.configfile = "cnf/reactor.conf";
  opts.parseConfig();

  // Set output unbuffered - otherwise, output is lost in crash (like segfault)
  cout.setf(ios::unitbuf);
  cerr.setf(ios::unitbuf);

  reactor = new SmartMet::Spine::Reactor(opts);
  reactor->init();
  names = reactor->getEngine<SmartMet::Engine::Geonames::Engine>("Geonames", NULL);

  cout << endl << "Geonames tester" << endl << "================" << endl;
  Tests::tests t;
  int result = t.run();
  names.reset();
  delete reactor;
  return result;
}
