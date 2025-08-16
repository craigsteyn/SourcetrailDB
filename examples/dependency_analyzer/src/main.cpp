#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <chrono>

#include "SourcetrailDBReader.h"

#define LOG 0

// Utility to print usage information
void printUsage()
{
    std::cout << "SourcetrailDB Symbol Dependency Analyzer" << std::endl;
    std::cout << "=======================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: dependency_analyzer <database_path> <command> [symbol_name]" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  deps <symbol>    - Show all dependencies of a symbol" << std::endl;
    std::cout << "  refs <symbol>    - Show all references to a symbol" << std::endl;
    std::cout << "  graph <symbol>   - Show bidirectional dependency graph" << std::endl;
    std::cout << "  stats            - Show database statistics" << std::endl;
    std::cout << "  list             - List all symbols by kind" << std::endl;
    std::cout << "  findtests <symbol_kind|*> <symbol> <test_namespace> - Find test classes in namespace that depend (directly or indirectly) on the symbol. symbol_kind matches SymbolKind enum (e.g. CLASS) or * for any." << std::endl;
}

// Get a readable name from a symbol
std::string getSymbolName(const sourcetrail::SourcetrailDBReader::Symbol& symbol)
{
    if (!symbol.nameHierarchy.nameElements.empty())
    {
        return symbol.nameHierarchy.nameElements[0].name;
    }
    return "Unknown";
}

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

// Show dependencies of a symbol (what it references)
void showDependencies(sourcetrail::SourcetrailDBReader& reader, const std::string& symbolName)
{
    auto symbols = (symbolName.find("::") != std::string::npos) ? 
        reader.findSymbolsByQualifiedName(symbolName, true) : 
        reader.findSymbolsByName(symbolName, false);
    
    if (symbols.empty())
    {
        std::cout << "No symbols found matching: " << symbolName << std::endl;
        return;
    }
    
    std::cout << "Dependencies for symbols matching '" << symbolName << "':" << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    
    for (const auto& symbol : symbols)
    {
        std::cout << std::endl;
        std::cout << "Symbol: " << getSymbolName(symbol) 
                  << " (ID: " << symbol.id 
                  << ", Kind: " << getSymbolKindName(symbol.symbolKind) << ")" << std::endl;
        
        auto references = reader.getReferencesFromSymbol(symbol.id);
        
        if (references.empty())
        {
            std::cout << "  No dependencies found." << std::endl;
        }
        else
        {
            std::cout << "  Dependencies (" << references.size() << "):" << std::endl;
            
            std::map<sourcetrail::EdgeKind, std::vector<int>> refsByKind;
            for (const auto& ref : references)
            {
                refsByKind[ref.edgeKind].push_back(ref.targetSymbolId);
            }
            
            for (auto it = refsByKind.begin(); it != refsByKind.end(); ++it)
            {
                sourcetrail::EdgeKind kind = it->first;
                const std::vector<int>& targetIds = it->second;
                
                std::cout << "    " << getEdgeKindName(kind) << ":" << std::endl;
                for (int targetId : targetIds)
                {
                    auto targetSymbol = reader.getSymbolById(targetId);
                    std::cout << "      → " << getSymbolName(targetSymbol) 
                              << " (" << getSymbolKindName(targetSymbol.symbolKind) << ")" << std::endl;
                }
            }
        }
    }
}

// Show references to a symbol (what references it)
void showReferences(sourcetrail::SourcetrailDBReader& reader, const std::string& symbolName)
{
    auto symbols = (symbolName.find("::") != std::string::npos) ? 
        reader.findSymbolsByQualifiedName(symbolName, true) : 
        reader.findSymbolsByName(symbolName, false);
    
    if (symbols.empty())
    {
        std::cout << "No symbols found matching: " << symbolName << std::endl;
        return;
    }
    
    std::cout << "References to symbols matching '" << symbolName << "':" << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    
    for (const auto& symbol : symbols)
    {
        std::cout << std::endl;
        std::cout << "Symbol: " << getSymbolName(symbol) 
                  << " (ID: " << symbol.id 
                  << ", Kind: " << getSymbolKindName(symbol.symbolKind) << ")" << std::endl;
        
        auto references = reader.getReferencesToSymbol(symbol.id);
        
        if (references.empty())
        {
            std::cout << "  No references found." << std::endl;
        }
        else
        {
            std::cout << "  Referenced by (" << references.size() << "):" << std::endl;
            
            std::map<sourcetrail::EdgeKind, std::vector<int>> refsByKind;
            for (const auto& ref : references)
            {
                refsByKind[ref.edgeKind].push_back(ref.sourceSymbolId);
            }
            
            for (auto it = refsByKind.begin(); it != refsByKind.end(); ++it)
            {
                sourcetrail::EdgeKind kind = it->first;
                const std::vector<int>& sourceIds = it->second;
                
                std::cout << "    " << getEdgeKindName(kind) << ":" << std::endl;
                for (int sourceId : sourceIds)
                {
                    auto sourceSymbol = reader.getSymbolById(sourceId);
                    std::cout << "      ← " << getSymbolName(sourceSymbol) 
                              << " (" << getSymbolKindName(sourceSymbol.symbolKind) << ")" << std::endl;
                }
            }
        }
    }
}

