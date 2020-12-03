#pragma once
#include <cstdint>
#include <cassert>
#include "fls.h"
#include <map>

namespace ugi {

    namespace tlsf {

        template< class T >
        class Vector {
        private:
            T*          _data;
            size_t      _size;
            size_t      _capacity;
        public:
            Vector( size_t size ) {
                _capacity = (size_t)ceil(log2(size));
                _data = new T[_capacity];
                _size = 0;
            }
            template< class ...ARGS >
            void emplace_back( ARGS&& ...args ) {
                if (_size == _capacity) {
                    auto data = new T[_capacity*2];
                    _capacity <<= 1;
                    for (size_t i = 0; i < _size; ++i) {
                        data[i] = std::move(_data[i]);
                    }
                    free(_data);
                    _data = (T*)data;
                }
                new(&_data[_size])T(std::forward<ARGS>(args)...);
                ++_size;
            }
            size_t size() const {
                return _size;
            }
            const T* begin() const {
                return _data;
            }
            const T* end() const {
                return  _data + _size;            
            }
            ~Vector() {
                if (_data) {
                    delete[]_data;
                }
            }
        };

        template< class T, size_t SIZE>
        class Array {
        private:
            T            _data[SIZE] = {};
        public:
            Array() {}
            T& operator[](size_t index) {
                return _data[index];
            }
            const T& operator[](size_t index) const {
                return _data[index];
            }
        };

        struct Node {
            Node*       prevPhy;
            Node*       nextPhy;
            //
            Node*       prev;
            Node*       next;
            uint32_t    offset;
            uint32_t    size : 31;
            uint32_t    free : 1;
        };

        struct Pool {
            size_t  size;
            Node*   node;
        };

        struct BitmapLevel{
            union {
                alignas(4) uint32_t val;
                struct {
                    alignas(2) uint16_t firstLevel;
                    alignas(2) uint16_t secondLevel;
                };
            };
            inline bool valid() {
                return val != 0xffffffff;
            }
            BitmapLevel()
                : val(0xffffffff)
            {}
            BitmapLevel( uint16_t f, uint16_t s)
                : firstLevel(f), secondLevel(s)
            {}
        };

        class Allocator {
        public:
            constexpr static size_t MinimiumAllocationSize = 16;                                // minimium allocation & alignment size
            constexpr static size_t FLC = sizeof(uint32_t) * 8 - 1;								// first level count max
            constexpr static size_t SLI = 5;                                                    // second level index bit count
            constexpr static size_t SLC = 1 << SLI;                                             // count of the segments per-first level
            constexpr static size_t FLM = MinimiumAllocationSize<<SLI;                          // first level max
            uint32_t BasePowLevel = tlsf_fls_sizet(FLM);
        private:
            uint32_t                            _1stBitmap;
            Array<uint32_t, 31>                 _2ndBitmap;
            Array<Array<Node*, SLC>, 31>        _allocationTable;
            std::map<uint32_t, Node*>           _allocationMap;
            Node*                               _head;
            //
            Node* createNode() {
                return new Node();
            }
            void destroyNode(Node* node) {
                delete node;
            }
        public:
            Allocator( uint32_t size )
                : _1stBitmap(0)
                , _2ndBitmap{}
                , _allocationTable{}
            {
                Node* node = createNode();
                node->size = size;
                node->prev = node->next = nullptr;
                node->nextPhy = node->prevPhy = nullptr;
                node->free = 1;
                node->offset = 0;
                insertFreeAllocation(node, false);
                _head = node;
            }

            BitmapLevel queryBitmapLevelForAlloc(size_t size) {
                BitmapLevel level;
                if (size <= FLM) {
                    level.firstLevel = 0;
                    level.secondLevel = (uint16_t)(size / MinimiumAllocationSize);
                    if (!(size & (MinimiumAllocationSize - 1))) {
                        --level.secondLevel;
                    }
                    return level;
                }
                else {
                    level.firstLevel = tlsf_fls_sizet(size);
                    size_t levelMin = 1ULL << level.firstLevel;
                    size_t segmentSize = levelMin >> SLI;
                    size += (segmentSize - 1);
                    size_t alignSize = (1ULL << (level.firstLevel - SLI));
                    level.secondLevel = (uint16_t)((size - levelMin) / alignSize);
                    if (level.secondLevel) {
                        --level.secondLevel;
                    }
                    else {
                        --level.firstLevel;
                        level.secondLevel = SLC - 1;
                    }
                    level.firstLevel -= (BasePowLevel - 1);
                    return level;
                }
            }

            BitmapLevel queryBitmapLevelForInsert(size_t size) {
                BitmapLevel level;
                if (size <= FLM) {
                    level.firstLevel = 0;
                    level.secondLevel = (uint16_t)(size / MinimiumAllocationSize) - 1;
                }
                else {
                    level.firstLevel = tlsf_fls_sizet(size);
                    size_t levelMin = 1ULL << level.firstLevel;
                    level.secondLevel = (uint16_t)((size - levelMin) / (levelMin >> SLI));
                    if (level.secondLevel == 0) {
                        --level.firstLevel;
                        level.secondLevel = SLC - 1;
                    }
                    else {
                        --level.secondLevel;
                    }
                    level.firstLevel -= level.secondLevel==0?1:0;
                    --level.secondLevel;
                    level.secondLevel&=(SLC-1);
                    level.firstLevel -= (BasePowLevel - 1);
                }
                return level;
            }

