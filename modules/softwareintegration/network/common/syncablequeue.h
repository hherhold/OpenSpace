/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2022                                                               *
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

#ifndef __OPENSPACE_MODULE_SOFTWAREINTEGRATION___SYNCABLEQUEUE___H__
#define __OPENSPACE_MODULE_SOFTWAREINTEGRATION___SYNCABLEQUEUE___H__

#include <openspace/util/syncable.h>
#include <openspace/util/concurrentqueue.h>

#include <openspace/util/syncbuffer.h>

namespace openspace {

class SyncBuffer;

template <typename T>
class SyncableQueue : public Syncable {
public:
    // virtual void preSync(bool isMaster) override;
    virtual void encode(SyncBuffer* syncBuffer) override;
    virtual void decode(SyncBuffer* syncBuffer) override;
    // virtual void postSync(bool isMaster) override;

    void push(T &&item);
    void push(const T& item);

    T pop();
    void pop(T& item);

    size_t size() const;

    bool empty() const;
    
private:
	ConcurrentQueue<T> _messages;

    std::mutex _clientMessageMutex;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_SOFTWAREINTEGRATION___SYNCABLEQUEUE___H__
