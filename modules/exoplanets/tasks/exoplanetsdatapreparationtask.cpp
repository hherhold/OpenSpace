/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2023                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/exoplanets/tasks/exoplanetsdatapreparationtask.h>

#include <modules/exoplanets/exoplanetshelper.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/util/coordinateconversion.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/fmt.h>
#include <ghoul/glm.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/dictionary.h>
#include <charconv>
#include <filesystem>
#include <fstream>

namespace {
    constexpr std::string_view _loggerCat = "ExoplanetsDataPreparationTask";

    struct [[codegen::Dictionary(ExoplanetsDataPreparationTask)]] Parameters {
        // The csv file to extract data from
        std::string inputDataFile;

        // The speck file with star locations
        std::string inputSPECK;

        // The bin file to export data into
        std::string outputBIN [[codegen::annotation("A valid filepath")]];

        // The txt file to write look-up table into
        std::string outputLUT [[codegen::annotation("A valid filepath")]];

        // The path to a teff to bv conversion file. Should be a txt file where each line
        // has the format 'teff,bv'
        std::string teffToBvFile;
    };
#include "exoplanetsdatapreparationtask_codegen.cpp"
} // namespace

namespace openspace::exoplanets {

documentation::Documentation ExoplanetsDataPreparationTask::documentation() {
    return codegen::doc<Parameters>("exoplanets_data_preparation_task");
}

ExoplanetsDataPreparationTask::ExoplanetsDataPreparationTask(
                                                      const ghoul::Dictionary& dictionary)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    _inputDataPath = absPath(p.inputDataFile);
    _inputSpeckPath = absPath(p.inputSPECK);
    _outputBinPath = absPath(p.outputBIN);
    _outputLutPath = absPath(p.outputLUT);
    _teffToBvFilePath = absPath(p.teffToBvFile);
}

std::string ExoplanetsDataPreparationTask::description() {
    return fmt::format(
        "Extract data about exoplanets from file {} and write as bin to {}. The data "
        "file should be a csv version of the Planetary Systems Composite Data from the "
        "NASA exoplanets archive (https://exoplanetarchive.ipac.caltech.edu/)",
        _inputDataPath, _outputBinPath
    );
}

