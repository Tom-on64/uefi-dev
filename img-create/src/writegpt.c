#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "lib.h"

char* imgname = "test.img";

int writeMbr(FILE* image);

int main(int argc, char** argv) {
    FILE* image = fopen(imgname, "wb+");

    if (image == NULL) {
        fprintf(stderr, "%s: Could not open %s\n", argv[0], imgname);
        return EXIT_FAILURE;
    }

    if (writeMbr(image) != 0) {
        fprintf(stderr, "%s: Could not write MBR in %s\n", argv[0], imgname);
        return EXIT_FAILURE;
    }

    fclose(image);

    return EXIT_SUCCESS;
}

int writeMbr(FILE* image) {
    uint64_t imageSizeSectors = BYTES2SECTORS(IMG_SIZE);
    if (imageSizeSectors > 0xFFFFFFFF) imageSizeSectors = 0x100000000;

    MasterBootRecord mbr = { 0 };
    mbr.partition[0] = (MbrPartition) {
        .bootIndicator = 0,
        .startChs = { 0x00, 0x02, 0x00 },
        .osType = 0xee,     // Protective GPT
        .endChs = { 0xff, 0xff, 0xff },
        .startLba = 1,
        .sizeLba = imageSizeSectors-1,
    };
    mbr.bootSignature = 0xaa55; 

    if (fwrite(&mbr, sizeof(mbr), 1, image) != 1) { return 1; }

    return 0;
}

