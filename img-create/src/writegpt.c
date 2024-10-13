#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include "lib.h"

char* imgname = "test.img";

// CRC32
uint32_t crcTable[256];
void createCrc32Table();
uint32_t calcCrc32(void* buf, int32_t len);

// Utils
int padLbaSize(FILE* image);
Guid newGuid();
void FAT32_getDentryTimeDate(uint16_t* time, uint16_t* date);

// Write
int writeMbr(FILE* image);
int writeGpts(FILE* image);
int writeEsp(FILE* image);

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
        fprintf(stderr, "%s: Could not write MBR to %s\n", argv[0], imgname);
        return EXIT_FAILURE;
    }

    if (writeGpts(image) != 0) {
        fprintf(stderr, "%s: Could not write GPT tables and headers to %s\n", argv[0], imgname);
        return EXIT_FAILURE;
    }

    if (writeEsp(image) != 0) {
        fprintf(stderr, "%s: Could not write ESP to %s\n", argv[0], imgname);
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

void FAT32_getDentryTimeDate(uint16_t* outTime, uint16_t* outDate) {
    time_t currTime = time(NULL);
    struct tm tm = *localtime(&currTime);

    *outDate = ((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday;
    if (tm.tm_sec == 60) tm.tm_sec = 59;
    *outTime = (tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2);
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
        .firstUsableLba = 1 + 1 + BYTES2LBAS(GPT_TABLE_SIZE),   // MBR + GPT + Primary GPT table
        .lastUsableLba = BYTES2LBAS(IMG_SIZE) - 1 - BYTES2LBAS(GPT_TABLE_SIZE) - 1, // Second GPT header + table
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
    secondaryGpt.partitionTableLba = BYTES2LBAS(IMG_SIZE) - 1 - BYTES2LBAS(GPT_TABLE_SIZE);

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

int writeEsp(FILE* image) {
    uint8_t resSec = 32;
    VolumeBootRecord vbr = {
        .BS_jmpBoot = { 0xeb, 0x00, 0x90 },     // jmp 0x02; nop
        .BS_OEMName = { "TESTDISK" },
        .BPB_BytesPerSec = LBA_SIZE,
        .BPB_SecPerClus = 1,
        .BPB_RsvdSecCnt = resSec,
        .BPB_NumFATs = 2,
        .BPB_RootEntCnt = 0,
        .BPB_TotSec16 = 0,
        .BPB_Media = 0xf8,  // Fixed, non-removable media
        .BPB_FATSz16 = 0,
        .BPB_SecPerTrk = 0,
        .BPB_NumHeads = 0,
        .BPB_HiddSec = ESP_LBA - 1, // Number of sectors before this
        .BPB_TotSec32 = BYTES2LBAS(ESP_SIZE),
        .BPB_FATSz32 = (ALIGN_LBA - resSec) / 2,
        .BPB_ExtFlags = 0,  // Mirrored FATs
        .BPB_FSVer = 0,
        .BPB_RootClus = 2,  // Clusters 0 and 1 are reserved
        .BPB_FSInfo = 1, 
        .BPB_BkBootSec = 6,
        .BPB_Reserved = { 0 },
        .BS_DrvNum = 0x80,  // Hard Drive #1
        .BS_Reserved1 = 0,
        .BS_BootSig = 0x29,
        .BS_VolID = { 0 },
        .BS_VolLab = { "NO NAME    " },
        .BS_FilSysType = { "FAT32   " },
        
        .bootCode = { 0 },
        .bootsectSig = 0xaa55,
    };

    FSInfo fsi = {
        .FSI_LeadSig = 0x41615252,
        .FSI_Reserved1 = { 0 },
        .FSI_StructSig = 0x61417272,
        .FSI_FreeCount = 0xFFFFFFFF,
        .FSI_NextFree = 0xFFFFFFFF,
        .FSI_Reserved2 = { 0 },
        .FSI_TrailSig = 0xaa550000,
    };

    fseek(image, ESP_LBA * LBA_SIZE, SEEK_SET);
    if (fwrite(&vbr, sizeof(vbr), 1, image) != 1) { return 1; }
    padLbaSize(image);

    if (fwrite(&fsi, sizeof(fsi), 1, image) != 1) { return 1; }
    padLbaSize(image);
    
    // Backup sector
    fseek(image, (ESP_LBA + vbr.BPB_BkBootSec) * LBA_SIZE, SEEK_SET);
    if (fwrite(&vbr, sizeof(vbr), 1, image) != 1) { return 1; }
    padLbaSize(image);

    // File Allocation Tables
    uint64_t fatStart = ESP_LBA + vbr.BPB_RsvdSecCnt;
    for (size_t i = 0; i < vbr.BPB_NumFATs; i++) {
        uint32_t cluster = 0;

        // Go to FAT location
        fseek(image, (fatStart + i * vbr.BPB_FATSz32) * LBA_SIZE, SEEK_SET);

        // Cluster 0; FAT Id
        // Lowest 8 bits - Media type
        cluster = 0xFFFFFF00 | vbr.BPB_Media;
        if (fwrite(&cluster, sizeof(cluster), 1, image) != 1) { return 1; }

        // Cluster 1; End of Chain marker (EOC)
        cluster = 0xFFFFFFFF;
        if (fwrite(&cluster, sizeof(cluster), 1, image) != 1) { return 1; }

        // Cluster 2; Root dir cluster
        cluster = 0xFFFFFFFF; // EOC
        if (fwrite(&cluster, sizeof(cluster), 1, image) != 1) { return 1; }

        // Cluster 3; /EFI directory
        cluster = 0xFFFFFFFF; // EOC
        if (fwrite(&cluster, sizeof(cluster), 1, image) != 1) { return 1; }

        // Cluster 4; /EFI/BOOT directory
        cluster = 0xFFFFFFFF; // EOC
        if (fwrite(&cluster, sizeof(cluster), 1, image) != 1) { return 1; }

        // Cluster 5+; Other files
    }

    // Write file data
    uint32_t dataStart = fatStart + (vbr.BPB_NumFATs * vbr.BPB_FATSz32);
    fseek(image, dataStart * LBA_SIZE, SEEK_SET);

    // Root Directory
    // /EFI dir entry
    uint16_t createTime, createDate;
    FAT32_getDentryTimeDate(&createTime, &createDate);

    FAT32_DirEntryShort dentry = { 0 };
    strncpy((char*)dentry.DIR_Name, "EFI        ", 11);
    dentry.DIR_Attr = ATTR_DIRECTORY;
    dentry.DIR_CrtTime = createTime;
    dentry.DIR_CrtDate = createDate;
    dentry.DIR_WrtTime = createTime;
    dentry.DIR_WrtDate = createDate;
    dentry.DIR_FstClusLo = 3;

    if (fwrite(&dentry, sizeof(dentry), 1, image) != 1) { return 1; }
    fseek(image, (dataStart + 1) * LBA_SIZE, SEEK_SET);
    
    // /EFI/.
    strncpy((char*)dentry.DIR_Name, ".          ", 11);
    if (fwrite(&dentry, sizeof(dentry), 1, image) != 1) { return 1; }

    // /EFI/..
    strncpy((char*)dentry.DIR_Name, "..         ", 11);
    dentry.DIR_FstClusLo = 0;   // Root
    if (fwrite(&dentry, sizeof(dentry), 1, image) != 1) { return 1; }

    // /EFI/BOOT
    strncpy((char*)dentry.DIR_Name, "BOOT       ", 11);
    dentry.DIR_FstClusLo = 4;   // BOOT dir cluster
    if (fwrite(&dentry, sizeof(dentry), 1, image) != 1) { return 1; }
    fseek(image, (dataStart + 2) * LBA_SIZE, SEEK_SET);

    // /EFI/BOOT/.
    strncpy((char*)dentry.DIR_Name, ".          ", 11);
    if (fwrite(&dentry, sizeof(dentry), 1, image) != 1) { return 1; }

    // /EFI/BOOT/..
    strncpy((char*)dentry.DIR_Name, "..         ", 11);
    dentry.DIR_FstClusLo = 3;   // /EFI
    if (fwrite(&dentry, sizeof(dentry), 1, image) != 1) { return 1; }

    return 0;
}