void ExoplanetsDataPreparationTask::perform(
                                           const Task::ProgressCallback& progressCallback)
{
    std::ifstream inputDataFile(_inputDataPath);
    if (!inputDataFile.good()) {
        LERROR(fmt::format("Failed to open input file {}", _inputDataPath));
        return;
    }

    std::ofstream binFile(_outputBinPath, std::ios::out | std::ios::binary);
    std::ofstream lutFile(_outputLutPath);

    if (!binFile.good()) {
        LERROR(fmt::format("Error when writing to {}",_outputBinPath));
        if (!std::filesystem::is_directory(_outputBinPath.parent_path())) {
            LERROR("Output directory does not exist");
        }
        return;
    }

    if (!lutFile.good()) {
        LERROR(fmt::format("Error when writing to {}", _outputLutPath));
        if (!std::filesystem::is_directory(_outputLutPath.parent_path())) {
            LERROR("Output directory does not exist");
        }
        return;
    }

    int version = 1;
    binFile.write(reinterpret_cast<char*>(&version), sizeof(int));

    auto readFirstDataRow = [](std::ifstream& file, std::string& line) -> void {
        while (std::getline(file, line)) {
            bool shouldSkip = line[0] == '#' || line.empty();
            if (!shouldSkip) break;
        }
    };

    // Find the line containing the data names
    std::string columnNamesRow;
    readFirstDataRow(inputDataFile, columnNamesRow);

    // Read column names into a vector, for later access
    std::vector<std::string> columnNames;
    std::stringstream sStream(columnNamesRow);
    std::string colName;
    while (std::getline(sStream, colName, ',')) {
        columnNames.push_back(colName);
    }

    // Read total number of items
    int total = 0;
    std::string row;
    while (std::getline(inputDataFile, row)) {
        ++total;
    }
    inputDataFile.clear();
    inputDataFile.seekg(0);

    // Read past the first line, containing the data names
    readFirstDataRow(inputDataFile, row);

    LINFO(fmt::format("Loading {} exoplanets", total));

    auto readFloatData = [](const std::string& str) -> float {
    #ifdef WIN32
        float result;
        auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
        if (ec == std::errc()) {
            return result;
        }
        return std::numeric_limits<float>::quiet_NaN();
    #else
        // clang is missing float support for std::from_chars
        return !str.empty() ? std::stof(str.c_str(), nullptr) : NAN;
    #endif
    };

    auto readDoubleData = [](const std::string& str) -> double {
    #ifdef WIN32
        double result;
        auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
        if (ec == std::errc()) {
            return result;
        }
        return std::numeric_limits<double>::quiet_NaN();
    #else
        // clang is missing double support for std::from_chars
        return !str.empty() ? std::stod(str.c_str(), nullptr) : NAN;
    #endif
    };

    auto readIntegerData = [](const std::string& str) -> int {
        int result;
        auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
        if (ec == std::errc()) {
            return result;
        }
        return -1;
    };

    auto readStringData = [](const std::string& str) -> std::string {
        std::string result = str;
        result.erase(std::remove(result.begin(), result.end(), '\"'), result.end());
        return result;
    };

    ExoplanetDataEntry p;
    std::string data;
    int exoplanetCount = 0;
    while (std::getline(inputDataFile, row)) {
        ++exoplanetCount;
        progressCallback(static_cast<float>(exoplanetCount) / static_cast<float>(total));

        std::string component;
        std::string starName;

        float ra = std::numeric_limits<float>::quiet_NaN(); // decimal degrees
        float dec = std::numeric_limits<float>::quiet_NaN(); // decimal degrees
        float distanceInParsec = std::numeric_limits<float>::quiet_NaN();

        std::istringstream lineStream(row);
        int columnIndex = 0;
        while (std::getline(lineStream, data, ',')) {
            const std::string& column = columnNames[columnIndex];
            columnIndex++;

            if (column == "pl_letter") {
                component = readStringData(data);
            }
            // Orbital semi-major axis
            else if (column == "pl_orbsmax") {
                p.a = readFloatData(data);
            }
            else if (column == "pl_orbsmaxerr1") {
                p.aUpper = readDoubleData(data);
            }
            else if (column == "pl_orbsmaxerr2") {
                p.aLower = -readDoubleData(data);
            }
            // Orbital eccentricity
            else if (column == "pl_orbeccen") {
                p.ecc = readFloatData(data);
            }
            else if (column == "pl_orbeccenerr1") {
                p.eccUpper = readFloatData(data);
            }
            else if (column == "pl_orbeccenerr2") {
                p.eccLower = -readFloatData(data);
            }
            // Orbital inclination
            else if (column == "pl_orbincl") {
                p.i = readFloatData(data);
            }
            else if (column == "pl_orbinclerr1") {
                p.iUpper = readFloatData(data);
            }
            else if (column == "pl_orbinclerr2") {
                p.iLower = -readFloatData(data);
            }
            // Argument of periastron
            else if (column == "pl_orblper") {
                p.omega = readFloatData(data);
            }
            else if (column == "pl_orblpererr1") {
                p.omegaUpper = readFloatData(data);
            }
            else if (column == "pl_orblpererr2") {
                p.omegaLower = -readFloatData(data);
            }
            // Orbital period
            else if (column == "pl_orbper") {
                p.per = readDoubleData(data);
            }
            else if (column == "pl_orbpererr1") {
                p.perUpper = readFloatData(data);
            }
            else if (column == "pl_orbpererr2") {
                p.perLower = -readFloatData(data);
            }
            // Radius of the planet (Jupiter radii)
            else if (column == "pl_radj") {
                p.r = readDoubleData(data);
            }
            else if (column == "pl_radjerr1") {
                p.rUpper = readDoubleData(data);
            }
            else if (column == "pl_radjerr2") {
                p.rLower = -readDoubleData(data);
            }
            // Time of transit midpoint
            else if (column == "pl_tranmid") {
                p.tt = readDoubleData(data);
            }
            else if (column == "pl_tranmiderr1") {
                p.ttUpper = readFloatData(data);
            }
            else if (column == "pl_tranmiderr2") {
                p.ttLower = -readFloatData(data);
            }
            // Star - name and position
            else if (column == "hostname") {
                starName = readStringData(data);
                glm::vec3 position = starPosition(starName);
                p.positionX = position[0];
                p.positionY = position[1];
                p.positionZ = position[2];
            }
            else if (column == "ra") {
                ra = readFloatData(data);
            }
            else if (column == "dec") {
                dec = readFloatData(data);
            }
            else if (column == "sy_dist") {
                distanceInParsec = readFloatData(data);
            }
            // Star radius
            else if (column == "st_rad") {
                p.rStar = readFloatData(data);
            }
            else if (column == "st_raderr1") {
                p.rStarUpper = readFloatData(data);
            }
            else if (column == "st_raderr2") {
                p.rStarLower = -readFloatData(data);
            }
            // Effective temperature and color of star
            // (B-V color index computed from star's effective temperature)
            else if (column == "st_teff") {
                p.teff = readFloatData(data);
                p.bmv = bvFromTeff(p.teff);
            }
            else if (column == "st_tefferr1") {
                p.teffUpper = readFloatData(data);
            }
            else if (column == "st_tefferr2") {
                p.teffLower = -readFloatData(data);
            }
            // Star luminosity
            else if (column == "st_lum") {
                float dataInLogSolar = readFloatData(data);
                p.luminosity = static_cast<float>(std::pow(10, dataInLogSolar));
            }
            else if (column == "st_lumerr1") {
                float dataInLogSolar = readFloatData(data);
                p.luminosityUpper = static_cast<float>(std::pow(10, dataInLogSolar));
            }
            else if (column == "st_lumerr2") {
                float dataInLogSolar = readFloatData(data);
                p.luminosityLower = static_cast<float>(-std::pow(10, dataInLogSolar));
            }
            // Is the planet orbiting a binary system?
            else if (column == "cb_flag") {
                p.binary = static_cast<bool>(readIntegerData(data));
            }
            // Number of stars in the system
            else if (column == "sy_snum") {
                p.nStars = readIntegerData(data);
            }
            // Number of planets in the system
            else if (column == "sy_pnum") {
                p.nPlanets = readIntegerData(data);
            }
        }

        // @TODO (emmbr 2020-10-05) Currently, the dataset has no information about the
        // longitude of the ascending node, but maybe it might in the future
        p.bigOmega = std::numeric_limits<float>::quiet_NaN();
        p.bigOmegaUpper = std::numeric_limits<float>::quiet_NaN();
        p.bigOmegaLower = std::numeric_limits<float>::quiet_NaN();

        bool foundPositionFromSpeck = !std::isnan(p.positionX);
        bool hasDistance = !std::isnan(distanceInParsec);
        bool hasIcrsCoords = !std::isnan(ra) && !std::isnan(dec) && hasDistance;

        if (!foundPositionFromSpeck && hasIcrsCoords) {
            glm::dvec3 pos = icrsToGalacticCartesian(ra, dec, distanceInParsec);
            p.positionX = static_cast<float>(pos.x);
            p.positionY = static_cast<float>(pos.y);
            p.positionZ = static_cast<float>(pos.z);
        }

        // Create look-up table
        long pos = static_cast<long>(binFile.tellp());
        std::string planetName = starName + " " + component;
        lutFile << planetName << "," << pos << std::endl;

        binFile.write(reinterpret_cast<char*>(&p), sizeof(ExoplanetDataEntry));
    }

    progressCallback(1.f);
}

