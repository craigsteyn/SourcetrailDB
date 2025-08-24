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

#ifndef SOURCETRAIL_SRCTRLDB_READER_H
#define SOURCETRAIL_SRCTRLDB_READER_H

#include <memory>
#include <string>
#include <vector>

#include "DefinitionKind.h"
#include "EdgeKind.h"
#include "ElementComponentKind.h"
#include "LocationKind.h"
#include "NameHierarchy.h"
#include "ReferenceKind.h" // kept for writer-side API elsewhere; reader now uses EdgeKind directly
#include "SourceRange.h"
#include "SymbolKind.h"

namespace sourcetrail
{
class DatabaseStorage;

/**
 * SourcetrailDBReader
 *
 * This class provides read-only access to a Sourcetrail project database.
 * It can be used to query symbols, references, and their relationships from 
 * an existing Sourcetrail database file.
 *
 * The following code snippet illustrates a basic usage of the SourcetrailDBReader class:
 *
 *   sourcetrail::SourcetrailDBReader reader;
 *   reader.open("MyProject.srctrldb");
 *   std::vector<Symbol> symbols = reader.getAllSymbols();
 *   auto references = reader.getReferencesToSymbol(symbolId);
 *   reader.close();
 */
class SourcetrailDBReader
{
public:
    // Structure to represent source location information
    struct SourceLocation
    {
        int id;
        int fileId;
        int startLine;
        int startColumn;
        int endLine;
        int endColumn;
        LocationKind locationType;
    };
    // Minimal, compact views for in-memory graph processing
    struct SymbolBrief
    {
        int id;
        SymbolKind symbolKind;
        DefinitionKind definitionKind;
    };

    struct EdgeBrief
    {
        int sourceSymbolId;
        int targetSymbolId;
        EdgeKind edgeKind;
    };

    // Structure to represent a symbol in the database
    struct Symbol
    {
        int id;
        NameHierarchy nameHierarchy;
        SymbolKind symbolKind;
        DefinitionKind definitionKind;
        std::vector<SourceLocation> locations;
    };

    // Structure to represent a reference/edge between symbols
    struct Reference
    {
        int id;
        int sourceSymbolId;
        int targetSymbolId;
        EdgeKind edgeKind;               // Previously ReferenceKind referenceKind; now reflects actual stored EdgeKind
        std::vector<SourceLocation> locations;
    };

    // Structure to represent a file in the database
    struct File
    {
        int id;
        std::string filePath;
        std::string language;
        bool indexed;
        bool complete;
    };

public:
    SourcetrailDBReader();
    ~SourcetrailDBReader();

    /**
     * Provides the version of the SourcetrailDB Core as string with format "vXX.dbYY.pZZ"
     *
     *  return: version string
     */
    std::string getVersionString() const;

    /**
     * Provides the supported database version as integer
     *
     *  return: supported database version
     */
    int getSupportedDatabaseVersion() const;

    /**
     * Provides the last error that occurred while using the SourcetrailDBReader
     *
     * The last error is empty if no error occurred since instantiation of the class or since the
     * error has last been cleared.
     *
     *  return: error message of last error that occurred
     */
    const std::string& getLastError() const;

    /**
     * Opens a Sourcetrail database for reading
     *
     * Call this method to open a Sourcetrail database file for read-only access.
     *
     *  param: databaseFilePath - absolute file path of the database file, including file extension
     *
     *  return: true if successful. false on failure. getLastError() provides the error message.
     */
    bool open(const std::string& databaseFilePath);

    /**
     * Closes the currently open Sourcetrail database
     *
     *  return: Returns true if the operation was successful. Otherwise false is returned and getLastError()
     *    can be checked for more detailed information.
     */
    bool close();

    /**
     * Checks if a database is currently open
     *
     *  return: true if a database is open and ready for queries
     */
    bool isOpen() const;

    /**
     * Get all symbols from the database
     *
     *  return: vector of all symbols in the database
     */
    std::vector<Symbol> getAllSymbols() const;

    // Compact arrays for high-performance, read-only consumers
    // Only integer ids and enum kinds; no strings or locations.
    std::vector<SymbolBrief> getAllSymbolsBrief() const;

