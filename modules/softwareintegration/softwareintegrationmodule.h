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

#ifndef __OPENSPACE_MODULE_SOFTWAREINTEGRATION___SOFTWAREINTEGRATIONMODULE___H__
#define __OPENSPACE_MODULE_SOFTWAREINTEGRATION___SOFTWAREINTEGRATIONMODULE___H__

#include <openspace/util/openspacemodule.h>
#include <modules/softwareintegration/utils/syncablestorage.h>
#include <openspace/documentation/documentation.h>
#include <modules/softwareintegration/network/network.h>

namespace openspace {

namespace softwareintegration {

class Session;

} // namespace softwareintegration

class SoftwareIntegrationModule : public OpenSpaceModule {
    friend class softwareintegration::Session;

public:
    constexpr static const char* Name = "SoftwareIntegration";

    SoftwareIntegrationModule();
    ~SoftwareIntegrationModule();

    void storeData(
        const SyncableStorage::Identifier& identifier,
        const simp::DataKey key,
        const std::vector<std::byte>& data
    );
    template <typename T>
    bool fetchData(
        const SyncableStorage::Identifier& identifier,
        const storage::Key key,
        T& resultingData
    );
    bool isDataDirty(
        const SyncableStorage::Identifier& identifier,
        const storage::Key key
    );
    void setDataLoaded(
        const SyncableStorage::Identifier& identifier,
        const storage::Key key
    );
    bool dataLoaded(
        const SyncableStorage::Identifier& identifier,
        const storage::Key key
    );

    std::vector<documentation::Documentation> documentations() const override;

    scripting::LuaLibrary luaLibrary() const override;

private:
    void internalInitialize(const ghoul::Dictionary&) override;
    void internalDeinitialize() override;

    std::vector<Syncable*> getSyncables();

    // Centralized storage for datasets
    SyncableStorage _syncableStorage;

    std::shared_ptr<softwareintegration::network::NetworkState> _networkState;
};

namespace softwareintegration {

void setDefaultDeltaTimeSteps();

} // namespace softwareintegration

} // namespace openspace

#include "softwareintegrationmodule.inl"

#endif // __OPENSPACE_MODULE_SOFTWAREINTEGRATION___SOFTWAREINTEGRATIONMODULE___H__
