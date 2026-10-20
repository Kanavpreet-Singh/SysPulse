#include <gtest/gtest.h>
#include "syspulse/proc_collector.hpp"
#include <filesystem>
#include <fstream>

using namespace syspulse;

TEST(ProcCollectorTest, ParseProcStatLineNormal) {
    std::string line = "1234 (syspulse_agent) S 1000 1234 1000 0 -1 4194304 "
                       "500 0 10 0 "
                       "150 50 0 0 "
                       "20 0 4 0 1234567 "
                       "104857600 2560 18446744073709551615 0 0 0 0 0 0 0 2147483647 0 0 0 0 17 0 0 0 0 0 0 0 0 0 0 0 0 0 0";

    int64_t pid = 0, ppid = 0;
    std::string name;
    uint32_t threads = 0;
    uint64_t utime = 0, stime = 0, vsize = 0, rss_pages = 0, faults = 0;

    bool ok = ProcCollector::parse_proc_stat_line(line, pid, name, ppid, threads, utime, stime, vsize, rss_pages, faults);
    ASSERT_TRUE(ok);
    EXPECT_EQ(pid, 1234);
    EXPECT_EQ(name, "syspulse_agent");
    EXPECT_EQ(ppid, 1000);
    EXPECT_EQ(threads, 4);
    EXPECT_EQ(utime, 150);
    EXPECT_EQ(stime, 50);
    EXPECT_EQ(vsize, 104857600);
    EXPECT_EQ(rss_pages, 2560);
    EXPECT_EQ(faults, 500 + 10);
}

TEST(ProcCollectorTest, ParseProcStatLineWithSpacesAndParenthesesInName) {
    // Edge case: comm name contains spaces and nested parentheses
    std::string line = "5678 (worker (main) proc) R 1000 5678 1000 0 -1 4194304 "
                       "100 0 5 0 "
                       "300 100 0 0 "
                       "20 0 8 0 1234567 "
                       "209715200 5120";

    int64_t pid = 0, ppid = 0;
    std::string name;
    uint32_t threads = 0;
    uint64_t utime = 0, stime = 0, vsize = 0, rss_pages = 0, faults = 0;

    bool ok = ProcCollector::parse_proc_stat_line(line, pid, name, ppid, threads, utime, stime, vsize, rss_pages, faults);
    ASSERT_TRUE(ok);
    EXPECT_EQ(pid, 5678);
    EXPECT_EQ(name, "worker (main) proc");
    EXPECT_EQ(threads, 8);
    EXPECT_EQ(utime, 300);
    EXPECT_EQ(stime, 100);
    EXPECT_EQ(vsize, 209715200);
    EXPECT_EQ(rss_pages, 5120);
}

TEST(ProcCollectorTest, ParseProcStatLineTruncatedOrMalformed) {
    int64_t pid = 0, ppid = 0;
    std::string name;
    uint32_t threads = 0;
    uint64_t utime = 0, stime = 0, vsize = 0, rss_pages = 0, faults = 0;

    // Missing closing paren
    EXPECT_FALSE(ProcCollector::parse_proc_stat_line("1234 (unfinished", pid, name, ppid, threads, utime, stime, vsize, rss_pages, faults));

    // Truncated after comm
    EXPECT_FALSE(ProcCollector::parse_proc_stat_line("1234 (prog) S 1", pid, name, ppid, threads, utime, stime, vsize, rss_pages, faults));

    // Empty line
    EXPECT_FALSE(ProcCollector::parse_proc_stat_line("", pid, name, ppid, threads, utime, stime, vsize, rss_pages, faults));
}

class MockProcfsFixture : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "syspulse_mock_proc";
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
        std::filesystem::create_directories(test_dir_, ec);

        // 1. Create mock /proc/stat
        std::ofstream stat(test_dir_ / "stat");
        stat << "cpu  1000 200 300 5000 100 50 50 0\n"
             << "cpu0 500 100 150 2500 50 25 25 0\n"
             << "cpu1 500 100 150 2500 50 25 25 0\n"
             << "ctxt 123456\n";

        // 2. Create mock /proc/meminfo
        std::ofstream mem(test_dir_ / "meminfo");
        mem << "MemTotal:       16000000 kB\n"
            << "MemAvailable:   10000000 kB\n"
            << "SwapTotal:       4000000 kB\n"
            << "SwapFree:        3000000 kB\n";

        // 3. Create mock /proc/vmstat
        std::ofstream vmstat(test_dir_ / "vmstat");
        vmstat << "nr_free_pages 10000\n"
               << "pgfault 987654\n";

        // 4. Create mock valid process /proc/2001
        auto p2001 = test_dir_ / "2001";
        std::filesystem::create_directories(p2001, ec);
        std::ofstream p2001_stat(p2001 / "stat");
        p2001_stat << "2001 (daemon (worker)) S 1 2001 1 0 -1 0 10 0 2 0 100 50 0 0 20 0 4 0 100 50000000 5000\n";

        std::ofstream p2001_io(p2001 / "io");
        p2001_io << "read_bytes: 4096\nwrite_bytes: 8192\n";

        std::ofstream p2001_status(p2001 / "status");
        p2001_status << "Name:\tdaemon\nFDSize:\t64\n";

        // 5. Create truncated process /proc/2002 (should be safely skipped without crashing)
        auto p2002 = test_dir_ / "2002";
        std::filesystem::create_directories(p2002, ec);
        std::ofstream p2002_stat(p2002 / "stat");
        p2002_stat << "2002"; // truncated
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
};

TEST_F(MockProcfsFixture, CollectSystemMetrics) {
    ProcCollector collector(test_dir_.string());

    auto res = collector.collect_system();
    ASSERT_TRUE(res.is_ok());

    const auto& m = res.value();
    EXPECT_EQ(m.mem_total_bytes, 16000000ULL * 1024);
    EXPECT_EQ(m.mem_available_bytes, 10000000ULL * 1024);
    EXPECT_EQ(m.swap_used_bytes, 1000000ULL * 1024); // 4000000 - 3000000 kB
    EXPECT_EQ(m.context_switches, 123456);
    EXPECT_EQ(m.page_faults, 987654);

    // Second sample to verify delta CPU calculation
    std::ofstream stat2(test_dir_ / "stat");
    stat2 << "cpu  1200 200 350 5100 100 50 50 0\n" // busy += 250, idle += 100, total += 350
          << "ctxt 123500\n";
    stat2.close();

    auto res2 = collector.collect_system();
    ASSERT_TRUE(res2.is_ok());
    // delta_busy = 250, delta_total = 350 -> cpu% ~ 71.4%
    EXPECT_GT(res2.value().cpu_total_pct, 50.0);
    EXPECT_LT(res2.value().cpu_total_pct, 90.0);
}

TEST_F(MockProcfsFixture, CollectProcessesWithEdgeCases) {
    ProcCollector collector(test_dir_.string());

    auto res = collector.collect_processes();
    ASSERT_TRUE(res.is_ok());

    const auto& procs = res.value();
    // PID 2001 should be present, truncated PID 2002 should be safely skipped
    ASSERT_EQ(procs.size(), 1);
    EXPECT_EQ(procs[0].pid, 2001);
    EXPECT_EQ(procs[0].name, "daemon (worker)");
    EXPECT_EQ(procs[0].thread_count, 4);
    EXPECT_EQ(procs[0].io_read_bytes, 4096);
    EXPECT_EQ(procs[0].io_write_bytes, 8192);
    EXPECT_EQ(procs[0].handle_or_fd_count, 64);
}
