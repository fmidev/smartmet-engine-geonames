// ======================================================================
/*!
 * \brief AutoCompleteIndex
 *
 * A static, array-backed replacement for TernarySearchTree specialised for
 * autocomplete / suggest workloads. The keys are expected to be
 * memcmp-comparable byte strings (e.g. ICU primary collation keys), so a
 * prefix query maps to a contiguous range that is located with two binary
 * searches instead of pointer-chasing a ternary tree.
 *
 * Design notes
 * ------------
 * - Build-once / read-many. insert() accumulates entries; the compressed form
 *   is built lazily on the first query (or explicitly via freeze()). After the
 *   engine publishes an immutable Impl via AtomicSharedPtr, the index is only
 *   read, so a static representation is the natural fit.
 *
 * - Many keys may map to the SAME value. "New York" and "York" both point at
 *   the same location; the suffix expansion that produces those keys lives in
 *   the caller (geonames to_treewords), and this container simply stores the
 *   same value under each key. Values are deduplicated by pointer identity into
 *   a single vector, and each key entry stores a 4-byte value id rather than a
 *   16-byte shared_ptr, which also removes atomic refcount traffic from the hot
 *   path. A precomputed score can live in the value payload T itself.
 *
 * - Ordering is NOT preserved for the caller: geonames re-sorts every result by
 *   priority/score, so the tree ordering the TST maintained was never used.
 *
 * Two storage modes:
 *   Plain      - keys packed contiguously, one uint32 offset per key.
 *   FrontCoded - keys front-coded in fixed-size blocks (restart key per block +
 *                LCP-delta encoded remainder). Much smaller for the highly
 *                shared keys autocomplete generates, still O(log N) lookup.
 */
