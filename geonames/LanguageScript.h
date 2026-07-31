// ======================================================================
/*!
 * \brief Writing-script checks for alternate place names
 *
 * geonames.org stores alternate names under a language code, but nothing
 * guarantees the name is actually written in that language's script. Ukrainian
 * places for instance carry 'uk' names written in Latin ("Lutsk" instead of
 * "Луцьк"), and since the name selection in Impl.cpp breaks ties by length and
 * alphabetical order, a romanized name can easily win over the Cyrillic one.
 *
 * Rejecting such names is only correct for languages written in a single
 * script, so it is configured per language ('language_scripts' in the
 * configuration file) rather than applied globally. Serbian is written in both
 * Cyrillic and Latin, and Japanese and Chinese mix scripts within one name, so
 * those languages must not be configured.
 */
// ======================================================================

#pragma once

#include <string>
#include <unicode/uscript.h>

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
/*!
 * \brief Resolve an ICU script name to a script code
 *
 * Accepts long and short ICU names, for example "Cyrillic" or "Cyrl".
 * Throws for unknown names so that configuration typos are caught at startup.
 */

UScriptCode parse_script_name(const std::string& theName);

/*!
 * \brief Test whether a name is written in the expected script
 *
 * Code points that carry no script of their own (spaces, digits, punctuation,
 * combining marks) are ignored. A name containing no scripted characters at all
 * is accepted, since there is nothing to judge it by.
 *
 * Mixed-script names are accepted as long as the expected script is at least as
 * common as any other, so that forms like "Kyiv (Київ)" survive.
 */

bool name_matches_script(const std::string& theName, UScriptCode theScript);

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
