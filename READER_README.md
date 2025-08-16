# SourcetrailDB Reader Extension

This extension adds read-only capabilities to the SourcetrailDB library, allowing you to query and analyze existing Sourcetrail database files programmatically.

## Features

The `SourcetrailDBReader` class provides the following functionality:

- **Symbol Queries**: Get all symbols, find symbols by ID, or search by name
- **Reference Analysis**: Discover relationships between symbols (what references what)
- **File Information**: Get details about indexed source files
- **Database Statistics**: Overview of database contents

## API Overview

### Core Classes

#### `SourcetrailDBReader`
Main interface for reading from Sourcetrail databases.

#### `Symbol` Structure
Represents a code symbol (class, function, variable, etc.) with:
- `id`: Unique identifier
- `nameHierarchy`: Hierarchical name information
- `symbolKind`: Type of symbol (class, function, namespace, etc.)
- `definitionKind`: How the symbol is defined
- `locations`: Source code locations (TODO)

#### `Reference` Structure
Represents a relationship between symbols with:
- `id`: Unique identifier
- `sourceSymbolId`: ID of the source symbol
- `targetSymbolId`: ID of the target symbol
- `referenceKind`: Type of reference (call, inheritance, etc.)
- `locations`: Source code locations (TODO)

#### `File` Structure
Represents a source file with:
- `id`: Unique identifier
- `filePath`: Absolute file path
- `language`: Programming language
- `indexed`: Whether the file was successfully indexed
- `complete`: Whether indexing completed without errors

## Usage Examples

### Basic Usage

```cpp
#include "SourcetrailDBReader.h"

sourcetrail::SourcetrailDBReader reader;

// Open database
if (!reader.open("project.srctrldb")) {
    std::cerr << "Error: " << reader.getLastError() << std::endl;
    return 1;
}

// Get all symbols
auto symbols = reader.getAllSymbols();
std::cout << "Found " << symbols.size() << " symbols" << std::endl;

// Search for specific symbols
auto results = reader.findSymbolsByName("MyClass", false);
for (const auto& symbol : results) {
    std::cout << "Found symbol: " << symbol.id << std::endl;
}

reader.close();
```

### Analyzing Symbol Relationships

```cpp
// Find a specific symbol
auto symbols = reader.findSymbolsByName("MyFunction", true);
if (!symbols.empty()) {
    int symbolId = symbols[0].id;
    
    // Find what calls this function
    auto callers = reader.getReferencesToSymbol(symbolId);
    std::cout << "Functions that call MyFunction:" << std::endl;
    for (const auto& ref : callers) {
        std::cout << "  Symbol ID: " << ref.sourceSymbolId << std::endl;
    }
    
    // Find what this function calls
    auto callees = reader.getReferencesFromSymbol(symbolId);
    std::cout << "Functions called by MyFunction:" << std::endl;
    for (const auto& ref : callees) {
        std::cout << "  Symbol ID: " << ref.targetSymbolId << std::endl;
    }
}
```

### Database Overview

```cpp
// Get statistics
std::cout << reader.getDatabaseStats() << std::endl;

// List all files
auto files = reader.getAllFiles();
for (const auto& file : files) {
    std::cout << "File: " << file.filePath 
              << " (Language: " << file.language << ")" << std::endl;
}
```

## Command Line Tool

A command-line example tool is provided:

```bash
# Show database overview
./sourcetraildb_reader_example project.srctrldb

# Search for specific symbols
./sourcetraildb_reader_example project.srctrldb MyClass
```

## Building

The reader functionality is automatically built when building SourcetrailDB with examples enabled:

```bash
cd build
cmake .. -G Ninja
ninja
```

This will create:
- Core library with `SourcetrailDBReader` class
- Example executable: `sourcetraildb_reader_example`

## Symbol and Reference Kinds

The API uses enums to represent different types of symbols and references:

### Symbol Kinds
- `NAMESPACE` (16): Namespace declarations
- `CLASS` (128): Class/struct definitions  
- `METHOD` (8192): Member functions
- `FUNCTION` (4096): Free functions
- `TYPE` (1): Type declarations
- `FILE` (262144): Source files

### Reference Kinds
- `INHERITANCE` (16): Class inheritance relationships
- `CALL` (8): Function calls
- `TYPE_USAGE` (2): Type usage/declarations
- `MEMBER` (1): Member access

## Limitations

- Source location information is not yet fully implemented
- Name parsing is simplified and may not handle all complex cases
- Read-only access (no modification capabilities)
- Limited to database schema version 25

## Integration with Main Sourcetrail

This reader can be used with databases created by:
- The main Sourcetrail application
- The existing `SourcetrailDBWriter` API
- Custom indexers that write to Sourcetrail databases

## Future Enhancements

- Full source location support
- Better name parsing/formatting
- Query optimization for large databases
- Support for more database schema versions
- Integration with LSP servers for IDE support
