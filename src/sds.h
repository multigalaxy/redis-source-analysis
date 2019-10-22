/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
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
/**
 * sds相比c原生字符串的优势
 * 1）、长度可控，不用字节对齐，节省空间；取长度复杂度降低O(1)；可通过alloc预分配空间(参考sdsMakeRoomFor函数)，减少内存分配，而且不会内存溢出，默认是1024 * 1024=1M。
 * 2）、兼容c字符串的'\0'结尾标识，可以存储文本或任意二进制的数据，直接使用len决定了字符串大小而非拿'\0'判断结束；兼容c字符串库<string.h>的部分函数，如strcasecmp、printf等
 */
#ifndef __SDS_H  // __sds_header
#define __SDS_H

// sds字符串默认预分配大小是1M
#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

// 字符串别名，指向sdshdr的buf
typedef char *sds;

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
// 下面是保存字符串对象的结构
// __attribute__ 用来在函数或数据声明中设置属性，后面跟以((attribute-list))的形式。这里((packed))表示编译器不要按自己对齐优化，按实际存放字节数对齐

struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */  // 最低三位是类型，高5位带边长度
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* used */  // 实际使用字节数，等于字符串长度
    uint8_t alloc; /* excluding the header and null terminator */  // 分配的总空间字节大小
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];  // 字节数组，保存字符串
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

// 定义数据类型，在创建sdsnewlen新的字符串、改变长度等操作时都会重新计算数据类型，以便重新分配合适大小的内存，也避免内存溢出。
// 5，8，16，32，64代表此类型所能支持的最大长度；0 - 2^5-1，2^5 - 2^8-1等
#define SDS_TYPE_5  0  // 5位二进制的数据类型，不用来按此值分配，只做比较用
#define SDS_TYPE_8  1  // 8位即1字节的，极小整数
#define SDS_TYPE_16 2  // 16位=2字节的，小整数
#define SDS_TYPE_32 3  // 32位=4字节的，正常整数
#define SDS_TYPE_64 4  // 64位=8字节的，大整数
#define SDS_TYPE_MASK 7  // 掩码=00000111，用来与各个类型的flag做'与操作'求出对应的type值
#define SDS_TYPE_BITS 3  // flags的最低3位有效位区分类型
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));  // 宏定义，sh指向该数据结构体的头部，如下图：
/**
 * 内存结构：
 * -------------------------
 * | struct sdshdr##T      |
 * |------------------------
 * | len | alloc | buf     |
 * |------------------------
 * ↓             ↓
 * *sh          sds
 *
 * 1）buf是可变数组，无大小
 * 2）sizeof(struct sdshdr##T) == len + alloc
 * 3）*sh = (s) - (sizeof(struct sdshdr##T)，表示结构体的头部开始
 */

#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)  // flags向右移3位变是type5的长度

/**
 * 下面这些函数，调用频繁，因此直接内联设置，不用函数进出栈
 * 静态函数只能在本文件被调用。
 * 下面提供的非static函数，默认是extern属性，即所有文件可见的，可以调用本文件定义的static函数
 */

