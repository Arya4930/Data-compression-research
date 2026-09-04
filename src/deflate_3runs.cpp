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

const std::string DEFLATE_EXE = "deflate.exe";

// ============================================================
// HELPER
// ============================================================

void printProgress(
    int completed,
    int total
) {
    const int barWidth = 50;

    double progress =
        static_cast<double>(completed)
        /
        static_cast<double>(total);

    int filled =
        static_cast<int>(
            progress * barWidth
        );

    std::cout << "\r[";

    for (
        int i = 0;
        i < barWidth;
        ++i
    ) {

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
        << "% "
        << "("
        << completed
        << "/"
        << total
        << ")"
        << std::flush;
}

// ============================================================
// RUN DEFLATE
// ============================================================

bool runDeflate(
    int threads
) {
    /*
        Your existing deflate.exe asks for the
        number of threads.

        This pipes the thread count into stdin.

        Equivalent to manually entering:

            4

        when deflate.exe starts.
    */

#ifdef _WIN32

    std::string command =
        "echo "
        +
        std::to_string(threads)
        +
        " | "
        +
        DEFLATE_EXE;

#else

    std::string command =
        "printf \""
        +
        std::to_string(threads)
        +
        "\\n\" | "
        +
        DEFLATE_EXE;

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
            << "DEFLATE 3-RUN THREAD SCALING EXPERIMENT\n"
            << "============================================================\n\n";

        std::cout
            << "Datasets:              15\n"
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
            MAX_THREADS
            -
            MIN_THREADS
            +
            1;

        int totalRuns =
            threadCount
            *
            RUNS_PER_THREAD;

        int totalDatasetMeasurements =
            totalRuns
            *
            15;

        std::cout
            << "Total benchmark runs: "
            << totalRuns
            << "\n";

        std::cout
            << "Total dataset measurements: "
            << totalDatasetMeasurements
            << "\n\n";

        // ====================================================
        // CHECK DEFLATE
        // ====================================================

        if (
            !fs::exists(
                DEFLATE_EXE
            )
        ) {

            std::cerr
                << "ERROR: Cannot find "
                << DEFLATE_EXE
                << "\n\n";

            std::cerr
                << "Make sure deflate.exe is inside:\n"
                << fs::current_path()
                << "\n";

            return 1;
        }

        // ====================================================
        // WARN USER
        // ====================================================

        std::cout
            << "IMPORTANT:\n"
            << "This program will execute deflate.exe "
            << totalRuns
            << " times.\n\n"

            << "Each execution processes all 15 datasets.\n\n"

            << "Previous results will NOT be deleted.\n\n";

        std::cout
            << "Starting in 3 seconds...\n";

        std::this_thread::sleep_for(
            std::chrono::seconds(3)
        );

        // ====================================================
        // PROGRESS
        // ====================================================

        int completedRuns = 0;
        int failedRuns = 0;

        auto experimentStart =
            std::chrono::steady_clock::now();

        // ====================================================
        // RUN EXPERIMENT
        // ====================================================

        for (
            int threads =
                MIN_THREADS;
            threads <=
                MAX_THREADS;
            ++threads
        ) {

            std::cout
                << "\n\n"
                << "============================================================\n"
                << "THREAD COUNT: "
                << threads
                << "\n"
                << "============================================================\n";

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

                auto start =
                    std::chrono::steady_clock::now();

                bool success =
                    runDeflate(
                        threads
                    );

                auto end =
                    std::chrono::steady_clock::now();

                double seconds =
                    std::chrono::duration<double>(
                        end - start
                    ).count();

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

                printProgress(
                    completedRuns,
                    totalRuns
                );

                std::cout
                    << "\n";
            }
        }

        // ====================================================
        // TOTAL TIME
        // ====================================================

        auto experimentEnd =
            std::chrono::steady_clock::now();

        double totalSeconds =
            std::chrono::duration<double>(
                experimentEnd
                -
                experimentStart
            ).count();

        // ====================================================
        // FINAL REPORT
        // ====================================================

        std::cout
            << "\n\n"
            << "============================================================\n"
            << "DEFLATE EXPERIMENT COMPLETE\n"
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
                (totalRuns - failedRuns)
                *
                15
            )
            << "\n";

        std::cout
            << "Total wall-clock time: "
            << std::fixed
            << std::setprecision(2)
            << totalSeconds
            << " seconds\n";

        std::cout
            << "\nResults are stored by deflate.exe "
            << "in the existing results directory.\n";

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