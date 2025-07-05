#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <map>
#include <optional>

namespace fs = std::filesystem;

struct BackupConfig {
    std::string sourcePath;
    std::string backupRoot = "CopiedFiles";
    std::string configFile = "paths.txt";
    size_t maxBackups = 10;
    std::chrono::minutes interval{30};
    bool daemon = false;
    bool instant = false;
    bool verbose = false;
    bool help = false;
    bool listBackups = false;
    std::optional<std::string> restoreBackup;
    std::optional<std::string> deleteBackup;
};

// Function to get timestamp-based folder name
std::string make_timestamp_folder_name() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;

    // Use localtime_s instead of localtime
    if (localtime_s(&tm_buf, &time_t) != 0) {
        // Handle error - maybe return a default name or throw
        return "Backup_Error";
    }

    std::ostringstream oss;
    oss << "Backup_" << std::put_time(&tm_buf, "%Y-%m-%d_%H-%M-%S");
    return oss.str();
}

// Function to read source path from file
std::string read_path_from_file(const std::string& txtFilePath) {
    std::ifstream in(txtFilePath);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open config file: " + txtFilePath);
    }

    std::string line;

    while (std::getline(in, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;

        // Skip comment lines
        if (line.starts_with("#") ||
            line.starts_with("//") ||
            line.starts_with("--")) {
            continue;
            }
        return line;
    }

    throw std::runtime_error("No valid path found in file: " + txtFilePath);
}


// Function to list all backups
void list_backups(const fs::path& backupRoot) {
    std::vector<fs::directory_entry> backups;

    if (!fs::exists(backupRoot)) {
        std::cout << "No backup directory found at: " << backupRoot << "\n";
        return;
    }

    for (const auto& entry : fs::directory_iterator(backupRoot)) {
        if (entry.is_directory() &&
            entry.path().filename().string().starts_with("Backup_")) {
            backups.push_back(entry);
        }
    }

    if (backups.empty()) {
        std::cout << "No backups found in: " << backupRoot << "\n";
        return;
    }

    // Sort by name (newest first)
    std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b) {
        return a.path().filename().string() > b.path().filename().string();
    });

    std::cout << "Available backups in " << backupRoot << ":\n";
    for (const auto& backup : backups) {
        auto size = fs::file_size(backup.path());
        std::cout << "  " << backup.path().filename().string()
            << " (Size: " << size << " bytes)\n";
    }
}

// Function to restore a backup
bool restore_backup(const std::string& backupName, const fs::path& backupRoot, const std::string& restorePath) {
    try {
        fs::path backupPath = backupRoot / backupName;

        if (!fs::exists(backupPath)) {
            std::cerr << "Backup not found: " << backupName << "\n";
            return false;
        }

        fs::path targetPath(restorePath);

        // Create parent directories if they don't exist
        if (targetPath.has_parent_path()) {
            fs::create_directories(targetPath.parent_path());
        }

        // Remove existing target if it exists
        if (fs::exists(targetPath)) {
            fs::remove_all(targetPath);
        }

        // Copy backup to target location
        fs::copy(backupPath, targetPath, fs::copy_options::recursive);

        std::cout << "✓ Restored backup '" << backupName << "' to: " << targetPath << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error during restore: " << e.what() << "\n";
        return false;
    }
}

// Function to delete a specific backup
bool delete_backup(const std::string& backupName, const fs::path& backupRoot) {
    try {
        fs::path backupPath = backupRoot / backupName;

        if (!fs::exists(backupPath)) {
            std::cerr << "Backup not found: " << backupName << "\n";
            return false;
        }

        fs::remove_all(backupPath);
        std::cout << "✓ Deleted backup: " << backupName << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error deleting backup: " << e.what() << "\n";
        return false;
    }
}

// Function to clean up old backups
void cleanup_old_backups(const fs::path& backupRoot, size_t maxBackups, bool verbose) {
    std::vector<fs::directory_entry> backups;

    // Collect all backup directories
    for (const auto& entry : fs::directory_iterator(backupRoot)) {
        if (entry.is_directory() &&
            entry.path().filename().string().starts_with("Backup_")) {
            backups.push_back(entry);
        }
    }

    // Sort by name (oldest first due to timestamp format)
    std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b) {
        return a.path().filename().string() < b.path().filename().string();
    });

    // Delete oldest backups if we exceed the limit
    while (backups.size() >= maxBackups) {
        if (verbose) {
            std::cout << "Deleting old backup: " << backups.front().path().filename() << "\n";
        }
        fs::remove_all(backups.front().path());
        backups.erase(backups.begin());
    }
}

