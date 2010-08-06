#ifndef LM_FILTER_PHRASE_H__
#define LM_FILTER_PHRASE_H__

#include "util/string_piece.hh"

#include <boost/unordered_map.hpp>

#include <iosfwd>
#include <vector>

#define LM_FILTER_PHRASE_METHOD(caps, lower) \
bool Find##caps(Hash key, const std::vector<unsigned int> *&out) const {\
  Table::const_iterator i(table_.find(key));\
  if (i==table_.end()) return false; \
  out = &i->second.lower; \
  return true; \
}

namespace lm {
namespace phrase {

typedef uint64_t Hash;

namespace detail {

// 64-bit extension of boost's hash_combine with arbitrary number prepended.
inline void CombineHash(Hash &seed, Hash with) {
  seed ^= with + 0x8fd2736e9e3779b9ULL + (seed<<6) + (seed>>2);
}

inline Hash StringHash(const StringPiece &str) {
  Hash ret = 0;
  for (const char *i = str.data(); i != str.data() + str.size(); ++i) {
    char val = *i;
    CombineHash(ret, static_cast<Hash>(val) * 0x478387ef381de5e3ULL);
  }
  return ret;
}

} // namespace detail

class Substrings {
  private:
    /* This is the value in a hash table where the key is a string.  It indicates
     * four sets of sentences:
     * substring is sentences with a phrase containing the key as a substring.  
     * left is sentencess with a phrase that begins with the key (left aligned).
     * right is sentences with a phrase that ends with the key (right aligned).
     * phrase is sentences where the key is a phrase.
     * Each set is encoded as a vector of sentence ids in increasing order.
     */
    struct SentenceRelation {
      std::vector<unsigned int> substring, left, right, phrase;
    };
    /* Most of the CPU is hash table lookups, so let's not complicate it with
     * vector equality comparisons.  If a collision happens, the SentenceRelation
     * structure will contain the union of sentence ids over the colliding strings.
     * In that case, the filter will be slightly more permissive.  
     * The key here is the same as boost's hash of std::vector<std::string>.  
     */
    typedef boost::unordered_map<Hash, SentenceRelation> Table;

  public:
    Substrings() {}

    /* If the string isn't a substring of any phrase, return NULL.  Otherwise,
     * return a pointer to std::vector<unsigned int> listing sentences with
     * matching phrases.  This set may be empty for Left, Right, or Phrase.
     * Example: const std::vector<unsigned int> *FindSubstring(Hash key)
     */
    LM_FILTER_PHRASE_METHOD(Substring, substring)
    LM_FILTER_PHRASE_METHOD(Left, left)
    LM_FILTER_PHRASE_METHOD(Right, right)
    LM_FILTER_PHRASE_METHOD(Phrase, phrase)

    // sentence_id must be non-decreasing.  Iterators are over words in the phrase.  
    template <class Iterator> void AddPhrase(unsigned int sentence_id, const Iterator &begin, const Iterator &end) {
      // Iterate over all substrings.  
      for (Iterator start = begin; start != end; ++start) {
        Hash hash = 0;
        SentenceRelation *relation;
        for (Iterator finish = start; finish != end; ++finish) {
          detail::CombineHash(hash, *finish);
          // Now hash is of [start, finish].
          relation = &table_[hash];
          AppendSentence(relation->substring, sentence_id);
          if (start == begin) AppendSentence(relation->left, sentence_id);
        }
        AppendSentence(relation->right, sentence_id);
        if (start == begin) AppendSentence(relation->phrase, sentence_id);
      }
    }

  private:
    void AppendSentence(std::vector<unsigned int> &vec, unsigned int sentence_id) {
      if (vec.empty() || vec.back() != sentence_id) vec.push_back(sentence_id);
    }

    Table table_;
};

// Read a file with one sentence per line containing tab-delimited phrases of
// space-separated words.  
unsigned int ReadMultiple(std::istream &in, Substrings &out);

namespace detail {
extern const StringPiece kEndSentence;

template <class Iterator> void MakeHashes(Iterator i, const Iterator &end, std::vector<size_t> &hashes) {
  hashes.clear();
  if (i == end) return;
  // TODO: check strict phrase boundaries after <s> and before </s>.  For now, just skip tags.  
  if ((i->data()[0] == '<') && (i->data()[i->size() - 1] == '>')) {
    ++i;
  }
  for (; i != end && (*i != kEndSentence); ++i) {
    hashes.push_back(detail::StringHash(*i));
  }
}

} // namespace detail

class Union {
  public:
    explicit Union(const Substrings &substrings) : substrings_(substrings) {}

    template <class Iterator> bool PassNGram(const Iterator &begin, const Iterator &end) {
      detail::MakeHashes(begin, end, hashes_);
      return hashes_.empty() || Evaluate();
    }

  private:
    bool Evaluate();

    std::vector<Hash> hashes_;

    const Substrings &substrings_;
};

template <class OutputT> class Multiple {
  public:
    typedef OutputT Output;
    
    explicit Multiple(const Substrings &substrings, Output &output) : output_(output), substrings_(substrings) {}

    Output &GetOutput() { return output_; }

    template <class Iterator> void AddNGram(const Iterator &begin, const Iterator &end, const std::string &line) {
      detail::MakeHashes(begin, end, hashes_);
      if (hashes_.empty()) {
        output_.AddNGram(line);
        return;
      }
      Evaluate(line);
    }

  private:
    void Evaluate(const std::string &line);

    Output &output_;

    std::vector<Hash> hashes_;

    const Substrings &substrings_;
};

} // namespace phrase
} // namespace lm
#endif // LM_FILTER_PHRASE_H__