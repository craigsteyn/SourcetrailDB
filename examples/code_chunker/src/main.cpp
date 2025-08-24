#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>
#include <filesystem>

#include "SourcetrailDBReader.h"
#include "yyjson.h"

// Simple JSON config structure
struct ChunkerConfig
{
    std::string db_path; // path to the Sourcetrail DB
    std::string project_name;
    std::string project_description;
    std::string root_dir;
    std::string indexed_root; // root path used when the DB was indexed (e.g., Z:/mcb)
    std::string chunk_output_root; // where to write chunk jsons
    std::vector<std::string> paths_to_chunk;
};

static bool readFileToBuffer(const std::string &path, std::string &out)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return false;
    ifs.seekg(0, std::ios::end);
    std::streampos size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0)
        ifs.read(&out[0], size);
    return true;
}

static bool parseConfig(const std::string &jsonPath, ChunkerConfig &cfg)
{
    std::string buf;
    if (!readFileToBuffer(jsonPath, buf))
    {
        std::cerr << "Failed to read config json: " << jsonPath << std::endl;
        return false;
    }
    yyjson_doc *doc = yyjson_read(buf.data(), buf.size(), 0);
    if (!doc)
    {
        std::cerr << "Failed to parse JSON config." << std::endl;
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root))
    {
        std::cerr << "Config root is not an object." << std::endl;
        yyjson_doc_free(doc);
        return false;
    }

    auto get_str = [&](const char *key, std::string &out)
    {
        if (yyjson_val *v = yyjson_obj_get(root, key))
        {
            if (yyjson_is_str(v))
            {
                const char *s = yyjson_get_str(v);
                if (s)
                    out = s;
            }
        }
    };

    get_str("db_path", cfg.db_path);
    get_str("project_name", cfg.project_name);
    get_str("project_description", cfg.project_description);
    get_str("root_dir", cfg.root_dir);
    get_str("indexed_root", cfg.indexed_root);
    get_str("chunk_output_root", cfg.chunk_output_root);

    if (yyjson_val *arr = yyjson_obj_get(root, "paths_to_chunk"))
    {
        if (yyjson_is_arr(arr))
        {
            size_t idx, max;
            yyjson_val *val;
            yyjson_arr_foreach(arr, idx, max, val)
            {
                if (yyjson_is_str(val))
                {
                    const char *s = yyjson_get_str(val);
                    if (s)
                        cfg.paths_to_chunk.emplace_back(s);
                }
            }
        }
    }

    yyjson_doc_free(doc);

    if (cfg.db_path.empty())
    {
        std::cerr << "Config missing 'db_path'." << std::endl;
        return false;
    }
    if (cfg.project_name.empty())
    {
        std::cerr << "Config missing 'project_name'." << std::endl;
        return false;
    }
    if (cfg.chunk_output_root.empty())
    {
        std::cerr << "Config missing 'chunk_output_root'." << std::endl;
        return false;
    }
    return true;
}

// --- Path helpers (simple, portable) ---
static inline std::string normalizePath(std::string p)
{
    // unify separators
    std::replace(p.begin(), p.end(), '\\', '/');
    // trim trailing '/'
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    return p;
}

static inline bool isAbsolutePath(const std::string &p)
{
    if (p.empty())
        return false;
    if (p[0] == '/' || p[0] == '\\')
        return true; // POSIX or UNC-like
    // crude Windows drive check: "C:/" or "C:\\"
    return (p.size() > 1 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':');
}

static inline std::string joinPath(const std::string &base, const std::string &rel)
{
    if (base.empty())
        return rel;
    if (rel.empty())
        return base;
    std::string a = normalizePath(base);
    std::string b = normalizePath(rel);
    if (isAbsolutePath(b))
        return b; // already absolute
    if (!a.empty() && a.back() != '/')
        a.push_back('/');
    return a + b;
}