// Function to perform a single backup operation
bool perform_backup(const BackupConfig& config) {
    try {
        std::string sourceDirPath;

        // Determine source path
        if (!config.sourcePath.empty()) {
            sourceDirPath = config.sourcePath;
        } else {
            sourceDirPath = read_path_from_file(config.configFile);
        }

        fs::path sourcePath(sourceDirPath);

        if (!fs::exists(sourcePath) || !fs::is_directory(sourcePath)) {
            std::cerr << "Warning: Source directory does not exist: " << sourcePath << "\n";
            return false;
        }

        fs::path backupRootPath(config.backupRoot);

        // Clean up old backups before creating new one
        cleanup_old_backups(backupRootPath, config.maxBackups, config.verbose);

        // Generate new backup folder name
        std::string newBackupName = make_timestamp_folder_name();
        fs::path newBackupPath = backupRootPath / newBackupName;

        if (config.verbose) {
            std::cout << "Backing up: " << sourcePath << " -> " << newBackupPath << "\n";
        }

        // Copy directory
        fs::copy(sourcePath, newBackupPath, fs::copy_options::recursive);

        std::cout << "✓ Created backup: " << newBackupName << "\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error during backup: " << e.what() << "\n";
        return false;
    }
}


void print_help(const std::string& programName) {
    std::cout << "FlameUp - Command Line Backup Utility\n";
    std::cout << "Usage: " << programName << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help              Show this help message\n";
    std::cout << "  -p, --path <path>       Source path to backup (overrides config file)\n";
    std::cout << "  -c, --config <file>     Config file path (default: paths.txt)\n";
    std::cout << "  -o, --output <path>     Backup output directory (default: CopiedFiles)\n";
    std::cout << "  -m, --max <number>      Maximum number of backups to keep (default: 10)\n";
    std::cout << "  -i, --interval <min>    Backup interval in minutes for daemon mode (default: 30)\n";
    std::cout << "  -d, --daemon            Run as background daemon (continuous backups)\n";
    std::cout << "  -n, --now               Perform instant backup and exit\n";
    std::cout << "  -v, --verbose           Enable verbose output\n";
    std::cout << "  -l, --list              List all available backups\n";
    std::cout << "  -r, --restore <name>    Restore specific backup by name\n";
    std::cout << "  --restore-to <path>     Target path for restore (use with --restore)\n";
    std::cout << "  --delete <name>         Delete specific backup by name\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " --now                    # Instant backup using paths.txt\n";
    std::cout << "  " << programName << " --path C:\\MyFiles --now  # Instant backup of specific path\n";
    std::cout << "  " << programName << " --daemon --interval 60   # Run daemon with 60min interval\n";
    std::cout << "  " << programName << " --list                   # List all backups\n";
    std::cout << "  " << programName << " --restore Backup_2024-01-01_12-00-00 --restore-to C:\\Restored\n";
    std::cout << "  " << programName << " --delete Backup_2024-01-01_12-00-00\n";
}

BackupConfig parse_arguments(int argc, char* argv[]) {
    BackupConfig config;
    std::string restoreTarget;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            config.help = true;
        } else if (arg == "-p" || arg == "--path") {
            if (i + 1 < argc) {
                config.sourcePath = argv[++i];
            } else {
                throw std::runtime_error("--path requires a value");
            }
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                config.configFile = argv[++i];
            } else {
                throw std::runtime_error("--config requires a value");
            }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                config.backupRoot = argv[++i];
            } else {
                throw std::runtime_error("--output requires a value");
            }
        } else if (arg == "-m" || arg == "--max") {
            if (i + 1 < argc) {
                config.maxBackups = std::stoul(argv[++i]);
            } else {
                throw std::runtime_error("--max requires a value");
            }
        } else if (arg == "-i" || arg == "--interval") {
            if (i + 1 < argc) {
                config.interval = std::chrono::minutes(std::stoul(argv[++i]));
            } else {
                throw std::runtime_error("--interval requires a value");
            }
        } else if (arg == "-d" || arg == "--daemon") {
            config.daemon = true;
        } else if (arg == "-n" || arg == "--now") {
            config.instant = true;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "-l" || arg == "--list") {
            config.listBackups = true;
        } else if (arg == "-r" || arg == "--restore") {
            if (i + 1 < argc) {
                config.restoreBackup = argv[++i];
            } else {
                throw std::runtime_error("--restore requires a backup name");
            }
        } else if (arg == "--restore-to") {
            if (i + 1 < argc) {
                restoreTarget = argv[++i];
            } else {
                throw std::runtime_error("--restore-to requires a path");
            }
        } else if (arg == "--delete") {
            if (i + 1 < argc) {
                config.deleteBackup = argv[++i];
            } else {
                throw std::runtime_error("--delete requires a backup name");
            }
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    // Handle restore target
    if (config.restoreBackup.has_value() && !restoreTarget.empty()) {
        // We'll handle this in main()
    }

    return config;
}

