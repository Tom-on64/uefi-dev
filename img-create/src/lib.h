#ifndef MBR_H
#define MBR_H

#include <stdint.h>
// #include <uchar.h> I don't have this on MacOS :/
typedef uint_least16_t char16_t;

/* Constants */
#define MEGABYTE    (1024*1024)
#define LBA_SIZE    512
#define ESP_SIZE    (33*MEGABYTE)
#define DATA_SIZE   (MEGABYTE)
#define IMG_SIZE    (ESP_SIZE + DATA_SIZE + (MEGABYTE*2 + (LBA_SIZE * 67)))

#define ALIGN_LBA   (MEGABYTE / LBA_SIZE)

#define ESP_GUID    ((Guid){ 0xc12a7328, 0xf81f, 0x11d2, 0xba, 0x4b, { 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b } })
#define DATA_GUID   ((Guid){ 0xEBD0A0A2, 0xB9E5, 0x4433, 0x87, 0xC0, { 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 } })

#define GPT_TABLE_ENTRY_SIZE    128
#define GPT_TABLE_ENTRY_COUNT   128
#define GPT_TABLE_SIZE  (128*128)   // Minimum size

/* Macros */
#define BYTES2LBAS(_b)  ( ((_b) / LBA_SIZE) + ((_b) % LBA_SIZE ? 1 : 0) )
#define NEXT_ALIGNED_LBA(_lba) ( (_lba) - ((_lba) % ALIGN_LBA) + ALIGN_LBA )

/* More Constants */
#define ESP_LBA     ALIGN_LBA
#define DATA_LBA    NEXT_ALIGNED_LBA(ESP_LBA + BYTES2LBAS(ESP_SIZE))

/* Typedefs */
typedef struct {
    uint8_t bootIndicator;
    uint8_t startChs[3];
    uint8_t osType;
    uint8_t endChs[3];
    uint32_t startLba;
    uint32_t sizeLba;
} __attribute__ ((packed)) MbrPartition;

typedef struct {
    uint8_t bootCode[440];
    uint32_t mbrSigniture;
    uint16_t unknown;
    MbrPartition partition[4];
    uint16_t bootSignature;
} __attribute__ ((packed)) MasterBootRecord;

typedef struct {
    uint32_t timeLow;
    uint16_t timeMid;
    uint16_t timeHighAndVer;
    uint8_t clkSeqHighAndRes;
    uint8_t clkSeqLow;
    uint8_t node[6];
} __attribute__ ((packed)) Guid;

typedef struct {
    uint8_t signature[8];
    uint32_t revision;
    uint32_t headerSize;
    uint32_t headerCrc32;
    uint32_t _reserved1;
    uint64_t myLba;
    uint64_t altLba;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    Guid diskGuid;
    uint64_t partitionTableLba;
    uint32_t entryCount;
    uint32_t entrySize;
    uint32_t partitionTableCrc32;

    uint8_t _reserved2[512-92];
} __attribute__ ((packed)) GptHeader;

typedef struct {
    Guid partitionTypeGuid;
    Guid uniqueGuid;
    uint64_t startLba;
    uint64_t endLba;
    uint64_t attributes;
    char16_t name[36];  // UTF-16 For some reason
} __attribute__ ((packed)) GptPartitionEntry;

#endif

