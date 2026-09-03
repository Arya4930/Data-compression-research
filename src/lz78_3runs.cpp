#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

// ============================================================
// CONFIGURATION
// ============================================================

const int MIN_THREADS = 1;
const int MAX_THREADS = 22;

const int RUNS_PER_THREAD = 3;

// Same dataset set as lz78.cpp.
const int DATASET_COUNT = 15;

const std::string LZ78_EXE = "lz78.exe";

// ============================================================
// HELPER — PROGRESS BAR
// ============================================================

void printProgress(
    int completed,
    int total
) {
    const int barWidth = 50;

    double progress =
        static_cast<double>(completed) /
        static_cast<double>(total);

    int filled =
        static_cast<int>(
            progress * barWidth
        );

    std::cout << "\r[";

    for (int i = 0; i < barWidth; ++i) {

        if (i < filled) {
            std::cout << '#';
        }
        else {
            std::cout << '-';
        }
    }

    std::cout
        << "] "
        << std::fixed
        << std::setprecision(1)
        << progress * 100.0
        << "% ("
        << completed
        << "/"
        << total
        << ")"
        << std::flush;
}

// ============================================================
// RUN LZ78
// ============================================================

bool runLZ78(
    int threads
) {
    /*
        lz78.exe asks the user:

            Enter number of threads:

        We automatically provide the requested
        thread count through stdin.

        Equivalent to manually entering:

            4
    */

#ifdef _WIN32

    std::string command =
        "echo "
        +
        std::to_string(threads)
        +
        " | "
        +
        LZ78_EXE;

#else

    std::string command =
        "printf \""
        +
        std::to_string(threads)
        +
        "\\n\" | "
        +
        LZ78_EXE;

#endif

    int result =
        std::system(
            command.c_str()
        );

    return result == 0;
}

// ============================================================
// MAIN
// ============================================================

int main()
{
    try {

        // ====================================================
        // HEADER
        // ====================================================

        std::cout
            << "\n"
            << "============================================================\n"
            << "LZ78 3-RUN THREAD SCALING EXPERIMENT\n"
            << "============================================================\n\n";

        std::cout
            << "Datasets:              "
            << DATASET_COUNT
            << "\n"

            << "Thread counts:         "
            << MIN_THREADS
            << "-"
            << MAX_THREADS
            << "\n"

            << "Runs per thread count: "
            << RUNS_PER_THREAD
            << "\n\n";

        // ====================================================
        // CALCULATE TOTALS
        // ====================================================

        int threadCount =
            MAX_THREADS -
            MIN_THREADS +
            1;

        int totalRuns =
            threadCount *
            RUNS_PER_THREAD;

        int totalDatasetMeasurements =
            totalRuns *
            DATASET_COUNT;

        std::cout
            << "Total benchmark runs: "
            << totalRuns
            << "\n";

        std::cout
            << "Total dataset measurements: "
            << totalDatasetMeasurements
            << "\n\n";

        // ====================================================
        // CHECK LZ78 EXECUTABLE
        // ====================================================

        if (
            !fs::exists(
                LZ78_EXE
            )
        ) {

            std::cerr
                << "ERROR: Cannot find "
                << LZ78_EXE
                << "\n\n";

            std::cerr
                << "Make sure lz78.exe is inside:\n"
                << fs::current_path()
                << "\n\n";

            return 1;
        }

        // ====================================================
        // WARNING
        // ====================================================

        std::cout
            << "IMPORTANT:\n"

            << "This program will execute "
            << LZ78_EXE
            << " "
            << totalRuns
            << " times.\n\n"

            << "Each execution processes all "
            << DATASET_COUNT
            << " datasets.\n\n"

            << "Previous results will NOT be deleted.\n\n";

        std::cout
            << "Starting in 3 seconds...\n";

        std::this_thread::sleep_for(
            std::chrono::seconds(3)
        );

        // ====================================================
        // PROGRESS TRACKING
        // ====================================================

        int completedRuns = 0;

        int failedRuns = 0;

        auto experimentStart =
            std::chrono::steady_clock::now();

        // ====================================================
        // THREAD COUNTS
        // ====================================================

        for (
            int threads = MIN_THREADS;
            threads <= MAX_THREADS;
            ++threads
        ) {

            std::cout
                << "\n\n"
                << "============================================================\n"
                << "THREAD COUNT: "
                << threads
                << "\n"
                << "============================================================\n";

            // =================================================
            // REPEATED RUNS
            // =================================================

            for (
                int run = 1;
                run <= RUNS_PER_THREAD;
                ++run
            ) {

                std::cout
                    << "\nRun "
                    << run
                    << "/"
                    << RUNS_PER_THREAD
                    << "\n";

                // ---------------------------------------------
                // RUN TIMER
                // ---------------------------------------------

                auto start =
                    std::chrono::steady_clock::now();

                bool success =
                    runLZ78(
                        threads
                    );

                auto end =
                    std::chrono::steady_clock::now();

                double seconds =
                    std::chrono::duration<double>(
                        end - start
                    ).count();

                // ---------------------------------------------
                // UPDATE COUNTERS
                // ---------------------------------------------

                completedRuns++;

                if (!success) {

                    failedRuns++;

                    std::cout
                        << "\n"
                        << "ERROR: Run failed.\n"

                        << "Threads: "
                        << threads
                        << "\n"

                        << "Run: "
                        << run
                        << "\n";
                }
                else {

                    std::cout
                        << "\n"
                        << "Completed in "
                        << std::fixed
                        << std::setprecision(2)
                        << seconds
                        << " seconds.\n";
                }

                // ---------------------------------------------
                // OVERALL PROGRESS
                // ---------------------------------------------

                printProgress(
                    completedRuns,
                    totalRuns
                );

                std::cout
                    << "\n";
            }
        }

        // ====================================================
        // TOTAL EXPERIMENT TIME
        // ====================================================

        auto experimentEnd =
            std::chrono::steady_clock::now();

        double totalSeconds =
            std::chrono::duration<double>(
                experimentEnd -
                experimentStart
            ).count();

        // ====================================================
        // FINAL REPORT
        // ====================================================

        std::cout
            << "\n\n"
            << "============================================================\n"
            << "LZ78 EXPERIMENT COMPLETE\n"
            << "============================================================\n\n";

        std::cout
            << "Thread counts tested: "
            << threadCount
            << "\n";

        std::cout
            << "Runs per thread: "
            << RUNS_PER_THREAD
            << "\n";

        std::cout
            << "Total runs attempted: "
            << totalRuns
            << "\n";

        std::cout
            << "Successful runs: "
            << totalRuns - failedRuns
            << "\n";

        std::cout
            << "Failed runs: "
            << failedRuns
            << "\n";

        std::cout
            << "Total dataset measurements: "
            << (
                (totalRuns - failedRuns) *
                DATASET_COUNT
            )
            << "\n";

        std::cout
            << "Total wall-clock time: "
            << std::fixed
            << std::setprecision(2)
            << totalSeconds
            << " seconds\n";

        std::cout
            << "\nResults are stored by "
            << LZ78_EXE
            << " in the existing results directory.\n";

        std::cout
            << "\n============================================================\n";

        return 0;
    }

    catch (
        const std::exception& e
    ) {

        std::cerr
            << "\nERROR: "
            << e.what()
            << "\n";

        return 1;
    }
}
