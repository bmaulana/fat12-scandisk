/* By: Bagus Maulana */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

#define MAX_NO_FILES 1023

void usage() {
    fprintf(stderr, "Usage: dos_scandisk <imagename>\n");
    exit(1);
}

struct file {
    char name[9];
    char ext[4];
    uint32_t size;
    uint16_t start_cluster;
    int clusters;
};

int follow_non_dir(uint16_t cluster, int *visited, uint8_t *image_buf, struct bpb33 *bpb) {
    // Follows a file's linked list to return the number of clusters in the file.
    int clusters = 0;
    while(1) {
        if(is_end_of_file(cluster))
            return clusters;

        visited[cluster] = 1;
        cluster = get_fat_entry(cluster, image_buf, bpb);  //get next cluster in file
        clusters++;
    }
}

int follow_unreferenced(uint16_t cluster, int *visited, uint8_t *image_buf, struct bpb33 *bpb) {
    // similar to follow_non_dir, but prints out each cluster to fulfil question 1
    int clusters = 0;
    while(1) {
        if(is_end_of_file(cluster))
            return clusters;

        printf(" %i", cluster);
        visited[cluster] = 1;
        cluster = get_fat_entry(cluster, image_buf, bpb);  //get next cluster in file
        clusters++;
    }
}

void change_last_cluster(uint16_t cluster, int free_after, uint8_t *image_buf, struct bpb33 *bpb) {
    // similar to follow_non_dir, but frees all clusters after a specified nth cluster. (Question 5)
    int ctr = 1;
    while(1) {
        if(is_end_of_file(cluster))
            return;

        int prev_cluster = cluster;
        cluster = get_fat_entry(cluster, image_buf, bpb);  //get next cluster in file

        if(ctr == free_after)
            set_fat_entry(prev_cluster, 4095, image_buf, bpb);
        if(ctr > free_after)
            set_fat_entry(prev_cluster, 0, image_buf, bpb);

        ctr++;
    }
}

void follow_dir(uint16_t cluster, int *visited, struct file *files, int *filectr, uint8_t *image_buf, struct bpb33 *bpb) {
    int d, i;
    struct direntry *dirent = (struct direntry *) cluster_to_addr(cluster, image_buf, bpb);
    while (1) {
        visited[cluster] = 1;  // visit current cluster

        // iterate over direntries in current dir
        for (d = 0; d < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; d += sizeof(struct direntry)) {
            char name[9], extension[4];
            uint32_t size;
            uint16_t file_cluster;

            name[8] = ' ';
            extension[3] = ' ';
            memcpy(name, &(dirent->deName[0]), 8);
            memcpy(extension, dirent->deExtension, 3);

            if (name[0] == SLOT_EMPTY)
                return;  // we have gone through all entries in root directory

            /* skip over deleted entries */
            if (((uint8_t) name[0]) == SLOT_DELETED)
                continue;

            /* names are space padded - remove the spaces */
            for (i = 8; i > 0; i--) {
                if (name[i] == ' ')
                    name[i] = '\0';
                else
                    break;
            }

            /* remove the spaces from extensions */
            for (i = 3; i > 0; i--) {
                if (extension[i] == ' ')
                    extension[i] = '\0';
                else
                    break;
            }

            /* don't need to consider "." or ".." directories */
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                dirent++;
                continue;
            }

            if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
                // If a subdir
                file_cluster = getushort(dirent->deStartCluster);   // get starting cluster of subdir
                follow_dir(file_cluster, visited, files, filectr, image_buf, bpb);   // call this function recursively on subdir

            } else if((dirent->deAttributes & ATTR_VOLUME) == 0) {
                // If a normal file
                size = getulong(dirent->deFileSize); // get size from direntry
                file_cluster = getushort(dirent->deStartCluster);   // get starting cluster of file
                int clusters = follow_non_dir(file_cluster, visited, image_buf, bpb);  // visit clusters used in file

                // add information about file to files array
                strcpy(files[filectr[0]].name, name);
                strcpy(files[filectr[0]].ext, extension);
                files[filectr[0]].size = size;
                files[filectr[0]].start_cluster = file_cluster;
                files[filectr[0]].clusters = clusters;
                filectr[0]++;
            }

            dirent++; // Get next direntry in dir
        }
        if (cluster == 0) {
            dirent++;  // root dir is special
        } else {
            cluster = get_fat_entry(cluster, image_buf, bpb);  // get next cluster in directory
            dirent = (struct direntry *) cluster_to_addr(cluster, image_buf, bpb);  // get direntry of next cluster
        }
    }
}

