/*
 * 底层数据结构：字典
 * 字典使用哈希表作为底层实现，一个哈希表有多个哈希结点，一个节点保存一个键值对
 * 字典使用在表示数据库、哈希键等
 *
 * Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

#define DICT_OK 0
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
#define DICT_NOTUSED(V) ((void) V)

/* 底层哈希表节点结构体 */
typedef struct dictEntry {
    // 键=8字节
    void *key;

    // 值=取最大长度，然后对齐到8的倍数
    union {
        void *val;  // 8
        uint64_t u64;  // 8 无符号长整型，【用于其他模块使用】
        int64_t s64;  // 8【用于其他模块使用】
        double d;  // 8【用于其他模块使用】
    } v;

    // 指向哈希表下个节点，将哈希冲突的键值对串起来，形成链表=8字节
    struct dictEntry *next;
} dictEntry;

/* 定义字典类型特定函数
 * 即开放给需要的模块实现自己的函数功能，不需要的置位NULL即可，参考server.c的setDictType结构
 * */
typedef struct dictType {
    // 计算哈希值的函数
    unsigned int (*hashFunction)(const void *key);

    // 复制键的函数，地址变了
    void *(*keyDup)(void *privdata, const void *key);

    // 复制键值的函数，地址变了
    void *(*valDup)(void *privdata, const void *obj);

    // 对比键的函数
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);

    // 销毁键的函数
    void (*keyDestructor)(void *privdata, void *key);

    // 销毁键值的函数
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

/* 定义底层哈希表结构 This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */
typedef struct dictht {
    // 依赖哈希结点，指定二维哈希表元素表，第一维存哈希算法后的节点地址，第二维存哈希冲突的那个链表的各个节点地址（参看dictEntry的next指针）
    dictEntry **table;

    // 哈希表大小
    unsigned long size;

    // 哈希表大小掩码，计算索引值，总是等于size-1
    unsigned long sizemask;

    // 该哈希表已有节点的数量
    unsigned long used;
} dictht;

/* 定义字典结构 */
typedef struct dict {
    // 指明特定类型函数
    dictType *type;

    // 私有数据，传给特定函数的参数
    void *privdata;

    // 声明两个哈希表，只使用第一个，第二个用来对ht[0]进行rehash操作时使用
    dictht ht[2];

    // 进行rehash操作的进度，没有则为-1
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */

    // 当前正在执行的迭代器数量
    int iterators; /* number of iterators currently running */
} dict;

/* 定义字典迭代器
 * If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */
typedef struct dictIterator {
    dict *d;  // 指明要迭代的字典
    long index;  // 迭代的索引位置
    int table, safe;  // safe标识是否可以在字典结构上继续修改、查找等操作，如果不能只能查询即调用迭代下一个dictNext()
    dictEntry *entry, *nextEntry;  // 当前和下一个迭代到的元素
    /* unsafe iterator fingerprint for misuse detection. */
    long long fingerprint;
} dictIterator;

/* 定义字典扫描函数 */
typedef void (dictScanFunction)(void *privdata, const dictEntry *de);

/* 哈希表的初始大小 This is the initial size of every hash table */
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- 定义操作字典表的宏函数，由各个调用模块自己实现需要的type函数 Macros ------------------------------------*/
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else \
        entry->v.val = (_val_); \
} while(0)

#define dictSetSignedIntegerVal(entry, _val_) \
    do { entry->v.s64 = _val_; } while(0)

#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { entry->v.u64 = _val_; } while(0)

#define dictSetDoubleVal(entry, _val_) \
    do { entry->v.d = _val_; } while(0)

#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        entry->key = (_key_); \
} while(0)

#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) : \
        (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
#define dictIsRehashing(d) ((d)->rehashidx != -1)

/* 模块 API */
dict *dictCreate(dictType *type, void *privDataPtr);  // 创建哈希表，指针privDataPtr由需要使用字典的模块各自生成并传入
int dictExpand(dict *d, unsigned long size);  // 哈希表扩展，返回0或1
int dictAdd(dict *d, void *key, void *val);  // 先添加key生成结点，再用结点添加val，返回0或1
dictEntry *dictAddRaw(dict *d, void *key);  // 底层添加指定key，生成字典元素结点，dictadd时可调用，也可以作为api通过返回值判断是否key存在等
int dictReplace(dict *d, void *key, void *val);  // 用参数指定的val替换指定key的val
dictEntry *dictReplaceRaw(dict *d, void *key);  // 先查找key是否存在，存在就直接返回NULL，否则返回新添加的指定key的哈希结点
int dictDelete(dict *d, const void *key);  // 删除指定key并释放内存
int dictDeleteNoFree(dict *d, const void *key);  // 删除指定key，不释放内存
void dictRelease(dict *d);  // 删除指定字典
dictEntry * dictFind(dict *d, const void *key);  // 查找指定key是否存在
void *dictFetchValue(dict *d, const void *key);  // 获取指定key的值，返回指针
int dictResize(dict *d);  // 按节点使用个数重新调整字典大小
dictIterator *dictGetIterator(dict *d);  // 创建一个指定字典的非安全迭代器
dictIterator *dictGetSafeIterator(dict *d);  // 创建一个指定字典的安全迭代器
dictEntry *dictNext(dictIterator *iter);  // 迭代下一个节点
void dictReleaseIterator(dictIterator *iter);  // 释放迭代器
dictEntry *dictGetRandomKey(dict *d);  // 随机获取一个节点
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);  // 随机获取几个键，并返回实际取到的数量
void dictGetStats(char *buf, size_t bufsize, dict *d);  // 获取指定字典的一些统计数据
unsigned int dictGenHashFunction(const void *key, int len);  // 生成hash值函数，最新的为Murmurhash3算法
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);  // 生成大小写敏感的hash值函数，最新的为Murmurhash3算法
void dictEmpty(dict *d, void(callback)(void*));  // 置空字典
void dictEnableResize(void);  // 全局：设置能重调哈希表大小的全局变量
void dictDisableResize(void);  // 全局：设置不能重调哈希表大小全局变量
int dictRehash(dict *d, int n);  // 重新哈希调整指定字典
int dictRehashMilliseconds(dict *d, int ms);  // 在指定毫秒内，不断的批量迁移数据，每批100个
void dictSetHashFunctionSeed(unsigned int initval);  // 设置哈希函数的随机数种子，默认是5381，server.c里设置
unsigned int dictGetHashFunctionSeed(void);  // 获取哈希函数随机种子
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata);  // 按指定扫描函数和参数，迭代一遍指定的字典

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