            size_t queryLevelSize(BitmapLevel level) {
                if (level.firstLevel) {
                    size_t firstLevelSize = 1ULL << (level.firstLevel + BasePowLevel - 1);
                    size_t rst = firstLevelSize + (firstLevelSize >> SLI)*(level.secondLevel + 1);
                    return rst;
                }
                else {
                    return ((size_t)level.secondLevel + 1) * MinimiumAllocationSize;
                }
            }

            size_t queryAlignedLevelSize(size_t size) {
                auto level = queryBitmapLevelForAlloc(size);
                auto levelSize = queryLevelSize(level);
                return levelSize;
            }

            //  看这个级别是不是有空闲块
            bool queryFreeStatus( BitmapLevel level ) {
                if( !_1stBitmap & (1<<level.firstLevel) ) {
                    return false;
                }
                uint32_t rst = _2ndBitmap[level.firstLevel] & (1<<level.secondLevel);
                if(rst != 0) {
                    return true;
                }
                return false;
            }

            BitmapLevel findLevelForSplit( BitmapLevel baseLevel ) {
                for( uint16_t firstLevel = baseLevel.firstLevel; firstLevel < FLC; ++firstLevel) {
                    if(!(_1stBitmap&(1<<firstLevel))) {
                        baseLevel.secondLevel = 0;
                        continue;
                    }
                    for( uint16_t secondLevel = baseLevel.secondLevel; secondLevel<SLC; ++secondLevel) {
                        BitmapLevel acquiredLevel = {firstLevel, secondLevel};
                        if(queryFreeStatus(acquiredLevel)) {
                            return acquiredLevel;
                        }
                    }
                }
                return BitmapLevel();
            }

            Node* queryAllocationWithFreeLevel( BitmapLevel level ) {
                Node** levelHeaderPtr = &_allocationTable[level.firstLevel][level.secondLevel];
                Node* originHeader = *levelHeaderPtr;
                assert(originHeader && "it must not be nullptr!");
                Node* nextFreeAlloc = originHeader->next;
                *levelHeaderPtr = nextFreeAlloc;
                if(nextFreeAlloc) {
                    nextFreeAlloc->prev = nullptr;
                } else {
                    // 如果这一级分配了之后就没有空间的内存块了，那么更新 bitmap
                    _2ndBitmap[level.firstLevel] &= ~(1<<level.secondLevel);
                    if( 0 == _2ndBitmap[level.firstLevel]) {
                        _1stBitmap &= ~(1<<level.firstLevel);
                    }
                }
                return originHeader;
            }

            void removeFreeAllocationAndUpdateBitmap( Node* allocation ) {
                BitmapLevel level = queryBitmapLevelForInsert(allocation->size);
                Node** levelHeaderPtr = &_allocationTable[level.firstLevel][level.secondLevel];
                Node* nextFreeAlloc = allocation->next; // could be nullptr
                Node* prevFreeAlloc = allocation->prev;

                if(prevFreeAlloc) {
                    prevFreeAlloc->next = allocation->next;
                } else {
                    *levelHeaderPtr = nextFreeAlloc;
                }
                if(nextFreeAlloc) {
                    nextFreeAlloc->prev = prevFreeAlloc;
                }
                if( nullptr == *levelHeaderPtr) { // 需要更新bitmap
                    _2ndBitmap[level.firstLevel] &= ~(1<<level.secondLevel);
                    if( 0 == _2ndBitmap[level.firstLevel]) {
                        _1stBitmap &= ~(1<<level.firstLevel);
                    }
                }
            }

            void removeFreeAllocationAndUpdateBitmap( Node* allocation, BitmapLevel level ) /*pass*/ {
                Node** levelHeaderPtr = &_allocationTable[level.firstLevel][level.secondLevel];
                Node* nextFreeAlloc = allocation->next; // could be nullptr
                Node* prevFreeAlloc = allocation->prev;
                if(prevFreeAlloc) {
                    prevFreeAlloc->next = allocation->next;
                } else {
                    *levelHeaderPtr = nextFreeAlloc;
                }
                if(nextFreeAlloc) {
                    nextFreeAlloc->prev = prevFreeAlloc;
                }
                if( nullptr == *levelHeaderPtr) { // 需要更新bitmap
                    _2ndBitmap[level.firstLevel] &= ~(1<<level.secondLevel);
                    if( 0 == _2ndBitmap[level.firstLevel]) {
                        _1stBitmap &= ~(1<<level.firstLevel);
                    }
                }
            }

