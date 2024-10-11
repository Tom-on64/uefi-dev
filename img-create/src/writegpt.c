#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "lib.h"

char* imgname = "test.img";

// CRC32
uint32_t crcTable[256];
void createCrc32Table();
uint32_t calcCrc32(void* buf, int32_t len);

int padLbaSize(FILE* image);
Guid newGuid();
int writeMbr(FILE* image);
int writeGpts(FILE* image);

int main(int argc, char** argv) {
    if (argc > 1) { imgname = argv[1]; }

    FILE* image = fopen(imgname, "wb+");

    // Seed RNG
    srand(time(NULL));

    if (image == NULL) {
        fprintf(stderr, "%s: Could not open %s\n", argv[0], imgname);
        return EXIT_FAILURE;
    }

    // Write protective MBR
    if (writeMbr(image) != 0) {
        fprintf(stderr, "%s: Could not write MBR in %s\n", argv[0], imgname);
        return EXIT_FAILURE;
    }

    if (writeGpts(image) != 0) {
        fprintf(stderr, "%s: Could not write GPT tables and headers to %s", argv[0], imgname);
        return EXIT_FAILURE;
    }

    fclose(image);

    return EXIT_SUCCESS;
}

// Source: https://www.w3.org/TR/png/#D-CRCAppendix
// Slight modification like using fixed size types and identifier changes
void createCrc32Table() { // Tbh i have no fucking clue how this works
    uint32_t c;
    int32_t n, k;

    for (n = 0; n < 256; n++) {
        c = (uint32_t) n;
        for (k = 0; k < 8; k++) {
            if (c & 1) c = 0xedb88320L ^ (c >> 1);
            else c = c >> 1;
        }
        crcTable[n] = c;
    }
}

uint32_t calcCrc32(void* buf, int32_t len) {
    static int crcTableComputed = 0;

    uint8_t* bufp = buf;
    uint32_t c = 0xffffffffL;
    int32_t n;

    if (!crcTableComputed) {
        createCrc32Table();
        crcTableComputed = 1;
    }
    for (n = 0; n < len; n++) c = crcTable[(c ^ bufp[n]) & 0xff] ^ (c >> 8);

    return c ^ 0xffffffffL;
}

int padLbaSize(FILE* image) {
    uint8_t zeroSector[512];

    for (size_t i = 0; i < (LBA_SIZE - sizeof(zeroSector)) / 512; i++) {
        if (fwrite(zeroSector, sizeof(zeroSector), 1, image) != 1) { return 1; }
    }

    return 0;
}

Guid newGuid() {
    uint8_t randArr[16];

    for (uint8_t i = 0; i < sizeof(randArr); i++) {
        randArr[i] = rand() % (UINT8_MAX + 1);
    }
    
    Guid res = {
        .timeLow = *(uint32_t*)&randArr[0],
        .timeMid = *(uint16_t*)&randArr[4],
        .timeHighAndVer = *(uint16_t*)&randArr[6],
        .clkSeqHighAndRes = randArr[8],
        .clkSeqLow = randArr[9],
        .node = { randArr[10], randArr[11], randArr[12], randArr[13], randArr[14], randArr[15] },
    };

    // Version bits (V4)
    res.timeHighAndVer &= ~(1 << 15);
    res.timeHighAndVer |= (1 << 14);
    res.timeHighAndVer &= ~(1 << 13);
    res.timeHighAndVer &= ~(1 << 12);

    // Variant bits
    res.clkSeqHighAndRes |= (1 << 7);
    res.clkSeqHighAndRes |= (1 << 6);
    res.clkSeqHighAndRes &= ~(1 << 5);

    return res;
}

int writeMbr(FILE* image) {
    uint64_t imageSizeLbas = BYTES2LBAS(IMG_SIZE);
    if (imageSizeLbas > 0xFFFFFFFF) imageSizeLbas = 0x100000000;

    MasterBootRecord mbr = { 0 };
    mbr.partition[0] = (MbrPartition) {
        .bootIndicator = 0,
        .startChs = { 0x00, 0x02, 0x00 },
        .osType = 0xee,     // Protective GPT
        .endChs = { 0xff, 0xff, 0xff },
        .startLba = 1,
        .sizeLba = imageSizeLbas-1,
    };
    mbr.bootSignature = 0xaa55; 

    if (fwrite(&mbr, sizeof(mbr), 1, image) != 1) { return 1; }

    return 0;
}

int writeGpts(FILE* image) {
    GptHeader primaryGpt = {
        .signature = { "EFI PART" },
        .revision = 0x00010000, // V1.0
        .headerSize = 92,
        .headerCrc32 = 0,   // To be calculated
        .myLba = 1,
        .altLba = BYTES2LBAS(IMG_SIZE) - 1,
        .firstUsableLba = 1 + 1 + 32,   // MBR + GPT + Primary GPT table
        .lastUsableLba = BYTES2LBAS(IMG_SIZE) - 1 - 32 - 1, // Second GPT header + table
        .diskGuid = newGuid(),
        .partitionTableLba = 2,
        .entryCount = 128,
        .entrySize = 128,
        .partitionTableCrc32 = 0,   // To be calculated
    };

    GptPartitionEntry gptTable[GPT_TABLE_ENTRY_COUNT] = {
        // EFI System Partition
        {
            .partitionTypeGuid = ESP_GUID,
            .uniqueGuid = newGuid(),
            .startLba = ESP_LBA,
            .endLba = ESP_LBA + BYTES2LBAS(ESP_SIZE),
            .attributes = 0,
            .name = u"EFI SYSTEM",
        },
        // Basic Data Partition
        {
            .partitionTypeGuid = DATA_GUID,
            .uniqueGuid = newGuid(),
            .startLba = DATA_LBA,
            .endLba = DATA_LBA + BYTES2LBAS(DATA_SIZE),
            .attributes = 0,
            .name = u"BASIC DATA",
        },
    };
    
    // Calc primary CRC32 values
    primaryGpt.partitionTableCrc32 = calcCrc32(gptTable, sizeof(gptTable));
    primaryGpt.headerCrc32 = calcCrc32(&primaryGpt, primaryGpt.headerSize);

    // Write primary GPT and GPT Table
    if (fwrite(&primaryGpt, sizeof(primaryGpt), 1, image) != 1) { return 0; }
    if (padLbaSize(image) != 0) { return 0; }
    if (fwrite(gptTable, sizeof(gptTable), 1, image) != 1) { return 0; }

    // Setup secondary (backup) GPT
    GptHeader secondaryGpt = primaryGpt;
    secondaryGpt.partitionTableCrc32 = 0;
    secondaryGpt.headerCrc32 = 0;
    secondaryGpt.myLba = primaryGpt.altLba;
    secondaryGpt.altLba = primaryGpt.myLba;
    secondaryGpt.partitionTableLba = BYTES2LBAS(IMG_SIZE) - 1 - 32;

    // Calc secondary CRC32 values
    secondaryGpt.partitionTableCrc32 = calcCrc32(gptTable, sizeof(gptTable));
    secondaryGpt.headerCrc32 = calcCrc32(&secondaryGpt, secondaryGpt.headerSize);

    // Write secondary GPT and GPT table
    fseek(image, secondaryGpt.partitionTableLba * LBA_SIZE, SEEK_SET);
    if (fwrite(gptTable, sizeof(gptTable), 1, image) != 1) { return 0; }
    if (fwrite(&secondaryGpt, sizeof(secondaryGpt), 1, image) != 1) { return 0; }
    if (padLbaSize(image) != 0) { return 0; }

    return 0;
}

