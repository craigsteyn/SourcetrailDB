#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <unordered_map>
#include <fstream>
#include <sstream>

#include "SourcetrailDBReader.h"

#define LOG 0

// Utility to print usage information
void printUsage()
{
    std::cout << "SourcetrailDB Symbol Dependency Analyzer" << std::endl;
    std::cout << "=======================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  dependency_analyzer <database_path> <config_file_path>" << std::endl;
    std::cout << std::endl;
    std::cout << "Description:" << std::endl;
    std::cout << "  Reads configuration from the given file to find test classes depending on configured symbols." << std::endl;
    std::cout << "  Config format (sections):" << std::endl;
    std::cout << "    [test_namespace]\n    MyTestNamespace" << std::endl;
    std::cout << "    [start_symbols]\n    kind=METHOD, MyNS::MyClass::myMethod\n    kind=*, MyNS::MyClass" << std::endl;
    std::cout << "    [exclude_symbols]\n    NameToIgnore\n    MyNS::Other" << std::endl;
}

// Minimal helpers kept for logging and parsing

// Get symbol kind name
std::string getSymbolKindName(sourcetrail::SymbolKind kind)
{
    switch (kind)
    {
        case sourcetrail::SymbolKind::TYPE: return "Type";
        case sourcetrail::SymbolKind::BUILTIN_TYPE: return "Builtin Type";
        case sourcetrail::SymbolKind::MODULE: return "Module";
        case sourcetrail::SymbolKind::NAMESPACE: return "Namespace";
        case sourcetrail::SymbolKind::PACKAGE: return "Package";
        case sourcetrail::SymbolKind::STRUCT: return "Struct";
        case sourcetrail::SymbolKind::CLASS: return "Class";
        case sourcetrail::SymbolKind::INTERFACE: return "Interface";
        case sourcetrail::SymbolKind::ANNOTATION: return "Annotation";
        case sourcetrail::SymbolKind::GLOBAL_VARIABLE: return "Global Variable";
        case sourcetrail::SymbolKind::FIELD: return "Field";
        case sourcetrail::SymbolKind::FUNCTION: return "Function";
        case sourcetrail::SymbolKind::METHOD: return "Method";
        case sourcetrail::SymbolKind::ENUM: return "Enum";
        case sourcetrail::SymbolKind::ENUM_CONSTANT: return "Enum Constant";
        case sourcetrail::SymbolKind::TYPEDEF: return "Typedef";
        case sourcetrail::SymbolKind::TYPE_PARAMETER: return "Type Parameter";
        case sourcetrail::SymbolKind::MACRO: return "Macro";
        case sourcetrail::SymbolKind::UNION: return "Union";
        default: return "Unknown(" + std::to_string(static_cast<int>(kind)) + ")";
    }
}

// Get edge kind name
std::string getEdgeKindName(sourcetrail::EdgeKind kind)
{
    switch (kind)
    {
        case sourcetrail::EdgeKind::MEMBER: return "Member";
        case sourcetrail::EdgeKind::TYPE_USAGE: return "Type Usage";
        case sourcetrail::EdgeKind::USAGE: return "Usage";
        case sourcetrail::EdgeKind::CALL: return "Call";
        case sourcetrail::EdgeKind::INHERITANCE: return "Inheritance";
        case sourcetrail::EdgeKind::OVERRIDE: return "Override";
        case sourcetrail::EdgeKind::TYPE_ARGUMENT: return "Type Argument";
        case sourcetrail::EdgeKind::TEMPLATE_SPECIALIZATION: return "Template Specialization";
        case sourcetrail::EdgeKind::INCLUDE: return "Include";
        case sourcetrail::EdgeKind::IMPORT: return "Import";
        case sourcetrail::EdgeKind::MACRO_USAGE: return "Macro Usage";
        case sourcetrail::EdgeKind::ANNOTATION_USAGE: return "Annotation Usage";
        default: return "Unknown(" + std::to_string(static_cast<int>(kind)) + ")";
    }
}

