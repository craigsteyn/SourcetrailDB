#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <unordered_map>

#include "SourcetrailDBReader.h"

#define LOG 0

// Utility to print usage information (only findtests supported)
void printUsage()
{
    std::cout << "SourcetrailDB Symbol Dependency Analyzer" << std::endl;
    std::cout << "=======================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  dependency_analyzer <database_path> findtests <symbol_kind|*> <symbol> <test_namespace> [ignore_list]" << std::endl;
    std::cout << std::endl;
    std::cout << "Description:" << std::endl;
    std::cout << "  Find test classes in the given namespace that depend directly or indirectly on the symbol." << std::endl;
    std::cout << "  <symbol_kind> matches SymbolKind enum (e.g. CLASS, METHOD) or * for any." << std::endl;
    std::cout << "  Optional [ignore_list] is a comma-separated list of names/FQNs to prune traversal." << std::endl;
}

// Minimal helpers kept for logging in findtests

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

// ...removed helpers for other commands to keep this tool focused on findtests...

int main(int argc, const char* argv[])
{
    // Expect at least: <db> findtests <kind|*> <symbol> <test_namespace> [ignore_list]
    if (argc < 6)
    {
        printUsage();
        return 1;
    }

    std::string dbPath = argv[1];
    std::string command = argv[2];

    if (command != "findtests")
    {
        std::cerr << "Unknown command: " << command << " (only 'findtests' is supported)" << std::endl;
        printUsage();
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
        // Only 'findtests' path remains
        {
            if (argc < 6)
            {
                std::cerr << "Error: findtests requires <symbol_kind|*> <symbol_name> <test_namespace_pattern> [ignore_list]" << std::endl;
                return 1;
            }
            std::string symbolKindFilterStr = argv[3]; // may be *
            std::string symbolPattern = argv[4];
            std::string testNamespace = argv[5]; // e.g. "UnitTests" or "My::UnitTests"

            // Optional ignore list (comma separated). If provided as last argument, we assume it's the ignore list.
            std::set<std::string> ignoreSet; // store raw entries
            if (argc >= 7)
            {
                std::string ignoreArg = argv[6];
                size_t start = 0;
                while (start <= ignoreArg.size())
                {
                    size_t pos = ignoreArg.find(',', start);
                    std::string token = (pos == std::string::npos) ? ignoreArg.substr(start) : ignoreArg.substr(start, pos - start);
                    // trim whitespace
                    auto ltrim = [](std::string &s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){ return !std::isspace(c); })); };
                    auto rtrim = [](std::string &s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){ return !std::isspace(c); }).base(), s.end()); };
                    ltrim(token); rtrim(token);
                    if (!token.empty()) ignoreSet.insert(token);
                    if (pos == std::string::npos) break;
                    start = pos + 1;
                }
                if (!ignoreSet.empty())
                {
                    std::cout << "[findtests] Ignore list (" << ignoreSet.size() << "):" << std::endl;
                    for (const auto &entry : ignoreSet) std::cout << "  - " << entry << std::endl;
                }
            }

            bool applyKindFilter = symbolKindFilterStr != "*";
            sourcetrail::SymbolKind kindFilterValue = sourcetrail::SymbolKind::CLASS; // default init
            if (applyKindFilter)
            {
                if (!parseSymbolKind(symbolKindFilterStr, kindFilterValue))
                {
                    std::cerr << "Error: Unknown symbol kind '" << symbolKindFilterStr << "'. Use values from SymbolKind enum or *." << std::endl;
                    return 1;
                }
            }

            // Support both unqualified and qualified symbol lookup. If the pattern contains
            // a scope operator we try qualified lookup first. Otherwise use name lookup.
            std::vector<sourcetrail::SourcetrailDBReader::Symbol> startSymbols;
            if (symbolPattern.find("::") != std::string::npos) {
                startSymbols = reader.findSymbolsByQualifiedName(symbolPattern, true);
                if (startSymbols.empty()) {
                    // fallback: try tail element unqualified
                    auto pos = symbolPattern.rfind("::");
                    if (pos != std::string::npos && pos+2 < symbolPattern.size()) {
                        auto tail = symbolPattern.substr(pos+2);
                        auto tailSyms = reader.findSymbolsByName(tail, true);
                        startSymbols.insert(startSymbols.end(), tailSyms.begin(), tailSyms.end());
                    }
                }
            } else {
                startSymbols = reader.findSymbolsByName(symbolPattern, true);
            }

            // De-duplicate startSymbols by id (possible if both lookups returned same symbols)
            {
                std::set<int> seen;
                std::vector<sourcetrail::SourcetrailDBReader::Symbol> unique;
                unique.reserve(startSymbols.size());
                for (auto &s : startSymbols)
                {
                    if (seen.insert(s.id).second) unique.push_back(s);
                }
                startSymbols.swap(unique);
            }
            if (applyKindFilter)
            {
                std::vector<sourcetrail::SourcetrailDBReader::Symbol> filtered;
                filtered.reserve(startSymbols.size());
                for (auto &s : startSymbols)
                {
                    if (s.symbolKind == kindFilterValue) filtered.push_back(s);
                }
                startSymbols.swap(filtered);
            }
            if (startSymbols.empty())
            {
                std::cerr << "No starting symbols found for pattern: " << symbolPattern;
                if (applyKindFilter) std::cerr << " with kind filter '" << symbolKindFilterStr << "'";
                std::cerr << std::endl;
                return 1;
            }

            // Diagnostics: list starting symbols with full qualified names.
            std::cout << "Resolved starting symbols (" << startSymbols.size() << ")";
            if (applyKindFilter) std::cout << " filtered by kind '" << symbolKindFilterStr << "'";
            std::cout << ":" << std::endl;
            for (auto &s : startSymbols) {
                std::string fqn;
                for (size_t i=0;i<s.nameHierarchy.nameElements.size();++i) {
                    if (i) fqn += s.nameHierarchy.nameDelimiter;
                    fqn += s.nameHierarchy.nameElements[i].name;
                }
                std::cout << "  ID=" << s.id << "  FQN=" << fqn << "  Kind=" << (int)s.symbolKind << std::endl;
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
            // Strategy: Walk incoming references (who depends on the current symbol) because
            // we want classes/tests that use the implementation under test. We stop when we reach
            // symbols whose fully qualified hierarchy starts with the given namespace delimiter.
            // Queue element used during BFS. parentIndex points to the index inside
            // the queue vector of the node from which this node was first reached.
            // For initial start symbols parentIndex = -1.
            struct QueueItem { int symbolId; int depth; int parentIndex; };
            std::set<int> visited;
            std::vector<std::pair<int,std::string>> foundTestSymbols; // (id, fqn)
            std::set<int> foundTestSymbolsSet; // uniqueness by id
            std::set<std::string> foundTestFqnsSet; // uniqueness by fqn
            std::vector<QueueItem> queue;
            queue.reserve(1024);
            for (auto& s : startSymbols) { queue.push_back({s.id,0,-1}); visited.insert(s.id); }

            auto inNamespace = [&testNamespace](const sourcetrail::SourcetrailDBReader::Symbol& sym) -> bool {
                // Check if symbol is within the test namespace (but not the namespace itself)
                // Look for testNamespace as a parent element, not the final element
                if (sym.nameHierarchy.nameElements.size() > 1) {
                    for (size_t i = 0; i < sym.nameHierarchy.nameElements.size() - 1; ++i) {
                        if (sym.nameHierarchy.nameElements[i].name == testNamespace) {
                            return true;
                        }
                    }
                }
                return false;
            };

            size_t head = 0; // manual queue for performance
            const size_t BFS_LIMIT = 100000000; // safety cap
            std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
            std::cout << "[findtests] BFS start. pattern='" << symbolPattern << "' testNamespace='" << testNamespace << "'";
            if (applyKindFilter) std::cout << " kind='" << symbolKindFilterStr << "'";
            std::cout << ". Initial queue size=" << queue.size() << " limit=" << BFS_LIMIT << std::endl;
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
                // If in ignore set, prune expansion (do not enqueue its incoming references)
                if (!ignoreSet.empty())
                {
                    auto eachNameElementIgnored = [&]() -> bool {
                        for (const auto& nameEle : sym->nameHierarchy.nameElements) {
                            if (ignoreSet.count(nameEle.name)) return true;
                        }
                        return false;
                    };
                    bool isIgnored = false;
                    if ((!fqnSym.empty() && eachNameElementIgnored()) || ignoreSet.count(fqnSym))
                        isIgnored = true;
                    else if (!sym->nameHierarchy.nameElements.empty())
                    {
                        const std::string &simple = sym->nameHierarchy.nameElements.back().name;
                        if (ignoreSet.count(simple)) isIgnored = true;
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
                auto processEdge = [&](int neighborId, sourcetrail::EdgeKind edgeKind){
                    int nextId = neighborId;
                    const auto* srcSym = getSymById(nextId);
                    switch (kindFilterValue) {
                        case sourcetrail::SymbolKind::CLASS:
                            // No special filtering for class mode (keep behavior consistent)
                            break;
                        case sourcetrail::SymbolKind::METHOD:
                            if (edgeKind == sourcetrail::EdgeKind::MEMBER || edgeKind == sourcetrail::EdgeKind::TYPE_USAGE) {
                                // skip structure edges when focusing on methods
                                return; 
                            }
                            break;
                        default:
                            break;
                    }
                    bool inserted = visited.insert(nextId).second;
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
                        queue.push_back({nextId, item.depth+1, currentIndex});
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

            std::cout << "Traversal explored " << visited.size() << " symbols. Found " << foundTestSymbols.size() << " candidate test symbols." << std::endl;
            for (auto &entry : foundTestSymbols) {
                std::cout << "  Test: " << entry.second << " (ID:" << entry.first << ")" << std::endl;
            }
            if (queue.size() >= BFS_LIMIT) {
                std::cerr << "Warning: BFS limit reached (" << BFS_LIMIT << ") results may be incomplete." << std::endl;
            }
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
