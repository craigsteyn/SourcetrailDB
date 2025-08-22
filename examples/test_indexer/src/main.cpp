#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>
// removed: unordered_set/deque/condition_variable (no longer needed for simplified class discovery)

#include "SourcetrailDBReader.h"
#include "SourcetrailDBWriter.h"

// Contract
// Inputs: <source_db> <target_db> <test_namespace>
// Behavior: Reads source_db, finds classes in test_namespace whose names end with Test/Tests,
// then for each method in those classes, performs BFS over outgoing references and records
// mappings (symbol -> test method) into the tests table of target_db.

static bool hasTestSuffix(const std::string& name) {
    if (name.size() >= 4 && name.compare(name.size()-4, 4, "Test") == 0) return true;
    if (name.size() >= 5 && name.compare(name.size()-5, 5, "Tests") == 0) return true;
    return false;
}

static std::string toFqn(const sourcetrail::NameHierarchy& nh) {
    std::string fqn;
    for (size_t i=0;i<nh.nameElements.size();++i) {
        if (i) fqn += nh.nameDelimiter;
        fqn += nh.nameElements[i].name;
    }
    return fqn;
}

int main(int argc, const char* argv[]) {
    if (argc < 4) {
        std::cout << "Usage: test_indexer <source_db> <target_db> <test_namespace>" << std::endl;
        return 1;
    }

    std::string sourceDb = argv[1];
    std::string targetDb = argv[2];
    std::string testNamespace = argv[3];

    sourcetrail::SourcetrailDBReader reader;
    if (!reader.open(sourceDb)) {
        std::cerr << "Failed to open source db: " << reader.getLastError() << std::endl;
        return 1;
    }
    std::cout << "Opened source db: " << sourceDb << std::endl;
    // Find the test namespace symbol(s)
    std::vector<sourcetrail::SourcetrailDBReader::Symbol> nsSymbols = reader.findSymbolsByQualifiedName(testNamespace, true);
    if (nsSymbols.empty()) {
        std::cerr << "Test namespace not found: " << testNamespace << std::endl;
        reader.close();
        return 1;
    }
    std::cout << "Found " << nsSymbols.size() << " namespace symbols for '" << testNamespace << "'" << std::endl;

    // Load compact symbol and edge tables into memory for fast read-only traversal
    auto briefSymbols = reader.getAllSymbolsBrief();
    auto briefEdges = reader.getAllEdgesBrief();
    if (!reader.getLastError().empty()) {
        std::cerr << "Warning: reader reported: " << reader.getLastError() << std::endl;
    }
    int maxId = 0;
    for (const auto& e : briefEdges) { if (e.sourceSymbolId > maxId) maxId = e.sourceSymbolId; if (e.targetSymbolId > maxId) maxId = e.targetSymbolId; }
    for (const auto& s : briefSymbols) { if (s.id > maxId) maxId = s.id; }
    std::vector<sourcetrail::SymbolKind> symbolKindById(maxId + 1, sourcetrail::SymbolKind::TYPE);
    for (const auto& s : briefSymbols) { if (s.id >= 0 && s.id <= maxId) symbolKindById[s.id] = s.symbolKind; }
    // Build adjacency lists by source; keep edge kind to filter MEMBER during BFS and to find MEMBERS during discovery
    std::vector<std::vector<std::pair<int,int>>> adjacency(maxId + 1);
    std::vector<std::vector<int>> memberChildren(maxId + 1);
    for (const auto& e : briefEdges) {
        if (e.sourceSymbolId < 0 || e.sourceSymbolId > maxId) continue;
        adjacency[e.sourceSymbolId].emplace_back(e.targetSymbolId, static_cast<int>(e.edgeKind));
        if (e.edgeKind == sourcetrail::EdgeKind::MEMBER) {
            memberChildren[e.sourceSymbolId].push_back(e.targetSymbolId);
        }
    }

    // Simplified discovery: only immediate members of the test namespace(s), using in-memory MEMBER edges
    std::vector<int> testClassIds;
    size_t childrenScanned = 0;
    auto lastLog = std::chrono::steady_clock::now();
    for (const auto& ns : nsSymbols) {
        const auto& kids = (ns.id >=0 && ns.id <= maxId) ? memberChildren[ns.id] : std::vector<int>{};
        for (const int childId : kids) {
            if (!childId) continue;
            ++childrenScanned;
            sourcetrail::SymbolKind sk = (childId >=0 && childId <= maxId)? symbolKindById[childId] : sourcetrail::SymbolKind::TYPE;
            if (sk == sourcetrail::SymbolKind::CLASS || sk == sourcetrail::SymbolKind::STRUCT) {
                // Only fetch name lazily for suffix check
                auto child = reader.getSymbolById(childId);
                if (child.id == 0) continue;
                const std::string name = child.nameHierarchy.nameElements.empty()? std::string() : child.nameHierarchy.nameElements.back().name;
                if (hasTestSuffix(name)) testClassIds.push_back(child.id);
            }

            auto now = std::chrono::steady_clock::now();
            if (now - lastLog >= std::chrono::seconds(5)) {
                std::cout << "[discover-classes] scanned children " << childrenScanned
                          << ", found classes " << testClassIds.size() << std::endl;
                lastLog = now;
            }
        }
    }
    std::sort(testClassIds.begin(), testClassIds.end());
    testClassIds.erase(std::unique(testClassIds.begin(), testClassIds.end()), testClassIds.end());
    std::cout << "[discover-classes] done. Found " << testClassIds.size() << " test classes (scanned " << childrenScanned << " children)" << std::endl;

    // Discover test methods for each test class by MEMBER edges (multithreaded)
    std::vector<int> testMethodIds;
    std::mutex testMethodMutex;
    {
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        const unsigned numThreads = hw ? hw : 4u;
        std::atomic<size_t> classIndex{0};
        std::atomic<size_t> classesProcessed{0};
        std::atomic<size_t> methodsFound{0};
        std::atomic<bool> methodDiscoverStop{false};

        // progress reporter
        std::thread methodProgress([&]() {
            while (!methodDiscoverStop.load(std::memory_order_relaxed)) {
                size_t collected = 0;
                {
                    std::lock_guard<std::mutex> lk(testMethodMutex);
                    collected = testMethodIds.size();
                }
                std::cout << "[discover-methods] classes " << classesProcessed.load(std::memory_order_relaxed)
                          << "/" << testClassIds.size()
                          << ", methods found ~" << methodsFound.load(std::memory_order_relaxed)
                          << ", collected so far " << collected << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        });

        std::vector<std::thread> methodWorkers;
        methodWorkers.reserve(numThreads);
        const size_t kClassChunk = 64; // number of classes a worker claims per fetch
        for (unsigned t = 0; t < numThreads; ++t) {
            methodWorkers.emplace_back([&, t]() {
                std::vector<int> localMethods;
                localMethods.reserve(256);

                while (true) {
                    size_t start = classIndex.fetch_add(kClassChunk, std::memory_order_relaxed);
                    if (start >= testClassIds.size()) break;
                    size_t end = std::min(start + kClassChunk, testClassIds.size());

                    for (size_t i = start; i < end; ++i) {
                        int classId = testClassIds[i];
                        const auto& kids = (classId >= 0 && classId <= maxId) ? memberChildren[classId] : std::vector<int>{};
                        for (int childId : kids) {
                            if (!childId) continue;
                            auto kind = (childId >= 0 && childId <= maxId) ? symbolKindById[childId] : sourcetrail::SymbolKind::TYPE;
                            if (kind == sourcetrail::SymbolKind::METHOD) {
                                localMethods.push_back(childId);
                                methodsFound.fetch_add(1, std::memory_order_relaxed);
                            }
                        }

                        if (localMethods.size() >= 256) {
                            std::lock_guard<std::mutex> lk(testMethodMutex);
                            testMethodIds.insert(testMethodIds.end(), localMethods.begin(), localMethods.end());
                            localMethods.clear();
                        }
                        classesProcessed.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                if (!localMethods.empty()) {
                    std::lock_guard<std::mutex> lk(testMethodMutex);
                    testMethodIds.insert(testMethodIds.end(), localMethods.begin(), localMethods.end());
                    localMethods.clear();
                }
            });
        }

        for (auto& th : methodWorkers) th.join();
        methodDiscoverStop.store(true, std::memory_order_relaxed);
        methodProgress.join();
    }
    // De-duplicate test methods just in case
    std::sort(testMethodIds.begin(), testMethodIds.end());
    testMethodIds.erase(std::unique(testMethodIds.begin(), testMethodIds.end()), testMethodIds.end());
    std::cout << "Found " << testClassIds.size() << " test classes and " << testMethodIds.size() << " unique test methods" << std::endl;

    // Multithreaded BFS per test method over outgoing references; collect mappings in memory.
    // We protect the shared mappingSet with a mutex. To reduce contention, workers batch inserts.
    std::set<std::pair<int,int>> mappingSet; // (targetSymbolId, testMethodId)
    std::mutex mappingMutex;

    // Progress metrics
    const size_t totalMethods = testMethodIds.size();
    std::atomic<size_t> methodsProcessed{0};
    std::atomic<size_t> nodesVisited{0};
    std::atomic<size_t> pairsDiscovered{0}; // approximate before de-dup into set
    std::atomic<bool> stopProgress{false};

    // Close reader now; remaining operations use in-memory structures only
    reader.close();

    // Progress reporter thread
    std::thread progressThread([&]() {
        while (!stopProgress.load(std::memory_order_relaxed)) {
            size_t processed = methodsProcessed.load(std::memory_order_relaxed);
            size_t visited = nodesVisited.load(std::memory_order_relaxed);
            size_t discovered = pairsDiscovered.load(std::memory_order_relaxed);
            size_t uniquePairs = 0;
            {
                std::lock_guard<std::mutex> lock(mappingMutex);
                uniquePairs = mappingSet.size();
            }
            std::cout << "[progress] methods " << processed << "/" << totalMethods
                      << ", nodes visited " << visited
                      << ", pairs discovered ~" << discovered
                      << ", unique mappings " << uniquePairs << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    });

    // Workers
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const unsigned numThreads = hw ? hw : 4u;
    std::atomic<size_t> nextIndex{0};
    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    for (unsigned t = 0; t < numThreads; ++t) {
        workers.emplace_back([&, t]() {
            std::vector<std::pair<int,int>> batch; // batched (targetSymbolId, testMethodId)
            batch.reserve(512);

            while (true) {
                size_t i = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (i >= totalMethods) break;
                int testMethodId = testMethodIds[i];

                std::set<int> visited; std::queue<int> q;
                visited.insert(testMethodId); q.push(testMethodId);
                while(!q.empty()) {
                    int cur = q.front(); q.pop();
                    nodesVisited.fetch_add(1, std::memory_order_relaxed);
                    const auto& edges = (cur >= 0 && cur <= maxId) ? adjacency[cur] : std::vector<std::pair<int,int>>{};
                    for (const auto& ref : edges) {
                        int kindInt = ref.second;
                        if (static_cast<sourcetrail::EdgeKind>(kindInt) == sourcetrail::EdgeKind::MEMBER) continue; // skip structure edges
                        int tgt = ref.first; if (!tgt) continue;
                        if (visited.insert(tgt).second) {
                            q.push(tgt);
                            batch.emplace_back(tgt, testMethodId);
                            pairsDiscovered.fetch_add(1, std::memory_order_relaxed);
                            if (batch.size() >= 256) {
                                std::lock_guard<std::mutex> lock(mappingMutex);
                                for (const auto& p : batch) mappingSet.emplace(p);
                                batch.clear();
                            }
                        }
                    }
                }

                // Flush any remaining batch for this method
                if (!batch.empty()) {
                    std::lock_guard<std::mutex> lock(mappingMutex);
                    for (const auto& p : batch) mappingSet.emplace(p);
                    batch.clear();
                }
                methodsProcessed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : workers) th.join();
    stopProgress.store(true, std::memory_order_relaxed);
    progressThread.join();

    std::cout << "Collected " << mappingSet.size() << " mappings. Writing to target DB..." << std::endl;

    sourcetrail::SourcetrailDBWriter writer;
    if (!writer.open(targetDb)) {
        std::cerr << "Failed to open target db: " << writer.getLastError() << std::endl;
        return 1;
    }
    writer.beginTransaction();
    size_t mappings = 0;
    for (const auto& p : mappingSet) {
        if (writer.recordTestMapping(p.first, p.second)) ++mappings;
        else std::cerr << "recordTestMapping failed: " << writer.getLastError() << std::endl;
    }
    writer.commitTransaction();
    std::cout << "Recorded " << mappings << " test mappings" << std::endl;
    writer.close();
    return 0;
}