// Show bidirectional dependency graph
void showGraph(sourcetrail::SourcetrailDBReader& reader, const std::string& symbolName)
{
    auto symbols = reader.findSymbolsByName(symbolName, false);
    
    if (symbols.empty())
    {
        std::cout << "No symbols found matching: " << symbolName << std::endl;
        return;
    }
    
    std::cout << "Dependency graph for symbols matching '" << symbolName << "':" << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    
    for (const auto& symbol : symbols)
    {
        std::cout << std::endl;
        std::cout << "Symbol: " << getSymbolName(symbol) 
                  << " (ID: " << symbol.id 
                  << ", Kind: " << getSymbolKindName(symbol.symbolKind) << ")" << std::endl;
        
        // Show references TO this symbol
        auto incomingRefs = reader.getReferencesToSymbol(symbol.id);
        if (!incomingRefs.empty())
        {
            std::cout << "  ↓ Referenced by:" << std::endl;
            for (const auto& ref : incomingRefs)
            {
                auto sourceSymbol = reader.getSymbolById(ref.sourceSymbolId);
                std::cout << "    " << getSymbolName(sourceSymbol) 
                          << " --[" << getEdgeKindName(ref.edgeKind) << "]--> " 
                          << getSymbolName(symbol) << std::endl;
            }
        }
        
        // Show references FROM this symbol  
        auto outgoingRefs = reader.getReferencesFromSymbol(symbol.id);
        if (!outgoingRefs.empty())
        {
            std::cout << "  ↓ Depends on:" << std::endl;
            for (const auto& ref : outgoingRefs)
            {
                auto targetSymbol = reader.getSymbolById(ref.targetSymbolId);
                std::cout << "    " << getSymbolName(symbol) 
                          << " --[" << getEdgeKindName(ref.edgeKind) << "]--> " 
                          << getSymbolName(targetSymbol) << std::endl;
            }
        }
    }
}

// Show statistics
void showStats(sourcetrail::SourcetrailDBReader& reader)
{
    std::cout << reader.getDatabaseStats() << std::endl;
    
    // Group symbols by kind
    auto symbols = reader.getAllSymbols();
    std::map<sourcetrail::SymbolKind, int> symbolCounts;
    
    for (const auto& symbol : symbols)
    {
        symbolCounts[symbol.symbolKind]++;
    }
    
    std::cout << "Symbols by kind:" << std::endl;
    for (auto it = symbolCounts.begin(); it != symbolCounts.end(); ++it)
    {
        sourcetrail::SymbolKind kind = it->first;
        int count = it->second;
        std::cout << "  " << getSymbolKindName(kind) << ": " << count << std::endl;
    }
    
    // Group references by kind
    auto references = reader.getAllReferences();
    std::map<sourcetrail::EdgeKind, int> refCounts;
    
    for (const auto& ref : references)
    {
    refCounts[ref.edgeKind]++;
    }
    
    std::cout << std::endl << "References by kind:" << std::endl;
    for (auto it = refCounts.begin(); it != refCounts.end(); ++it)
    {
    sourcetrail::EdgeKind kind = it->first;
        int count = it->second;
    std::cout << "  " << getEdgeKindName(kind) << ": " << count << std::endl;
    }
}

