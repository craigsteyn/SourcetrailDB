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

// Properly deserialize a serialized name hierarchy (mirrors logic from main Sourcetrail app)
static NameHierarchy parseSerializedNameHierarchy(const std::string& serializedName)
{
    static const std::string META_DELIMITER = "\tm";      // separates delimiter from first element
    static const std::string NAME_DELIMITER = "\tn";      // separates name elements
    static const std::string PART_DELIMITER = "\ts";      // separates name from prefix
    static const std::string SIGNATURE_DELIMITER = "\tp"; // separates prefix from postfix

    NameHierarchy hierarchy;

    // Find meta delimiter to extract the hierarchy delimiter (e.g. "::")
    size_t mpos = serializedName.find(META_DELIMITER);
    if (mpos == std::string::npos)
    {
        // Fallback: treat entire string as a single element
        hierarchy.nameDelimiter = "::";
        if (!serializedName.empty())
        {
            NameElement e;
            e.name = serializedName; 
            hierarchy.nameElements.push_back(std::move(e));
        }
        return hierarchy;
    }

    hierarchy.nameDelimiter = serializedName.substr(0, mpos);
    size_t cursor = mpos + META_DELIMITER.size();

    while (cursor < serializedName.size())
    {
        // name up to PART_DELIMITER
        size_t spos = serializedName.find(PART_DELIMITER, cursor);
        if (spos == std::string::npos) break; // malformed
        std::string name = serializedName.substr(cursor, spos - cursor);
        spos += PART_DELIMITER.size();

        // prefix up to SIGNATURE_DELIMITER
        size_t ppos = serializedName.find(SIGNATURE_DELIMITER, spos);
        if (ppos == std::string::npos) break; // malformed
        std::string prefix = serializedName.substr(spos, ppos - spos);
        ppos += SIGNATURE_DELIMITER.size();

        // postfix up to NAME_DELIMITER (or end)
        size_t npos = serializedName.find(NAME_DELIMITER, ppos);
        std::string postfix;
        if (npos == std::string::npos)
        {
            postfix = serializedName.substr(ppos);
            cursor = serializedName.size();
        }
        else
        {
            postfix = serializedName.substr(ppos, npos - ppos);
            cursor = npos + NAME_DELIMITER.size();
        }

        NameElement element;
        element.name = std::move(name);
        element.prefix = std::move(prefix);
        element.postfix = std::move(postfix);
        hierarchy.nameElements.push_back(std::move(element));
    }

    // Fallback: if nothing parsed, keep whole string
    if (hierarchy.nameElements.empty() && !serializedName.empty())
    {
        NameElement e; e.name = serializedName; hierarchy.nameElements.push_back(std::move(e));
    }

    return hierarchy;
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
    if (!isOpen()) { setLastError("Database is not open"); return symbols; }
    try {
        // targeted: only fetch nodes that are actually symbols via helper
        std::vector<StorageNode> storageNodes = m_databaseStorage->getAllSymbolNodes();
        for (const auto& n : storageNodes) {
            Symbol s; s.id = n.id; s.nameHierarchy = parseSerializedNameHierarchy(n.serializedName); s.symbolKind = static_cast<SymbolKind>(n.nodeKind);
            int defKind = m_databaseStorage->getDefinitionKindForSymbol(n.id); if (defKind >= 0) s.definitionKind = static_cast<DefinitionKind>(defKind); else s.definitionKind = DefinitionKind::EXPLICIT; symbols.push_back(std::move(s));
        }
    } catch (const std::exception& e) { setLastError(std::string("Exception while getting symbols: ") + e.what()); }
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

    try {
        StorageNode n = m_databaseStorage->getNodeById(symbolId);
        if (n.id != 0) {
            // ensure it's actually a symbol
            int defKind = m_databaseStorage->getDefinitionKindForSymbol(n.id);
            if (defKind >= 0) {
                symbol.id = n.id; symbol.nameHierarchy = parseSerializedNameHierarchy(n.serializedName); symbol.symbolKind = static_cast<SymbolKind>(n.nodeKind); symbol.definitionKind = static_cast<DefinitionKind>(defKind);
            } else { setLastError("Id " + std::to_string(symbolId) + " is not a symbol"); }
        } else { setLastError("Symbol with ID " + std::to_string(symbolId) + " not found"); }
    } catch (const std::exception& e) { setLastError(std::string("Exception while getting symbol by ID: ") + e.what()); }

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

    try {
        // The serialized_name column contains the full hierarchy encoding; for quick filtering we can pattern match.
        // We use LIKE with %name% as heuristic and post-filter exact element name when needed.
        std::string likePattern = "%" + name + "%";
        std::vector<StorageNode> candidateNodes = m_databaseStorage->findSymbolNodesBySerializedNameLike(likePattern);
        for (const auto& n : candidateNodes) {
            Symbol s; s.id = n.id; s.nameHierarchy = parseSerializedNameHierarchy(n.serializedName); s.symbolKind = static_cast<SymbolKind>(n.nodeKind); int defKind = m_databaseStorage->getDefinitionKindForSymbol(n.id); if (defKind >= 0) s.definitionKind = static_cast<DefinitionKind>(defKind); else s.definitionKind = DefinitionKind::EXPLICIT; 
            std::string finalName = s.nameHierarchy.nameElements.empty()? std::string() : s.nameHierarchy.nameElements.back().name;
            bool match = exactMatch ? (finalName == name) : (finalName.find(name) != std::string::npos);
            if (match) matchingSymbols.push_back(std::move(s));
        }
    } catch (const std::exception& e) { setLastError(std::string("Exception while searching symbols by name: ") + e.what()); }

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

    try {
        // All references (still may be large, future: add pagination). For now we keep previous behavior.
        std::vector<StorageEdge> storageEdges = m_databaseStorage->getAll<StorageEdge>();
        references.reserve(storageEdges.size());
        for (const auto& e : storageEdges) { Reference r; r.id = e.id; r.sourceSymbolId = e.sourceNodeId; r.targetSymbolId = e.targetNodeId; r.referenceKind = static_cast<ReferenceKind>(e.edgeKind); references.push_back(std::move(r)); }
    } catch (const std::exception& e) { setLastError(std::string("Exception while getting references: ") + e.what()); }

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

    try { auto edges = m_databaseStorage->getEdgesToNode(symbolId); for (const auto& e: edges) { Reference r; r.id = e.id; r.sourceSymbolId = e.sourceNodeId; r.targetSymbolId = e.targetNodeId; r.referenceKind = static_cast<ReferenceKind>(e.edgeKind); references.push_back(std::move(r)); } }
    catch (const std::exception& e) { setLastError(std::string("Exception while getting references to symbol: ") + e.what()); }

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

    try { auto edges = m_databaseStorage->getEdgesFromNode(symbolId); for (const auto& e: edges) { Reference r; r.id = e.id; r.sourceSymbolId = e.sourceNodeId; r.targetSymbolId = e.targetNodeId; r.referenceKind = static_cast<ReferenceKind>(e.edgeKind); references.push_back(std::move(r)); } }
    catch (const std::exception& e) { setLastError(std::string("Exception while getting references from symbol: ") + e.what()); }

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

    try { auto edges = m_databaseStorage->getEdgesByType(static_cast<int>(referenceKind)); for (const auto& e: edges) { Reference r; r.id = e.id; r.sourceSymbolId = e.sourceNodeId; r.targetSymbolId = e.targetNodeId; r.referenceKind = static_cast<ReferenceKind>(e.edgeKind); references.push_back(std::move(r)); } }
    catch (const std::exception& e) { setLastError(std::string("Exception while getting references by type: ") + e.what()); }

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

    try { auto storageFiles = m_databaseStorage->getAll<StorageFile>(); files.reserve(storageFiles.size()); for (const auto& sf: storageFiles) { File f; f.id=sf.id; f.filePath=sf.filePath; f.language=sf.languageIdentifier; f.indexed=sf.indexed; f.complete=sf.complete; files.push_back(std::move(f)); } }
    catch (const std::exception& e) { setLastError(std::string("Exception while getting files: ") + e.what()); }

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
