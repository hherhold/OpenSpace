/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2017                                                               *
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

#include <modules/globebrowsing/chunk/chunknode.h>

#include <ghoul/misc/assert.h>

#include <stack>
#include <queue>

namespace {
	const std::string _loggerCat = "ChunkNode";
}

namespace openspace {
namespace globebrowsing {

ChunkNode::ChunkNode(const Chunk& chunk, ChunkNode* parent)
    : _chunk(chunk)
    , _parent(parent)
    , _children({ nullptr, nullptr, nullptr, nullptr })
{}

bool ChunkNode::isRoot() const {
    return _parent == nullptr;
}

bool ChunkNode::isLeaf() const {
    return _children[0] == nullptr;
}

bool ChunkNode::updateChunkTree(const RenderData& data) {
    if (isLeaf()) {
        Chunk::Status status = _chunk.update(data);
        if (status == Chunk::Status::WantSplit) {
            split();
        }
        return status == Chunk::Status::WantMerge;
    }
    else {
        char requestedMergeMask = 0;
        for (int i = 0; i < 4; ++i) {
            if (_children[i]->updateChunkTree(data)) {
                requestedMergeMask |= (1 << i);
            }
        }

        bool allChildrenWantsMerge = requestedMergeMask == 0xf;
        bool thisChunkWantsSplit = _chunk.update(data) == Chunk::Status::WantSplit;

        if (allChildrenWantsMerge && !thisChunkWantsSplit) {
            merge();
        }

        return false;
    }
}

void ChunkNode::addSites(const std::vector<std::shared_ptr<Subsite>> subSites) {
	_subsites = subSites;
}

void ChunkNode::depthFirst(const std::function<void(const ChunkNode&)>& f) const {
    f(*this);
    if (!isLeaf()) {
        for (int i = 0; i < 4; ++i) {
            _children[i]->depthFirst(f);
        }
    }
}

void ChunkNode::breadthFirst(const std::function<void(const ChunkNode&)>& f) const {
    std::queue<const ChunkNode*> Q;

    // Loop through nodes in breadths first order
    Q.push(this);
    while (Q.size() > 0) {
        const ChunkNode* node = Q.front();
        Q.pop();

        f(*node);

        // Add children to queue, if any
        if (!node->isLeaf()) {
            for (int i = 0; i < 4; ++i) {
                Q.push(node->_children[i].get());
            }
        }
    }
}

void ChunkNode::reverseBreadthFirst(const std::function<void(const ChunkNode&)>& f) const
{
    std::stack<const ChunkNode*> S;
    std::queue<const ChunkNode*> Q;

    // Loop through nodes in breadths first order
    Q.push(this);
    while (Q.size() > 0) {
        const ChunkNode* node = Q.front(); 
        Q.pop();

        // Add node to future stack
        S.push(node);

        // Add children to queue, if any
        if (!node->isLeaf()) {
            for (const auto& c : node->_children) {
                Q.push(c.get());
            }
            //for (int i = 0; i < 4; ++i) {
            //    Q.push(node->_children[i].get());
            //}
        }
    }

    // Loop through all nodes in stack, this will be reversed breadth first 
    while (S.size() > 0) {
        f(*S.top());
        S.pop();
    }
}

const ChunkNode& ChunkNode::find(const Geodetic2& location) const {
    const ChunkNode* node = this;

    while (!node->isLeaf()) {
        const Geodetic2 center = node->_chunk.surfacePatch().center();
        int index = 0;
        if (center.lon < location.lon) {
            ++index;
        }
        if (location.lat < center.lat) {
            ++index;
            ++index;
        }
        node = &(node->getChild((Quad)index));
    }
    return *node;
}

const ChunkNode& ChunkNode::getChild(Quad quad) const {
    return *_children[quad];
}

void ChunkNode::split(int depth) {
    if (depth > 0 && isLeaf()) {
        for (size_t i = 0; i < _children.size(); ++i) {
            Chunk chunk(_chunk.owner(), _chunk.tileIndex().child((Quad)i));
			globebrowsing::GeodeticPatch patch = chunk.surfacePatch();
			std::vector<std::shared_ptr<Subsite>> tempSites;
			std::unique_ptr<ChunkNode> temp = std::make_unique<ChunkNode>(chunk, this);
			if (this != nullptr) {
				for (int k = 0; k < this->_subsites.size(); ++k) {
					globebrowsing::Geodetic2 temp = globebrowsing::Geodetic2{ this->_subsites.at(k)->lat, this->_subsites.at(k)->lon } / 180.0 * glm::pi<double>();
					if (patch.contains(temp)) {
						tempSites.push_back(this->_subsites.at(k));
					}
				}
				temp->addSites(tempSites);
			}
			_children[i] = std::move(temp);
        }
    }

    if (depth - 1 > 0) {
        for (int i = 0; i < _children.size(); ++i) {
            _children[i]->split(depth - 1);
        }
    }
}

void ChunkNode::merge() {
    for (int i = 0; i < _children.size(); ++i) {
        if (_children[i] != nullptr) {
            _children[i]->merge();
        }
        _children[i] = nullptr;
    }
    
    ghoul_assert(isLeaf(), "ChunkNode must be leaf after merge");
}

const Chunk& ChunkNode::getChunk() const {
    return _chunk;
}

const std::vector<std::shared_ptr<Subsite>> ChunkNode::getSubsites() const {
	return _subsites;
}

} // namespace globebrowsing
} // namespace openspace
