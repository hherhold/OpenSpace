/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2021                                                               *
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

#include <modules/fieldlinessequence/util/fieldlinesstate.h>

#include <openspace/json.h>
#include <openspace/util/time.h>
#include <ghoul/fmt.h>
#include <ghoul/logging/logmanager.h>
#include <fstream>
#include <iomanip>

namespace {
    constexpr const char* _loggerCat = "FieldlinesState";
    constexpr const int CurrentVersion = 0;
    using json = nlohmann::json;
} // namespace

namespace openspace {

/**
 * Converts all glm::vec3 in _vertexPositions from spherical (radius, latitude, longitude)
 * coordinates into cartesian coordinates. The longitude and latitude coordinates are
 * expected to be in degrees. scale is an optional scaling factor.
 */
void FieldlinesState::convertLatLonToCartesian(float scale) {
    for (glm::vec3& p : _vertexPositions) {
        const float r = p.x * scale;
        const float lat = glm::radians(p.y);
        const float lon = glm::radians(p.z);
        const float rCosLat = r * cos(lat);

        p = glm::vec3(rCosLat * cos(lon), rCosLat* sin(lon), r * sin(lat));
    }
}

//void FieldlinesState::scalePositions(float scale) {
//    for (glm::vec3& p : _vertexPositions) {
//        p *= scale;
//    }
//}

void FieldlinesState::scaleflowline(float scale) {
    for (std::vector<glm::vec3>& v : _vertexPath) {
        for (glm::vec3& i : v) {
            i *= scale;
        }
    }
}

void FieldlinesState::scaleFieldlines(float scale) {
    for (std::vector<std::vector<glm::vec3>>& v1 : _fieldLines) {
        for (std::vector<glm::vec3>& v : v1) {
            for (glm::vec3& i : v) {
                i *= scale;
            }
        }
    }
}

void FieldlinesState::addOpenIndices(std::vector<int>& open, int i) {
    if (_open.empty()) {
        _open.push_back(open);
    }

    //std::vector<std::vector<int>> openVec;
    //openVec.push_back(open);
    //_open.insert(_open.begin() + i, openVec.begin(), openVec.end());

    else {
        for (int& index : open) {
            index += _vertexPath[i].size();
        }
        if (_open[i].empty()) {
            _open.push_back(open);
        }
        else {
            for (float o : open) {
                _open[i].push_back(o);
            }
        }
    }
}

void FieldlinesState::addClosedIndices(std::vector<int>& closed, int i) {
    //std::vector<std::vector<int>> closedVec;
       //closedVec.push_back(closed);
       //_closed.insert(_closed.begin() + i, closedVec.begin(), closedVec.end());
    if (_closed.empty()) {
        _closed.push_back(closed);
    }
    else {
        for (int& index : closed) {
            index += _vertexPath[i].size();
        }
       
        if (_closed[i].empty()) {
            _closed.push_back(closed);
        }
        else {
            for (float c : closed) {
                _closed[i].push_back(c);
            }
        }
    }
    
}

bool FieldlinesState::loadStateFromOsfls(const std::string& pathToOsflsFile) {
    std::ifstream ifs(pathToOsflsFile, std::ifstream::binary);
    if (!ifs.is_open()) {
        LERROR("Couldn't open file: " + pathToOsflsFile);
        return false;
    }

    int binFileVersion;
    ifs.read(reinterpret_cast<char*>(&binFileVersion), sizeof(int));

    switch (binFileVersion) {
        case 0:
            // No need to put everything in this scope now, as only version 0 exists!
            break;
        default:
            LERROR("VERSION OF BINARY FILE WAS NOT RECOGNIZED!");
            return false;
    }

    // Define tmp variables to store meta data in
    size_t nLines;
    size_t nPoints;
    size_t nExtras;
    size_t byteSizeAllNames;

    // Read single value variables
    ifs.read(reinterpret_cast<char*>(&_triggerTime), sizeof(double));
    ifs.read(reinterpret_cast<char*>(&_model), sizeof(int32_t));
    ifs.read(reinterpret_cast<char*>(&_isMorphable), sizeof(bool));
    ifs.read(reinterpret_cast<char*>(&nLines), sizeof(uint64_t));
    ifs.read(reinterpret_cast<char*>(&nPoints), sizeof(uint64_t));
    ifs.read(reinterpret_cast<char*>(&nExtras), sizeof(uint64_t));
    ifs.read(reinterpret_cast<char*>(&byteSizeAllNames), sizeof(uint64_t));

    _lineStart.resize(nLines);
    _lineCount.resize(nLines);
    _vertexPositions.resize(nPoints);
    _extraQuantities.resize(nExtras);
    _extraQuantityNames.resize(nExtras);

    // Read vertex position data
    ifs.read(reinterpret_cast<char*>(_lineStart.data()), sizeof(int32_t) * nLines);
    ifs.read(reinterpret_cast<char*>(_lineCount.data()), sizeof(uint32_t) * nLines);
    ifs.read(
        reinterpret_cast<char*>(_vertexPositions.data()),
        3 * sizeof(float) * nPoints
    );

    // Read all extra quantities
    for (std::vector<float>& vec : _extraQuantities) {
        vec.resize(nPoints);
        ifs.read(reinterpret_cast<char*>(vec.data()), sizeof(float) * nPoints);
    }

    // Read all extra quantities' names. Stored as multiple c-strings
    std::string allNamesInOne;
    std::vector<char> buffer(byteSizeAllNames);
    ifs.read(buffer.data(), byteSizeAllNames);
    allNamesInOne.assign(buffer.data(), byteSizeAllNames);

    size_t offset = 0;
    for (size_t i = 0; i < nExtras; ++i) {
        auto endOfVarName = allNamesInOne.find('\0', offset);
        endOfVarName -= offset;
        const std::string varName = allNamesInOne.substr(offset, endOfVarName);
        offset += varName.size() + 1;
        _extraQuantityNames[i] = varName;
    }

    return true;
}

bool FieldlinesState::loadStateFromJson(const std::string& pathToJsonFile,
                                        fls::Model Model, float coordToMeters)
{

    // --------------------- ENSURE FILE IS VALID, THEN PARSE IT --------------------- //
    std::ifstream ifs(pathToJsonFile);

    if (!ifs.is_open()) {
        LERROR(fmt::format("FAILED TO OPEN FILE: {}", pathToJsonFile));
        return false;
    }

    json jFile;
    ifs >> jFile;
    // -------------------------------------------------------------------------------- //

    _model = Model;

    const char* sData  = "data";
    const char* sTrace = "trace";

    // ----- EXTRACT THE EXTRA QUANTITY NAMES & TRIGGER TIME (same for all lines) ----- //
    {
        const char* sTime = "time";
        const json& jTmp = *(jFile.begin()); // First field line in the file
        _triggerTime = Time::convertTime(jTmp[sTime]);

        const char* sColumns = "columns";
        const json::value_type& variableNameVec = jTmp[sTrace][sColumns];
        const size_t nVariables = variableNameVec.size();
        const size_t nPosComponents = 3; // x,y,z

        if (nVariables < nPosComponents) {
            LERROR(
                pathToJsonFile + ": Each field '" + sColumns +
                "' must contain the variables: 'x', 'y' and 'z' (order is important)."
            );
            return false;
        }

        for (size_t i = nPosComponents ; i < nVariables ; ++i) {
            _extraQuantityNames.push_back(variableNameVec[i]);
        }
    }

    const size_t nExtras = _extraQuantityNames.size();
    _extraQuantities.resize(nExtras);

    size_t lineStartIdx = 0;
    // Loop through all fieldlines
    for (json::iterator lineIter = jFile.begin(); lineIter != jFile.end(); ++lineIter) {
        // The 'data' field in the 'trace' variable contains all vertex positions and the
        // extra quantities. Each element is an array related to one vertex point.
        const std::vector<std::vector<float>>& jData = (*lineIter)[sTrace][sData];
        const size_t nPoints = jData.size();

        for (size_t j = 0; j < nPoints; ++j) {
            const std::vector<float>& variables = jData[j];

            // Expects the x, y and z variables to be stored first!
            const size_t xIdx = 0;
            const size_t yIdx = 1;
            const size_t zIdx = 2;
            _vertexPositions.push_back(
                coordToMeters * glm::vec3(
                    variables[xIdx],
                    variables[yIdx],
                    variables[zIdx]
                )
            );

            // Add the extra quantites. Stored in the same array as the x,y,z variables.
            // Hence index of the first extra quantity = 3
            for (size_t xtraIdx = 3, k = 0 ; k < nExtras; ++k, ++xtraIdx) {
                _extraQuantities[k].push_back(variables[xtraIdx]);
            }
        }
        _lineCount.push_back(static_cast<GLsizei>(nPoints));
        _lineStart.push_back(static_cast<GLsizei>(lineStartIdx));
        lineStartIdx += nPoints;
    }
    return true;
}

/**
 * \param absPath must be the path to the file (incl. filename but excl. extension!)
 * Directory must exist! File is created (or overwritten if already existing).
 * File is structured like this: (for version 0)
 *  0. int                    - version number of binary state file! (in case something
 *                              needs to be altered in the future, then increase
 *                              CurrentVersion)
 *  1. double                 - _triggerTime
 *  2. int                    - _model
 *  3. bool                   - _isMorphable
 *  4. size_t                 - Number of lines in the state  == _lineStart.size()
 *                                                            == _lineCount.size()
 *  5. size_t                 - Total number of vertex points == _vertexPositions.size()
 *                                                           == _extraQuantities[i].size()
 *  6. size_t                 - Number of extra quantites     == _extraQuantities.size()
 *                                                           == _extraQuantityNames.size()
 *  7. site_t                 - Number of total bytes that ALL _extraQuantityNames
 *                              consists of (Each such name is stored as a c_str which
 *                              means it ends with the null char '\0' )
 *  7. std::vector<GLint>     - _lineStart
 *  8. std::vector<GLsizei>   - _lineCount
 *  9. std::vector<glm::vec3> - _vertexPositions
 * 10. std::vector<float>     - _extraQuantities
 * 11. array of c_str         - Strings naming the extra quantities (elements of
 *                              _extraQuantityNames). Each string ends with null char '\0'
 */
void FieldlinesState::saveStateToOsfls(const std::string& absPath) {
    // ------------------------------- Create the file ------------------------------- //
    std::string pathSafeTimeString = std::string(Time(_triggerTime).ISO8601());
    pathSafeTimeString.replace(13, 1, "-");
    pathSafeTimeString.replace(16, 1, "-");
    pathSafeTimeString.replace(19, 1, "-");
    const std::string& fileName = pathSafeTimeString + ".osfls";

    std::ofstream ofs(absPath + fileName, std::ofstream::binary | std::ofstream::trunc);
    if (!ofs.is_open()) {
        LERROR(fmt::format(
            "Failed to save state to binary file: {}{}", absPath, fileName
        ));
        return;
    }

    // --------- Add each string of _extraQuantityNames into one long string ---------- //
    std::string allExtraQuantityNamesInOne = "";
    for (const std::string& str : _extraQuantityNames) {
        allExtraQuantityNamesInOne += str + '\0'; // Add null char '\0' for easier reading
    }

    const size_t nLines = _lineStart.size();
    const size_t nPoints = _vertexPositions.size();
    const size_t nExtras = _extraQuantities.size();
    const size_t nStringBytes = allExtraQuantityNamesInOne.size();

    //----------------------------- WRITE EVERYTHING TO FILE -----------------------------
    // VERSION OF BINARY FIELDLINES STATE FILE - IN CASE STRUCTURE CHANGES IN THE FUTURE
    ofs.write(reinterpret_cast<const char*>(&CurrentVersion), sizeof(int));

    //-------------------- WRITE META DATA FOR STATE --------------------------------
    ofs.write(reinterpret_cast<const char*>(&_triggerTime), sizeof(_triggerTime));
    ofs.write(reinterpret_cast<const char*>(&_model), sizeof(int32_t));
    ofs.write(reinterpret_cast<const char*>(&_isMorphable), sizeof(bool));

    ofs.write(reinterpret_cast<const char*>(&nLines), sizeof(uint64_t));
    ofs.write(reinterpret_cast<const char*>(&nPoints), sizeof(uint64_t));
    ofs.write(reinterpret_cast<const char*>(&nExtras), sizeof(uint64_t));
    ofs.write(reinterpret_cast<const char*>(&nStringBytes), sizeof(uint64_t));

    //---------------------- WRITE ALL ARRAYS OF DATA --------------------------------
    ofs.write(reinterpret_cast<char*>(_lineStart.data()), sizeof(int32_t) * nLines);
    ofs.write(reinterpret_cast<char*>(_lineCount.data()), sizeof(uint32_t) * nLines);
    ofs.write(
        reinterpret_cast<char*>(_vertexPositions.data()),
        3 * sizeof(float) * nPoints
    );
    // Write the data for each vector in _extraQuantities
    for (std::vector<float>& vec : _extraQuantities) {
        ofs.write(reinterpret_cast<char*>(vec.data()), sizeof(float) * nPoints);
    }
    ofs.write(allExtraQuantityNamesInOne.c_str(), nStringBytes);
}

// TODO: This should probably be rewritten, but this is the way the files were structured
// by CCMC
// Structure of File! NO TRAILING COMMAS ALLOWED!
// Additional info can be stored within each line as the code only extracts the keys it
// needs (time, trace & data)
// The key/name of each line ("0" & "1" in the example below) is arbitrary
// {
//     "0":{
//         "time": "YYYY-MM-DDTHH:MM:SS.XXX",
//         "trace": {
//             "columns": ["x","y","z","s","temperature","rho","j_para"],
//             "data": [[8.694,127.853,115.304,0.0,0.047,9.249,-5e-10],...,
//                     [8.698,127.253,114.768,0.800,0.0,9.244,-5e-10]]
//         },
//     },
//     "1":{
//         "time": "YYYY-MM-DDTHH:MM:SS.XXX
//         "trace": {
//             "columns": ["x","y","z","s","temperature","rho","j_para"],
//             "data": [[8.694,127.853,115.304,0.0,0.047,9.249,-5e-10],...,
//                     [8.698,127.253,114.768,0.800,0.0,9.244,-5e-10]]
//         },
//     }
// }
void FieldlinesState::saveStateToJson(const std::string& absPath) {
    // Create the file
    const char* ext = ".json";
    std::ofstream ofs(absPath + ext, std::ofstream::trunc);
    if (!ofs.is_open()) {
        LERROR(fmt::format(
            "Failed to save state to json file at location: {}{}", absPath, ext
        ));
        return;
    }
    LINFO(fmt::format("Saving fieldline state to: {}{}", absPath, ext));

    json jColumns = { "x", "y", "z" };
    for (const std::string& s : _extraQuantityNames) {
        jColumns.push_back(s);
    }

    json jFile;

    std::string_view timeStr = Time(_triggerTime).ISO8601();
    const size_t nLines = _lineStart.size();
    // const size_t nPoints = _vertexPositions.size();
    const size_t nExtras = _extraQuantities.size();

    size_t pointIndex = 0;
    for (size_t lineIndex = 0; lineIndex < nLines; ++lineIndex) {
        json jData = json::array();
        for (GLsizei i = 0; i < _lineCount[lineIndex]; i++, ++pointIndex) {
            const glm::vec3 pos = _vertexPositions[pointIndex];
            json jDataElement = { pos.x, pos.y, pos.z };

            for (size_t extraIndex = 0; extraIndex < nExtras; ++extraIndex) {
                jDataElement.push_back(_extraQuantities[extraIndex][pointIndex]);
            }
            jData.push_back(jDataElement);
        }

        jFile[std::to_string(lineIndex)] = {
            { "time", timeStr },
            { "trace", {
                { "columns", jColumns },
                { "data", jData }
            }}
        };
    }

    //----------------------------- WRITE EVERYTHING TO FILE -----------------------------
    const int indentationSpaces = 2;
    ofs << std::setw(indentationSpaces) << jFile << std::endl;

    LINFO(fmt::format("Saved fieldline state to: {}{}", absPath, ext));
}

void FieldlinesState::setModel(fls::Model m) {
    _model = m;
}

void FieldlinesState::setTriggerTime(double t) {
    _triggerTime = t;
}

// Returns one of the extra quantity vectors, _extraQuantities[index].
// If index is out of scope an empty vector is returned and the referenced bool is false.
std::vector<float> FieldlinesState::extraQuantity(size_t index, bool& isSuccessful) const
{
    if (index < _extraQuantities.size()) {
        isSuccessful = true;
        return _extraQuantities[index];
    }
    else {
        isSuccessful = false;
        LERROR("Provided Index was out of scope!");
        return {};
    }
}

// Moves the points in @param line over to _vertexPositions and updates
// _lineStart & _lineCount accordingly.

void FieldlinesState::addLine(std::vector<glm::vec3>& line) {
    const size_t nNewPoints = line.size();
    const size_t nOldPoints = _vertexPositions.size();
    _lineStart.push_back(static_cast<GLint>(nOldPoints));
    _lineCount.push_back(static_cast<GLsizei>(nNewPoints));
    _numberOfLines++;
    _timeSinceLastInterpolation.push_back(0.0f);
    _vertexIndex.push_back(0);
    _vertexPositions.reserve(nOldPoints + nNewPoints);
    _vertexPositions.insert(
        _vertexPositions.end(),
        std::make_move_iterator(line.begin()),
        std::make_move_iterator(line.end())
    );
    line.clear();

}

void FieldlinesState::appendToExtra(size_t idx, float val) {
    _extraQuantities[idx].push_back(val);
}

glm::vec3 lerp(glm::vec3 a, glm::vec3 b, float t) {
    return a * (1.f - t) + b * t;
}

void FieldlinesState::moveLine(double dt) {
    bool forward;
    if (dt > DBL_EPSILON) {
        forward = true;
        atStart = false;
    }
    else {
        forward = false;
        atEnd = false;
    }

    bool vertexEnd = false;
    bool vertexStart = false;

    //each vertex on the rendered field line

    for (int i = 0; i < _numberOfLines; i++) {

        _timeSinceLastInterpolation[i] += float(dt);

        if (forward) {

            //if a vertex has reached its last vertex in the path line, stop updating the
            //field line positions
            if (atEnd) break;

            while (_timeSinceLastInterpolation[i] > _vertexTimes[i][_vertexIndex[i]]) {
                _timeSinceLastInterpolation[i] -= _vertexTimes[i][_vertexIndex[i]];
                _vertexIndex[i]++;

                //check if at end after increasing index

                //size - 3 as a margin to avoid bugs when simulation reaches end
                if (_vertexIndex[i] == _vertexPath[i].size() - 2) {
                    _timeSinceLastInterpolation[i] = _vertexTimes[i][_vertexIndex[i]-1];
                    int k = 0;
                    for (int j = _lineStart[i]; j < _lineCount[i] + _lineStart[i]; j++) {
                        _vertexPositions[j] = _fieldLines[i][_vertexIndex[i]][k];
                        k++;
                    }
                    vertexEnd = true;
                    break;
                }
            }

            if (vertexEnd) continue;

            if (std::find(_open[i].begin(), _open[i].end(), _vertexIndex[i]+1)
                != _open[i].end() &&
                std::find(_open[i].begin(), _open[i].end(), _vertexIndex[i])
                == _open[i].end()) {
                _vertexIndex[i]++;
                _timeSinceLastInterpolation[i] = 0.0f;
                int k = 0;
                for (int j = _lineStart[i]; j < _lineCount[i] + _lineStart[i]; j++) {
                    glm::vec3 a = _fieldLines[i][_vertexIndex[i]][k];
                    _vertexPositions[j] = a;
                    k++;
                }


            }

            else if (std::find(_closed[i].begin(), _closed[i].end(), _vertexIndex[i]+1)
                != _closed[i].end() &&
                std::find(_closed[i].begin(), _closed[i].end(), _vertexIndex[i])
                == _closed[i].end()) {
                _vertexIndex[i]++;
                _timeSinceLastInterpolation[i] = 0.0f;
                int k = 0;
                for (int j = _lineStart[i]; j < _lineCount[i] + _lineStart[i]; j++) {
                    glm::vec3 a = _fieldLines[i][_vertexIndex[i]][k];
                    _vertexPositions[j] = a;
                    k++;
                }

            }
            else {
                //linear interpolation
                //normalize t to [0,1]
                float t = _timeSinceLastInterpolation[i] / _vertexTimes[i][_vertexIndex[i]];
                int k = 0;
                for (int j = _lineStart[i]; j < _lineCount[i] + _lineStart[i]; j++) {
                    glm::vec3 a = _fieldLines[i][_vertexIndex[i]][k];
                    glm::vec3 b = _fieldLines[i][_vertexIndex[i] + 1][k];

                    _vertexPositions[j] = lerp(a, b, t);
                    k++;
                }
            }
        }
        else {

            if (atStart) break;

            //the inital position, end condition
            if (_vertexIndex[i] == 0) continue;

            while (_timeSinceLastInterpolation[i] < FLT_EPSILON) {
                _vertexIndex[i]--;
                _timeSinceLastInterpolation[i] += _vertexTimes[i][_vertexIndex[i]];

                //check if at end after decreasing index and updating timeSinceLastVertex
                if (_vertexIndex[i] == 0 && _timeSinceLastInterpolation[i] < FLT_EPSILON) {
                    int k = 0;
                    for (int j = _lineStart[i]; j < _lineCount[i] + _lineStart[i]; j++) {
                        _vertexPositions[j] = _fieldLines[i][_vertexIndex[i]][k];
                        k++;
                    }
                    _timeSinceLastInterpolation[i] = 0.0f;
                    vertexStart = true;
                    break;
                }
            }

            if (vertexStart) continue;

            if (std::find(_open[i].begin(), _open[i].end(), _vertexIndex[i]+1)
                != _open[i].end() &&
                std::find(_open[i].begin(), _open[i].end(), _vertexIndex[i])
                == _open[i].end()) {
                _vertexIndex[i]--;
                _timeSinceLastInterpolation[i] = _vertexTimes[i][_vertexIndex[i]];
                int k = 0;
                for (int j = _lineStart[i]; j < _lineCount[i] + _lineStart[i]; j++) {
                    _vertexPositions[j] = _fieldLines[i][_vertexIndex[i]][k];;
                    k++;
                }
            }
            else if (std::find(_closed[i].begin(), _closed[i].end(), _vertexIndex[i]+1)
                != _closed[i].end() &&
                std::find(_closed[i].begin(), _closed[i].end(), _vertexIndex[i])
                == _closed[i].end()) {
                _vertexIndex[i]--;
                _timeSinceLastInterpolation[i] = _vertexTimes[i][_vertexIndex[i]];
                int k = 0;
                for (int j = _lineStart[i]; j < _lineCount[i] + _lineStart[i]; j++) {
                    _vertexPositions[j] = _fieldLines[i][_vertexIndex[i]][k];
                    k++;
                }

            }
            else {
                //linear interpolation
                float t = _timeSinceLastInterpolation[i] / _vertexTimes[i][_vertexIndex[i]];
                int k = 0;
                for (int j = _lineStart[i]; j < _lineCount[i] + _lineStart[i]; j++) {
                    glm::vec3 a = _fieldLines[i][_vertexIndex[i]][k];
                    glm::vec3 b = _fieldLines[i][_vertexIndex[i]+1][k];

                    _vertexPositions[j] = lerp(a, b, t);
                    k++;
                }
            }
        }
    }


    if (vertexEnd) atEnd = true;
    if (vertexStart) atStart = true;
}

void FieldlinesState::addPath(std::vector<glm::vec3>& path, int i) {
    //
    //if ( _vertexPath.empty() ) {
    //    _vertexPath.push_back(path);
    //}
    //else {
    //    std::vector<std::vector<glm::vec3>> pathVec;
    //    pathVec.push_back(path);

    //    _vertexPath.insert(_vertexPath.begin() + i, pathVec.begin(), pathVec.end());
    //}
    if (_vertexPath.empty()) {
        _vertexPath.push_back(path);
    }
    else if (_vertexPath[i].empty()) {
        _vertexPath.push_back(path);
    }
    else {
        for (glm::vec3 p : path) {
            _vertexPath[i].push_back(p);
        }
    }

}

void FieldlinesState::addFieldLines(std::vector<std::vector<glm::vec3>>& fieldLines, int i){
    if (_fieldLines.empty()) {
        _fieldLines.push_back(fieldLines);
    }
    else if (_fieldLines[i].empty()) {
        _fieldLines.push_back(fieldLines);
    }
    else {
        for (std::vector<glm::vec3> f : fieldLines) {
            _fieldLines[i].push_back(f);
        }
    }
}

void FieldlinesState::addTimes(std::vector<float> times, int i) {
    if (_vertexTimes.empty()) {
        _vertexTimes.push_back(times);
    }
    //else {
    //    std::vector<std::vector<float>> timesVec;

    //    _vertexTimes.insert(_vertexTimes.begin() + i, timesVec.begin(), timesVec.end());
    //}

    else if (_vertexTimes[i].empty()) {
        _vertexTimes.push_back(times);
    }
    else {

        for (float p : times) {
            std::vector<float>& temp = _vertexTimes[i];
            temp.push_back(p);
            //_vertexTimes[i] = temp;
        }
    }
}

void FieldlinesState::addLinesToBeRendered() {
    for (int i = 0; i < _fieldLines.size(); i++) {
        addLine(_fieldLines[i][0]);
    }
}

//void FieldlinesState::computeTimes() {
//
//    int j = 0;
//    for (std::vector<glm::vec3>& v : _vertexPath) {
//        std::vector<float> times;
//        for (int i = 0; i < v.size() - 2; i++) {
//
//            //distance to next interpolation
//            float d = glm::length(v[i + 1] - v[i]);
//
//            //time to next interpolation = distance / velocity
//            float time = d / _vertexVelocities[j][i];
//
//            times.push_back(time);
//        }
//        j++;
//        _vertexTimes.push_back(times);
//    }
//}

void FieldlinesState::setExtraQuantityNames(std::vector<std::string> names) {
    _extraQuantityNames = std::move(names);
    _extraQuantities.resize(_extraQuantityNames.size());
}

const std::vector<std::vector<float>>& FieldlinesState::extraQuantities() const {
    return _extraQuantities;
}

const std::vector<std::string>& FieldlinesState::extraQuantityNames() const {
    return _extraQuantityNames;
}

const std::vector<GLsizei>& FieldlinesState::lineCount() const {
    return _lineCount;
}

const std::vector<GLint>& FieldlinesState::lineStart() const {
    return _lineStart;
}

fls::Model FieldlinesState::FieldlinesState::model() const {
    return _model;
}

size_t FieldlinesState::nExtraQuantities() const {
    return _extraQuantities.size();
}

double FieldlinesState::triggerTime() const {
    return _triggerTime;
}

const std::vector<std::vector<int>> FieldlinesState::closed() const {
    return _closed;
}

const std::vector<std::vector<int>> FieldlinesState::open() const {
    return _open;
}

const std::vector<glm::vec3>& FieldlinesState::vertexPositions() const {
    return _vertexPositions;
}
//
//const std::vector<size_t> FieldlinesState::vertexIndex() const {
//    return _vertexIndex;
//}

const std::vector < std::vector<glm::vec3>>& FieldlinesState::vertexPath() const {
    return _vertexPath;
}

const std::vector < std::vector<float>>& FieldlinesState::vertexVelocities() const {
    return _vertexVelocities;
}

const std::vector < std::vector<float>>& FieldlinesState::vertexTimes() const {
    return _vertexTimes;
}

const std::vector<std::vector<std::vector<glm::vec3>>>& FieldlinesState::fieldLines() const {
    return _fieldLines;
}

const std::vector<float>& FieldlinesState::timeSinceLastInterpolation() const {
    return _timeSinceLastInterpolation;
}

} // namespace openspace