// Parse a string into SymbolKind. Returns true on success. Accepts exact enum identifiers (case-insensitive).
bool parseSymbolKind(const std::string& in, sourcetrail::SymbolKind& out)
{
    std::string s;
    s.reserve(in.size());
    for (char c : in)
    {
        s.push_back(static_cast<char>(::toupper(static_cast<unsigned char>(c))));
    }
    // Map string to enum
    if (s == "TYPE") out = sourcetrail::SymbolKind::TYPE;
    else if (s == "BUILTIN_TYPE") out = sourcetrail::SymbolKind::BUILTIN_TYPE;
    else if (s == "MODULE") out = sourcetrail::SymbolKind::MODULE;
    else if (s == "NAMESPACE") out = sourcetrail::SymbolKind::NAMESPACE;
    else if (s == "PACKAGE") out = sourcetrail::SymbolKind::PACKAGE;
    else if (s == "STRUCT") out = sourcetrail::SymbolKind::STRUCT;
    else if (s == "CLASS") out = sourcetrail::SymbolKind::CLASS;
    else if (s == "INTERFACE") out = sourcetrail::SymbolKind::INTERFACE;
    else if (s == "ANNOTATION") out = sourcetrail::SymbolKind::ANNOTATION;
    else if (s == "GLOBAL_VARIABLE") out = sourcetrail::SymbolKind::GLOBAL_VARIABLE;
    else if (s == "FIELD") out = sourcetrail::SymbolKind::FIELD;
    else if (s == "FUNCTION") out = sourcetrail::SymbolKind::FUNCTION;
    else if (s == "METHOD") out = sourcetrail::SymbolKind::METHOD;
    else if (s == "ENUM") out = sourcetrail::SymbolKind::ENUM;
    else if (s == "ENUM_CONSTANT") out = sourcetrail::SymbolKind::ENUM_CONSTANT;
    else if (s == "TYPEDEF") out = sourcetrail::SymbolKind::TYPEDEF;
    else if (s == "TYPE_PARAMETER") out = sourcetrail::SymbolKind::TYPE_PARAMETER;
    else if (s == "MACRO") out = sourcetrail::SymbolKind::MACRO;
    else if (s == "UNION") out = sourcetrail::SymbolKind::UNION;
    else return false;
    return true;
}

// Helpers
static inline std::string trimCopy(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e-1]))) --e;
    return s.substr(b, e - b);
}

struct StartSymbolSpec {
    bool anyKind = true;
    sourcetrail::SymbolKind kind = sourcetrail::SymbolKind::CLASS; // default init
    std::string pattern; // name or qualified name
};

struct ConfigData {
    std::string testNamespace;
    std::vector<StartSymbolSpec> startSymbols;
    std::set<std::string> excludeSymbols; // entries to ignore during traversal
};

static bool parseConfigFile(const std::string& path, ConfigData& outCfg)
{
    std::ifstream in(path.c_str());
    if (!in.is_open()) return false;

    enum Section { NONE, TEST_NAMESPACE, START_SYMBOLS, EXCLUDE_SYMBOLS };
    Section section = NONE;

    std::string line;
    while (std::getline(in, line))
    {
        // Remove potential carriage return and trim
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trimCopy(line);
        if (t.empty()) continue;
        if (t[0] == '#' || t[0] == ';') continue; // comment
        if (t.size() >= 3 && t.front() == '[' && t.back() == ']')
        {
            const std::string sec = t.substr(1, t.size()-2);
            if (sec == "test_namespace") section = TEST_NAMESPACE;
            else if (sec == "start_symbols") section = START_SYMBOLS;
            else if (sec == "exclude_symbols") section = EXCLUDE_SYMBOLS;
            else section = NONE;
            continue;
        }

        switch(section)
        {
            case TEST_NAMESPACE:
                if (outCfg.testNamespace.empty())
                {
                    outCfg.testNamespace = t;
                }
                break;
            case START_SYMBOLS:
            {
                // format: kind=<KIND|*>, <Pattern>
                // tolerant parsing
                std::string kindPart;
                std::string patternPart;
                size_t commaPos = t.find(',');
                if (commaPos == std::string::npos)
                {
                    // allow just a pattern -> implies kind='*'
                    patternPart = t;
                }
                else
                {
                    kindPart = trimCopy(t.substr(0, commaPos));
                    patternPart = trimCopy(t.substr(commaPos + 1));
                }

                StartSymbolSpec spec;
                spec.anyKind = true;
                if (!kindPart.empty())
                {
                    // Expect prefix kind=
                    const std::string key = "kind=";
                    std::string lower;
                    lower.reserve(kindPart.size());
                    for (char c : kindPart) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                    size_t pos = lower.find(key);
                    std::string rhs = pos == std::string::npos ? kindPart : trimCopy(kindPart.substr(pos + key.size()));
                    if (!rhs.empty())
                    {
                        std::string rhsTrim = trimCopy(rhs);
                        if (rhsTrim != "*")
                        {
                            sourcetrail::SymbolKind parsed;
                            if (parseSymbolKind(rhsTrim, parsed))
                            {
                                spec.anyKind = false;
                                spec.kind = parsed;
                            }
                            else
                            {
                                std::cerr << "[config] Warning: unknown kind '" << rhsTrim << "' in line: " << line << std::endl;
                            }
                        }
                        else
                        {
                            spec.anyKind = true;
                        }
                    }
                }
                spec.pattern = patternPart;
                if (!spec.pattern.empty())
                {
                    outCfg.startSymbols.push_back(spec);
                }
                break;
            }
            case EXCLUDE_SYMBOLS:
                outCfg.excludeSymbols.insert(t);
                break;
            default:
                break;
        }
    }
    return true;
}

