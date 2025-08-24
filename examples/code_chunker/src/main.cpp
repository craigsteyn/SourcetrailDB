#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>

#include "SourcetrailDBReader.h"
#include "yyjson.h"

// Simple JSON config structure
struct ChunkerConfig {
    std::string project_name;
    std::string project_description;
    std::string root_dir;
    std::string indexed_root; // root path used when the DB was indexed (e.g., Z:/mcb)
    std::vector<std::string> paths_to_chunk;
};

static bool readFileToBuffer(const std::string& path, std::string& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    ifs.seekg(0, std::ios::end);
    std::streampos size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) ifs.read(&out[0], size);
    return true;
}

static bool parseConfig(const std::string& jsonPath, ChunkerConfig& cfg) {
    std::string buf;
    if (!readFileToBuffer(jsonPath, buf)) {
        std::cerr << "Failed to read config json: " << jsonPath << std::endl;
        return false;
    }
    yyjson_doc* doc = yyjson_read(buf.data(), buf.size(), 0);
    if (!doc) {
        std::cerr << "Failed to parse JSON config." << std::endl;
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        std::cerr << "Config root is not an object." << std::endl;
        yyjson_doc_free(doc);
        return false;
    }

    auto get_str = [&](const char* key, std::string& out) {
        if (yyjson_val* v = yyjson_obj_get(root, key)) {
            if (yyjson_is_str(v)) {
                const char* s = yyjson_get_str(v);
                if (s) out = s;
            }
        }
    };

    get_str("project_name", cfg.project_name);
    get_str("project_description", cfg.project_description);
    get_str("root_dir", cfg.root_dir);
    get_str("indexed_root", cfg.indexed_root);

    if (yyjson_val* arr = yyjson_obj_get(root, "paths_to_chunk")) {
        if (yyjson_is_arr(arr)) {
            size_t idx, max;
            yyjson_val* val;
            yyjson_arr_foreach(arr, idx, max, val) {
                if (yyjson_is_str(val)) {
                    const char* s = yyjson_get_str(val);
                    if (s) cfg.paths_to_chunk.emplace_back(s);
                }
            }
        }
    }

    yyjson_doc_free(doc);

    if (cfg.project_name.empty()) {
        std::cerr << "Config missing 'project_name'." << std::endl;
        return false;
    }
    return true;
}