glm::vec3 ExoplanetsDataPreparationTask::starPosition(const std::string& starName) {
    std::ifstream exoplanetsFile(_inputSpeckPath);
    if (!exoplanetsFile) {
        LERROR(fmt::format("Error opening file expl.speck"));
    }

    glm::vec3 position{ std::numeric_limits<float>::quiet_NaN() };
    std::string line;

    while (std::getline(exoplanetsFile, line)) {
        bool shouldSkipLine = (
            line.empty() || line[0] == '#' || line.substr(0, 7) == "datavar" ||
            line.substr(0, 10) == "texturevar" || line.substr(0, 7) == "texture"
        );

        if (shouldSkipLine) {
            continue;
        }

        std::string data;
        std::string name;
        std::istringstream linestream(line);
        std::getline(linestream, data, '#');
        std::getline(linestream, name);
        name.erase(0, 1);

        std::string coord;
        if (name == starName) {
            std::stringstream dataStream(data);
            std::getline(dataStream, coord, ' ');
            position[0] = std::stof(coord.c_str(), nullptr);
            std::getline(dataStream, coord, ' ');
            position[1] = std::stof(coord.c_str(), nullptr);
            std::getline(dataStream, coord, ' ');
            position[2] = std::stof(coord.c_str(), nullptr);
            break;
        }
    }

    return position;
}

float ExoplanetsDataPreparationTask::bvFromTeff(float teff) {
    if (std::isnan(teff)) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    std::ifstream teffToBvFile(_teffToBvFilePath);
    if (!teffToBvFile.good()) {
        LERROR(fmt::format("Failed to open teff_bv.txt file"));
        return std::numeric_limits<float>::quiet_NaN();
    }

    float bv = 0.f;
    float bvUpper = 0.f;
    float bvLower = 0.f;
    float teffLower = 0.f;
    float teffUpper;
    std::string row;
    while (std::getline(teffToBvFile, row)) {
        std::istringstream lineStream(row);
        std::string teffString;
        std::getline(lineStream, teffString, ',');
        std::string bvString;
        std::getline(lineStream, bvString);

        float teffCurrent = std::stof(teffString.c_str(), nullptr);
        float bvCurrent = std::stof(bvString.c_str(), nullptr);

        if (teff > teffCurrent) {
            teffLower = teffCurrent;
            bvLower = bvCurrent;
        }
        else {
            teffUpper = teffCurrent;
            bvUpper = bvCurrent;
            if (bvLower == 0.f) {
                bv = 2.f;
            }
            else {
                float bvDiff = (bvUpper - bvLower);
                float teffDiff = (teffUpper - teffLower);
                bv = ((bvDiff * (teff - teffLower)) / teffDiff) + bvLower;
            }
            break;
        }
    }
    return bv;
}

} // namespace openspace::exoplanets