// ======================================================================

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace SmartMet
{
namespace Engine
{
namespace Geonames
{
template <typename T>
class AutoCompleteIndex
{
 public:
  using element_type = std::shared_ptr<T>;
  using result_type = std::list<element_type>;

  enum class Mode
  {
    Plain,
    FrontCoded
  };

  explicit AutoCompleteIndex(Mode mode = Mode::FrontCoded, std::uint32_t block_size = 16)
      : itsMode(mode), itsBlockSize(block_size == 0 ? 1 : block_size)
  {
  }

  bool empty() const
  {
    freeze();
    return itsCount == 0;
  }

  // Number of distinct keys stored
  std::size_t size() const
  {
    freeze();
    return itsCount;
  }

  bool insert(const std::string& key, const T& data)
  {
    return insert(key, std::make_shared<T>(data));
  }

  bool insert(const std::string& key, element_type data)
  {
    if (key.empty())
      return false;
    itsBuild.emplace_back(key, std::move(data));
    itsFrozen = false;
    return true;
  }

  bool contains(const std::string& key) const { return static_cast<bool>(find(key)); }

  element_type find(const std::string& key) const
  {
    freeze();
    if (key.empty() || itsCount == 0)
      return element_type();
    std::size_t pos = lower_bound_index(key);
    if (pos >= itsCount)
      return element_type();
    if (decode_key(pos) != key)
      return element_type();
    return itsValues[itsValueId[pos]];
  }

  // Drop-in replacement for TernarySearchTree::findprefix
  result_type findprefix(const std::string& key) const
  {
    result_type results;
    visitprefix(key, [&](const element_type& v) { results.push_back(v); });
    return results;
  }

  // Zero-allocation hot-path variant: invokes visit(const element_type&) for
  // every match in key order (values may repeat, exactly as the TST returns).
  template <typename Visitor>
  void visitprefix(const std::string& key, Visitor&& visit) const
  {
    freeze();
    if (key.empty() || itsCount == 0)
      return;

    std::size_t lo = lower_bound_index(key);

    std::string upper;
    const bool bounded = next_prefix(key, upper);
    std::size_t hi = bounded ? lower_bound_index(upper) : itsCount;

    for (std::size_t i = lo; i < hi; ++i)
      visit(itsValues[itsValueId[i]]);
  }

  // Approximate bytes owned by the index itself (excludes the T payloads, which
  // are shared with the caller just as in TernarySearchTree).
  std::size_t memory_usage() const
  {
    freeze();
    std::size_t bytes = 0;
    bytes += itsBlob.capacity();
    bytes += itsOffset.capacity() * sizeof(std::uint32_t);
    bytes += itsFC.capacity();
    bytes += itsBlockFCOff.capacity() * sizeof(std::uint32_t);
    bytes += itsValueId.capacity() * sizeof(std::uint32_t);
    bytes += itsValues.capacity() * sizeof(element_type);
    return bytes;
  }

  Mode mode() const { return itsMode; }

  // Build the compressed representation. Thread-safe, idempotent.
  void freeze() const { std::call_once(itsFreezeOnce, [this] { build(); }); }

 private:
  // ---- comparison: unsigned lexicographic (memcmp semantics) ----
  static int cmp(const char* a, std::size_t alen, const char* b, std::size_t blen)
  {
    std::size_t n = alen < blen ? alen : blen;
    int c = n ? std::memcmp(a, b, n) : 0;
    if (c != 0)
      return c;
    if (alen < blen)
      return -1;
    if (alen > blen)
      return 1;
    return 0;
  }

  // Smallest string strictly greater than every string having `key` as a
  // byte-prefix. Returns false if no such bound exists (key is all 0xFF).
  static bool next_prefix(const std::string& key, std::string& out)
  {
    out = key;
    while (!out.empty())
    {
      auto c = static_cast<unsigned char>(out.back());
      if (c != 0xFF)
      {
        out.back() = static_cast<char>(c + 1);
        return true;
      }
      out.pop_back();
    }
    return false;
  }

  // ---- varint (LEB128) ----
  static void put_varint(std::string& s, std::uint32_t v)
  {
    while (v >= 0x80)
    {
      s.push_back(static_cast<char>((v & 0x7F) | 0x80));
      v >>= 7;
    }
    s.push_back(static_cast<char>(v));
  }

  static std::uint32_t get_varint(const char* data, std::size_t& p)
  {
    std::uint32_t v = 0;
    int shift = 0;
    while (true)
    {
      auto b = static_cast<unsigned char>(data[p++]);
      v |= static_cast<std::uint32_t>(b & 0x7F) << shift;
      if ((b & 0x80) == 0)
        break;
      shift += 7;
    }
    return v;
  }

  // ---- key access ----
  std::string decode_key(std::size_t pos) const
  {
    if (itsMode == Mode::Plain)
    {
      std::uint32_t s = itsOffset[pos];
      std::uint32_t e = itsOffset[pos + 1];
      return itsBlob.substr(s, e - s);
    }
    // FrontCoded: decode forward within the block from the restart key
    std::size_t b = pos / itsBlockSize;
    std::size_t first = b * itsBlockSize;
    std::uint32_t rs = itsOffset[b];
    std::uint32_t re = itsOffset[b + 1];
    std::string cur = itsBlob.substr(rs, re - rs);
    std::size_t fp = itsBlockFCOff[b];
    for (std::size_t i = first + 1; i <= pos; ++i)
    {
      std::uint32_t lcp = get_varint(itsFC.data(), fp);
      std::uint32_t slen = get_varint(itsFC.data(), fp);
      cur.resize(lcp);
      cur.append(itsFC, fp, slen);
      fp += slen;
    }
    return cur;
  }

  // First index whose key >= target
  std::size_t lower_bound_index(const std::string& target) const
  {
    if (itsMode == Mode::Plain)
    {
      std::size_t lo = 0, hi = itsCount;
      while (lo < hi)
      {
        std::size_t mid = lo + (hi - lo) / 2;
        std::uint32_t s = itsOffset[mid];
        std::uint32_t e = itsOffset[mid + 1];
        if (cmp(itsBlob.data() + s, e - s, target.data(), target.size()) < 0)
          lo = mid + 1;
        else
          hi = mid;
      }
      return lo;
    }

    // FrontCoded: binary search on restart keys, then scan one block.
    std::size_t numBlocks = itsBlockFCOff.size();
    std::size_t lo = 0, hi = numBlocks;
    while (lo < hi)
    {
      std::size_t mid = lo + (hi - lo) / 2;
      std::uint32_t s = itsOffset[mid];
      std::uint32_t e = itsOffset[mid + 1];
      if (cmp(itsBlob.data() + s, e - s, target.data(), target.size()) < 0)
        lo = mid + 1;
      else
        hi = mid;
    }
    // lo = first block whose restart key >= target; answer is in block lo-1 or at lo's start
    std::size_t startBlock = (lo > 0) ? lo - 1 : 0;
    std::size_t idx = startBlock * itsBlockSize;
    std::uint32_t rs = itsOffset[startBlock];
    std::uint32_t re = itsOffset[startBlock + 1];
    std::string cur = itsBlob.substr(rs, re - rs);
    std::size_t blockEnd = startBlock + 1 < numBlocks ? (startBlock + 1) * itsBlockSize : itsCount;
    std::size_t fp = itsBlockFCOff[startBlock];
    for (std::size_t i = idx; i < blockEnd; ++i)
    {
      if (i > idx)
      {
        std::uint32_t lcp = get_varint(itsFC.data(), fp);
        std::uint32_t slen = get_varint(itsFC.data(), fp);
        cur.resize(lcp);
        cur.append(itsFC, fp, slen);
        fp += slen;
      }
      if (cmp(cur.data(), cur.size(), target.data(), target.size()) >= 0)
        return i;
    }
    return blockEnd;  // == start of next block, which has restart >= target
  }

  // ---- build ----
  void build() const
  {
    if (itsBuild.empty())
    {
      itsCount = 0;
      return;
    }

    // Stable sort by key so that on duplicate keys the first-inserted wins,
    // matching TernarySearchTree::insert which rejects an already-occupied key.
    std::stable_sort(itsBuild.begin(),
                     itsBuild.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    // Deduplicate values by pointer identity.
    std::unordered_map<const T*, std::uint32_t> valueIds;
    valueIds.reserve(itsBuild.size());
    auto value_id = [&](const element_type& v) -> std::uint32_t {
      const T* raw = v.get();
      auto it = valueIds.find(raw);
      if (it != valueIds.end())
        return it->second;
      auto id = static_cast<std::uint32_t>(itsValues.size());
      itsValues.push_back(v);
      valueIds.emplace(raw, id);
      return id;
    };

    itsValueId.reserve(itsBuild.size());

    if (itsMode == Mode::Plain)
    {
      itsOffset.reserve(itsBuild.size() + 1);
      itsOffset.push_back(0);
      const std::string* prev = nullptr;
      for (auto& kv : itsBuild)
      {
        if (prev != nullptr && *prev == kv.first)
          continue;  // drop duplicate key (first wins)
        itsBlob += kv.first;
        itsOffset.push_back(static_cast<std::uint32_t>(itsBlob.size()));
        itsValueId.push_back(value_id(kv.second));
        prev = &kv.first;
      }
      itsCount = itsValueId.size();
    }
    else
    {
      // Deduplicate keys first into a compact ordered list of indices.
      std::vector<std::size_t> keep;
      keep.reserve(itsBuild.size());
      const std::string* prev = nullptr;
      for (std::size_t i = 0; i < itsBuild.size(); ++i)
      {
        if (prev != nullptr && *prev == itsBuild[i].first)
          continue;
        keep.push_back(i);
        prev = &itsBuild[i].first;
      }

      itsCount = keep.size();
      std::size_t numBlocks = (itsCount + itsBlockSize - 1) / itsBlockSize;
      itsOffset.reserve(numBlocks + 1);
      itsBlockFCOff.reserve(numBlocks);
      itsOffset.push_back(0);

      std::string prevkey;
      for (std::size_t i = 0; i < itsCount; ++i)
      {
        const std::string& key = itsBuild[keep[i]].first;
        itsValueId.push_back(value_id(itsBuild[keep[i]].second));

        if (i % itsBlockSize == 0)
        {
          // restart key: stored in full in the blob
          itsBlockFCOff.push_back(static_cast<std::uint32_t>(itsFC.size()));
          itsBlob += key;
          itsOffset.push_back(static_cast<std::uint32_t>(itsBlob.size()));
        }
        else
        {
          // front-code against previous key
          std::size_t lcp = 0;
          std::size_t maxl = std::min(prevkey.size(), key.size());
          while (lcp < maxl && prevkey[lcp] == key[lcp])
            ++lcp;
          put_varint(itsFC, static_cast<std::uint32_t>(lcp));
          put_varint(itsFC, static_cast<std::uint32_t>(key.size() - lcp));
          itsFC.append(key, lcp, key.size() - lcp);
        }
        prevkey = key;
      }
    }

    // Release the build buffer and tighten storage.
    std::vector<std::pair<std::string, element_type>>().swap(itsBuild);
    itsBlob.shrink_to_fit();
    itsFC.shrink_to_fit();
    itsOffset.shrink_to_fit();
    itsBlockFCOff.shrink_to_fit();
    itsValueId.shrink_to_fit();
    itsValues.shrink_to_fit();
    itsFrozen = true;
  }

  Mode itsMode;
  std::uint32_t itsBlockSize;

  // Build-time buffer (cleared after freeze)
  mutable std::vector<std::pair<std::string, element_type>> itsBuild;

  // Frozen representation
  mutable std::string itsBlob;                  // Plain: all keys; FC: restart keys
  mutable std::vector<std::uint32_t> itsOffset;  // Plain: n+1 key offsets; FC: block restart offsets
  mutable std::string itsFC;                     // FC: front-coded remainder stream
  mutable std::vector<std::uint32_t> itsBlockFCOff;  // FC: per-block start offset into itsFC
  mutable std::vector<std::uint32_t> itsValueId;     // per-key value id (key order)
  mutable std::vector<element_type> itsValues;       // deduplicated values

  mutable std::size_t itsCount = 0;
  mutable bool itsFrozen = false;
  mutable std::once_flag itsFreezeOnce;
};

}  // namespace Geonames
}  // namespace Engine
}  // namespace SmartMet