// Map a DB file path (indexed under indexed_root) to a local path under root_dir
static inline std::string mapDbPathToLocal(const std::string &dbPath,
                                           const std::string &indexedRootNorm,
                                           const std::string &localRootNorm)
{
    std::string dbNorm = normalizePath(dbPath);
    std::string idx = normalizePath(indexedRootNorm);
    std::string loc = normalizePath(localRootNorm);
    if (!idx.empty())
    {
        std::string idxSlash = idx;
        if (idxSlash.back() != '/')
            idxSlash.push_back('/');
        if (dbNorm == idx)
        {
            return loc;
        }
        if (dbNorm.size() > idxSlash.size() && dbNorm.rfind(idxSlash, 0) == 0)
        {
            std::string rel = dbNorm.substr(idxSlash.size());
            if (!loc.empty() && loc.back() != '/')
                return loc + "/" + rel;
            return loc + rel;
        }
    }
    return dbNorm; // fallback to the DB path if it doesn't lie under indexedRoot
}

// Return relative path of absolutePath w.r.t base. If not under base, return filename only.
static inline std::string makeRelativeTo(const std::string &absolutePath, const std::string &base)
{
    std::string abs = normalizePath(absolutePath);
    std::string b = normalizePath(base);
    if (b.empty())
        return abs;
    std::string bSlash = b;
    if (bSlash.back() != '/')
        bSlash.push_back('/');
    if (abs == b)
        return std::string();
    if (abs.size() > bSlash.size() && abs.rfind(bSlash, 0) == 0)
        return abs.substr(bSlash.size());
    // fallback: last path segment
    auto pos = abs.find_last_of('/');
    return (pos == std::string::npos) ? abs : abs.substr(pos + 1);
}

// no longer need POSIX mkdir machinery; prefer std::filesystem

// Ensure directory exists for given file path (creates parent dirs)
static inline bool ensureParentDir(const std::string &filePath)
{
    try
    {
        std::filesystem::path p(filePath);
        p = p.parent_path();
        if (p.empty())
            return true;

        std::error_code ec;
        if (std::filesystem::exists(p, ec))
            return true;

        // create_directories is idempotent; still check existence in case of benign races
        if (std::filesystem::create_directories(p, ec))
            return true;

        return std::filesystem::exists(p); // true if another process created it
    }
    catch (...)
    {
        return false;
    }
}

static inline const char *edgeKindToString(sourcetrail::EdgeKind k)
{
    using EK = sourcetrail::EdgeKind;
    switch (k)
    {
    case EK::MEMBER: return "member";
    case EK::TYPE_USAGE: return "type_usage";
    case EK::USAGE: return "usage";
    case EK::CALL: return "call";
    case EK::INHERITANCE: return "inheritance";
    case EK::OVERRIDE: return "override";
    case EK::TYPE_ARGUMENT: return "type_argument";
    case EK::TEMPLATE_SPECIALIZATION: return "template_specialization";
    case EK::INCLUDE: return "include";
    case EK::IMPORT: return "import";
    case EK::MACRO_USAGE: return "macro_usage";
    case EK::ANNOTATION_USAGE: return "annotation_usage";
    default: return "unknown";
    }
}

static inline const char *symbolKindToString(sourcetrail::SymbolKind k)
{
    using SK = sourcetrail::SymbolKind;
    switch (k)
    {
    case SK::CLASS: return "class";
    case SK::STRUCT: return "struct";
    case SK::INTERFACE: return "interface";
    case SK::FUNCTION: return "function";
    case SK::METHOD: return "method";
    case SK::FIELD: return "field";
    case SK::GLOBAL_VARIABLE: return "global_variable";
    case SK::NAMESPACE: return "namespace";
    case SK::ENUM: return "enum";
    case SK::ENUM_CONSTANT: return "enum_constant";
    case SK::TYPEDEF: return "typedef";
    case SK::UNION: return "union";
    case SK::BUILTIN_TYPE: return "builtin_type";
    case SK::TYPE_PARAMETER: return "type_parameter";
    case SK::MODULE: return "module";
    case SK::PACKAGE: return "package";
    case SK::ANNOTATION: return "annotation";
    case SK::MACRO: return "macro";
    case SK::TYPE: return "type";
    default: return "unknown";
    }
}