// --- Path helpers (simple, portable) ---
static inline std::string normalizePath(std::string p) {
    // unify separators
    std::replace(p.begin(), p.end(), '\\', '/');
    // trim trailing '/'
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

static inline bool isAbsolutePath(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true; // POSIX or UNC-like
    // crude Windows drive check: "C:/" or "C:\\"
    return (p.size() > 1 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':');
}

static inline std::string joinPath(const std::string& base, const std::string& rel) {
    if (base.empty()) return rel;
    if (rel.empty()) return base;
    std::string a = normalizePath(base);
    std::string b = normalizePath(rel);
    if (isAbsolutePath(b)) return b; // already absolute
    if (!a.empty() && a.back() != '/') a.push_back('/');
    return a + b;
}

// Map a DB file path (indexed under indexed_root) to a local path under root_dir
static inline std::string mapDbPathToLocal(const std::string& dbPath,
                                           const std::string& indexedRootNorm,
                                           const std::string& localRootNorm) {
    std::string dbNorm = normalizePath(dbPath);
    std::string idx = normalizePath(indexedRootNorm);
    std::string loc = normalizePath(localRootNorm);
    if (!idx.empty()) {
        std::string idxSlash = idx;
        if (idxSlash.back() != '/') idxSlash.push_back('/');
        if (dbNorm == idx) {
            return loc;
        }
        if (dbNorm.size() > idxSlash.size() && dbNorm.rfind(idxSlash, 0) == 0) {
            std::string rel = dbNorm.substr(idxSlash.size());
            if (!loc.empty() && loc.back() != '/') return loc + "/" + rel;
            return loc + rel;
        }
    }
    return dbNorm; // fallback to the DB path if it doesn't lie under indexedRoot
}

int main(int argc, const char* argv[]) {
    // Expect: <db> <config.json>
    if (argc < 3) {
        std::cout << "SourcetrailDB Code Chunker" << std::endl;
        std::cout << "==========================" << std::endl;
        std::cout << "Usage:\n  code_chunker <database_path> <config_json_path>\n";
        return 1;
    }

    const std::string dbPath = argv[1];
    const std::string jsonPath = argv[2];

    ChunkerConfig cfg;
    if (!parseConfig(jsonPath, cfg)) return 1;

    std::cout << "Project: " << cfg.project_name << std::endl;
    if (!cfg.project_description.empty()) {
        std::cout << "Description: " << cfg.project_description << std::endl;
    }

    sourcetrail::SourcetrailDBReader reader;
    std::cout << "Opening database: " << dbPath << std::endl;
    if (!reader.open(dbPath)) {
        std::cerr << "Error opening database: " << reader.getLastError() << std::endl;
        return 1;
    }

    try {
        auto t0 = std::chrono::steady_clock::now();

        std::cout << "Loading files from database..." << std::endl;
        auto files = reader.getAllFiles();
        std::cout << "Loaded " << files.size() << " files from database." << std::endl;
        // print a random selection of files we loaded
        const size_t maxFilesToShow = 5;
        std::random_device rd;
        std::mt19937 gen(rd());
        for (size_t i = 0; i < std::min(files.size(), maxFilesToShow); ++i) {
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
        for (const auto& p : cfg.paths_to_chunk) {
            if (p.empty()) continue;
            std::string entry = normalizePath(p);
            std::string dbPref;
            // Special handling: if entry starts with '/' (POSIX absolute) but our DB was
            // indexed under a Windows-like drive root, interpret it as relative to indexed_root.
            bool entryIsPosixAbs = (!entry.empty() && (entry[0] == '/'));
            bool indexedIsWinDrive = (!indexedNorm.empty() && indexedNorm.size() > 1 && std::isalpha(static_cast<unsigned char>(indexedNorm[0])) && indexedNorm[1] == ':');
            if (isAbsolutePath(entry) && !(entryIsPosixAbs && indexedIsWinDrive)) {
                // If absolute and starts with local root, translate local->DB
                std::string rootSlash = rootNorm;
                if (!rootSlash.empty() && rootSlash.back() != '/') rootSlash.push_back('/');
                if (!rootNorm.empty() && (entry == rootNorm || (entry.size() > rootSlash.size() && entry.rfind(rootSlash, 0) == 0))) {
                    // Compute relative to local root, then join with indexed root
                    std::string rel = (entry == rootNorm) ? std::string() : entry.substr(rootSlash.size());
                    dbPref = indexedNorm.empty() ? entry : joinPath(indexedNorm, rel);
                } else {
                    // Already absolute: assume it's in DB space
                    dbPref = entry;
                }
            } else {
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
    if (!includePrefixesDB.empty()) {
            for (const auto& f : files) {
                std::string fpath = normalizePath(f.filePath);
        for (const auto& pref : includePrefixesDB) {
                    if (fpath == pref) { selectedFiles.push_back(f); break; }
                    // directory prefix match: pref + "/"
                    if (!pref.empty()) {
                        std::string prefSlash = pref;
                        if (prefSlash.back() != '/') prefSlash.push_back('/');
                        if (fpath.size() > prefSlash.size() && fpath.rfind(prefSlash, 0) == 0) {
                            selectedFiles.push_back(f);
                            break;
                        }
                    }
                }
            }
        std::cout << "Filtered files to " << selectedFiles.size() << " based on paths_to_chunk (from " << files.size() << ")." << std::endl;
        } else {
            selectedFiles = files; // include all when no filter provided
        }

    std::cout << "Loading symbols and edges from database..." << std::endl;
    // Load symbols and edges into memory
    auto symbols = reader.getAllSymbols();
    std::cout << "Loaded " << symbols.size() << " symbols from database." << std::endl;
        auto edges = reader.getAllEdgesBrief();
        std::cout << "Loaded " << edges.size() << " edges from database." << std::endl;
        // Build adjacency lists for traversals
        int maxId = 0;
        for (const auto& e : edges) {
            if (e.sourceSymbolId > maxId) maxId = e.sourceSymbolId;
            if (e.targetSymbolId > maxId) maxId = e.targetSymbolId;
        }
        for (const auto& s : symbols) {
            if (s.id > maxId) maxId = s.id;
        }
        std::vector<std::vector<std::pair<int,int>>> outgoingAdj(maxId + 1);
        std::vector<std::vector<std::pair<int,int>>> incomingAdj(maxId + 1);
        for (const auto& e : edges) {
            if (e.sourceSymbolId >= 0 && e.sourceSymbolId <= maxId)
                outgoingAdj[e.sourceSymbolId].emplace_back(e.targetSymbolId, static_cast<int>(e.edgeKind));
            if (e.targetSymbolId >= 0 && e.targetSymbolId <= maxId)
                incomingAdj[e.targetSymbolId].emplace_back(e.sourceSymbolId, static_cast<int>(e.edgeKind));
        }
        std::cout << "Built adjacency for " << (maxId + 1) << " symbol ID slots." << std::endl;

        // Filter symbols to only those that appear in the selected files using a targeted DB query
        std::vector<sourcetrail::SourcetrailDBReader::Symbol> symbolsToVisit;
        if (!selectedFiles.empty() && !includePrefixesDB.empty()) {
            std::vector<int> fileIds;
            fileIds.reserve(selectedFiles.size());
            for (const auto& f : selectedFiles) fileIds.push_back(f.id);
            symbolsToVisit = reader.getSymbolsInFiles(fileIds);
            std::cout << "Filtered symbols to " << symbolsToVisit.size() << " based on selected files (DB query)." << std::endl;
        } else {
            // When no file filter is provided, visit all symbols
            symbolsToVisit = symbols;
            std::cout << "No file filter provided; using all symbols (" << symbolsToVisit.size() << ")." << std::endl;
        }

        // Also load the entire source_location table into memory via per-file fetch
        // (reader API exposes per-file and per-symbol; we gather per-file for coverage)
        std::vector<sourcetrail::SourcetrailDBReader::SourceLocation> allSrcLocs;
        size_t totalLocs = 0;
        allSrcLocs.reserve(1024);
        for (const auto& f : selectedFiles) {
            auto locs = reader.getSourceLocationsInFile(f.id);
            totalLocs += locs.size();
            allSrcLocs.insert(allSrcLocs.end(), locs.begin(), locs.end());
        }

        auto t1 = std::chrono::steady_clock::now();
        std::chrono::duration<double> dt = t1 - t0;

        std::cout << "Loaded: "
                  << symbols.size() << " symbols, "
                  << edges.size() << " edges, "
                  << files.size() << " files, "
                  << totalLocs << " source locations in "
                  << dt.count() << "s" << std::endl;

        // Build quick lookup maps for later chunking implementation
        int maxSymbolId = 0;
        for (const auto& s : symbols) if (s.id > maxSymbolId) maxSymbolId = s.id;

        // ID -> symbol index
        std::vector<int> symIndexById(maxSymbolId + 1, -1);
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i].id >= 0 && symbols[i].id <= maxSymbolId) symIndexById[symbols[i].id] = static_cast<int>(i);
        }

        // FileId -> indices of source locations
        std::unordered_map<int, std::vector<size_t>> srcLocsByFile;
        srcLocsByFile.reserve(files.size() * 2);
        for (size_t i = 0; i < allSrcLocs.size(); ++i) {
            srcLocsByFile[allSrcLocs[i].fileId].push_back(i);
        }

        // Placeholder: iterate requested paths to chunk (no-op for now)
        if (!cfg.paths_to_chunk.empty()) {
            std::cout << "Paths to chunk (" << cfg.paths_to_chunk.size() << "):\n";
            for (const auto& p : cfg.paths_to_chunk) std::cout << "  - " << p << std::endl;
        }

        // Show a few mapped file paths (DB -> local) using indexed_root/root_dir
        if (!selectedFiles.empty() && !cfg.indexed_root.empty() && !cfg.root_dir.empty()) {
            std::cout << "Sample path remapping (DB -> local):" << std::endl;
            size_t show = std::min<size_t>(5, selectedFiles.size());
            for (size_t i = 0; i < show; ++i) {
                const auto& f = selectedFiles[i];
                std::string mapped = mapDbPathToLocal(f.filePath, indexedNorm, rootNorm);
                std::cout << "  " << normalizePath(f.filePath) << " -> " << mapped << std::endl;
            }
        }

        reader.close();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        reader.close();
        return 1;
    }
}
