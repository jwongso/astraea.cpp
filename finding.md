# Astraea C++23 Port — Findings

**Context.** This is a partial in-progress port of the Astraea Python
framework (`/home/junisap/proj/astraea/`, ~3 260 LOC across 19 core
modules) to C++23. So far only two modules have been ported:
`core/routing.py` → `routing.{hpp,cpp}` and `core/sanitize.py` →
`sanitize.{hpp,cpp}`, plus a `Config` struct and a `JurisdictionBase`
skeleton. The remaining 17 Python modules (HTTP API, retriever, anchor,
LLM generator, queue, reranker, embedder, MCP, etc.) have no C++
counterpart yet.

**Scope of this review.** Everything under `astraea.cpp/`, cross-checked
against the Python originals.

**Priority order.** Because the port is in progress, correctness vs the
Python reference outranks performance. Sections below are ranked
accordingly:

1. **§1 Port-parity bugs** — C++ behaves differently from Python in
   ways that will fail the existing test suite and one of which is
   security-relevant. **Fix before shipping.**
2. **§2 Differential test harness** — the only sustainable way to keep
   the port honest as it grows. Land this next.
3. **§3 Performance** — `normalize_query`, `build_route_decision`,
   `sanitize_question` hot paths. The 20-route × ~1 370-term NZ
   tenancy table makes some of these matter sooner than they would for
   a toy port.
4. **§4 API shape and style** — small things that compound across the
   remaining 17 modules still to be ported.

---

## 1. Port-parity bugs (correctness)

Each of these is a divergence from `core/routing.py` or
`core/sanitize.py` that would cause the existing pytest suite to fail
if run against the C++ implementation through a pybind11 wrapper. Some
are also security-relevant.

### 1.1. `sanitize.cpp` strips far fewer Unicode characters than Python

**Python** (`core/sanitize.py:62-65`):

```python
text = "".join(
    c for c in text
    if unicodedata.category(c) not in ("Cc", "Cf") or c in "\n\t"
)
```

This removes **every codepoint** whose Unicode general category is
`Cc` (control) or `Cf` (format), except `\n` and `\t`. The full Cc/Cf
set includes:

* C0 controls: U+0000–U+001F (minus `\n \t`).
* DEL: U+007F.
* **C1 controls: U+0080–U+009F.**
* U+00AD (soft hyphen).
* U+061C (Arabic letter mark).
* U+200B–U+200F (zero-width spaces, LRM, RLM).
* **U+202A–U+202E (LRE/RLE/PDF/LRO/RLO — bidi override controls).**
* **U+2066–U+2069 (LRI/RLI/FSI/PDI — bidi isolate controls).**
* U+2060–U+2064, U+FEFF (BOM), and a long tail of plane-1 format
  controls.

**C++** (`sanitize.cpp::is_strip_char` + `sanitize_question`) strips:

* U+0000–U+007F (minus `\t \n`).
* U+00AD only.
* U+200B–U+200F only.
* U+FEFF only.

Everything else passes through unchanged, including:

* C1 controls (U+0080–U+009F).
* **All bidi override and isolate controls** (U+202A–U+202E,
  U+2066–U+2069) — these are the most common prompt-injection vector
  using Unicode formatting tricks. The Python implementation strips
  them; the C++ port does not.
* The rest of the Cf range.

**Impact.** Two problems:

1. Behaviour divergence — the Python test suite would fail on any
   input mixing these codepoints.
