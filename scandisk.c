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

#define MAX_NO_FILES 20

void usage() {
    fprintf(stderr, "Usage: scandisk <imagename>\n");
    exit(1);
}

void print_mem(void const *vp, size_t n) {
    // for testing only
    unsigned char const *p = vp;
    size_t i;
    for (i=0; i<n; i++)
        printf("%02x ", p[i]);
    putchar('\n');
};

void print_bpb(struct bpb33 *bpb) {
    // for testing only
    printf("bpbSecPerClust, %i\n", bpb->bpbSecPerClust);	/* sectors per cluster */
    printf("bpbResSectors, %i\n", bpb->bpbResSectors);	/* number of reserved sectors */
    printf("bpbFATs, %i\n", bpb->bpbFATs);	/* number of FATs */
    printf("bpbRootDirEnts, %i\n", bpb->bpbRootDirEnts);	/* number of root directory entries */
    printf("bpbSectors, %i\n", bpb->bpbSectors);	/* total number of sectors */
    printf("bpbMedia, %i\n", bpb->bpbMedia);	/* media descriptor */
    printf("bpbFATsecs, %i\n", bpb->bpbFATsecs);	/* number of sectors per FAT */
    printf("bpbSecPerTrack, %i\n", bpb->bpbSecPerTrack);	/* sectors per track */
    printf("bpbHeads, %i\n", bpb->bpbHeads);	/* number of heads */
    printf("bpbHiddenSecs, %i\n", bpb->bpbHiddenSecs);  /* number of hidden sectors */
}

void print_arr(char *label, u_int8_t *arr, int size) {
    // for testing only
    printf("%s ", label);
    int i;
    for (i=0; i<size; i++)
        printf("%02x ", arr[i]);
    if(size == 3 || size == 8) {
        for (i = 0; i < size; i++)
            printf("%c", arr[i]);
    }
    if(size == 2)
        printf("%i", (int) getushort(arr));
    if(size == 4)
        printf("%i", (int) getulong(arr));
    putchar('\n');
}

void print_de(struct direntry *de) {
    // for testing only
    print_arr("deName", de->deName, 8); /* filename, blank filled */
    print_arr("deExtension", de->deExtension, 3); /* extension, blank filled */
    print_arr("deAttributes", &de->deAttributes, 1); /* file attributes */
    print_arr("deLowerCase", &de->deLowerCase, 1); /* NT VFAT lower case flags */
    print_arr("deCHundredth", &de->deCHundredth, 1);	/* hundredth of seconds in CTime */
    print_arr("deCTime", de->deCTime, 2);	/* create time */
    print_arr("deCDate", de->deCDate, 2);	/* create date */
    print_arr("deADate", de->deADate, 2);	/* access date */
    print_arr("deHighClust", de->deHighClust, 2);	/* high bytes of cluster number */
    print_arr("deMTime", de->deMTime, 2);	/* last update time */
    print_arr("deMDate", de->deCDate, 2);	/* last update date */
    print_arr("deStartCluster", de->deStartCluster, 2); /* starting cluster of file */
    print_arr("deFileSize", de->deFileSize, 4);	/* size of file in bytes */
}

struct file {
    char name[9];
    char ext[4];
    uint32_t size;
    uint16_t start_cluster;
    int clusters;
};

void print_file(struct file *f) {
    // for testing only
    printf("name: %s\n", f->name);
    printf("ext: %s\n", f->ext);
    printf("size: %i\n", f->size);
    printf("start cluster: %i\n", f->start_cluster);
    printf("no. of clusters: %i\n", f->clusters);
}

int follow_non_dir(uint16_t cluster, int *visited, uint8_t *image_buf, struct bpb33 *bpb) {
    // Follows a file's linked list to return the number of clusters in the file.
    int clusters = 0;
    while(1) {
        if(is_end_of_file(cluster))
            return clusters;

        //printf("%i ", cluster);
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
    // similar to follow_non_dir, but frees all cluster after a specified nth cluster. (Question 5)
    int ctr = 0;
    while(1) {
        if(is_end_of_file(cluster))
            return;

        //printf("%i: ", cluster);
        //print_mem(cluster_to_addr(cluster, image_buf, bpb), bpb->bpbBytesPerSec * bpb->bpbSecPerClust);

        int prevcluster = cluster;
        cluster = get_fat_entry(cluster, image_buf, bpb);  //get next cluster in file

        if(ctr + 1 == free_after)
            set_fat_entry(prevcluster, 4095, image_buf, bpb);
        if(ctr >= free_after)
            set_fat_entry(prevcluster, 0, image_buf, bpb);

        ctr++;
    }
}

void follow_dir(uint16_t cluster, int *visited, struct file *files, int *filectr, uint8_t *image_buf, struct bpb33 *bpb) {
    int d, i;
    struct direntry *dirent = (struct direntry *) cluster_to_addr(cluster, image_buf, bpb);
    while (1) {
        // visit current cluster
        //printf("visit cluster %i\n", cluster);
        visited[cluster] = 1;

        // iterate over dirents in current dir, one dirent for each file/subdir
        for (d = 0; d < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; d += sizeof(struct direntry)) {
            //print_de(dirent);

            char name[9], extension[4];
            uint32_t size;
            uint16_t file_cluster;

            name[8] = ' ';
            extension[3] = ' ';
            memcpy(name, &(dirent->deName[0]), 8);
            memcpy(extension, dirent->deExtension, 3);

            if (name[0] == SLOT_EMPTY)
                return;  // went through all files in root directory's dirent

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

            if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
                //printf("Volume: %s\n", name);
            } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
                // If a subdir
                //printf("%s (directory)\n", name);
                file_cluster = getushort(dirent->deStartCluster);   // get starting cluster of subdir
                follow_dir(file_cluster, visited, files, filectr, image_buf, bpb);   // call this function recursively on subdir
            } else {
                // If a file
                size = getulong(dirent->deFileSize); // get size from dirent
                //printf("%s.%s (%u bytes)\nvisit clusters: ", name, extension, size);
                file_cluster = getushort(dirent->deStartCluster);   // get starting cluster of file
                int clusters = follow_non_dir(file_cluster, visited, image_buf, bpb); // visit clusters used in file
                //printf("(%i clusters)\n", clusters);

                // add file to files array
                strcpy(files[filectr[0]].name, name);
                strcpy(files[filectr[0]].ext, extension);
                files[filectr[0]].size = size;
                files[filectr[0]].start_cluster = file_cluster;
                files[filectr[0]].clusters = clusters;
                filectr[0]++;
            }
            dirent++; // Get next file in dir
        }
        if (cluster == 0) {
            dirent++;  // root dir is special. Go to next cluster
        } else {
            cluster = get_fat_entry(cluster, image_buf, bpb);  // get next cluster in file
            dirent = (struct direntry *) cluster_to_addr(cluster, image_buf, bpb);  // get dirent of next cluster
        }
    }
}

