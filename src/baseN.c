/**
 Copyright © 2015 2coding. All Rights Reserved.
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 1. Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 2. Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 
 3. The name of the author may not be used to endorse or promote products derived
 from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

#include "baseN.h"

static const int _chunklen = 76;

static size_t baseN_encoding_length(const baseN *p, size_t datalen);
static void baseN_encoding(const baseN *p, const byte *data, size_t datalen, byte *buf, size_t *buflen);

static size_t baseN_decoding_length(const baseN *p, size_t datalen);
static BOOL baseN_decoding(const baseN *p, const byte *data, size_t datalen, byte *buf, size_t *buflen);

void baseN_init(baseN *p, byte group, byte bitslen, char *entable, byte *detable, size_t mask) {
    cdcassert(p);
    cdcassert(entable);
    cdcassert(detable);
    cdcassert(group > 0);
    cdcassert(bitslen > 0 && bitslen < 8);
    cdcassert(mask > 0);
    
    p->group = group;
    p->bitslen = bitslen;
    p->egroup = group * 8 / bitslen;
    p->chunkled = TRUE;
    p->padding = TRUE;
    p->entable = entable;
    p->detable = detable;
    p->mask = mask;
}

CODECode baseN_setup(baseN *bn, CODECOption opt, va_list args) {
    CODECode code = CODECOk;
    switch (opt) {
        case CODECBaseNChunkled:
            bn->chunkled = va_arg(args, long);
            break;
            
        case CODECBaseNPadding:
            bn->padding = va_arg(args, long);
            break;
            
        default:
            code = CODECIgnoredOption;
            break;
    }
    
    return code;
}

CODECode baseN_work(baseN *bn, CODECBase *p, const CODECData *data) {
    if (p->method == CODECEncoding) {
        size_t buflen = baseN_encoding_length(bn, data->length);
        if (!buflen) {
            return CODECEmptyInput;
        }
        
        CODECDATA_REINIT(p, buflen);
        baseN_encoding(bn, data->data, data->length, p->result->data, &p->result->length);
    }
    else {
        size_t buflen = baseN_decoding_length(bn, data->length);
        if (!buflen) {
            return CODECEmptyInput;
        }
        
        CODECDATA_REINIT(p, buflen);
        if (!baseN_decoding(bn, data->data, data->length, p->result->data, &p->result->length)) {
            return CODECInvalidInput;
        }
    }
    
    return CODECOk;
}

#pragma mark - encoding
size_t baseN_encoding_length(const baseN *p, size_t datalen) {
    if (!datalen) {
        return 0;
    }
    
    double times = (double)datalen / p->group;
    times = ceil(times);
    
    size_t len = times * p->egroup;
    if (p->chunkled) {
        len += (len  - 1) / _chunklen * 2;
    }
    
    return len;
}

static size_t _chunk(const baseN *p, byte *buf, size_t idx) {
//    if (p->chunkled
//        && idx > 0
//        && ((idx % _chunklen == 0) || ((idx + p->egroup - 1) / _chunklen > idx / _chunklen ))) {
//        buf[idx++] = '\r';
//        buf[idx++] = '\n';
//    }
    if (p->chunkled
        && idx > 0
        && idx % _chunklen == 0) {
        buf[idx++] = '\r';
        buf[idx++] = '\n';
    }
    
    return idx;
}

static size_t _encoding_group(const baseN *p, const byte *data, byte *buf, size_t idx, byte group) {
//    idx = _chunk(p, buf, idx);
    
    size_t i = 0;
    uint64_t t = 0;
    byte *tmp = (byte *)&t;
    for (i = 0; i < group; ++i) {
        tmp[i + 1] = data[group - i - 1];
    }
    
    int bits = (group + 1) * 8;
    const char *table = p->entable;
    uint64_t mask = p->mask;
    while (bits > 8) {
        bits -= p->bitslen;
        
        idx = _chunk(p, buf, idx);
        buf[idx++] = table[(t >> bits) & mask];
    }
    
    return idx;
}

static size_t _encoding_left(const baseN *p, const byte *data, size_t datalen, byte *buf, size_t idx) {
    size_t left = datalen % p->group;
    if (!left) {
        return idx;
    }
    
    idx = _chunk(p, buf, idx);
    idx = _encoding_group(p, data + (datalen - left), buf, idx, left);
    
    float t = left * 8.0f / p->bitslen;
    t = ceilf(t);
    int padding = p->egroup - t;
    while (p->padding && padding > 0) {
        idx = _chunk(p, buf, idx);
        buf[idx++] = '=';
        --padding;
    }
    
    return idx;
}

void baseN_encoding(const baseN *p, const byte *data, size_t datalen, byte *buf, size_t *buflen) {
    cdcassert(p);
    cdcassert(buf);
    cdcassert(buflen);
    if (!data || !datalen) {
        *buflen = 0;
        buf[0] = 0;
        return;
    }
    
    size_t i = 0, idx = 0, n = datalen / p->group;
    for (i = 0; i < n; ++i) {
        idx = _encoding_group(p, data + i * p->group, buf, idx, p->group);
    }
    
    *buflen = _encoding_left(p, data, datalen, buf, idx);
}

#pragma mark - decoding
size_t baseN_decoding_length(const baseN *p, size_t datalen) {
    if (!datalen) {
        return 0;
    }
    
    double len = (double)datalen / p->egroup;
    len = ceil(len);
    return len * p->group;
}

long _check_char(const baseN *p, byte c) {
    if (c == '\r' || c == '\n' || c == '=') {
        return 0;
    }
    
    if (p->detable[c] == 0xff) {
        return -1;
    }
    
    return 1;
}

size_t _decoding_group(const baseN *p, uint64_t tmp, size_t group, byte *buf, size_t idx) {
    size_t k = 0, bitlens = 8 * (p->group - 1);
    for (k = 0; k < group; ++k) {
        buf[idx++] = (tmp >>  (bitlens -  8 * k)) & 0xff;
    }
    
    return idx;
}

BOOL baseN_decoding(const baseN *p, const byte *data, size_t datalen, byte *buf, size_t *buflen) {
    cdcassert(p);
    cdcassert(buflen);
    cdcassert(buf);
    if (!data || !datalen) {
        buf[0] = 0;
        *buflen = 0;
        return TRUE;
    }
    
    size_t i = 0, k = 0, idx = 0;
    uint64_t t = 0;
    long ret = 0;
    const byte *table = p->detable;
    uint64_t mask = p->mask;
    const byte bitlens = p->bitslen * (p->egroup - 1);
    for (i = 0; i < datalen; ++i) {
        ret = _check_char(p, data[i]);
        if (ret == 0) {
            continue;
        }
        else if (ret < 0) {
            return FALSE;
        }
        
        t |= (table[data[i]] & mask) << (bitlens - p->bitslen * k);
        ++k;
        if (k == p->egroup) {
            idx = _decoding_group(p, t, p->group, buf, idx);
            k = 0;
            t = 0;
        }
    }
    
    if (k > 0 && t != 0) {
        idx = _decoding_group(p, t, k * p->bitslen / 8, buf, idx);
    }
    
    *buflen = idx;
    return TRUE;
}