2. **Prompt-injection hardening regression** — `sanitize.py` was
   explicitly designed (its docstring says "non-overridable security
   layer") to strip bidi controls so an attacker can't smuggle
   instructions past `INJECTION_RE` with visually identical but
   reordered text. The C++ port lets these through.

**Fix.** Either:

* Generate a compact UTF-8 strip table from the Unicode database (the
  Cc/Cf set is ~150 codepoints in practice; a sorted array of
  `(start, end)` ranges with binary search costs ~10 lines plus a
  build-time codegen script), or
* Link against ICU and call `u_charType(cp) == U_CONTROL_CHAR ||
  u_charType(cp) == U_FORMAT_CHAR`, or
* At minimum, **extend the existing hand-table to cover the bidi
  controls and C1 range** as a stopgap. The minimal extra entries:

  ```cpp
  // C1 controls: U+0080–U+009F = C2 80..BF + C3 80..9F (UTF-8: C2 80–C2 9F)
  if (b == 0xC2 && i + 1 < text.size()) {
      auto b1 = static_cast<unsigned char>(text[i + 1]);
      if (b1 >= 0x80 && b1 <= 0x9F) { i += 2; continue; }
      if (b1 == 0xAD)                { i += 2; continue; } // already handled
  }
  // U+202A..U+202E = E2 80 AA..AE; U+2066..U+2069 = E2 81 A6..A9; U+2060..U+2064 = E2 81 A0..A4
  if (b == 0xE2 && i + 2 < text.size()) {
      auto b1 = static_cast<unsigned char>(text[i + 1]);
      auto b2 = static_cast<unsigned char>(text[i + 2]);
      if (b1 == 0x80) {
          if (b2 >= 0x8B && b2 <= 0x8F) { i += 3; continue; } // existing
          if (b2 >= 0xAA && b2 <= 0xAE) { i += 3; continue; } // bidi overrides
      }
      if (b1 == 0x81) {
          if (b2 >= 0xA0 && b2 <= 0xA4) { i += 3; continue; } // word joiners
          if (b2 >= 0xA6 && b2 <= 0xA9) { i += 3; continue; } // bidi isolates
      }
  }
  // U+061C = D8 9C (Arabic letter mark)
  if (b == 0xD8 && i + 1 < text.size()
          && static_cast<unsigned char>(text[i + 1]) == 0x9C) {
      i += 2; continue;
  }
  ```

  This still doesn't cover plane-1 format characters (U+E0001 etc.)
  but at least closes the security gap.

### 1.2. `routing.cpp` unions allow-lists; Python uses only the dominant

**Python** (`core/routing.py:140-146`):

```python
allow_candidates = [r for r in matched if r.leg_allow_list]
if allow_candidates:
    dominant = max(allow_candidates, key=lambda r: r.priority)
    leg_allow_list = dominant.leg_allow_list      # <-- only the dominant
else:
    dominant = max(matched, key=lambda r: r.priority) if matched else None
    leg_allow_list = ()
```

**C++** (`routing.cpp:151-161`):

```cpp
if (!allow_candidates.empty()) {
    dominant_ptr = *std::max_element(...);
    std::set<std::string> seen_allow;
    for (const auto* r : allow_candidates)            // <-- iterates ALL candidates
        for (const auto& s : r->leg_allow_list)
            if (seen_allow.insert(s).second) leg_allow_list.push_back(s);
}
```

The C++ port takes the **union** of every allow-list. Python takes
only the dominant route's allow-list. This is a behavioural change
that defeats the purpose of priority-based allow-list resolution — the
dominant route's restrictions get diluted by every other matched
route's whitelist.

**Fix.** Match Python exactly:

```cpp
if (!allow_candidates.empty()) {
    dominant_ptr = *std::max_element(allow_candidates.begin(),
                                      allow_candidates.end(),
        [](auto a, auto b) { return a->priority < b->priority; });
    leg_allow_list = dominant_ptr->leg_allow_list;   // copy from dominant only
}
```

### 1.3. `dominance_reason` text drift

**Python** (`core/routing.py:198-202`):

```python
why = (
    f"lower priority ({r.priority} < {dominant.priority}); "
    "allow-list not used, forced sections still merged"
    if r.leg_allow_list
    else "no allow-list; forced sections still merged"
)
```

**C++** (`routing.cpp:231-235`):

```cpp
std::string why = r->leg_allow_list.empty()
    ? "no allow-list; forced sections merged"
    : "lower priority (" + std::to_string(r->priority) + " < "
        + std::to_string(dominant_ptr->priority)
        + "); allow-list unioned, forced sections merged";
```

Two differences:

* `"forced sections merged"` vs Python's `"forced sections still merged"` — drops "still".
* `"allow-list unioned"` vs Python's `"allow-list not used"` — this is
  a direct consequence of the §1.2 bug; the C++ message is *accurate*
  for the buggy implementation but won't match Python output once
  §1.2 is fixed.

Once §1.2 is fixed, change the C++ message to match Python verbatim.
Same applies to the success-branch message:

```cpp
// Python builds:  ["has leg_allow_list"] (+ optional "priority {p}") joined by ", "
// Current C++:    "has leg_allow_list", "has leg_allow_list, priority {p}"
// Python output:  exactly the same when reconstructed via ", ".join(...)
// → matches today, but write a test to lock it in.
```

### 1.4. `normalize_query` performs Unicode transliteration that Python does not

**Python** (`core/routing.py:74-75`):

```python
def normalize_query(text: str) -> str:
    return " ".join(text.lower().replace("-", " ").split())
```

That is the entire function. It does:

1. ASCII-meaningful lowercase via Python's full Unicode `str.lower()`.
2. Replace `-` with space.
3. Split on Unicode whitespace and rejoin with single spaces.

It does **not** transliterate smart quotes (`U+2018/U+2019/U+201C/U+201D`)
to ASCII quotes, and it does **not** map em/en/figure dashes
(`U+2012–U+2015`) to spaces.

**C++** (`routing.cpp:20-69`) does all of the above transliterations.
The header comment even claims:

```
// Ports Python:
//   str.maketrans({0x2018->0x27, 0x2019->0x27, 0x201C->0x22, 0x201D->0x22,
//                  0x2014->0x20, 0x2013->0x20, 0x2012->0x20, 0x2015->0x20,
//                  0x002D->0x20})
//   text.lower().translate(...).split()  -> " ".join(...)
```

…but the actual Python source has no such `translate` call. The C++ port
appears to be implementing an *intended* normalisation that wasn't
applied in Python, or porting an older version of `routing.py`.

**Impact.** A user query containing a smart quote or em-dash will:

* In Python: keep the original codepoints in `q`, so a route term like
  `"healthy homes"` won't match `"healthy—homes"`.
* In C++: have the dash mapped to space and split into separate words,
  so `"healthy homes"` *will* match.

This is real divergence. Decide which behaviour is desired:

* **If Python is canonical** (most likely while porting): delete the
  smart-quote/dash transliteration from the C++ implementation.
  `normalize_query` collapses to lowercase + hyphen→space + whitespace
  collapse, matching Python byte-for-byte for ASCII input.
* **If the C++ enhancement is desired**: backport the same
  `str.maketrans` to Python first so both behave identically, then
  re-enable in C++.

Pick one and document it. Today the two implementations will
disagree on any input with curly quotes.

### 1.5. `normalize_query` ASCII-only lowercase vs Python Unicode lowercase

Python's `str.lower()` is full Unicode case-folding —
`"Māori".lower() == "māori"`, `"İGNORE".lower() == "i̇gnore"`, etc.

The C++ port lowercases only A–Z. For an English-only NZ tenancy
corpus this rarely matters, but as soon as a Te Reo Māori route is
added (and `Ā Ē Ī Ō Ū` show up in user input), the C++ port will fail
to match terms like `"māori land court"` when the user types
`"Māori land court"`.

This is the same class of issue as §1.4 — document the assumption or
add a small lookup for the specific codepoints that matter.

### 1.6. `sanitize_question` trim uses ASCII whitespace only

Python `text.strip()` strips Unicode whitespace including NBSP
(U+00A0), various spaces in U+2000–U+200A, U+2028/U+2029, U+3000, etc.
C++ uses `find_first_not_of(" \t\n\r")`, which is ASCII only.

In practice, low-impact for legal-question input. List it in the
divergence ledger so the differential harness doesn't surprise you.

### 1.7. `boosted_act_ids` parse — verified equivalent

Python: `parts = s.split("/"); if len(parts) >= 2: boosted.add(parts[1])`.

C++: finds first slash, then second slash, takes substring between
them (or to end if no second slash).

For inputs `"NZLEG/RTA/s18"`, `"NZLEG/RTA"`, `"NZLEG"` the outputs
match. ✓ No bug here.

### Divergence summary table

| # | Symptom | Severity | Module | Fix size |
|---|---------|----------|--------|----------|
| 1.1 | C1 controls and bidi overrides not stripped | **security** | sanitize | ~25 LOC |
| 1.2 | Allow-list unioned instead of dominant-only | **logic bug** | routing | 3 LOC |
| 1.3 | Dominance-reason text drifts from Python | logic / log | routing | 2 LOC |
| 1.4 | Smart quotes / em-dashes transliterated in C++ but not Python | logic | routing | ~10 LOC (delete) |
| 1.5 | ASCII-only lowercase vs Python Unicode lower | latent | routing | doc or table |
| 1.6 | ASCII-only strip vs Python Unicode strip | minor | sanitize | ~5 LOC |

---

## 2. Differential test harness — build this before optimising

The Python repo already has the corpus you need:

* `tests/core/test_sanitize.py` — 169 lines of address-only,
  injection, and edge-case fixtures.
* `tests/jurisdictions/test_routing.py` — runs
  `build_route_decision` over every `RouteFixture` defined by a
  jurisdiction.
* `jurisdictions/nz_tenancy/jurisdiction.py` — provides
  `route_fixtures` and `smoke_fixtures` lists you can iterate.

The cheapest path to a parity harness:

1. Wrap the C++ implementations with **pybind11**, exposing
   `normalize_query`, `sanitize_question`, and `build_route_decision`.
2. Add a pytest module that, for every fixture, calls both Python and
   C++ implementations and asserts byte-for-byte equality on every
   output field (`RouteDecision` serialised to a tuple/dict;
   `sanitize_question` return value or raised error message).
3. CI runs this on every C++ change. Any divergence fails the build
   with a diff.

Once the harness exists, the §1 bugs above produce explicit failing
tests that document themselves. And every §3 perf change can be merged
with confidence because the harness re-runs against thousands of
fixtures in seconds.

For property-based coverage, layer `hypothesis` strategies on top —
random Unicode strings, random combinations of valid route terms —
and assert equality. Catches edge cases neither set of hand-written
fixtures will think of.

---

## 3. Performance

After the §1 bugs are fixed and the §2 harness exists, the
optimisations below are safe to land. I have re-scoped them against
the actual route table size:

* **20 routes** in `jurisdictions/nz_tenancy/routes.py`.
* **~1 370 quoted strings** total (trigger terms, forced sections,
  synth queries, etc.).
* Estimated ~50–80 trigger terms per route on average.

This is large enough that `string::find` × routes × terms × |q|
adds up. A 300-char combined query against 20 routes × 70 terms is
~420 000 byte comparisons per request just for triggering — modest in
absolute terms, but the dominant cost once the regex bottleneck in
§3.3 is gone.

### 3.1. `normalize_query` — one pass, no locale

Two passes currently (build `buf`, then collapse into `out`) and
`std::tolower(b0)` per char (locale call).

Fix A — single pass with index write and inline whitespace collapse:

```cpp
std::string normalize_query(std::string_view text) {
    std::string out;
    out.resize(text.size());
    std::size_t w = 0;
    bool in_space = true;          // strip leading whitespace

    auto emit = [&](char c) {
        if (c == ' ') {
            if (!in_space) { out[w++] = ' '; in_space = true; }
        } else {
            out[w++] = c; in_space = false;
        }
    };

    for (std::size_t i = 0, n = text.size(); i < n; ++i) {
        const auto b = static_cast<unsigned char>(text[i]);
        if (b == '-' || b == ' ' || b == '\t' || b == '\n' || b == '\r')
            emit(' ');
        else
            emit(static_cast<char>(b | ((b - 'A' < 26u) << 5))); // ASCII tolower
    }
    if (w > 0 && out[w - 1] == ' ') --w;
    out.resize(w);
    return out;
}
```

Note: this assumes §1.4 was resolved by dropping the smart-quote /
em-dash transliteration. If that transliteration stays in (because
you back-ported it to Python), the same single-pass structure works
— just keep the multi-byte cases as `if (b == 0xE2 ...)` arms emitting
the replacement.

Fix B — branchless ASCII lowercase (used in the snippet above):
`c | ((c - 'A' < 26u) << 5)`. About 3× faster than
`std::tolower((unsigned char) c)` in the inner loop because there is
no locale dispatch.

Expected total speedup on `normalize_query`: ~2–3×.

### 3.2. `build_route_decision` — eliminate redundant work

Three things compound here:

**3.2.1. Three string allocations for `q`:**

```cpp
const std::string q = normalize_query(std::string(original) + " " + std::string(rewritten));
```

→ one reserved buffer:

```cpp
std::string combined;
combined.reserve(original.size() + 1 + rewritten.size());
combined.append(original);
combined.push_back(' ');
combined.append(rewritten);
const std::string q = normalize_query(combined);
```

**3.2.2. The same `t in q` check runs up to three times per term:**
once in `route_triggered`, once in the trigger-terms collection
loop, once in the near-miss loop (different routes, same q). Cache
the matched terms during the first pass:

```cpp
struct MatchInfo {
    const StatuteRoute* route;
    std::string_view    path;                     // "precise" / "broad+context" / "legacy"
    std::vector<std::string_view> matched_terms;  // views into route's strings
};
std::vector<MatchInfo> matched;
matched.reserve(routes.size());
```

Fill `matched_terms` as you decide `route_triggered` returned true.
The later "trigger_terms / trigger_paths" loop becomes a free
copy. Saves roughly half the substring scans on the hot path.

**3.2.3. Replace every dedup `std::set<std::string>` with
`std::unordered_set<std::string_view>`** pointing into the original
`StatuteRoute` storage (which outlives the call):

Affected: `seen_sections`, `seen_allow`, `seen_lsynth`,
`seen_csynth`, `trigger_term_set`, `matched_intents`. All of these
are membership-only guards — never iterated for output — so
unordered_set is semantically equivalent and ~5–10× faster than the
red-black tree, with no per-node allocation.

The `trigger_terms` output is sorted at the end, so external
determinism is preserved.

Expected total speedup on `build_route_decision`: ~1.5–2× today,
larger as the route table grows.

### 3.3. `sanitize.cpp` — `std::regex` is the dominant cost

Three regex passes per call (`INJECTION_RE`, `LEGAL_TERMS_RE`,
`ADDRESS_ONLY_RE`). `std::regex` is famously slow (20–100 µs per
search on short input). On a typical request these regexes
dominate `sanitize_question`'s wall time.

Three options, increasing payoff and increasing risk:

**A. Replace `INJECTION_RE` with a literal-phrase set.**

The pattern is an alternation of fixed phrases. Lowercase the input
once, then `lower.find(phrase)` over a static
`std::array<std::string_view, N>`. Enumerate the singular and plural
variants explicitly (clearer and auditable). <1 µs vs ~50 µs.

Risk: Python's `re.IGNORECASE` does Unicode case-folding; a literal
matcher does not. Unicode case-bypass of prompt injection is a real
attack class but vanishingly rare in practice. Document and accept.

**B. Replace `LEGAL_TERMS_RE` with a word-set probe.**

Tokenise `lower` on ASCII word boundaries once, probe each token
against `std::unordered_set<std::string_view>`. Handle the `s\d+`
case (section references) as a special arm: `tok.starts_with('s')
&& std::all_of(tok.begin() + 1, tok.end(), is_ascii_digit)`.

Risk: the alternation `fixed.?term` / `healthy.?homes?` /
`arrears?` / `repair(s?)` etc. needs each variant enumerated in the
set. List them once at startup. Run the harness against the existing
test suite to confirm.

**C. Migrate `ADDRESS_ONLY_RE` to RE2** (`apt install libre2-dev`).

This is the one genuinely structural pattern. RE2 gives linear-time
guarantees and is ~10× faster than `std::regex` on a pattern of this
shape. Drop-in API change:

```cpp
static const re2::RE2 ADDRESS_ONLY_RE{R"(...)", re2::RE2::Quiet};
if (re2::RE2::FullMatch(out, ADDRESS_ONLY_RE)) { /* reject */ }
```

Risk: RE2's `\w` is ASCII-only by default, which matches the intent
of the pattern (and Python's behaviour on ASCII addresses). No
divergence expected on the existing test corpus, but confirm via
harness.

Recommended ordering: A → B → C. Each is independently testable.
Compute the lowercase copy of `out` once and reuse it for both A and
B; the regex used to do this internally per pass via `regex::icase` —
once it's gone, amortise the lower-case work.

### 3.4. Substring search at scale — Aho-Corasick is now justifiable

With 20 routes × ~70 terms × ~300-char query, each request does
~420 000 byte comparisons in the match phase. An Aho-Corasick
automaton built **once per `routes` set** (effectively once at
process startup since each process serves one jurisdiction) reduces
the entire match phase to one O(|q|) scan emitting (route, term,
kind) hits. Then `build_route_decision` aggregates the hit set
instead of re-scanning per-term.

Implementation options:

* Header-only `aho_corasick` library on GitHub (~500 LOC, MIT).
* Roll your own (~150 LOC for the construction +
  matching), specialised to UTF-8 byte-level matching since terms
  are ASCII-after-normalisation.

Expected speedup on `build_route_decision` match phase: 10–30×.

Defer until the §1 bugs and §2 harness are in place; this is a
sizeable change and benefits hugely from the harness re-running on
every commit.

### 3.5. Smaller perf items

* `dominance_reason` building uses `+=` with `std::to_string`.
  Replace with `std::format`:

  ```cpp
  dominance_reason = std::format(
      "lower priority ({} < {}); allow-list not used, forced sections still merged",
      r->priority, dominant_ptr->priority);
  ```

  One allocation instead of 4–6, and the format string is easier to
  keep in sync with the Python message text (§1.3).

* `sanitize_question` final trim:

  ```cpp
  out = out.substr(first, last - first + 1);  // allocates
  ```

  Replace with in-place:

  ```cpp
  out.erase(last + 1);
  out.erase(0, first);
  ```

* `RouteDecision::boosted_act_ids` is `std::unordered_set<std::string>`.
  If callers only iterate, switch to `std::vector<std::string>`
  (already deduped during construction) and avoid the hash table.
  Check the Python callers (`core/anchor.py`, `core/api.py`) before
  changing the API.

* `is_strip_char` → `[[nodiscard]] constexpr`.

---

## 4. API shape and code quality

These don't affect runtime perf today, but they will compound as the
remaining 17 Python modules get ported.

### 4.1. Use `std::span` more aggressively

`any_in` / `all_in` / `route_triggered` take
`const std::vector<std::string>&`. Switching to
`std::span<const std::string>` lets callers pass slices or views
without copy and codegens identically. Same for `low_priority_sections`
in `allow_section`.

### 4.2. Store `StatuteRoute` terms as views into an arena

For a long-lived process the route table is effectively immutable
after `Jurisdiction` construction. Storing the terms as
`std::vector<std::string_view>` pointing into a single arena
(`std::pmr::monotonic_buffer_resource` or a hand-rolled `std::string`
holding the concatenated bytes) avoids ~1 370 small-string allocations
on startup and improves cache locality during matching. Modest win
today, easier once you build it in from the start.

### 4.3. `Config::from_env` throws on malformed env vars

`std::stoi("not-an-int")` throws `std::invalid_argument`, which most
likely aborts the process at startup with no useful message.
Use `std::from_chars` with a fallback + warning log:

```cpp
auto get_int = [](const char* k, int def) -> int {
    const char* v = std::getenv(k);
    if (!v) return def;
    int out = def;
    auto [p, ec] = std::from_chars(v, v + std::strlen(v), out);
    if (ec != std::errc{}) {
        std::fprintf(stderr, "warn: %s=%s is not an integer; using %d\n", k, v, def);
        return def;
    }
    return out;
};
```

(For `float`, `std::from_chars<float>` is missing in older libstdc++;
use `std::strtof` with explicit `errno` and end-pointer checks.)

### 4.4. `JurisdictionBase` defaults returning refs to function-local statics

`confidence_config()`, `leg_sources()`, `low_priority_sections()`
return references to function-local `static` objects. This is
thread-safe in C++11+ (magic statics), but easy to mistake for UB
at a glance. One-line comment in the header is enough.

### 4.5. Helpers in `routing.cpp` are `static`

Fine, but consider an anonymous namespace instead. Also mark them
`[[nodiscard]]` (`contains`, `any_in`, `all_in`, `route_triggered`).

### 4.6. `SanitizeError` carries `http_status` but the C++ port doesn't have an HTTP layer yet

Python raises `HTTPException(400, detail={"error": ...})`. The C++
`SanitizeError` carries `int http_status = 400` and a message
string. When the HTTP layer is ported (presumably from `core/api.py`),
make sure the JSON shape matches Python (`{"error": "..."}`, not a
bare string) so existing API clients don't break.

---

## Suggested rollout order (revised given porting context)

1. **Fix §1.1** — extend the Cf strip table. **Security-relevant.**
2. **Fix §1.2** — revert allow-list union to dominant-only. **Logic bug.**
3. **Fix §1.3, §1.4** — align `dominance_reason` strings and decide
   the smart-quote question (delete in C++ or port to Python).
4. **Build §2** — pybind11 + pytest differential harness against
   `route_fixtures` and `test_sanitize.py` corpus.
5. **§3.1** — `normalize_query` single-pass + ASCII lower.
6. **§3.2** — `build_route_decision` internal refactor.
7. **§3.3** — sanitize regex replacement, gated by §2.
8. **§3.4** — Aho-Corasick once the harness gives full confidence and
   if profiling confirms routing is on the hot path.

Items §4 are continuous polish — apply as the rest of the Python
modules are ported, not as a separate sweep.