int main(int argc, const char* argv[])
{
    // Expect: <db> <config_file>
    if (argc < 3)
    {
        printUsage();
        return 1;
    }

    std::string dbPath = argv[1];
    std::string configPath = argv[2];

    ConfigData cfg;
    if (!parseConfigFile(configPath, cfg))
    {
        std::cerr << "Error: Failed to read config file: " << configPath << std::endl;
        return 1;
    }
    if (cfg.testNamespace.empty())
    {
        std::cerr << "Error: [test_namespace] section missing or empty in config file." << std::endl;
        return 1;
    }
    if (cfg.startSymbols.empty())
    {
        std::cerr << "Error: [start_symbols] section missing or contains no entries in config file." << std::endl;
        return 1;
    }

    sourcetrail::SourcetrailDBReader reader;

    std::cout << "Opening database: " << dbPath << std::endl;
    if (!reader.open(dbPath))
    {
        std::cerr << "Error opening database: " << reader.getLastError() << std::endl;
        return 1;
    }

    try
    {
        // Resolve all configured start symbols into concrete DB symbols
        std::vector<sourcetrail::SourcetrailDBReader::Symbol> allStartSymbols; // across all specs
        std::vector<int> startSymbolMode; // parallel vector: -1 for ANY, else static_cast<int>(SymbolKind)
        for (const auto& spec : cfg.startSymbols)
        {
            std::vector<sourcetrail::SourcetrailDBReader::Symbol> startSymbolsForSpec;
            if (spec.pattern.find("::") != std::string::npos) {
                startSymbolsForSpec = reader.findSymbolsByQualifiedName(spec.pattern, true);
                if (startSymbolsForSpec.empty()) {
                    auto pos = spec.pattern.rfind("::");
                    if (pos != std::string::npos && pos+2 < spec.pattern.size()) {
                        auto tail = spec.pattern.substr(pos+2);
                        auto tailSyms = reader.findSymbolsByName(tail, true);
                        startSymbolsForSpec.insert(startSymbolsForSpec.end(), tailSyms.begin(), tailSyms.end());
                    }
                }
            } else {
                startSymbolsForSpec = reader.findSymbolsByName(spec.pattern, true);
            }
            // Filter by kind if requested
            if (!spec.anyKind)
            {
                std::vector<sourcetrail::SourcetrailDBReader::Symbol> filtered;
                filtered.reserve(startSymbolsForSpec.size());
                for (auto &s : startSymbolsForSpec)
                {
                    if (s.symbolKind == spec.kind) filtered.push_back(s);
                }
                startSymbolsForSpec.swap(filtered);
            }
            // De-duplicate by id for this spec
            {
                std::set<int> seen;
                std::vector<sourcetrail::SourcetrailDBReader::Symbol> unique;
                unique.reserve(startSymbolsForSpec.size());
                for (auto &s : startSymbolsForSpec)
                {
                    if (seen.insert(s.id).second) unique.push_back(s);
                }
                startSymbolsForSpec.swap(unique);
            }
            for (auto &s : startSymbolsForSpec)
            {
                allStartSymbols.push_back(s);
                startSymbolMode.push_back(spec.anyKind ? -1 : static_cast<int>(spec.kind));
            }
        }
        if (allStartSymbols.empty())
        {
            std::cerr << "No starting symbols found from config patterns." << std::endl;
            return 1;
        }

        // Diagnostics: list starting symbols with full qualified names and mode
        std::cout << "Resolved starting symbols (" << allStartSymbols.size() << "):" << std::endl;
        for (size_t idx = 0; idx < allStartSymbols.size(); ++idx) {
            auto &s = allStartSymbols[idx];
            std::string fqn;
            for (size_t i=0;i<s.nameHierarchy.nameElements.size();++i) {
                if (i) fqn += s.nameHierarchy.nameDelimiter;
                fqn += s.nameHierarchy.nameElements[i].name;
            }
            std::cout << "  ID=" << s.id << "  FQN=" << fqn << "  Kind=" << (int)s.symbolKind
                      << "  Mode=" << (startSymbolMode[idx] < 0 ? std::string("*") : getSymbolKindName(static_cast<sourcetrail::SymbolKind>(startSymbolMode[idx])))
                      << std::endl;
        }

        // Load full symbols and compact edges into memory for fast traversal (avoid per-node lookups)
        auto allSymbols = reader.getAllSymbols();
        auto briefEdges = reader.getAllEdgesBrief();
        if (!reader.getLastError().empty()) {
            std::cerr << "Warning: reader reported: " << reader.getLastError() << std::endl;
        }
        int maxId = 0;
        for (const auto& e : briefEdges) {
            if (e.sourceSymbolId > maxId) maxId = e.sourceSymbolId;
            if (e.targetSymbolId > maxId) maxId = e.targetSymbolId;
        }
        for (const auto& s : allSymbols) { if (s.id > maxId) maxId = s.id; }
        // Fast ID -> Symbol lookup table (id 0 means invalid)
        std::vector<sourcetrail::SourcetrailDBReader::Symbol> symbolById(maxId + 1);
        for (const auto& s : allSymbols) {
            if (s.id >= 0 && s.id <= maxId) symbolById[s.id] = s;
        }
        auto getSymById = [&](int id) -> const sourcetrail::SourcetrailDBReader::Symbol* {
            if (id >= 0 && id <= maxId) {
                const auto& s = symbolById[id];
                if (s.id != 0) return &s;
            }
            return nullptr;
        };
        // Build FQN string for each symbol and an index FQN -> ids
        auto buildFqnFromSymbol = [](const sourcetrail::SourcetrailDBReader::Symbol& s) {
            std::string fqn;
            for (size_t i=0;i<s.nameHierarchy.nameElements.size();++i) {
                if (i) fqn += s.nameHierarchy.nameDelimiter;
                fqn += s.nameHierarchy.nameElements[i].name;
            }
            return fqn;
        };
        std::vector<std::string> fqnById(maxId + 1);
        std::unordered_map<std::string, std::vector<int>> fqnToIds;
        fqnToIds.reserve(allSymbols.size()*2);
        for (const auto& s : allSymbols) {
            if (s.id < 0 || s.id > maxId) continue;
            auto fqn = buildFqnFromSymbol(s);
            fqnById[s.id] = fqn;
            if (!fqn.empty()) fqnToIds[fqn].push_back(s.id);
        }
        // Build adjacency lists: incoming (by target) and outgoing (by source)
        std::vector<std::vector<std::pair<int,int>>> incomingAdj(maxId + 1);
        std::vector<std::vector<std::pair<int,int>>> outgoingAdj(maxId + 1);
        for (const auto& e : briefEdges) {
            if (e.sourceSymbolId >= 0 && e.sourceSymbolId <= maxId) {
                outgoingAdj[e.sourceSymbolId].emplace_back(e.targetSymbolId, static_cast<int>(e.edgeKind));
            }
            if (e.targetSymbolId >= 0 && e.targetSymbolId <= maxId) {
                // For incoming adjacency, neighbor is the source symbol
                incomingAdj[e.targetSymbolId].emplace_back(e.sourceSymbolId, static_cast<int>(e.edgeKind));
            }
        }

        // Close the reader now; traversal uses in-memory structures only
        reader.close();

        // BFS traversal collecting reachable test classes in namespace
        struct QueueItem { int symbolId; int depth; int parentIndex; int modeKind; }; // modeKind: -1 for any, else SymbolKind value
        std::set<std::pair<int,int>> visited; // (symbolId, modeKind)
        std::vector<std::pair<int,std::string>> foundTestSymbols; // (id, fqn)
        std::set<int> foundTestSymbolsSet; // uniqueness by id
        std::set<std::string> foundTestFqnsSet; // uniqueness by fqn
        std::vector<QueueItem> queue;
        queue.reserve(1024);
        // seed queue
        for (size_t i=0;i<allStartSymbols.size();++i) {
            int id = allStartSymbols[i].id;
            int mode = startSymbolMode[i];
            queue.push_back({id, 0, -1, mode});
            visited.insert(std::make_pair(id, mode));
        }

        auto inNamespace = [&cfg](const sourcetrail::SourcetrailDBReader::Symbol& sym) -> bool {
            // Check if symbol is within the test namespace (but not the namespace itself)
            // Look for testNamespace as a parent element, not the final element
            if (sym.nameHierarchy.nameElements.size() > 1) {
                for (size_t i = 0; i < sym.nameHierarchy.nameElements.size() - 1; ++i) {
                    if (sym.nameHierarchy.nameElements[i].name == cfg.testNamespace) {
                        return true;
                    }
                }
            }
            return false;
        };

        size_t head = 0; // manual queue for performance
        const size_t BFS_LIMIT = 100000000; // safety cap
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
        std::cout << "[findtests] BFS start. start_symbols=" << queue.size() << " testNamespace='" << cfg.testNamespace << "' limit=" << BFS_LIMIT << std::endl;
        while (head < queue.size() && queue.size() < BFS_LIMIT)
        {
            int currentIndex = static_cast<int>(head); // index BEFORE increment will be head
            QueueItem item = queue[head++];
            const auto* sym = getSymById(item.symbolId);
            if (!sym) continue;
            // Build FQN for logging
            std::string fqnSym;
            for (size_t i=0;i<sym->nameHierarchy.nameElements.size();++i) {
                if (i) fqnSym += sym->nameHierarchy.nameDelimiter;
                fqnSym += sym->nameHierarchy.nameElements[i].name;
            }
            // If in exclude set, prune expansion (do not enqueue its incoming references)
            if (!cfg.excludeSymbols.empty())
            {
                auto eachNameElementIgnored = [&]() -> bool {
                    for (const auto& nameEle : sym->nameHierarchy.nameElements) {
                        if (cfg.excludeSymbols.count(nameEle.name)) return true;
                    }
                    return false;
                };
                bool isIgnored = false;
                if ((!fqnSym.empty() && eachNameElementIgnored()) || cfg.excludeSymbols.count(fqnSym))
                    isIgnored = true;
                else if (!sym->nameHierarchy.nameElements.empty())
                {
                    const std::string &simple = sym->nameHierarchy.nameElements.back().name;
                    if (cfg.excludeSymbols.count(simple)) isIgnored = true;
                }
                if (isIgnored)
                {
                    std::cout << "[findtests]   Pruned (ignored) symbol id=" << sym->id << " fqn=" << fqnSym << std::endl;
                    continue; // skip detection & expansion
                }
            }
            // Gather incoming references from in-memory adjacency and count outgoing OVERRIDE edges
            const std::vector<std::pair<int,int>>* inEdgesPtr = (sym->id >= 0 && sym->id <= maxId) ? &incomingAdj[sym->id] : nullptr;
            const std::vector<std::pair<int,int>>* outEdgesPtr = (sym->id >= 0 && sym->id <= maxId) ? &outgoingAdj[sym->id] : nullptr;
            size_t overrideOutCount = 0;
            if (outEdgesPtr) {
                for (const auto& e : *outEdgesPtr) {
                    if (static_cast<sourcetrail::EdgeKind>(e.second) == sourcetrail::EdgeKind::OVERRIDE) ++overrideOutCount;
                }
            }
#if LOG
            std::cout << "[findtests] Pop depth=" << item.depth
                      << " id=" << sym->id
                      << " kind=" << (int)sym->symbolKind
                      << " fqn=" << fqnSym
                      << " incoming_refs=" << ((inEdgesPtr?inEdgesPtr->size():0) + overrideOutCount)
                      << " visited=" << visited.size()
                      << " queue_remaining=" << (queue.size() - head)
                      << std::endl;
#endif
            if (inNamespace(*sym))
            {
                auto hasFqn = [&](const std::string& fqn) {
                    return foundTestFqnsSet.find(fqn) != foundTestFqnsSet.end();
                };
                // Helper to construct FQN from symbol id (cached via local lambda)
                auto buildFqnFromId = [&](int sid) -> std::string {
                    const auto* sObj = getSymById(sid);
                    if (!sObj) return std::string();
                    return fqnById[sid];
                };
                // Build path (list of symbol ids) from a queue index up to root.
                auto buildPathChain = [&](int idx) -> std::vector<int> {
                    std::vector<int> chain;
                    while (idx >= 0) {
                        chain.push_back(queue[idx].symbolId);
                        idx = queue[idx].parentIndex;
                    }
                    std::reverse(chain.begin(), chain.end());
                    return chain;
                };
                auto ensureAddTestClass = [&](int classId, const std::string& fqn, const std::vector<int>& extraPathIds = {}){
                    if (classId <= 0) return;
                    bool idInserted = foundTestSymbolsSet.insert(classId).second;
                    bool fqnInserted = foundTestFqnsSet.insert(fqn).second;
                    if (idInserted && fqnInserted) {
                        foundTestSymbols.emplace_back(classId, fqn);
                        // Reconstruct path from one of the starting symbols to this test class.
                        auto pathIds = buildPathChain(currentIndex);
                        // In method->class promotion scenario we append extra path ids (e.g. parent class id)
                        for (int pid : extraPathIds) pathIds.push_back(pid);
                        // Ensure last element is the classId (in case of direct detection it already is)
                        if (pathIds.empty() || pathIds.back() != classId) pathIds.push_back(classId);
                        std::cout << "[findtests]   Added test class id=" << classId << " fqn=" << fqn << std::endl;
                        std::cout << "[findtests]     Path: ";
                        bool first = true;
                        for (int sid : pathIds) {
                            if (!first) std::cout << " -> ";
                            first = false;
                            auto f = buildFqnFromId(sid);
                            if (f.empty()) std::cout << sid; else std::cout << f;
                        }
                        std::cout << std::endl;
                    }
                };
                // Helper to check if name looks like test class name
                auto isTestClassName = [](const std::string& n){
                    if (n.size() >= 4 && n.compare(n.size()-4, 4, "Test") == 0) return true;
                    if (n.size() >= 5 && n.compare(n.size()-5, 5, "Tests") == 0) return true;
                    return false;
                };

                if (!sym->nameHierarchy.nameElements.empty())
                {
                    const std::string last = sym->nameHierarchy.nameElements.back().name;
                    // Direct class/struct detection
                    if ((sym->symbolKind == sourcetrail::SymbolKind::CLASS || sym->symbolKind == sourcetrail::SymbolKind::STRUCT) && isTestClassName(last))
                    {
                        ensureAddTestClass(sym->id, fqnSym);
                    }
                    // Method inside a test class: ascend to parent element
                    else if (sym->symbolKind == sourcetrail::SymbolKind::METHOD && sym->nameHierarchy.nameElements.size() >= 2)
                    {
                        const std::string parentName = sym->nameHierarchy.nameElements[sym->nameHierarchy.nameElements.size()-2].name;
                        if (isTestClassName(parentName))
                        {
                            // Build parent FQN
                            std::string parentFqn;
                            for (size_t i=0;i<sym->nameHierarchy.nameElements.size()-1;++i) {
                                if (i) parentFqn += sym->nameHierarchy.nameDelimiter;
                                parentFqn += sym->nameHierarchy.nameElements[i].name;
                            }
                            if(!hasFqn(parentFqn)) {
                                auto it = fqnToIds.find(parentFqn);
                                if (it != fqnToIds.end()) {
                                    for (int pid : it->second) {
                                        const auto* ps = getSymById(pid);
                                        if (ps && (ps->symbolKind == sourcetrail::SymbolKind::CLASS || ps->symbolKind == sourcetrail::SymbolKind::STRUCT))
                                            ensureAddTestClass(ps->id, parentFqn, {ps->id});
                                    }
                                }
                            }
                            else
                            {
                                // In this case. we already indexed the test class so we can skip chasing further
                                continue;
                            }
                        }
                    }
                }
            }
            // Expand incoming references (who uses this symbol) and also outgoing OVERRIDE edges
            size_t enqueuedThisNode = 0;
            int modeKindLocal = item.modeKind; // capture mode for this branch
            auto processEdge = [&](int neighborId, sourcetrail::EdgeKind edgeKind){
                int nextId = neighborId;
                const auto* srcSym = getSymById(nextId);
                if (modeKindLocal == static_cast<int>(sourcetrail::SymbolKind::METHOD)) {
                    if (edgeKind == sourcetrail::EdgeKind::MEMBER || edgeKind == sourcetrail::EdgeKind::TYPE_USAGE) {
                        // skip structure edges when focusing on methods
                        return; 
                    }
                }
                bool inserted = visited.insert(std::make_pair(nextId, modeKindLocal)).second;
#if 1 // detailed per-reference debug (toggle to 0 to silence)
                // Build FQN of source symbol (caller / user)
                std::string srcFqn;
                if (srcSym && srcSym->id) {
                    for (size_t i = 0; i < srcSym->nameHierarchy.nameElements.size(); ++i) {
                        if (i) srcFqn += srcSym->nameHierarchy.nameDelimiter;
                        srcFqn += srcSym->nameHierarchy.nameElements[i].name;
                    }
                }
                std::cout << "[findtests]     Incoming ref: "
                          << (srcFqn.empty() ? std::string("<anon:"+std::to_string(nextId)+">") : srcFqn)
                          << " --[" << getEdgeKindName(edgeKind) << "]--> "
                          << (fqnSym.empty() ? std::string("<anon:"+std::to_string(sym->id)+">") : fqnSym)
                          << " srcKind=" << (srcSym && srcSym->id ? getSymbolKindName(srcSym->symbolKind) : std::string("<missing>"))
                          << " action=" << (inserted ? "ENQUEUE" : "SKIP_ALREADY_VISITED")
                          << std::endl;
#endif
                if (inserted) {
                    queue.push_back({nextId, item.depth+1, currentIndex, modeKindLocal});
                    ++enqueuedThisNode;
                }
            };
            // Process true incoming edges (neighbors are sources)
            if (inEdgesPtr) {
                for (const auto& ref : *inEdgesPtr) {
                    processEdge(ref.first, static_cast<sourcetrail::EdgeKind>(ref.second));
                }
            }
            // Additionally process outgoing OVERRIDE edges as if incoming
            if (outEdgesPtr) {
                for (const auto& ref : *outEdgesPtr) {
                    auto kind = static_cast<sourcetrail::EdgeKind>(ref.second);
                    if (kind == sourcetrail::EdgeKind::OVERRIDE) {
                        processEdge(ref.first, kind);
                    }
                }
            }
#if 1
            if (enqueuedThisNode) {
                std::cout << "[findtests]   Enqueued " << enqueuedThisNode << " new symbols. Queue size now=" << queue.size() - head << std::endl;
            }
#endif
        }
        std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> duration = endTime - startTime;
        std::cout << "[findtests] BFS duration: " << duration.count() << " seconds." << std::endl;
        std::cout << "[findtests] BFS done. Total visited=" << visited.size() << " queue_final=" << queue.size() << std::endl;

        std::cout << "Traversal explored " << visited.size() << " symbol/mode states. Found " << foundTestSymbols.size() << " candidate test symbols." << std::endl;
        for (auto &entry : foundTestSymbols) {
            std::cout << "  Test: " << entry.second << " (ID:" << entry.first << ")" << std::endl;
        }
        if (queue.size() >= BFS_LIMIT) {
            std::cerr << "Warning: BFS limit reached (" << BFS_LIMIT << ") results may be incomplete." << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    reader.close();
    return 0;
}
