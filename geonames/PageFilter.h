// ======================================================================
/*!
 * \brief Paging helper for suggest results
 *
 * Declared in its own header so that the paging logic can be unit tested
 * without a database. The implementation is defined in Impl.cpp.
 */
// ======================================================================

#pragma once

#include <spine/Location.h>

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
// Keep only the desired page of suggest results.
//
// The result contains at most 'maxresults' locations starting at offset
// page*maxresults. The function is robust for any values, including
// page/maxresults that far exceed the list size and page==0: it never
// advances an iterator past end() and returns an empty list when the
// requested page starts beyond the available data.
void keep_wanted_page(SmartMet::Spine::LocationList &locs,
                      unsigned int maxresults,
                      unsigned int page);

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet
