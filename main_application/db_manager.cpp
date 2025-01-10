#include <iostream>
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <chrono>
#include <thread>
#include <set>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

const std::string LOG_DIR = "./data_logs";    // Directory for log files
const std::string DB_FILE = "logs.db";       // SQLite database file

// Function declarations
void processLogFile(const fs::path& filePath, sqlite3* db);
void insertSensorReading(sqlite3* db, const std::string& timestamp, const std::string& log_date, const std::string& sensorType, const std::string& value);
bool isFileFullyProcessed(sqlite3* db, const std::string& log_date);
size_t getLastProcessedLine(sqlite3* db, const std::string& log_date);
void updateLastProcessedLine(sqlite3* db, const std::string& log_date, size_t lineNum);

// Function to initialize the SQLite database and create necessary tables
void initializeDatabase(sqlite3* db) {
    const char* createTables = R"(
        CREATE TABLE IF NOT EXISTS SensorReadings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME,
            log_date DATE,
            sensor_type TEXT,
            value REAL
        );
        
        CREATE TABLE IF NOT EXISTS LogProgress (
            log_date DATE PRIMARY KEY,
            last_processed_line INTEGER
        );
    )";

    char* errMsg = nullptr;
    if (sqlite3_exec(db, createTables, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "Error creating tables: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
}

// Main function to monitor the log directory and process new log files
int main() {
    sqlite3* db;
    int rc = sqlite3_open(DB_FILE.c_str(), &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }

    std::cout << "Database opened successfully.\n";
    initializeDatabase(db);  // Set up the database tables if they don't exist

    // Sort files by date, processing oldest logs first
    std::vector<fs::path> logFiles;
    for (const auto& entry : fs::directory_iterator(LOG_DIR)) {
        if (fs::is_regular_file(entry.path())) {  // Check if entry is a regular file
            logFiles.push_back(entry.path());
        }
    }
    std::sort(logFiles.begin(), logFiles.end());  // Sort files by name (assumes chronological order)

    for (const auto& filePath : logFiles) {
        std::string fileName = filePath.filename().string();
        std::string log_date = fileName.substr(9, 10); // Extract "YYYY-MM-DD" from filename

        std::cout << "Processing file: " << fileName << std::endl;
        processLogFile(filePath, db);
    }

    sqlite3_close(db);
    return 0;
}

// Function to get the last processed line for a log file from the database
size_t getLastProcessedLine(sqlite3* db, const std::string& log_date) {
    std::string sql = "SELECT last_processed_line FROM LogProgress WHERE log_date = ?;";
    sqlite3_stmt* stmt;
    size_t lastProcessedLine = 0;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, log_date.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            lastProcessedLine = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to retrieve last processed line: " << sqlite3_errmsg(db) << std::endl;
    }

    return lastProcessedLine;
}

// Function to update the last processed line for a log file in the database
void updateLastProcessedLine(sqlite3* db, const std::string& log_date, size_t lineNum) {
    std::string sql = "INSERT OR REPLACE INTO LogProgress (log_date, last_processed_line) VALUES (?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, log_date.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, lineNum);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Error updating last processed line: " << sqlite3_errmsg(db) << std::endl;
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to prepare statement for updating last processed line: " << sqlite3_errmsg(db) << std::endl;
    }
}

// Function to process each log file line-by-line, resuming from the last processed line
void processLogFile(const fs::path& filePath, sqlite3* db) {
    std::ifstream infile(filePath);
    std::string line;
    std::string lastTimestamp;
    std::string fileName = filePath.filename().string();
    std::string log_date = fileName.substr(9, 10); // Extract "YYYY-MM-DD" from filename

    size_t lastProcessedLine = getLastProcessedLine(db, log_date);
    size_t currentLine = 0;

    // Regular expression for timestamps and sensor readings
    std::regex timestampRegex(R"(\[(\d{2}:\d{2}:\d{2})\])");
    std::regex sensorPattern(R"((ALS|Temperature|Humidity|Sound Magnitude|Rotary Encoder Right Position): ([\d\.\-]+))");

    while (std::getline(infile, line)) {
        currentLine++;
        if (currentLine <= lastProcessedLine) {
            continue;  // Skip lines that have already been processed
        }

        std::smatch match;

        // Check if the line contains a timestamp
        if (std::regex_search(line, match, timestampRegex)) {
            lastTimestamp = match[1].str();  // Update the last known timestamp
            line = match.suffix().str();     // Process remainder of line for sensor data
        }

        // Parse all sensor data on the line with or without a timestamp
        std::string::const_iterator searchStart(line.cbegin());
        while (std::regex_search(searchStart, line.cend(), match, sensorPattern)) {
            std::string sensorType = match[1].str();
            std::string value = match[2].str();

            // Insert the reading with the last known timestamp and date
            insertSensorReading(db, lastTimestamp, log_date, sensorType, value);
            searchStart = match.suffix().first;  // Move to the next match
        }

        // Update the last processed line in the database
        updateLastProcessedLine(db, log_date, currentLine);
    }

    std::cout << "File processed: " << filePath.filename() << std::endl;
}

// Function to insert a sensor reading into the SensorReadings table with log_date
void insertSensorReading(sqlite3* db, const std::string& timestamp, const std::string& log_date, const std::string& sensorType, const std::string& value) {
    std::string sql = "INSERT INTO SensorReadings (timestamp, log_date, sensor_type, value) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, timestamp.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, log_date.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, sensorType.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 4, std::stod(value));

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Error inserting sensor reading: " << sqlite3_errmsg(db) << std::endl;
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to prepare statement for sensor reading: " << sqlite3_errmsg(db) << std::endl;
    }
}
