// ======================================================================
/*!
 * \brief Implementation of writing-script checks for alternate place names
 */
// ======================================================================

#include "LanguageScript.h"

#include <macgyver/Exception.h>

#include <algorithm>
#include <map>
#include <unicode/utf8.h>

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
// ----------------------------------------------------------------------
/*!
 * \brief Resolve an ICU script name to a script code
 */
// ----------------------------------------------------------------------

UScriptCode parse_script_name(const std::string &theName)
{
  try
  {
    UErrorCode status = U_ZERO_ERROR;
    UScriptCode code = USCRIPT_INVALID_CODE;

    // Accepts both long and short ICU names, for example "Cyrillic" and "Cyrl".
    const auto count = uscript_getCode(theName.c_str(), &code, 1, &status);

    if (U_FAILURE(status) || count <= 0 || code == USCRIPT_INVALID_CODE)
      throw Fmi::Exception(BCP, "Unknown writing script name '" + theName + "'");

    // Guard against names that carry no usable script, e.g. "Common".
    if (code == USCRIPT_COMMON || code == USCRIPT_INHERITED || code == USCRIPT_UNKNOWN)
      throw Fmi::Exception(BCP, "'" + theName + "' is not a usable writing script name");

    return code;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Test whether a name is written in the expected script
 */
// ----------------------------------------------------------------------

namespace
{
/*!
 * \brief Count the characters of each writing script in a UTF-8 string
 *
 * Code points carrying no script of their own (spaces, digits, hyphens,
 * apostrophes, combining marks) are left out, since they say nothing about how
 * the name is written.
 */

std::map<UScriptCode, int> count_scripts(const std::string &theName)
{
  std::map<UScriptCode, int> counts;

  const auto *bytes = reinterpret_cast<const uint8_t *>(theName.data());
  const auto length = static_cast<int32_t>(theName.size());

  int32_t i = 0;
  while (i < length)
  {
    UChar32 c = 0;
    U8_NEXT(bytes, i, length, c);

    if (c < 0)
      continue;  // invalid UTF-8, nothing to judge

    UErrorCode status = U_ZERO_ERROR;
    const auto script = uscript_getScript(c, &status);

    if (U_FAILURE(status) || script == USCRIPT_COMMON || script == USCRIPT_INHERITED ||
        script == USCRIPT_UNKNOWN || script == USCRIPT_INVALID_CODE)
      continue;

    ++counts[script];
  }

  return counts;
}
}  // namespace

bool name_matches_script(const std::string &theName, UScriptCode theScript)
{
  try
  {
    const auto counts = count_scripts(theName);

    // Nothing scripted to judge by, so accept the name as is.

    if (counts.empty())
      return true;

    const auto expected = counts.find(theScript);

    if (expected == counts.end())
      return false;

    // Accept mixed-script names as long as the expected script is at least as
    // common as any other, so that forms like "Kyiv (Київ)" survive.

    return std::none_of(counts.begin(),
                        counts.end(),
                        [&expected](const auto &count)
                        { return count.second > expected->second; });
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