// List all symbols organized by kind
void listSymbols(sourcetrail::SourcetrailDBReader& reader)
{
    auto symbols = reader.getAllSymbols();
    
    // Group by kind
    std::map<sourcetrail::SymbolKind, std::vector<sourcetrail::SourcetrailDBReader::Symbol>> symbolsByKind;
    for (const auto& symbol : symbols)
    {
        symbolsByKind[symbol.symbolKind].push_back(symbol);
    }
    
    std::cout << "All symbols organized by kind:" << std::endl;
    std::cout << std::string(40, '=') << std::endl;
    
    for (auto it = symbolsByKind.begin(); it != symbolsByKind.end(); ++it)
    {
        sourcetrail::SymbolKind kind = it->first;
        const std::vector<sourcetrail::SourcetrailDBReader::Symbol>& symbolList = it->second;
        
        std::cout << std::endl << getSymbolKindName(kind) << " (" << symbolList.size() << "):" << std::endl;
        for (const auto& symbol : symbolList)
        {
            std::cout << "  " << getSymbolName(symbol) << " (ID: " << symbol.id << ")" << std::endl;
        }
    }
}

int main(int argc, const char* argv[])
{
    if (argc < 3)
    {
        printUsage();
        return 1;
    }
    
    std::string dbPath = argv[1];
    std::string command = argv[2];
    
    sourcetrail::SourcetrailDBReader reader;
    
    std::cout << "Opening database: " << dbPath << std::endl;
    if (!reader.open(dbPath))
    {
        std::cerr << "Error opening database: " << reader.getLastError() << std::endl;
        return 1;
    }
    
    try
    {
        if (command == "stats")
        {
            showStats(reader);
        }
        else if (command == "list")
        {
            listSymbols(reader);
        }
    else if (command == "deps" || command == "refs" || command == "graph")
        {
            if (argc < 4)
            {
                std::cerr << "Error: " << command << " command requires a symbol name" << std::endl;
                return 1;
            }
            
            std::string symbolName = argv[3];
            
            if (command == "deps")
            {
                showDependencies(reader, symbolName);
            }
            else if (command == "refs")
            {
                showReferences(reader, symbolName);
            }
            else if (command == "graph")
            {
                showGraph(reader, symbolName);
            }
        }
        else if (command == "findtests")
        {
            if (argc < 6)
            {
                std::cerr << "Error: findtests requires <symbol_kind|*> <symbol_name> <test_namespace_pattern>" << std::endl;
                return 1;
            }
            std::string symbolKindFilterStr = argv[3]; // may be *
            std::string symbolPattern = argv[4];
            std::string testNamespace = argv[5]; // e.g. "UnitTests" or "My::UnitTests"

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
            const size_t BFS_LIMIT = 100000; // safety cap
            std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
            std::cout << "[findtests] BFS start. pattern='" << symbolPattern << "' testNamespace='" << testNamespace << "'";
            if (applyKindFilter) std::cout << " kind='" << symbolKindFilterStr << "'";
            std::cout << ". Initial queue size=" << queue.size() << " limit=" << BFS_LIMIT << std::endl;
            while (head < queue.size() && queue.size() < BFS_LIMIT)
            {
                int currentIndex = static_cast<int>(head); // index BEFORE increment will be head
                QueueItem item = queue[head++];
                auto sym = reader.getSymbolById(item.symbolId);
                if (sym.id==0) continue;
                // Build FQN for logging
                std::string fqnSym;
                for (size_t i=0;i<sym.nameHierarchy.nameElements.size();++i) {
                    if (i) fqnSym += sym.nameHierarchy.nameDelimiter;
                    fqnSym += sym.nameHierarchy.nameElements[i].name;
                }
                // Count incoming references (for logging before expansion)
                auto incomingPreview = reader.getReferencesToSymbol(sym.id);
#if LOG
                std::cout << "[findtests] Pop depth=" << item.depth
                          << " id=" << sym.id
                          << " kind=" << (int)sym.symbolKind
                          << " fqn=" << fqnSym
                          << " incoming_refs=" << incomingPreview.size()
                          << " visited=" << visited.size()
                          << " queue_remaining=" << (queue.size() - head)
                          << std::endl;
#endif
                if (inNamespace(sym))
                {
                    auto hasFqn = [&](const std::string& fqn) {
                        return foundTestFqnsSet.find(fqn) != foundTestFqnsSet.end();
                    };
                    // Helper to construct FQN from symbol id (cached via local lambda)
                    auto buildFqnFromId = [&](int sid) -> std::string {
                        auto sObj = reader.getSymbolById(sid);
                        if (sObj.id == 0) return std::string();
                        std::string fqn;
                        for (size_t i=0;i<sObj.nameHierarchy.nameElements.size();++i) {
                            if (i) fqn += sObj.nameHierarchy.nameDelimiter;
                            fqn += sObj.nameHierarchy.nameElements[i].name;
                        }
                        return fqn;
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

                    if (!sym.nameHierarchy.nameElements.empty())
                    {
                        const std::string last = sym.nameHierarchy.nameElements.back().name;
                        // Direct class/struct detection
                        if ((sym.symbolKind == sourcetrail::SymbolKind::CLASS || sym.symbolKind == sourcetrail::SymbolKind::STRUCT) && isTestClassName(last))
                        {
                            ensureAddTestClass(sym.id, fqnSym);
                        }
                        // Method inside a test class: ascend to parent element
                        else if (sym.symbolKind == sourcetrail::SymbolKind::METHOD && sym.nameHierarchy.nameElements.size() >= 2)
                        {
                            const std::string parentName = sym.nameHierarchy.nameElements[sym.nameHierarchy.nameElements.size()-2].name;
                            if (isTestClassName(parentName))
                            {
                                // Build parent FQN
                                std::string parentFqn;
                                for (size_t i=0;i<sym.nameHierarchy.nameElements.size()-1;++i) {
                                    if (i) parentFqn += sym.nameHierarchy.nameDelimiter;
                                    parentFqn += sym.nameHierarchy.nameElements[i].name;
                                }
                                if(!hasFqn(parentFqn)) {
                                        
                                    auto parentSyms = reader.findSymbolsByQualifiedName(parentFqn, true);
                                    for (auto &ps : parentSyms) {
                                        if (ps.symbolKind == sourcetrail::SymbolKind::CLASS || ps.symbolKind == sourcetrail::SymbolKind::STRUCT)
                                            ensureAddTestClass(ps.id, parentFqn, {ps.id});
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
                // Expand incoming references (who uses this symbol)
                size_t enqueuedThisNode = 0;
                for (const auto& ref : incomingPreview) {
                    // we need to be more specific about which nodes we queue.
                    int nextId = ref.sourceSymbolId;
                    auto srcSym = reader.getSymbolById(nextId);
                    // this code is pretty gross. but the idea is. depending on our kind filtering we want to apply
                    // certain visiting rules to avoid exploding outwards to ALL TYPES
                    switch (kindFilterValue) {
                    case sourcetrail::SymbolKind::CLASS:
                        break;
                    case sourcetrail::SymbolKind::METHOD:
                        if( ref.edgeKind == sourcetrail::EdgeKind::MEMBER) {
                            // skip type usages, type usage references represent a class which owns the method.
                            continue; 
                        }
                        break;
                    default:
                        // no filter, enqueue all references
                        break;
                    }
                    bool inserted = visited.insert(nextId).second;
#if 1 // detailed per-reference debug (toggle to 0 to silence)
                    // Build FQN of source symbol (caller / user)
                    std::string srcFqn;
                    if (srcSym.id) {
                        for (size_t i = 0; i < srcSym.nameHierarchy.nameElements.size(); ++i) {
                            if (i) srcFqn += srcSym.nameHierarchy.nameDelimiter;
                            srcFqn += srcSym.nameHierarchy.nameElements[i].name;
                        }
                    }
                    std::cout << "[findtests]     Incoming ref: "
                              << (srcFqn.empty() ? std::string("<anon:"+std::to_string(nextId)+">") : srcFqn)
                              << " --[" << getEdgeKindName(ref.edgeKind) << "]--> "
                              << (fqnSym.empty() ? std::string("<anon:"+std::to_string(sym.id)+">") : fqnSym)
                              << " srcKind=" << (srcSym.id ? getSymbolKindName(srcSym.symbolKind) : std::string("<missing>"))
                              << " action=" << (inserted ? "ENQUEUE" : "SKIP_ALREADY_VISITED")
                              << std::endl;
#endif
                    if (inserted) {
                        queue.push_back({nextId, item.depth+1, currentIndex});
                        ++enqueuedThisNode;
                    }
                }
#if 1
                if (enqueuedThisNode) {
                    std::cout << "[findtests]   Enqueued " << enqueuedThisNode << " new symbols. Queue size now=" << queue.size() << std::endl;
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
        else
        {
            std::cerr << "Unknown command: " << command << std::endl;
            printUsage();
            return 1;
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