void create_readme_file() {
    std::string filename = "README.txt";
    if (fs::exists(filename)) {
        return; // File already exists
    }

    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Warning: Could not create README.txt\n";
        return;
    }

    out << "FlameUp - Command Line Arguments\n";
    out << "===========================================\n\n";
    out << "Overview\n";
    out << "--------\n";
    out << "A flexible command-line backup utility that supports instant backups, scheduled daemon mode, backup management, and restore operations.\n\n";
    out << "Basic Usage\n";
    out << "-----------\n";
    out << "FlameUp.exe [OPTIONS]\n\n";
    out << "Command Line Arguments\n";
    out << "----------------------\n\n";
    out << "Help and Information\n";
    out << "--------------------\n";
    out << "Argument            Description\n";
    out << "---------           ------------------------------------------------\n";
    out << "-h, --help          Display help message and exit\n";
    out << "-l, --list          List all available backups in the backup directory\n\n";
    out << "Backup Configuration\n";
    out << "--------------------\n";
    out << "Argument               Description                               Default\n";
    out << "---------              ----------------------------------------  ----------\n";
    out << "-p, --path <path>      Source path to backup (overrides config)  Uses paths.txt\n";
    out << "-c, --config <file>    Config file path containing source path   paths.txt\n";
    out << "-o, --output <path>    Backup output directory                   CopiedFiles\n";
    out << "-m, --max <number>     Maximum number of backups to keep         10\n\n";
    out << "Backup Operations\n";
    out << "-----------------\n";
    out << "Argument               Description\n";
    out << "---------              -----------------------------------------------\n";
    out << "-n, --now             Perform instant backup and exit\n";
    out << "-d, --daemon          Run as background daemon (continuous backups)\n";
    out << "-i, --interval <min>  Backup interval in minutes for daemon mode (default: 30)\n\n";
    out << "Backup Management\n";
    out << "-----------------\n";
    out << "Argument               Description\n";
    out << "---------              -----------------------------------------------\n";
    out << "-r, --restore <name>  Restore specific backup by name\n";
    out << "--restore-to <path>   Target path for restore (required with --restore)\n";
    out << "--delete <name>       Delete specific backup by name\n\n";
    out << "Output Control\n";
    out << "--------------\n";
    out << "Argument               Description\n";
    out << "---------              -----------------------------------------------\n";
    out << "-v, --verbose        Enable verbose output with detailed information\n";

    out.close();
}


int main(int argc, char* argv[]) {
    create_readme_file();
    try {
        BackupConfig config = parse_arguments(argc, argv);

        if (config.help) {
            print_help(argv[0]);
            return 0;
        }

        fs::path backupRootPath(config.backupRoot);

        // Ensure backup root exists for most operations
        if (!config.help && !config.listBackups) {
            if (!fs::exists(backupRootPath)) {
                fs::create_directories(backupRootPath);
                if (config.verbose) {
                    std::cout << "Created backup directory: " << backupRootPath << "\n";
                }
            }
        }

        // Handle list operation
        if (config.listBackups) {
            list_backups(backupRootPath);
            return 0;
        }

        // Handle restore operation
        if (config.restoreBackup.has_value()) {
            std::string restoreTarget;

            // Check if restore target was specified
            for (int i = 1; i < argc; i++) {
                if (std::string(argv[i]) == "--restore-to" && i + 1 < argc) {
                    restoreTarget = argv[i + 1];
                    break;
                }
            }

            if (restoreTarget.empty()) {
                std::cerr << "Error: --restore-to <path> is required when using --restore\n";
                return 1;
            }

            return restore_backup(config.restoreBackup.value(), backupRootPath, restoreTarget) ? 0 : 1;
        }

        // Handle delete operation
        if (config.deleteBackup.has_value()) {
            return delete_backup(config.deleteBackup.value(), backupRootPath) ? 0 : 1;
        }

        // Handle instant backup
        if (config.instant) {
            if (config.verbose) {
                std::cout << "Performing instant backup...\n";
            }
            return perform_backup(config) ? 0 : 1;
        }

        // Handle daemon mode
        if (config.daemon) {
            std::cout << "Starting backup daemon...\n";
            std::cout << "Backup interval: " << config.interval.count() << " minutes\n";
            std::cout << "Max backups: " << config.maxBackups << "\n";
            std::cout << "Backup directory: " << backupRootPath << "\n";
            std::cout << "Press Ctrl+C to stop...\n\n";

            while (true) {
                auto startTime = std::chrono::steady_clock::now();

                if (config.verbose) {
                    std::cout << "\n--- Starting backup cycle ---\n";
                }

                bool success = perform_backup(config);

                if (success) {
                    if (config.verbose) {
                        std::cout << "Backup completed successfully.\n";
                    }
                } else {
                    std::cout << "Backup failed, will retry in " << config.interval.count() << " minutes.\n";
                }

                // Calculate next backup time
                auto nextBackup = startTime + config.interval;
                auto now = std::chrono::steady_clock::now();

                if (nextBackup > now) {
                    auto waitTime = std::chrono::duration_cast<std::chrono::minutes>(nextBackup - now);
                    if (config.verbose) {
                        std::cout << "Next backup in " << waitTime.count() << " minutes...\n";
                    }
                    std::this_thread::sleep_until(nextBackup);
                }
            }
        } else {
            // No specific operation specified, show help
            print_help(argv[0]);
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}