void append_de(struct direntry *de, uint8_t *image_buf, struct bpb33 *bpb) {
    // appends a new direntry to the end of the root folder's direntries. (Question 3)
    struct direntry *dirent = (struct direntry *) cluster_to_addr(0, image_buf, bpb);
    while (1) {
        // Iterate over direntries in the root folder until we reach the end
        char name[9];
        name[8] = ' ';
        memcpy(name, &(dirent->deName[0]), 8);

        /* we have reached the end of the root direntries - this is where we want to append the new direntry. */
        if (name[0] == SLOT_EMPTY) {
            memcpy(dirent, de, sizeof(struct direntry));
            return;
        }

        /* skip over deleted entries */
        if (((uint8_t) name[0]) == SLOT_DELETED)
            continue;

        dirent++;
    }
}

int main(int argc, char **argv) {
    // Check that no. of arguments is correct
    if (argc < 2 || argc > 2)
        usage();

    // Initialise image_buf and bpb
    int fd;
    uint8_t *image_buf = mmap_file(argv[1], &fd);
    struct bpb33 *bpb = check_bootsector(image_buf);

    // Store information on all referenced files and visit the clusters they use
    int *visited = malloc(bpb->bpbSectors / bpb->bpbSecPerClust * sizeof(int));  // boolean array of clusters used in files
    struct file *files = malloc(MAX_NO_FILES * sizeof(struct file));
    int filectr = 0;
    follow_dir(0, visited, files, &filectr, image_buf, bpb);

    // Look for clusters contained in unreferenced files and print them
    // Also store information about each unreferenced file
    struct file *unref = malloc(MAX_NO_FILES * sizeof(struct file));
    int unrefctr = 0, printed = 0, i;
    for(i=2; i < bpb->bpbSectors / bpb->bpbSecPerClust; i++) {
        if(visited[i] || !get_fat_entry(i, image_buf, bpb))  // cluster referenced or is empty
            continue;

        if(!printed) {
            printf("Unreferenced:");
            printed++;
        }

        int clusters = follow_unreferenced(i, visited, image_buf, bpb);
        char filename[9];
        sprintf(filename, "FOUND%i", unrefctr + 1);
        strcpy(unref[unrefctr].name, filename);
        strcpy(unref[unrefctr].ext, "DAT");
        unref[unrefctr].size = clusters * bpb->bpbBytesPerSec * bpb->bpbSecPerClust;
        unref[unrefctr].start_cluster = i;
        unref[unrefctr].clusters = clusters;
        unrefctr++;
    }
    printf("\n");

    // For each unreferenced file, print information about the file and create a new direntry on root that links to them
    for(i=0; i < MAX_NO_FILES; i++) {
        if(!unref[i].size)  // Empty (size = 0)
            break;

        printf("Lost file: %i %i\n", unref[i].start_cluster, unref[i].clusters);

        // create new direntry for the unreferenced file
        struct direntry *newde = malloc(sizeof(struct direntry));
        memcpy(newde->deName, unref[i].name, 8);
        memcpy(newde->deExtension, unref[i].ext, 3);
        newde->deAttributes = 0x20;  // set as normal file (not e.g. a directory)
        putushort(newde->deStartCluster, unref[i].start_cluster);
        putulong(newde->deFileSize, unref[i].size);

        append_de(newde, image_buf, bpb);  // Append new direntry to root
    }

    // For each file, check if its size in the directory entry is inconsistent with its size in the FAT (no. of clusters)
    // If they are inconsistent, print information about the file and free clusters beyond the end of file in the direntry
    for(i=0; i < MAX_NO_FILES; i++) {
        if(!files[i].size)  // Empty (size = 0)
            break;

        if(files[i].size / (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) + 1 < files[i].clusters) {
            printf("%s.%s %i %i\n", files[i].name, files[i].ext, files[i].size, files[i].clusters * bpb->bpbBytesPerSec * bpb->bpbSecPerClust);
            change_last_cluster(files[i].start_cluster, files[i].size / (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) + 1, image_buf, bpb);
        }
    }

    close(fd);
    exit(0);
}