// not certain this functions is correct
static inline std::string nameHierarchyToString(const sourcetrail::NameHierarchy &nh)
{
    // Build qualified name by joining element names with delimiter, then
    // apply the last element's signature (prefix/postfix) to the full name.
    if (nh.nameElements.empty())
        return std::string();

    // 1) Join names with delimiter
    std::string qualified;
    for (size_t i = 0; i < nh.nameElements.size(); ++i)
    {
        if (i > 0)
            qualified += nh.nameDelimiter;
        qualified += nh.nameElements[i].name;
    }

    // 2) Apply signature of the last element
    const auto &last = nh.nameElements.back();
    const bool hasPrefix = !last.prefix.empty();
    const bool hasPostfix = !last.postfix.empty();
    if (!hasPrefix && !hasPostfix)
        return qualified;

    std::string out;
    out.reserve(last.prefix.size() + qualified.size() + last.postfix.size() + 1);
    out += last.prefix;
    if (hasPrefix && !qualified.empty())
        out += ' ';
    out += qualified;
    out += last.postfix;
    return out;
}

// Build line start offsets for fast [line,col] -> offset mapping (1-based lines/cols)
static inline std::vector<size_t> buildLineOffsets(const std::string &text)
{
    std::vector<size_t> offs;
    offs.reserve(1024);
    offs.push_back(0); // line 1 starts at 0
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n')
        {
            offs.push_back(i + 1);
        }
    }
    // add sentinel for end
    offs.push_back(text.size());
    return offs;
}

static inline std::string sliceByRange(const std::string &text, const std::vector<size_t> &lineOffs,
                                       int startLine, int startCol, int endLine, int endCol)
{
    // Validate basic bounds (lineOffs has a sentinel at index = number_of_lines)
    if (startLine <= 0 || endLine <= 0)
        return std::string();
    if ((size_t)startLine >= lineOffs.size() || (size_t)endLine >= lineOffs.size())
        return std::string();

    // Convert 1-based (line,col) with inclusive end column into 0-based [start, end) byte offsets.
    // startCol/endCol of 0 are treated as beginning/end of line respectively.
    size_t start = lineOffs[(size_t)startLine - 1] + (startCol > 0 ? (size_t)startCol - 1 : 0);
    size_t end;
    if (endCol > 0)
    {
        // Inclusive end column -> make it exclusive by not subtracting 1
        end = lineOffs[(size_t)endLine - 1] + (size_t)endCol;
    }
    else
    {
        // endCol == 0 -> until end of the endLine (start of next line)
        end = lineOffs[(size_t)endLine];
    }

    // Clamp to text size to avoid out-of-range
    if (start > text.size()) start = text.size();
    if (end > text.size()) end = text.size();
    if (end < start) end = start;

    return text.substr(start, end - start);
}

