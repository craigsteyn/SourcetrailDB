/*
 * Copyright 2018 Coati Software KG
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SourcetrailDBReader.h"

#include <iostream>
#include <algorithm>

#include "DatabaseStorage.h"
#include "version.h"

namespace sourcetrail
{

// Helper function to extract a readable name from the serialized format
std::string extractReadableName(const std::string& serializedName)
{
    // Simple approach: extract sequences of alphanumeric characters and underscores
    // that are likely to be meaningful names
    std::vector<std::string> nameComponents;
    std::string current;
    
    for (char c : serializedName)
    {
        if (std::isalnum(c) || c == '_')
        {
            current += c;
        }
        else
        {
            if (!current.empty() && current.length() > 1)
            {
                // Only add components that are longer than 1 character and meaningful
                if (current != "cpp" && current != "void" && current.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos)
                {
                    nameComponents.push_back(current);
                }
            }
            current.clear();
        }
    }
    
    // Add the last component if any
    if (!current.empty() && current.length() > 1)
    {
        if (current != "cpp" && current != "void" && current.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos)
        {
            nameComponents.push_back(current);
        }
    }
    
    // Join meaningful components with ::
    std::string result;
    for (size_t i = 0; i < nameComponents.size(); ++i)
    {
        if (i > 0) result += "::";
        result += nameComponents[i];
    }
    
    return result.empty() ? serializedName : result;
}

SourcetrailDBReader::SourcetrailDBReader()
{
}

SourcetrailDBReader::~SourcetrailDBReader()
{
    close();
}

std::string SourcetrailDBReader::getVersionString() const
{
    return VERSION_STRING;
}

int SourcetrailDBReader::getSupportedDatabaseVersion() const
{
    return DatabaseStorage::getSupportedDatabaseVersion();
}

const std::string& SourcetrailDBReader::getLastError() const
{
    return m_lastError;
}

bool SourcetrailDBReader::open(const std::string& databaseFilePath)
{
    clearLastError();

    try
    {
        m_databaseStorage = DatabaseStorage::openDatabase(databaseFilePath);
        if (!m_databaseStorage)
        {
            setLastError("Failed to open database");
            return false;
        }

        if (!m_databaseStorage->isCompatible())
        {
            setLastError("Database version is not compatible with this SourcetrailDB version");
            return false;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while opening database: ") + e.what());
        return false;
    }
}

bool SourcetrailDBReader::close()
{
    clearLastError();

    try
    {
        m_databaseStorage.reset();
        return true;
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while closing database: ") + e.what());
        return false;
    }
}

bool SourcetrailDBReader::isOpen() const
{
    return m_databaseStorage != nullptr;
}

std::vector<SourcetrailDBReader::Symbol> SourcetrailDBReader::getAllSymbols() const
{
    std::vector<Symbol> symbols;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return symbols;
    }

    try
    {
        // Get all nodes (symbols) from the database
        std::vector<StorageNode> storageNodes = m_databaseStorage->getAll<StorageNode>();
        
        for (const auto& storageNode : storageNodes)
        {
            Symbol symbol;
            symbol.id = storageNode.id;
            
            // Extract a readable name from the serialized format
            std::string readableName = extractReadableName(storageNode.serializedName);
            
            symbol.nameHierarchy.nameDelimiter = "::";
            NameElement element;
            element.name = readableName;
            symbol.nameHierarchy.nameElements.push_back(element);
            
            symbol.symbolKind = static_cast<SymbolKind>(storageNode.nodeKind);
            symbol.definitionKind = DefinitionKind::EXPLICIT; // Default, would need to be queried separately
            
            // TODO: Get source locations for this symbol
            // symbol.locations = getSourceLocationsForSymbol(symbol.id);
            
            symbols.push_back(symbol);
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting symbols: ") + e.what());
    }

    return symbols;
}

SourcetrailDBReader::Symbol SourcetrailDBReader::getSymbolById(int symbolId) const
{
    Symbol symbol;
    symbol.id = 0; // Initialize as invalid
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return symbol;
    }

    try
    {
        // This would need to be implemented in DatabaseStorage or we need direct SQL access
        // For now, we'll search through all symbols
        auto symbols = getAllSymbols();
        auto it = std::find_if(symbols.begin(), symbols.end(), 
                              [symbolId](const Symbol& s) { return s.id == symbolId; });
        
        if (it != symbols.end())
        {
            symbol = *it;
        }
        else
        {
            setLastError("Symbol with ID " + std::to_string(symbolId) + " not found");
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting symbol by ID: ") + e.what());
    }

    return symbol;
}

std::vector<SourcetrailDBReader::Symbol> SourcetrailDBReader::findSymbolsByName(const std::string& name, bool exactMatch) const
{
    std::vector<Symbol> matchingSymbols;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return matchingSymbols;
    }

    try
    {
        auto allSymbols = getAllSymbols();
        
        for (const auto& symbol : allSymbols)
        {
            // Extract the actual name from the NameHierarchy
            std::string symbolName;
            if (!symbol.nameHierarchy.nameElements.empty())
            {
                symbolName = symbol.nameHierarchy.nameElements.back().name;
            }
            
            bool matches = false;
            if (exactMatch)
            {
                matches = (symbolName == name);
            }
            else
            {
                matches = (symbolName.find(name) != std::string::npos);
            }
            
            if (matches)
            {
                matchingSymbols.push_back(symbol);
            }
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while searching symbols by name: ") + e.what());
    }

    return matchingSymbols;
}

std::vector<SourcetrailDBReader::Reference> SourcetrailDBReader::getAllReferences() const
{
    std::vector<Reference> references;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return references;
    }

    try
    {
        // Get all edges (references) from the database
        std::vector<StorageEdge> storageEdges = m_databaseStorage->getAll<StorageEdge>();
        
        for (const auto& storageEdge : storageEdges)
        {
            Reference reference;
            reference.id = storageEdge.id;
            reference.sourceSymbolId = storageEdge.sourceNodeId;
            reference.targetSymbolId = storageEdge.targetNodeId;
            reference.referenceKind = static_cast<ReferenceKind>(storageEdge.edgeKind);
            
            // TODO: Get source locations for this reference
            // reference.locations = getSourceLocationsForReference(reference.id);
            
            references.push_back(reference);
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting references: ") + e.what());
    }

    return references;
}

std::vector<SourcetrailDBReader::Reference> SourcetrailDBReader::getReferencesToSymbol(int symbolId) const
{
    std::vector<Reference> references;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return references;
    }

    try
    {
        auto allReferences = getAllReferences();
        
        for (const auto& reference : allReferences)
        {
            if (reference.targetSymbolId == symbolId)
            {
                references.push_back(reference);
            }
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting references to symbol: ") + e.what());
    }

    return references;
}

std::vector<SourcetrailDBReader::Reference> SourcetrailDBReader::getReferencesFromSymbol(int symbolId) const
{
    std::vector<Reference> references;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return references;
    }

    try
    {
        auto allReferences = getAllReferences();
        
        for (const auto& reference : allReferences)
        {
            if (reference.sourceSymbolId == symbolId)
            {
                references.push_back(reference);
            }
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting references from symbol: ") + e.what());
    }

    return references;
}

std::vector<SourcetrailDBReader::Reference> SourcetrailDBReader::getReferencesByType(ReferenceKind referenceKind) const
{
    std::vector<Reference> references;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return references;
    }

    try
    {
        auto allReferences = getAllReferences();
        
        for (const auto& reference : allReferences)
        {
            if (reference.referenceKind == referenceKind)
            {
                references.push_back(reference);
            }
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting references by type: ") + e.what());
    }

    return references;
}

std::vector<SourcetrailDBReader::File> SourcetrailDBReader::getAllFiles() const
{
    std::vector<File> files;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return files;
    }

    try
    {
        // Get all files from the database
        std::vector<StorageFile> storageFiles = m_databaseStorage->getAll<StorageFile>();
        
        for (const auto& storageFile : storageFiles)
        {
            File file;
            file.id = storageFile.id;
            file.filePath = storageFile.filePath;
            file.language = storageFile.languageIdentifier;
            file.indexed = storageFile.indexed;
            file.complete = storageFile.complete;
            
            files.push_back(file);
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting files: ") + e.what());
    }

    return files;
}

SourcetrailDBReader::File SourcetrailDBReader::getFileById(int fileId) const
{
    File file;
    file.id = 0; // Initialize as invalid
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return file;
    }

    try
    {
        auto files = getAllFiles();
        auto it = std::find_if(files.begin(), files.end(), 
                              [fileId](const File& f) { return f.id == fileId; });
        
        if (it != files.end())
        {
            file = *it;
        }
        else
        {
            setLastError("File with ID " + std::to_string(fileId) + " not found");
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting file by ID: ") + e.what());
    }

    return file;
}

std::vector<SourcetrailDBReader::File> SourcetrailDBReader::findFilesByPath(const std::string& path, bool exactMatch) const
{
    std::vector<File> matchingFiles;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return matchingFiles;
    }

    try
    {
        auto allFiles = getAllFiles();
        
        for (const auto& file : allFiles)
        {
            bool matches = false;
            if (exactMatch)
            {
                matches = (file.filePath == path);
            }
            else
            {
                matches = (file.filePath.find(path) != std::string::npos);
            }
            
            if (matches)
            {
                matchingFiles.push_back(file);
            }
        }
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while searching files by path: ") + e.what());
    }

    return matchingFiles;
}

std::vector<SourcetrailDBReader::SourceLocation> SourcetrailDBReader::getSourceLocationsForSymbol(int symbolId) const
{
    std::vector<SourceLocation> locations;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return locations;
    }

    try
    {
        // This would need to be implemented by querying the source_location and occurrence tables
        // For now, return empty vector
        setLastError("getSourceLocationsForSymbol not yet fully implemented");
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting source locations for symbol: ") + e.what());
    }

    return locations;
}

std::vector<SourcetrailDBReader::SourceLocation> SourcetrailDBReader::getSourceLocationsInFile(int fileId) const
{
    std::vector<SourceLocation> locations;
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return locations;
    }

    try
    {
        // This would need to be implemented by querying the source_location table with file_id filter
        // For now, return empty vector
        setLastError("getSourceLocationsInFile not yet fully implemented");
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting source locations in file: ") + e.what());
    }

    return locations;
}

std::string SourcetrailDBReader::getDatabaseStats() const
{
    clearLastError();

    if (!isOpen())
    {
        setLastError("Database is not open");
        return "";
    }

    try
    {
        auto symbols = getAllSymbols();
        auto references = getAllReferences();
        auto files = getAllFiles();

        std::string stats;
        stats += "Database Statistics:\n";
        stats += "  Symbols: " + std::to_string(symbols.size()) + "\n";
        stats += "  References: " + std::to_string(references.size()) + "\n";
        stats += "  Files: " + std::to_string(files.size()) + "\n";
        stats += "  Database Version: " + std::to_string(getSupportedDatabaseVersion()) + "\n";

        return stats;
    }
    catch (const std::exception& e)
    {
        setLastError(std::string("Exception while getting database stats: ") + e.what());
        return "";
    }
}

void SourcetrailDBReader::setLastError(const std::string& error) const
{
    m_lastError = error;
}

void SourcetrailDBReader::clearLastError() const
{
    m_lastError.clear();
}

} // namespace sourcetrail
