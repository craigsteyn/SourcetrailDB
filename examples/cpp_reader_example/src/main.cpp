#include <iostream>
#include <string>
#include <vector>

#include "SourcetrailDBReader.h"

void printUsage()
{
    std::cout << "Usage: cpp_reader_example <database_path> [symbol_name]" << std::endl;
    std::cout << "  database_path: Path to the .srctrldb file" << std::endl;
    std::cout << "  symbol_name: Optional symbol name to search for" << std::endl;
}

void printSymbolInfo(const sourcetrail::SourcetrailDBReader::Symbol& symbol)
{
    std::cout << "Symbol ID: " << symbol.id << std::endl;
    
    // Extract and display the symbol name
    std::string displayName = "Unknown";
    if (!symbol.nameHierarchy.nameElements.empty())
    {
        // Build the full hierarchical name
        std::string fullName;
        for (size_t i = 0; i < symbol.nameHierarchy.nameElements.size(); ++i)
        {
            if (i > 0) fullName += symbol.nameHierarchy.nameDelimiter;
            const auto& element = symbol.nameHierarchy.nameElements[i];
            fullName += element.prefix + element.name + element.postfix;
        }
        displayName = fullName;
    }
    
    std::cout << "  Name: " << displayName << std::endl;
    std::cout << "  Symbol Kind: " << static_cast<int>(symbol.symbolKind) << std::endl;
    std::cout << "  Definition Kind: " << static_cast<int>(symbol.definitionKind) << std::endl;
    std::cout << "  Locations: " << symbol.locations.size() << std::endl;
    std::cout << std::endl;
}

void printReferenceInfo(const sourcetrail::SourcetrailDBReader::Reference& reference)
{
    std::cout << "Reference ID: " << reference.id << std::endl;
    std::cout << "  From Symbol: " << reference.sourceSymbolId << std::endl;
    std::cout << "  To Symbol: " << reference.targetSymbolId << std::endl;
    std::cout << "  Reference Kind: " << static_cast<int>(reference.referenceKind) << std::endl;
    std::cout << "  Locations: " << reference.locations.size() << std::endl;
    std::cout << std::endl;
}

void printFileInfo(const sourcetrail::SourcetrailDBReader::File& file)
{
    std::cout << "File ID: " << file.id << std::endl;
    std::cout << "  Path: " << file.filePath << std::endl;
    std::cout << "  Language: " << file.language << std::endl;
    std::cout << "  Indexed: " << (file.indexed ? "Yes" : "No") << std::endl;
    std::cout << "  Complete: " << (file.complete ? "Yes" : "No") << std::endl;
    std::cout << std::endl;
}

int main(int argc, const char* argv[])
{
    std::cout << "\nSourcetrailDB C++ Reader Example" << std::endl;
    std::cout << "================================" << std::endl;

    if (argc < 2 || argc > 3)
    {
        printUsage();
        return 1;
    }

    std::string dbPath = argv[1];
    std::string searchSymbol;
    if (argc == 3)
    {
        searchSymbol = argv[2];
    }

    sourcetrail::SourcetrailDBReader reader;

    std::cout << "SourcetrailDB version: " << reader.getVersionString() << std::endl;
    std::cout << "Supported database version: " << reader.getSupportedDatabaseVersion() << std::endl;
    std::cout << std::endl;

    // Open the database
    std::cout << "Opening Database: " << dbPath << std::endl;
    if (!reader.open(dbPath))
    {
        std::cerr << "Error opening database: " << reader.getLastError() << std::endl;
        return 1;
    }

    // Print database statistics
    std::cout << std::endl;
    std::cout << reader.getDatabaseStats() << std::endl;

    // If a symbol name was provided, search for it
    if (!searchSymbol.empty())
    {
        std::cout << "Searching for symbols containing: '" << searchSymbol << "'" << std::endl;
        std::cout << "============================================" << std::endl;
        
        auto symbols = reader.findSymbolsByName(searchSymbol, false);
        std::cout << "Found " << symbols.size() << " matching symbols:" << std::endl;
        std::cout << std::endl;

        for (const auto& symbol : symbols)
        {
            printSymbolInfo(symbol);

            // Show references TO this symbol
            auto referencesToSymbol = reader.getReferencesToSymbol(symbol.id);
            if (!referencesToSymbol.empty())
            {
                std::cout << "  References TO this symbol (" << referencesToSymbol.size() << "):" << std::endl;
                for (const auto& ref : referencesToSymbol)
                {
                    std::cout << "    From Symbol ID: " << ref.sourceSymbolId 
                              << " (Kind: " << static_cast<int>(ref.referenceKind) << ")" << std::endl;
                }
                std::cout << std::endl;
            }

            // Show references FROM this symbol
            auto referencesFromSymbol = reader.getReferencesFromSymbol(symbol.id);
            if (!referencesFromSymbol.empty())
            {
                std::cout << "  References FROM this symbol (" << referencesFromSymbol.size() << "):" << std::endl;
                for (const auto& ref : referencesFromSymbol)
                {
                    std::cout << "    To Symbol ID: " << ref.targetSymbolId 
                              << " (Kind: " << static_cast<int>(ref.referenceKind) << ")" << std::endl;
                }
                std::cout << std::endl;
            }
        }
    }
    else
    {
        // Show overview of all data
        std::cout << "Database Overview:" << std::endl;
        std::cout << "==================" << std::endl;

        // Show all files
        auto files = reader.getAllFiles();
        std::cout << "Files (" << files.size() << "):" << std::endl;
        for (const auto& file : files)
        {
            printFileInfo(file);
        }

        // Show first 10 symbols
        auto symbols = reader.getAllSymbols();
        std::cout << "Symbols (showing first 10 of " << symbols.size() << "):" << std::endl;
        size_t count = 0;
        for (const auto& symbol : symbols)
        {
            if (count >= 10) break;
            printSymbolInfo(symbol);
            count++;
        }

        // Show first 10 references
        auto references = reader.getAllReferences();
        std::cout << "References (showing first 10 of " << references.size() << "):" << std::endl;
        count = 0;
        for (const auto& reference : references)
        {
            if (count >= 10) break;
            printReferenceInfo(reference);
            count++;
        }
    }

    reader.close();
    std::cout << "Done!" << std::endl;
    return 0;
}