int main(int argc, const char *argv[])
{
    // Usage: <config.json>
    if (argc < 2)
    {
        std::cout << "SourcetrailDB Code Chunker" << std::endl;
        std::cout << "==========================" << std::endl;
        std::cout << "Usage:\n  code_chunker <config_json_path>\n";
        return 1;
    }
    std::string jsonPath = argv[1];

    ChunkerConfig cfg;
    if (!parseConfig(jsonPath, cfg))
        return 1;

    std::cout << "Project: " << cfg.project_name << std::endl;
    if (!cfg.project_description.empty())
    {
        std::cout << "Description: " << cfg.project_description << std::endl;
    }

    sourcetrail::SourcetrailDBReader reader;
    std::cout << "Opening database: " << cfg.db_path << std::endl;
    if (!reader.open(cfg.db_path))
    {
        std::cerr << "Error opening database: " << reader.getLastError() << std::endl;
        return 1;
    }

    try
    {
        auto t0 = std::chrono::steady_clock::now();

        std::cout << "Loading files from database..." << std::endl;
        auto files = reader.getAllFiles();
        std::cout << "Loaded " << files.size() << " files from database." << std::endl;
        // print a random selection of files we loaded
        const size_t maxFilesToShow = 5;
        std::random_device rd;
        std::mt19937 gen(rd());
        for (size_t i = 0; i < std::min(files.size(), maxFilesToShow); ++i)
        {
            auto index = gen() % files.size();
            std::cout << "  " << files[index].filePath << std::endl;
        }

        // Build list of file path prefixes to include (DB space)
        // We accept paths relative to the project root. We generate DB-side prefixes
        // by joining with indexed_root (if provided). Absolute paths are kept as-is,
        // but if they start with local root, we translate to DB root.
        std::vector<std::string> includePrefixesDB;
        includePrefixesDB.reserve(cfg.paths_to_chunk.size());
        const std::string rootNorm = normalizePath(cfg.root_dir);
        const std::string indexedNorm = normalizePath(cfg.indexed_root);
        for (const auto &p : cfg.paths_to_chunk)
        {
            if (p.empty())
                continue;
            std::string entry = normalizePath(p);
            std::string dbPref;
            // Special handling: if entry starts with '/' (POSIX absolute) but our DB was
            // indexed under a Windows-like drive root, interpret it as relative to indexed_root.
            bool entryIsPosixAbs = (!entry.empty() && (entry[0] == '/'));
            bool indexedIsWinDrive = (!indexedNorm.empty() && indexedNorm.size() > 1 && std::isalpha(static_cast<unsigned char>(indexedNorm[0])) && indexedNorm[1] == ':');
            if (isAbsolutePath(entry) && !(entryIsPosixAbs && indexedIsWinDrive))
            {
                // If absolute and starts with local root, translate local->DB
                std::string rootSlash = rootNorm;
                if (!rootSlash.empty() && rootSlash.back() != '/')
                    rootSlash.push_back('/');
                if (!rootNorm.empty() && (entry == rootNorm || (entry.size() > rootSlash.size() && entry.rfind(rootSlash, 0) == 0)))
                {
                    // Compute relative to local root, then join with indexed root
                    std::string rel = (entry == rootNorm) ? std::string() : entry.substr(rootSlash.size());
                    dbPref = indexedNorm.empty() ? entry : joinPath(indexedNorm, rel);
                }
                else
                {
                    // Already absolute: assume it's in DB space
                    dbPref = entry;
                }
            }
            else
            {
                // Relative: join with DB indexed root when provided, otherwise with local root
                // If entry is POSIX-absolute but we consider it relative to the Windows indexed root, strip leading '/'
                std::string rel = (entryIsPosixAbs && indexedIsWinDrive) ? entry.substr(1) : entry;
                dbPref = !indexedNorm.empty() ? joinPath(indexedNorm, rel) : joinPath(rootNorm, rel);
            }
            dbPref = normalizePath(dbPref);
            includePrefixesDB.push_back(dbPref);
            std::cout << "  Including DB path prefix: " << dbPref << std::endl;
        }

        // Filter files to selected set (if includePrefixes non-empty)
        std::vector<sourcetrail::SourcetrailDBReader::File> selectedFiles;
        selectedFiles.reserve(files.size());
        if (!includePrefixesDB.empty())
        {
            for (const auto &f : files)
            {
                std::string fpath = normalizePath(f.filePath);
                for (const auto &pref : includePrefixesDB)
                {
                    if (fpath == pref)
                    {
                        selectedFiles.push_back(f);
                        break;
                    }
                    // directory prefix match: pref + "/"
                    if (!pref.empty())
                    {
                        std::string prefSlash = pref;
                        if (prefSlash.back() != '/')
                            prefSlash.push_back('/');
                        if (fpath.size() > prefSlash.size() && fpath.rfind(prefSlash, 0) == 0)
                        {
                            selectedFiles.push_back(f);
                            break;
                        }
                    }
                }
            }
            std::cout << "Filtered files to " << selectedFiles.size() << " based on paths_to_chunk (from " << files.size() << ")." << std::endl;
        }
        else
        {
            selectedFiles = files; // include all when no filter provided
        }

        // Remove files whose output chunk already exists on disk
        if (!selectedFiles.empty())
        {
            const std::string outRoot = normalizePath(cfg.chunk_output_root);
            const size_t beforeCount = selectedFiles.size();
            std::vector<sourcetrail::SourcetrailDBReader::File> filtered;
            filtered.reserve(beforeCount);
            for (const auto &f : selectedFiles)
            {
                // Derive output-relative path similarly to the emission phase, but without I/O
                std::string relForOut;
                if (!cfg.indexed_root.empty())
                    relForOut = makeRelativeTo(f.filePath, cfg.indexed_root);
                if (relForOut.empty())
                {
                    if (!cfg.root_dir.empty())
                    {
                        // Map DB path to local path using configured roots
                        std::string localPathEst = mapDbPathToLocal(f.filePath, indexedNorm, rootNorm);
                        relForOut = makeRelativeTo(localPathEst, cfg.root_dir);
                    }
                    if (relForOut.empty())
                    {
                        auto p = normalizePath(f.filePath);
                        auto pos = p.find_last_of('/');
                        relForOut = (pos == std::string::npos) ? p : p.substr(pos + 1);
                    }
                }

                std::string outPath = joinPath(outRoot, relForOut + ".json");
                std::ifstream existCheck(outPath, std::ios::binary);
                if (!existCheck.good())
                    filtered.push_back(f);
            }
            if (filtered.size() != beforeCount)
            {
                std::cout << "Skipping " << (beforeCount - filtered.size())
                          << " files with existing chunks." << std::endl;
            }
            selectedFiles.swap(filtered);
        }

        std::cout << "Loading symbols and edges from database..." << std::endl;
        // Load symbols and edges into memory
        auto symbols = reader.getAllSymbols();
        std::cout << "Loaded " << symbols.size() << " symbols from database." << std::endl;
        auto edges = reader.getAllEdgesBrief();
        std::cout << "Loaded " << edges.size() << " edges from database." << std::endl;
        // Build adjacency lists for traversals
        int maxId = 0;
        for (const auto &e : edges)
        {
            if (e.sourceSymbolId > maxId)
                maxId = e.sourceSymbolId;
            if (e.targetSymbolId > maxId)
                maxId = e.targetSymbolId;
        }
        for (const auto &s : symbols)
        {
            if (s.id > maxId)
                maxId = s.id;
        }
        std::vector<std::vector<std::pair<int, int>>> outgoingAdj(maxId + 1);
        std::vector<std::vector<std::pair<int, int>>> incomingAdj(maxId + 1);
        for (const auto &e : edges)
        {
            if (e.sourceSymbolId >= 0 && e.sourceSymbolId <= maxId)
                outgoingAdj[e.sourceSymbolId].emplace_back(e.targetSymbolId, static_cast<int>(e.edgeKind));
            if (e.targetSymbolId >= 0 && e.targetSymbolId <= maxId)
                incomingAdj[e.targetSymbolId].emplace_back(e.sourceSymbolId, static_cast<int>(e.edgeKind));
        }
        std::cout << "Built adjacency for " << (maxId + 1) << " symbol ID slots." << std::endl;

        std::unordered_map<int, std::vector<sourcetrail::SourcetrailDBReader::Symbol>> symbolsToVisitInFiles;
        if (!selectedFiles.empty())
        {
            std::vector<int> fileIds(1, -1);
            for (const auto &f : selectedFiles) {
                fileIds[0] = f.id;
                auto &symbols = symbolsToVisitInFiles[f.id];
                symbols = reader.getSymbolsInFiles(fileIds);
                for(auto &symbol : symbols)
                    symbol.locations = reader.getSourceLocationsForSymbol(symbol.id);
            }
            std::cout << "Filtered symbols to " << symbolsToVisitInFiles.size() << " based on selected files (DB query)." << std::endl;
        }
        else
        {
            std::cout << "No file filter provided; using all symbols (" << symbols.size() << ")." << std::endl;
        }

        auto t1 = std::chrono::steady_clock::now();
        std::chrono::duration<double> dt = t1 - t0;

        std::cout << "Loaded: "
                  << symbols.size() << " symbols, "
                  << edges.size() << " edges, "
                  << files.size() << " files, "
                  << dt.count() << "s" << std::endl;

        // Build quick lookup maps for later chunking implementation
        int maxSymbolId = 0;
        for (const auto &s : symbols)
            if (s.id > maxSymbolId)
                maxSymbolId = s.id;

        // ID -> symbol index
        std::vector<int> symIndexById(maxSymbolId + 1, -1);
        for (size_t i = 0; i < symbols.size(); ++i)
        {
            if (symbols[i].id >= 0 && symbols[i].id <= maxSymbolId)
                symIndexById[symbols[i].id] = static_cast<int>(i);
        }

        // Show a few mapped file paths (DB -> local) using indexed_root/root_dir
        if (!selectedFiles.empty() && !cfg.indexed_root.empty() && !cfg.root_dir.empty())
        {
            std::cout << "Sample path remapping (DB -> local):" << std::endl;
            size_t show = std::min<size_t>(5, selectedFiles.size());
            for (size_t i = 0; i < show; ++i)
            {
                const auto &f = selectedFiles[i];
                std::string mapped = mapDbPathToLocal(f.filePath, indexedNorm, rootNorm);
                std::cout << "  " << normalizePath(f.filePath) << " -> " << mapped << std::endl;
            }
        }

        // --- Generate chunks per file ---
        std::cout << "Generating chunks to: " << cfg.chunk_output_root << std::endl;
        const std::string outRoot = normalizePath(cfg.chunk_output_root);
        for (const auto &f : selectedFiles)
        {
            // Determine local path for reading file text
            std::string localPath = mapDbPathToLocal(f.filePath, indexedNorm, rootNorm);
            // Fallbacks if mapping didn't produce an existing file
            std::string fileText;
            if (!readFileToBuffer(localPath, fileText))
            {
                // Try joining root_dir with relative to indexed_root
                std::string relToIdx = makeRelativeTo(f.filePath, indexedNorm);
                std::string alt = joinPath(rootNorm, relToIdx);
                if (!readFileToBuffer(alt, fileText))
                {
                    // Try DB path as-is (may be relative to CWD)
                    if (!readFileToBuffer(f.filePath, fileText))
                    {
                        std::cerr << "Warning: could not read source file: " << localPath << " (or alternatives), skipping file." << std::endl;
                        continue;
                    }
                    else
                    {
                        localPath = f.filePath;
                    }
                }
                else
                {
                    localPath = alt;
                }
            }
            auto lineOffs = buildLineOffsets(fileText);

            // Compute relative path for JSON metadata/output path
            std::string relForOut;
            if (!cfg.indexed_root.empty())
                relForOut = makeRelativeTo(f.filePath, cfg.indexed_root);
            if (relForOut.empty())
            {
                if (!cfg.root_dir.empty())
                    relForOut = makeRelativeTo(localPath, cfg.root_dir);
                if (relForOut.empty())
                {
                    auto p = normalizePath(f.filePath);
                    auto pos = p.find_last_of('/');
                    relForOut = (pos == std::string::npos) ? p : p.substr(pos + 1);
                }
            }

            std::string outPath = joinPath(outRoot, relForOut + ".json");
            if (!ensureParentDir(outPath))
            {
                std::cerr << "Warning: could not create parent directory for: " << outPath << std::endl;
                continue;
            }

            // Build JSON for this file
            yyjson_mut_doc *mdoc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val *mroot = yyjson_mut_obj(mdoc);
            yyjson_mut_doc_set_root(mdoc, mroot);
            yyjson_mut_obj_add_str(mdoc, mroot, "file_path", relForOut.c_str());
            yyjson_mut_val *chunksArr = yyjson_mut_arr(mdoc);
            yyjson_mut_obj_add_val(mdoc, mroot, "chunks", chunksArr);

            // Symbols for this file
            auto itFileSyms = symbolsToVisitInFiles.find(f.id);
            const auto &fileSyms = (itFileSyms != symbolsToVisitInFiles.end()) ? itFileSyms->second : symbols;

            for (const auto &sym : fileSyms)
            {
                // Find SCOPE location within this file
                auto locs = reader.getSourceLocationsForSymbolInFile(sym.id, f.id);
                const sourcetrail::SourcetrailDBReader::SourceLocation *scopeLoc = nullptr;
                for (const auto &loc : locs)
                {
                    if (loc.locationType == sourcetrail::LocationKind::SCOPE)
                    {
                        scopeLoc = &loc;
                        break;
                    }
                }
                if (!scopeLoc) {
                    // in this case it should only have 1 token location
                    for (const auto &loc : locs)
                    {
                        if (loc.locationType == sourcetrail::LocationKind::TOKEN)
                        {
                            scopeLoc = &loc;
                            break;
                        }
                    }
                }
                if (!scopeLoc) {
                    std::cerr << "Warning: no SCOPE location found for symbol: " << sym.id << std::endl;
                    continue; // no location in this file
                }

                yyjson_mut_val *chunk = yyjson_mut_obj(mdoc);
                yyjson_mut_arr_add_val(chunksArr, chunk);

                yyjson_mut_obj_add_int(mdoc, chunk, "id", sym.id);
                yyjson_mut_obj_add_str(mdoc, chunk, "type", symbolKindToString(sym.symbolKind));
                std::string fqn = nameHierarchyToString(sym.nameHierarchy);
                yyjson_mut_obj_add_strcpy(mdoc, chunk, "fully_qualified_name", fqn.c_str());
                std::string simpleName;
                if (!sym.nameHierarchy.nameElements.empty())
                    simpleName = sym.nameHierarchy.nameElements.back().name;
                yyjson_mut_obj_add_strcpy(mdoc, chunk, "name", simpleName.c_str());
                yyjson_mut_obj_add_str(mdoc, chunk, "en_chunk", "");

                yyjson_mut_val *outRefs = yyjson_mut_arr(mdoc);
                yyjson_mut_obj_add_val(mdoc, chunk, "outgoing_references", outRefs);
                if (sym.id >= 0 && sym.id < (int)outgoingAdj.size())
                {
                    for (const auto &edge : outgoingAdj[sym.id])
                    {
                        yyjson_mut_val *refObj = yyjson_mut_obj(mdoc);
                        yyjson_mut_arr_add_val(outRefs, refObj);
                        yyjson_mut_obj_add_str(mdoc, refObj, "type", edgeKindToString(static_cast<sourcetrail::EdgeKind>(edge.second)));
                        yyjson_mut_obj_add_int(mdoc, refObj, "id", edge.first);
                    }
                }

                yyjson_mut_obj_add_int(mdoc, chunk, "start_line", scopeLoc->startLine);
                yyjson_mut_obj_add_int(mdoc, chunk, "start_column", scopeLoc->startColumn);
                yyjson_mut_obj_add_int(mdoc, chunk, "end_line", scopeLoc->endLine);
                yyjson_mut_obj_add_int(mdoc, chunk, "end_column", scopeLoc->endColumn);

                std::string code = sliceByRange(fileText, lineOffs, scopeLoc->startLine, scopeLoc->startColumn, scopeLoc->endLine, scopeLoc->endColumn);
                yyjson_mut_obj_add_strcpy(mdoc, chunk, "code_chunk", code.c_str());
            }

            // Write JSON file
            size_t len = 0;
            char *json = yyjson_mut_write(mdoc, YYJSON_WRITE_PRETTY, &len);
            if (!json)
            {
                std::cerr << "Warning: failed to serialize JSON for: " << outPath << std::endl;
                yyjson_mut_doc_free(mdoc);
                continue;
            }
            {
                std::ofstream ofs(outPath, std::ios::binary);
                if (!ofs)
                {
                    std::cerr << "Warning: failed to open for write: " << outPath << std::endl;
                }
                else
                {
                    ofs.write(json, static_cast<std::streamsize>(len));
                }
            }
            free(json);
            yyjson_mut_doc_free(mdoc);
            std::cout << "Wrote chunks: " << outPath << std::endl;
        }

        reader.close();
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        reader.close();
        return 1;
    }
}