void append_de(struct direntry *de, uint8_t *image_buf, struct bpb33 *bpb) {
    // appends a new dir entry to the end of the root folder's direntries.
    struct direntry *dirent = (struct direntry *) cluster_to_addr(0, image_buf, bpb);
    while (1) {
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
    uint8_t *image_buf;
    int fd;
    struct bpb33 *bpb;

    if (argc < 2 || argc > 2) {
        usage();
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
    // print_bpb(bpb);

    // Store all referenced files and the clusters they use in arrays
    int *visited = malloc(bpb->bpbSectors / bpb->bpbSecPerClust * sizeof(int));  // boolean array of clusters used in files
    struct file *files = malloc(MAX_NO_FILES * sizeof(struct file));
    int filectr = 0;
    follow_dir(0, visited, files, &filectr, image_buf, bpb);

    int i;
    //for(i=0; i < MAX_NO_FILES; i++) {
    //    if(!files[i].size)  // Empty (size = 0)
    //        break;
    //    print_file(&files[i]);
    //}

    //for(i=0; i < bpb->bpbSectors / bpb->bpbSecPerClust; i++) {
    //    printf("%i:%i ", i, get_fat_entry(i, image_buf, bpb));
    //}

    //for(i=0; i < bpb->bpbSectors / bpb->bpbSecPerClust; i++) {
    //    printf("%i:%i ", i, visited[i]);
    //    if(!(i % 20))
    //        printf("\n");
    //}

    // Look for clusters with unreferenced files
    struct file *unref = malloc(MAX_NO_FILES * sizeof(struct file));
    int unrefctr = 0, printed = 0;

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

    // For each unreferenced file, print them and create new direntry on root to them
    for(i=0; i < MAX_NO_FILES; i++) {
        if(!unref[i].size)  // Empty (size = 0)
            break;
        //print_file(&unref[i]);
        printf("Lost file: %i %i\n", unref[i].start_cluster, unref[i].clusters);

        struct direntry *newde = malloc(sizeof(struct direntry));
        memcpy(newde->deName, unref[i].name, 8);
        memcpy(newde->deExtension, unref[i].ext, 3);
        newde->deAttributes = 0x20;  // set as normal file (not e.g. directory)
        //newde->deLowerCase = 0x00;
        //newde->deCHundredth = 0;	/* hundredth of seconds in CTime */
        //newde->deCTime = 0;	/* create time */
        //newde->deCDate = 0;	/* create date */
        //newde->deADate = 0;	/* access date */
        //newde->deHighClust[0] = 0x00;
        //newde->deHighClust[1] = 0x00;
        //newde->deMTime = 0;	/* last update time */
        //newde->deCDate = 0;	/* last update date */
        putushort(newde->deStartCluster, unref[i].start_cluster);
        putulong(newde->deFileSize, unref[i].size);
        //print_de(newde);
        append_de(newde, image_buf, bpb);
    }

    //printf("Bytes per cluster: %i\n", bpb->bpbBytesPerSec * bpb->bpbSecPerClust);
    for(i=0; i < MAX_NO_FILES; i++) {
        if(!files[i].size)  // Empty (size = 0)
            break;
        if(files[i].size / (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) + 1 < files[i].clusters) {
            printf("%s.%s %i %i\n", files[i].name, files[i].ext, files[i].size, files[i].clusters * bpb->bpbBytesPerSec * bpb->bpbSecPerClust);
            //printf("%i %i\n", files[i].size / (bpb->bpbBytesPerSec * bpb->bpbSecPerClust), files[i].clusters);
            change_last_cluster(files[i].start_cluster, files[i].size / (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) + 1, image_buf, bpb);
        }
    }

    /*for(i=2; i < bpb->bpbSectors / bpb->bpbSecPerClust; i++) {
        //if(visited[i])
        //    continue;

        uint8_t *ptr = cluster_to_addr(i, image_buf, bpb);
        int j, isAllf6 = 1;

        for(j=0; j < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; j++) {
            if(ptr[j] != 0xf6) {
                isAllf6 = 0;
                break;
            }
        }

        if(isAllf6)
            continue;

        printf(" %i: ", i);
        print_mem(ptr, bpb->bpbBytesPerSec * bpb->bpbSecPerClust);
    }*/

    close(fd);
    exit(0);
}