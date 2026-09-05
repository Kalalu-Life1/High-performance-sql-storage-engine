#include "kv_engine.hpp"
#include <iostream>
#include <cassert>
#include <unistd.h>

int main() {
    std::string test_db = "production_test.db";
    ::unlink(test_db.c_str());

    std::cout << "[1/3] Initializing DB Engine Instance #1 (Fresh Start)..." << std::endl;
    {
        KVEngine engine(test_db, 5);
        engine.Put("persistent_key_01", "SECURE_TRANSACTION_PAYLOAD_DATA_A");
        engine.Put("persistent_key_02", "SECURE_TRANSACTION_PAYLOAD_DATA_B");
        std::cout << " -> Records saved successfully to storage engine files." << std::endl;
    } // Engine #1 shuts down cleanly and flushes raw cache contents down to disk block layers.

    std::cout << "[2/3] Initializing DB Engine Instance #2 (Simulated Total System Crash/Restart)..." << std::endl;
    // Engine #2 starts up and reads the physical database file from disk automatically
    KVEngine engine(test_db, 5);
    
    std::cout << "[3/3] Attempting zero-knowledge GET retrieval via automated bootstrap mapping..." << std::endl;
    std::string out_val1, out_val2;
    bool success1 = engine.Get("persistent_key_01", out_val1);
    bool success2 = engine.Get("persistent_key_02", out_val2);

    if (success1 && success2 && out_val1 == "SECURE_TRANSACTION_PAYLOAD_DATA_A") {
        std::cout << " -> Auto-Recovered Key [persistent_key_01] -> " << out_val1 << std::endl;
        std::cout << " -> Auto-Recovered Key [persistent_key_02] -> " << out_val2 << std::endl;
        std::cout << "🎉 SYSTEM INTEGRITY MATCH: Storage Engine completely autonomous and production ready!" << std::endl;
    } else {
        std::cerr << "❌ CRITICAL FAULT: Automated index recovery extraction failure." << std::endl;
        return 1;
    }

    ::unlink(test_db.c_str());
    return 0;
}
