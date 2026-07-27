#include <gtest/gtest.h>
#include "tbox_someip/someip_log_adapter.h"
#include <unistd.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    // Flush logger before exit to drain async queue
    if (tbox::someip::SomeipLogAdapter::isInitialized()) {
        try {
            tbox::someip::SomeipLogAdapter::runtime().flush();
        } catch (...) {
            // Ignore flush errors during shutdown
        }
    }
    // Use _exit to skip static destructors — framework-log's async worker
    // thread may still be alive and accessing static state during destruction,
    // causing a segfault at process exit.
    _exit(result);
}
