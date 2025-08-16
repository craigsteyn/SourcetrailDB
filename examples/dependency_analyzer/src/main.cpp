#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

#include "SourcetrailDBReader.h"

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

// Get reference kind name
std::string getReferenceKindName(sourcetrail::ReferenceKind kind)
{
    switch (kind)
    {
        case sourcetrail::ReferenceKind::TYPE_USAGE: return "Type Usage";
        case sourcetrail::ReferenceKind::USAGE: return "Usage";
        case sourcetrail::ReferenceKind::CALL: return "Call";
        case sourcetrail::ReferenceKind::INHERITANCE: return "Inheritance";
        case sourcetrail::ReferenceKind::OVERRIDE: return "Override";
        case sourcetrail::ReferenceKind::TYPE_ARGUMENT: return "Type Argument";
        case sourcetrail::ReferenceKind::TEMPLATE_SPECIALIZATION: return "Template Specialization";
        case sourcetrail::ReferenceKind::INCLUDE: return "Include";
        case sourcetrail::ReferenceKind::IMPORT: return "Import";
        case sourcetrail::ReferenceKind::MACRO_USAGE: return "Macro Usage";
        case sourcetrail::ReferenceKind::ANNOTATION_USAGE: return "Annotation Usage";
        default: return "Unknown(" + std::to_string(static_cast<int>(kind)) + ")";
    }
}

// Show dependencies of a symbol (what it references)
void showDependencies(sourcetrail::SourcetrailDBReader& reader, const std::string& symbolName)
{
    auto symbols = reader.findSymbolsByName(symbolName, false);
    
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
            
            std::map<sourcetrail::ReferenceKind, std::vector<int>> refsByKind;
            for (const auto& ref : references)
            {
                refsByKind[ref.referenceKind].push_back(ref.targetSymbolId);
            }
            
            for (auto it = refsByKind.begin(); it != refsByKind.end(); ++it)
            {
                sourcetrail::ReferenceKind kind = it->first;
                const std::vector<int>& targetIds = it->second;
                
                std::cout << "    " << getReferenceKindName(kind) << ":" << std::endl;
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
    auto symbols = reader.findSymbolsByName(symbolName, false);
    
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
            
            std::map<sourcetrail::ReferenceKind, std::vector<int>> refsByKind;
            for (const auto& ref : references)
            {
                refsByKind[ref.referenceKind].push_back(ref.sourceSymbolId);
            }
            
            for (auto it = refsByKind.begin(); it != refsByKind.end(); ++it)
            {
                sourcetrail::ReferenceKind kind = it->first;
                const std::vector<int>& sourceIds = it->second;
                
                std::cout << "    " << getReferenceKindName(kind) << ":" << std::endl;
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
                          << " --[" << getReferenceKindName(ref.referenceKind) << "]--> " 
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
                          << " --[" << getReferenceKindName(ref.referenceKind) << "]--> " 
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
    std::map<sourcetrail::ReferenceKind, int> refCounts;
    
    for (const auto& ref : references)
    {
        refCounts[ref.referenceKind]++;
    }
    
    std::cout << std::endl << "References by kind:" << std::endl;
    for (auto it = refCounts.begin(); it != refCounts.end(); ++it)
    {
        sourcetrail::ReferenceKind kind = it->first;
        int count = it->second;
        std::cout << "  " << getReferenceKindName(kind) << ": " << count << std::endl;
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