            void insertFreeAllocation( Node* allocation, bool mergeCheck = false ) /*pass*/ {
                BitmapLevel level;
                if(mergeCheck) {
                    Node* prevPhyAlloc = allocation->prevPhy;
                    Node* nextPhyAlloc = allocation->nextPhy;
                    ///////////////// Node* mergedAlloc = allocation;
                    if(prevPhyAlloc && prevPhyAlloc->free) {
                        removeFreeAllocationAndUpdateBitmap(prevPhyAlloc);
                        prevPhyAlloc->size += allocation->size;
                        // allocation = prevPhyAlloc;
                        prevPhyAlloc->nextPhy = nextPhyAlloc;
                        destroyNode(allocation);
                        allocation = prevPhyAlloc;
                    }
                    if(nextPhyAlloc) {
                        if(nextPhyAlloc->free) {
                            removeFreeAllocationAndUpdateBitmap(nextPhyAlloc);
                            allocation->size += nextPhyAlloc->size;
                            auto nextNextAlloc = nextPhyAlloc->nextPhy;
                            if(nextNextAlloc) {
                                nextNextAlloc->prevPhy = allocation;
                            }
                            allocation->nextPhy = nextNextAlloc;
                            destroyNode(nextPhyAlloc);
                        } else {
                            nextPhyAlloc->prevPhy = allocation;
                        }
                    }
                }
                level = queryBitmapLevelForInsert(allocation->size);
                Node** levelHeaderPtr = &_allocationTable[level.firstLevel][level.secondLevel];
                Node* originHeader = *levelHeaderPtr;
                *levelHeaderPtr = allocation;
                allocation->next = originHeader;
                allocation->prev = nullptr;
                if(!originHeader) { // update bitmap if need
                    _2ndBitmap[level.firstLevel] |= 1<<level.secondLevel;
                    _1stBitmap |= 1<<(level.firstLevel);
                } else {
                    originHeader->prev = allocation;
                }
            }

            Node* splitAllocation( BitmapLevel level, size_t size ) {
                Node* targetAlloc  = queryAllocationWithFreeLevel(level);
                assert(targetAlloc && "it must not be nullptr!");
                assert(targetAlloc->size >= size );
                removeFreeAllocationAndUpdateBitmap(targetAlloc, level);
                if(targetAlloc->size - size < MinimiumAllocationSize) {
                    return targetAlloc;  // 剩余的太小了，就不分割了
                }
                // splited free allocation
                auto splitedNode = createNode();
                splitedNode->nextPhy = targetAlloc->nextPhy;
                if(splitedNode->nextPhy) {
                    splitedNode->nextPhy->prevPhy = splitedNode;
                }
                splitedNode->prevPhy = targetAlloc;
                targetAlloc->nextPhy = splitedNode;
                splitedNode->offset = targetAlloc->offset + size;
                splitedNode->size = targetAlloc->size - size;
                splitedNode->free = 1;
                targetAlloc->size = size;
                //
                insertFreeAllocation(splitedNode);
                return targetAlloc;
            }

            Node* queryFreeAllocation( size_t size ) {
                BitmapLevel level = queryBitmapLevelForAlloc(size);
                if(queryFreeStatus(level)) { // 恰好有空间块可以分配
                    auto allocation = queryAllocationWithFreeLevel(level);
                    return allocation;
                } else { // 没有合适的内存块分配，找一个可分割的大些的内存块
                    size = queryLevelSize(level);
                    if(++level.secondLevel >= SLC) {
                        ++level.firstLevel;
                        level.secondLevel = 0;
                    }
                    BitmapLevel splitLevel = findLevelForSplit(level);
                    if(!splitLevel.valid()) {
                        return nullptr; // 找不着合适的块了，不能再分配了
                    }
                    // 拿着合适的块分割，再分配
                    Node* allocation = splitAllocation(splitLevel, size);
                    return allocation;
                }
            }

            uint32_t alloc( size_t size ) {
                auto node = queryFreeAllocation(size);
                if(!node) {
                    return ~0;
                } else {
                    _allocationMap[node->offset] = node;
                    node->free = 0;
                    return node->offset;
                }
            }

            uint32_t realloc( uint32_t offset, size_t size ) {
                Node* node = _allocationMap[offset];
                assert(node);
                Node* nextPhyAlloc = node->nextPhy;
                if(nextPhyAlloc && nextPhyAlloc->free) {
                    size_t alignedLevelSize = queryAlignedLevelSize(size);
                    size_t mergedSize = nextPhyAlloc->size + node->size;
                    if( (mergedSize >= size) && (mergedSize < alignedLevelSize) ) {
                        removeFreeAllocationAndUpdateBitmap(nextPhyAlloc);
                        node->size = mergedSize;
                        return offset;
                    }
                }
                insertFreeAllocation(node, true); // 回收旧的，分配新的
                node->free = 1;
                _allocationMap.erase(offset);
                return alloc(size);
            }

            void free( uint32_t offset ) {
                Node* node = _allocationMap[offset];
                assert(node);
                insertFreeAllocation(node, true);
                node->free = 1;
                _allocationMap.erase(offset);
            }

        };

    }

}