    /**
     * Get a symbol by its ID
     *
     *  param: symbolId - the ID of the symbol to retrieve
     *
     *  return: the symbol if found, otherwise an empty symbol with id = 0
     */
    Symbol getSymbolById(int symbolId) const;

    /**
     * Find symbols by name (supports partial matching)
     *
     *  param: name - the name or partial name to search for
     *  param: exactMatch - if true, only exact matches are returned
     *
     *  return: vector of symbols matching the name criteria
     */
    std::vector<Symbol> findSymbolsByName(const std::string& name, bool exactMatch = false) const;

    /**
     * Find symbols by qualified name pattern (e.g., "MyNamespace::MyClass::myFunction")
     *
     *  param: qualifiedPattern - the qualified name pattern to search for (using the delimiter that the project uses, usually ::)
     *  param: exactMatch - when true only symbols whose fully qualified name exactly equals qualifiedPattern are returned.
     *                      when false (default) the previous behavior is kept: return symbols whose fully qualified name ends
     *                      with qualifiedPattern on a delimiter boundary (suffix match). This allows passing partial tails.
     *
     *  return: vector of symbols matching according to the chosen strategy
     */
    std::vector<Symbol> findSymbolsByQualifiedName(const std::string& qualifiedPattern, bool exactMatch = false) const;

    /**
     * Get all references/edges from the database
     *
     *  return: vector of all references in the database
     */
    std::vector<Reference> getAllReferences() const;

    // Compact edges array without ids/locations; ideal for building adjacency in memory
    std::vector<EdgeBrief> getAllEdgesBrief() const;

    /**
     * Get all references that point TO a specific symbol
     *
     *  param: symbolId - the ID of the target symbol
     *
     *  return: vector of references pointing to the symbol
     */
    std::vector<Reference> getReferencesToSymbol(int symbolId) const;

    /**
     * Get all references that originate FROM a specific symbol
     *
     *  param: symbolId - the ID of the source symbol
     *
     *  return: vector of references originating from the symbol
     */
    std::vector<Reference> getReferencesFromSymbol(int symbolId) const;
    std::vector<Reference> getReferencesFromSymbolWithKind(int symbolId, sourcetrail::EdgeKind kind) const;


    /**
     * Get references of a specific type
     *
     *  param: referenceKind - the type of reference to filter by
     *
     *  return: vector of references of the specified type
     */
    std::vector<Reference> getReferencesByType(EdgeKind edgeKind) const; // Updated to EdgeKind

    /**
     * Get all files from the database
     *
     *  return: vector of all files in the database
     */
    std::vector<File> getAllFiles() const;

    /**
     * Get a file by its ID
     *
     *  param: fileId - the ID of the file to retrieve
     *
     *  return: the file if found, otherwise an empty file with id = 0
     */
    File getFileById(int fileId) const;

    /**
     * Find files by path (supports partial matching)
     *
     *  param: path - the path or partial path to search for
     *  param: exactMatch - if true, only exact matches are returned
     *
     *  return: vector of files matching the path criteria
     */
    std::vector<File> findFilesByPath(const std::string& path, bool exactMatch = false) const;

    /**
     * Get source locations for a symbol
     *
     *  param: symbolId - the ID of the symbol
     *
     *  return: vector of source locations for the symbol
     */
    std::vector<SourceLocation> getSourceLocationsForSymbol(int symbolId) const;

    /**
     * Get source locations in a specific file
     *
     *  param: fileId - the ID of the file
     *
     *  return: vector of source locations in the file
     */
    std::vector<SourceLocation> getSourceLocationsInFile(int fileId) const;

    // New: Fetch symbols that have at least one location in any of the given file IDs
    std::vector<Symbol> getSymbolsInFiles(const std::vector<int>& fileIds) const;

    // New: Return SourceLocation entries only within a specific file for the given symbol/node id
    std::vector<SourceLocation> getSourceLocationsForSymbolInFile(int symbolId, int fileId) const;

    /**
     * Get database statistics
     *
     *  return: string containing various database statistics
     */
    std::string getDatabaseStats() const;

private:
    std::unique_ptr<DatabaseStorage> m_databaseStorage;
    mutable std::string m_lastError;

    void setLastError(const std::string& error) const;
    void clearLastError() const;
};

} // namespace sourcetrail

#endif // SOURCETRAIL_SRCTRLDB_READER_H
