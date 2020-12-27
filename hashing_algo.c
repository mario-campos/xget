#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <openssl/md5.h>

#include "hashing_algo.h"
#include "file.h"

int md5_equal(unsigned char hash1[], unsigned char hash2[]) {
	return memcmp(hash1, hash2, MD5_DIGEST_LENGTH) == 0;
}

void freeHashAlgo(HashAlgorithm *algo) {
    free(algo->ctx);
    free(algo);
}

static HashAlgorithm* createMD5() {
    HashAlgorithm *md5 = (HashAlgorithm*) malloc(sizeof (HashAlgorithm));
    MD5_CTX *md5_context = (MD5_CTX*) malloc(sizeof (MD5_CTX));
    md5->hashType = HashType_MD5;
    md5->ctx = (void*) md5_context;
    md5->hashSize = MD5_DIGEST_LENGTH;
    md5->equals = md5_equal;
    md5->init = MD5_Init;
    md5->update = MD5_Update;
    md5->final = MD5_Final;
    return md5;
}

HashAlgorithm* createHashAlgorithm(char *hashAlgorithm) {
    if (!strcmp("MD5", hashAlgorithm)) {
        return createMD5();
    }
    else {
        return NULL;
    }
}

static void updateHash(void *buffer, unsigned int bytesRead, void *ctx) {
    HashAlgorithm *algo = (HashAlgorithm*) ctx;
    algo->update(algo->ctx, buffer, bytesRead);
}

void getHashFromFile(HashAlgorithm *algo, char *filename, unsigned char *hash) {
    algo->init(algo->ctx);
    readFile(filename, updateHash, algo);
    algo->final(hash, algo->ctx);
}

void getHashFromStringIter(HashAlgorithm *algo, char *string, unsigned char *hash, int numIterations) {
    int i = 0;
    algo->init(algo->ctx);
    for (i = 0; i < numIterations; i++) {
        algo->update(algo->ctx, (const void**)&string, strlen(string));
    }
    algo->final(hash, algo->ctx);
}

void getHashFromString(HashAlgorithm *algo, char *string, unsigned char *hash) {
    getHashFromStringIter(algo, string, hash, 1);
}

static unsigned char hexCharToBin(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else {
        return 0;
    }
}

static unsigned char hexToBin(char c1, char c2) {
    unsigned char temp = 0;
    temp = hexCharToBin(c2);
    temp |= hexCharToBin(c1) << 4;
    return temp;
}

unsigned char* convertHashStringToBinary(HashAlgorithm *algo, char *hashString) {
   unsigned int i, j;
    unsigned char *hashBinary = (unsigned char*) malloc(sizeof (unsigned char) * algo->hashSize);
    for (i = 0, j = 0; i < algo->hashSize; i++, j += 2) {
        hashBinary[i] = hexToBin(hashString[j], hashString[j + 1]);
    }
    return hashBinary;
}