// 内联：获取sds实际长度
static inline size_t sdslen(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

// 内联：获取sds剩下的可用空间，alloc - len
static inline size_t sdsavail(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

/* 内联：设置sds实际使用长度 */
static inline void sdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

// 内联：给sds增加指定的len长度
static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

/* sdsalloc() = sdsavail() + sdslen() */
// 内联：获取sds可用的总空间大小
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

// 内联：设置分配的总空间为指定长度
static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

/**
 * 提供如下3类 第一类常用api
 **/
sds sdsnewlen(const void *init, size_t initlen);  // 生成s，按任意指定数据
sds sdsnew(const char *init);  // 生成s，指定字符串
sds sdsempty(void);  // 创建空串，len=0
sds sdsdup(const sds s);  // 构造一个新的字符串，s作为初始值，与cpy不同的是，dup是构造新地址，cpy只是内容变了
void sdsfree(sds s);  // 释放内存
sds sdsgrowzero(sds s, size_t len);  // 实际长度增加到指定长度，alloc长度也要增加len-curlen的量
sds sdscatlen(sds s, const void *t, size_t len);  // 连接两个串，任意串*t按指定的len连接到s
sds sdscat(sds s, const char *t);  // 连接两个串，必须是c字符串，内部调用的sdscatlen，len=strlen(t)
sds sdscatsds(sds s, const sds t);  // 连接两个sds串，内部调用sdscatlen，len=strlen(t)
sds sdscpylen(sds s, const char *t, size_t len);  // 将c字符串t按指定字节长度赋值给s的内存区域
sds sdscpy(sds s, const char *t);  // 字符串赋值给s，调用sdscpylen，len=strlen(t)

sds sdscatvprintf(sds s, const char *fmt, va_list ap);  // 格式化字符串，使用列表替换传入多个参数的方式来格式化指定的字符串
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);  // 格式化字符串，...代表可传入多个参数，其参数个数与fmt中格式符个数相同，内部调用sdscatvprintf
#endif

sds sdscatfmt(sds s, char const *fmt, ...);  // 格式化字符串，用while方式逐一匹配实现，拜托了对libc中较慢的sprintf的依赖。此处char const和const char等价，为啥不统一格式为const char？？？？
sds sdstrim(sds s, const char *cset);  // 移除指定sds两边从cset指定的所有字符
void sdsrange(sds s, int start, int end);  // 去除s指定范围的子串部分，即将start到end部分的子串重新拷贝给s所指的内存区域，改变原来的值
void sdsupdatelen(sds s);  // 重新计算s->len的值。（在可能修改过s的值之后）更新sds串的实际长度为strlen（如果有多个\0，将取到第一个出现的位置结束）获取到的长度
void sdsclear(sds s);  // 清空s串，长度为0，是个空串
int sdscmp(const sds s1, const sds s2);  // 比较两串s1和s2的字节大小
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);  // 按sep分割s串并返回分割后的数组，count为数组个数
void sdsfreesplitres(sds *tokens, int count);  // 释放由sdssplitlen分割出来的tokens数组
void sdstolower(sds s);  // s串转成小写
void sdstoupper(sds s);  // s串转成大写
sds sdsfromlonglong(long long value);  // 8字节=64位，从longlong整形数据转成sds串存储，替代sdscatprintf(sdsempty(),"%lld\n", value);
sds sdscatrepr(sds s, const char *p, size_t len); // 把sds串与给定的字符表达式聚合到一起，用字符表达式替换sds对应位置的字符
sds *sdssplitargs(const char *line, int *argc);  // 解析命令行参数并返回sds数组，并写入参数个数到argc里
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);  //  用to里每个字符，替换s里所有的由from指按顺序定的所有字符，如s="fHemo prut!", from="HH", to="01", 最终s="f1emo prut!"。因为H被最终匹配到的值计算
sds sdsjoin(char **argv, int argc, char *sep);  // 把c字符串数组按指定的sep连接符连接起来
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);  // 把sds字符串数组按指定的sep连接符连接起来

/* 第二类 底层api函数
 * Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);  // 为sds扩展alloc总内存空间
void sdsIncrLen(sds s, int incr);  // 增加s的实际长度。s被其他函数调用后可能需要改变长度。
sds sdsRemoveFreeSpace(sds s);  // 删除空闲空间，重新整理一个sds，alloc=len
size_t sdsAllocSize(sds s);  // 分配的所有内存大小，包括结构体头部+ alloc（实际字符串+空闲部分）+1个结束符
void *sdsAllocPtr(sds s);  // 返回s的控制结构的起始地址

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate.
 * 第三类 几个操作内存的函数
 **/
void *sds_malloc(size_t size);  // 申请给定大小的内存，返回申请好的指针（绑定到sds上）
void *sds_realloc(void *ptr, size_t size);  // 给指定指针，重新分配内存，并返回新分配的内存指针
void sds_free(void *ptr);  // 释放指定指针的内存，此指针一释放意味着变量内存找不着了，也就释放了变量的内存

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[]);
#endif

#endif
