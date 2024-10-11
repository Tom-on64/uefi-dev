#ifndef MBR_H
#define MBR_H

/* Constants */
#define MEGABYTE    (1024*1024)
#define LBA_SIZE    512
#define ESP_SIZE    (33*MEGABYTE)
#define DATA_SIZE   (MEGABYTE)
#define IMG_SIZE    (ESP_SIZE + DATA_SIZE + MEGABYTE)

/* Macros */
#define BYTES2SECTORS(_b)  ( ((_b) / LBA_SIZE) + ((_b) % LBA_SIZE ? 1 : 0) )

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

#endif

