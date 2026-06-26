# llm_wiki

A local LLM-powered wiki that compiles raw source documents into structured,
interlinked knowledge pages using Claude. Think of it as a compiler rather than
a search engine: raw sources go in, structured wiki pages come out.

## Concept

```
Raw sources (PDFs, text)  -->  [Claude ingest]  -->  Wiki pages (Markdown)
                                                         |
User question  -----------------------------------------+-> [Claude query] --> Answer + citations
                                                         |
                                           [Claude lint] --> Issues + suggestions
```

Wiki pages are plain Markdown files on disk. Claude maintains them - merging new
information into existing pages, creating new pages, and updating cross-links.

## Build

From the repo root:

```sh
cmake --build build-prod --parallel $(nproc)
```

Binary: `build-prod/apps/llm_wiki/llm_wiki`

## Configuration

Create a `.env` file next to the binary (or in your working directory):

```
ANTHROPIC_API_KEY=sk-ant-...
WIKI_MODEL=claude-sonnet-4-6       # optional, default: claude-sonnet-4-6
WIKI_DIR=./wiki_data               # optional, default: ./wiki_data
WIKI_PORT=8090                     # optional, default: 8090
```

Shell environment always wins over `.env`.

## Run

```sh
cd build-prod/apps/llm_wiki
./llm_wiki
# Open http://localhost:8090
```

Data layout under `WIKI_DIR`:

```
wiki_data/
  SCHEMA.md         - naming and structure rules (editable)
  raw/              - uploaded source documents (immutable)
  wiki/
    INDEX.md        - auto-maintained page index
    CHANGELOG.md    - append-only change log
    *.md            - wiki pages
```

## UI

- **Sidebar** - browse and search wiki pages; list raw sources
- **+ Ingest** - paste text or enter a server-side file path
- **Drag-and-drop** - drop a PDF anywhere on the page to upload and ingest
- **? Query** - ask a question; Claude answers with `[[page]]` citations
- **Lint** - Claude checks for orphaned pages, broken links, contradictions

## API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/wiki/pages` | List all page names |
| GET | `/wiki/pages/{name}` | Read a page |
| GET | `/wiki/raw` | List raw source paths |
| GET | `/wiki/index` | Read INDEX.md |
| POST | `/wiki/upload` | Upload a file (multipart/form-data, field: `file`) |
| POST | `/wiki/ingest` | Compile content into wiki pages |
| POST | `/wiki/query` | Query the wiki |
| POST | `/wiki/lint` | Check for issues |

### POST /wiki/ingest

```json
{ "content": "...", "source_name": "RTA s18", "file_path": "" }
```

`file_path` is a server-side path under `raw/` - alternative to pasting content.
PDFs are extracted via `pdftotext` before being sent to Claude.

Response:

```json
{ "ok": true, "pages_created": 2, "pages_updated": 1, "page_names": ["bond-lodgement", "..."] }
```

### POST /wiki/query

```json
{ "question": "What is the bond lodgement deadline?" }
```

Response includes `answer` (Markdown with `[[citations]]`) and optional
`wiki_update` if Claude suggests an update based on the answer.

## Dependencies

- Drogon (HTTP server + async runtime)
- glaze (JSON)
- spdlog (logging)
- pdftotext (poppler-utils, for PDF ingestion)
- Claude API (via `astraea_clients`)
