/// VFX asset tag query — AND / OR / NOT composition.
///
/// Every taggable VFX asset (VfxSystem, ParticleEmitter, VfxNodeGraph) carries
/// a free-form `std::vector<std::string>` tag list. The asset browser and the
/// snippet library (task 8.8.0) filter these with `VfxAssetTagMatches(query,
/// tags)`. The query grammar is whitespace-tokenised and intentionally small:
///
/// | Construct   | Meaning                                                         |
/// |-------------|-----------------------------------------------------------------|
/// | `fire`      | asset must carry `fire`                                         |
/// | `fire magic`| asset must carry `fire` AND `magic`                             |
/// | `-ambient`  | asset must NOT carry `ambient` (prefix `-` or `!` both work)    |
/// | `a OR b`    | alternative group — either side must match (literal `OR` token) |
/// | `""`        | empty query matches every asset                                 |
///
/// The query is evaluated in disjunctive-normal form: groups separated by the
/// `OR` token are alternatives, and within each group the positive tokens
/// must all be present and the negative tokens must all be absent. Matching
/// is case-insensitive on both query tokens and the asset's tags.
///
/// @ref UE5 NiagaraScriptBase::Tags + FAssetData::TagsAndValues filter helper.
/// @ref docs/engine/25-vfx/19-asset-tagging.md

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace uz {

namespace detail {

/// Returns `s` lowercased in-place — ASCII only, matches the behaviour of
/// the asset browser which normalises every token to ASCII lower-case.
inline std::string ToLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

/// Whitespace-tokenise `query` into non-empty tokens. Tokens are the atoms
/// consumed by the tag-query evaluator (positive term, negative term, or the
/// `OR` literal). Case is preserved for the caller to decide.
inline std::vector<std::string> TokenizeTagQuery(std::string_view query) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : query) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

/// Returns true when `tag` appears in `tags` (case-insensitive ASCII).
inline bool HasTag(const std::vector<std::string>& tags, const std::string& tag) {
    const std::string needle = ToLowerAscii(tag);
    return std::any_of(tags.begin(), tags.end(), [&](const std::string& t) {
        return ToLowerAscii(t) == needle;
    });
}

} // namespace detail

/// Evaluates a tag query against an asset's tag list.
///
/// # Grammar
/// See the file header. Empty / whitespace-only query matches every asset.
/// Unknown operators fall back to positive match. The evaluator is linear in
/// `tokens * tags` — acceptable for asset-browser list sizes (O(1000) assets,
/// O(10) tags each, O(10) tokens).
///
/// # Arguments
/// - `query` — the user-entered filter expression (see grammar).
/// - `tags`  — the tags stored on the asset.
///
/// # Returns
/// `true` iff the asset satisfies the query.
[[nodiscard]] inline bool VfxAssetTagMatches(std::string_view query,
                                              const std::vector<std::string>& tags) {
    auto tokens = detail::TokenizeTagQuery(query);
    if (tokens.empty()) return true;

    // Split tokens into OR-separated groups. The `OR` literal (ASCII upper
    // case) is the only group separator; every other token is a positive or
    // negative term inside the current group.
    std::vector<std::vector<std::string>> groups;
    groups.emplace_back();
    for (auto& tok : tokens) {
        if (tok == "OR") {
            // Empty trailing group means the user typed "foo OR " — treat
            // as a syntactic no-op so typing-in-progress stays permissive.
            if (!groups.back().empty()) groups.emplace_back();
            continue;
        }
        groups.back().push_back(std::move(tok));
    }

    // Drop any trailing empty group caused by a stray `OR`.
    while (!groups.empty() && groups.back().empty()) groups.pop_back();
    if (groups.empty()) return true;

    for (const auto& group : groups) {
        bool groupOk = true;
        for (const auto& term : group) {
            const bool negate = (!term.empty()) &&
                                (term.front() == '-' || term.front() == '!');
            const std::string body = negate ? term.substr(1) : term;
            if (body.empty()) {
                // Bare '-' or '!' is a typo, not a filter constraint — skip.
                continue;
            }
            const bool present = detail::HasTag(tags, body);
            if (negate ? present : !present) {
                groupOk = false;
                break;
            }
        }
        if (groupOk) return true;
    }

    return false;
}

} // namespace uz
