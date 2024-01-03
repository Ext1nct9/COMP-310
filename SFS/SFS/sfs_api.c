/**
 * COMP 310/ECSE 427 Assignment 3
 * Author: William Zhang
 * McGill ID: 260975150
 * 
 * Submitted files:
 * - sfs_api.c : Main file system
 * - sfs_api.h : File given by the assignment
 * - disk_emu.c: File given by the assignment
 * - disk_emu.h: File given by the assignment
 * - fuse_wrap_new.c: File given by the assignment
 * - fuse_wrap_old.c: File given by the assignment
 * - Makefile: File given by the assignment
 * - sfs_test0.c: File given by the assignment
 * - sfs_test1.c: File given by the assignment
 * - sfs_test2.c: File given by the assignment
 * 
 * I passed test0, test1 and test2 but was not able to try FUSE in the Ubuntu Trottier machines so I do not know if it works.
 * 
 * Instructions:
 * 1. Uncomment the test that needs to be run in the make file
 * 2. Start the terminal and go to the file system assignment files directory.
 * 3. Run 'Make clean' to clean the executables and .o files
 * 4. Run 'Make' to make new .o and executable files from the updated code.
 * 5. Execute './William_Zhang_sfs' in the terminal.
 * 6. For test0, if the input in the test file is printed out, the code passes the test.
 * 7. For test1 and test2, the number of errors is given by the program if the code gets to the end without a problem.
 * 
 * Thank you!
*/


#include "sfs_api.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disk_emu.h"

/**
 * Super block structure
 * @param magic: Number used to recognize a file format.
 * @param block_size: size of the blocks in the file system in bytes.
 * @param fs_size: size of the file system in bytes.
 * @param inode_table_length: number of inode blocks in the inode table.
 * @param root_dir_inode: inode address pointing to root directory (0 since the root directory is always first).
 * @param length_free_block_list: length of the free block list.
 * @param number_free_blocks: number of free blocks in the file system.
*/
typedef struct{
    uint64_t magic;
    uint64_t block_size;
    uint64_t fs_size;
    uint64_t inode_table_length;
    uint64_t root_dir_inode;
    uint64_t length_free_block_list;
    uint64_t number_free_blocks;
}superblock_t;

#define NUM_INDIRECT_DATA_POINTERS 512 // The number of indirect data pointers is BLOCK_SIZE/BLOCK NUMBER SIZE= 2048/4 bytes (unsigned int has 4 bytes) = 512 indirect data pointers.


/**
 * Inode Structure
 * @param mode: Permissions for the file.
 * @param taken: Whether inode is taken or not.
 * @param link_count: Number of links to the file
 * @param uid: ID of the file owner
 * @param gid: Group ID of the file
 * @param size: Size of the file in bytes
 * @param data_ptrs: Array representing the direct pointers to the data blocks.
 * @param indirect: Array representing the indirect pointers to the data blocks.
*/
typedef struct{
    unsigned int mode;
    unsigned int taken;
    unsigned int link_count;
    unsigned int uid;
    unsigned int gid;
    unsigned int size;
    unsigned int data_ptrs[12];
    unsigned int indirect[NUM_INDIRECT_DATA_POINTERS]; 
}inode_t;

/**
 * Directory Table Entry Structure
 * 
 * @param names: Associated names.
 * @param mode: Mode of entry.
*/
typedef struct{
    char *names;
    int mode;
}directory_table;

/**
 * File Descriptor Structure
 * 
 * @param inode: Associated inode.
 * @param read_write_pointer: Pointer.
*/
typedef struct{
    uint64_t inode;
    uint64_t read_write_pointer;
}file_descriptor;

/**
 * Free Block Entry 
 * 
 * @param number: Free Block Number.
*/
typedef struct{
    uint64_t number;
}free_block_list;


#define SFS_DISK "sfs_disk.disk"
#define BLOCK_SIZE 2048 // Predetermined
#define NUM_BLOCKS 50000 // Max number of blocks
#define NUM_INODES 193 // 12 data pointers*16 +1=193
#define MAXFILENAME 20 // Predetermined

// Logic calculations
#define NUM_INODE_BLOCKS ((sizeof(inode_t) + 1) * NUM_INODES/BLOCK_SIZE)
#define NUM_FREE_BLOCKS_LIST ((NUM_INODES-1) * (12 + BLOCK_SIZE/sizeof(unsigned int)))
#define MAX_DATA_BLOCKS_PER_FILE 12 + BLOCK_SIZE/(sizeof(unsigned int))
#define NUM_ENTRIES_FREE_BLOCKS NUM_FREE_BLOCKS_LIST/64 //uint64_t is used which will map 64 blocks with 8 bytes.


superblock_t sb; // Super block
inode_t table[NUM_INODES]; 
unsigned int num_files = 0;
unsigned int current_file = 0;
int NUMBER_BLOCKS_TAKEN_BY_ROOT = 0; // Number of blocks taken by the root directory.


// Root Directory Stuff
directory_table root[NUM_INODES-1];
file_descriptor fdt[NUM_INODES];
free_block_list free_blocks[NUM_ENTRIES_FREE_BLOCKS];


// Super Block Initialization
void init_superblock() {
    sb.magic = 0xACBD0005; // File format shenanigans.
    sb.block_size = BLOCK_SIZE;
    sb.inode_table_length = NUM_INODE_BLOCKS;
    sb.length_free_block_list = NUM_FREE_BLOCKS_LIST;
    sb.number_free_blocks = (sizeof(free_block_list)*NUM_ENTRIES_FREE_BLOCKS)/BLOCK_SIZE +1; //uint64_t used to map 64 blocks
    sb.fs_size = (NUMBER_BLOCKS_TAKEN_BY_ROOT+1+NUM_INODE_BLOCKS+NUM_FREE_BLOCKS_LIST+sb.number_free_blocks)*BLOCK_SIZE;
    sb.root_dir_inode = 0;
}

// Implement MKSFS
void mksfs(int fresh){
    if (fresh){
        //printf("Creating new file system.\n");
        NUMBER_BLOCKS_TAKEN_BY_ROOT = sizeof(root)/BLOCK_SIZE + 1;
        init_superblock();
        init_fresh_disk(SFS_DISK, BLOCK_SIZE, sb.fs_size/(BLOCK_SIZE) +1);
        // Initialize tables
        for (int i = 0; i<NUM_INODES; i++){
            table[i].taken = 0;
            fdt[i].inode = -1;   
        }
        for (int j = 0;j<NUM_INODES-1;j++){
                root[j].names = "";
                root[j].mode = 0;
            }

        write_blocks(0,1,&sb); // Write to first block with super block as pointer.

        current_file = 0;
        num_files = 0;
        // Root directory
        fdt[0].inode = 0;
        fdt[0].read_write_pointer=0;

        // Taken by Root
        table[0].taken = 1;
        uint64_t n = 1;

        free_blocks[0].number |= n<<63; // Bitwise OR

        //printf("First entry &lu\n", free_blocks[0].number);
        
        // Write blocks to the disk from the respective tables.
        write_blocks(1, NUM_INODE_BLOCKS, table);
        write_blocks(1+NUM_INODE_BLOCKS, sizeof(directory_table) * (NUM_INODES-1)/BLOCK_SIZE +1, root);
        write_blocks(1+NUM_INODE_BLOCKS+NUM_FREE_BLOCKS_LIST+NUMBER_BLOCKS_TAKEN_BY_ROOT, sb.number_free_blocks, free_blocks);

        //printf("size of inode: %d bytes\n", sizeof(inode_t));
        //printf("number of blocks for inode table: %d\n", NUM_INODE_BLOCKS);
        //printf("size of root: %d bytes\n", sizeof(root));
        //printf("number of blocks for root: %d\n", sizeof(directory_table)*(NUM_INODES-1)/BLOCK_SIZE +1);
        //printf("number of data blocks: %d\n", NUM_FREE_BLOCKS_LIST);

        fflush(stdout);
    }
    // Open Existing File System
    else{
        for (int i =0; i<NUM_INODES;i++){
            fdt[i].inode = -1;
            fdt[i].read_write_pointer = 0;
        }
        current_file = 0;

        // Open SB
        read_blocks(0,1,&sb);
        NUMBER_BLOCKS_TAKEN_BY_ROOT= sizeof(root)/BLOCK_SIZE + 1;

        // Read from the disk and put into the tables.
        read_blocks(1,NUM_INODE_BLOCKS,table);
        read_blocks(1 + NUM_INODE_BLOCKS,sizeof(directory_table)*(NUM_INODES-1)/BLOCK_SIZE +1, root);
        read_blocks(1+NUM_INODE_BLOCKS+NUM_FREE_BLOCKS_LIST+NUMBER_BLOCKS_TAKEN_BY_ROOT, sb.number_free_blocks, free_blocks);
        //printf("Block size: %lu bytes\n", sb.block_size);
        //printf("Beginning of free block list: %d\n", 1+NUM_INODE_BLOCKS+NUM_FREE_BLOCKS_LIST+NUMBER_BLOCKS_TAKEN_BY_ROOT);

        // Open Root Directory and check number of taken files.
        num_files = 0;
        for (int i = 1; i<NUM_INODES;i++){
            if(table[i].taken == 1){
                num_files = num_files+1;
            }
        }
        //printf("Number of data files: &d\n", num_files);

    }
    return;
}

// Returns 1 if there is a  new file or 0 if all the files have been returned.
int sfs_getnextfilename(char* fname){
    unsigned int * curr = &current_file;
    num_files = 0;
    for(int k = 1; k<NUM_INODES; k++){
        if(table[k].taken == 1){
            num_files = num_files+1;
        }
    }

    if (num_files == 0){
        *curr = 0;
        current_file = *curr;
        return 0; // No files.
    }

    int first = 0;
    int second = 0;
    int checked = 0;
    for(int i = 0; i<NUM_INODES-1;i++){
        if (root[i].mode ==1){ // Root checks for opened files.
            if(checked == current_file){
                while (root[i].names[second] != '\0'){ // Check for name.
                    second = second+1;
                }   
                for (first = 0; first<second;first++){
                    fname[first] = root[i].names[first]; // Copy name onto fname.
                }

                *curr = *curr +1; // Go to next file.
                current_file = *curr;
                fname[first] = '\0'; // Reset fname.
                return 1;
            }
            checked = checked+1; // Increment if the value does not correspond to the value of the current file.
        }
    }
    *curr = 0; // Reset if all files have been returned.
    current_file = *curr;

    return 0;
}

// Returns the size of a given file.
int sfs_getfilesize(const char* path){
    //printf("Getting file size\n");
    int size = 0;
    num_files = 0;
    for(int i = 1; i<NUM_INODES; i++){ // Skip first inode since it is the root directory inode.
        if(table[i].taken == 1){
            num_files = num_files+1;
        }
    }
    int index = -1;
    for(int k = 0;k<NUM_INODES-1; k++){
        if (strcmp(path, root[k].names) == 0){
            if (table[k+1].size >0){
                size = size+table[k+1].size;
            }
            // Direct Pointers
            for(int count = 0;count<12; count++){
                index = table[k+1].data_ptrs[count];
                if(index > 0){
                    char buff_data[BLOCK_SIZE]; // AVOID STACK SMASHING
                    read_blocks(index, 1, (void*)buff_data); // Get from disk
                    for(int j = 0; j<BLOCK_SIZE;j++){
                        if(buff_data[j] != '\0'){
                            size = size+1; // Increase size for each character
                        }
                    }
                    //printf("Block:%d Size:%d Buffer:%s Length:%d\n", index, strlen(buff_data), buff_data, strlen(buff_data));
                }
                index = -1; 
            }
            // Indirect pointers (512)
            for(int indcount = 0; indcount<MAX_DATA_BLOCKS_PER_FILE-12;indcount++){ // direct+indirect pointers - direct pointers = 512
                index = table[k+1].indirect[indcount];
                if(index>0){
                    char buff_data[BLOCK_SIZE];
                    read_blocks(index, 1, (void*) buff_data);
                    for(int l=0;l<BLOCK_SIZE;l++){
                        if (buff_data[l] != '\0'){
                            size = size+1;
                        }
                    }
                    //printf("Block:%d Size:%d Buffer:%s Length:%d\n", index, strlen(buff_data), buff_data, strlen(buff_data));
                }
                index = -1;
            }
            break;
        }
    }
    return size;
}

// Returns the index in the file descriptor table that corresponds to the newly opened file or -1 if the file is already opened or the name of file is incorrect.
int sfs_fopen(char* name){
    num_files = 0;
    for(int i = 1; i<NUM_INODES; i++){
        if (table[i].taken == 1){
            num_files = num_files+1;
        }
    }

    // Check if file name exceeds 20 characters
    int len = 0;
    while(name[len] != '\0'){
        len = len +1;
    }
    if (len > 20){
        //printf("File name length exceeded (20)!\n");
        return -1;
    }
    for (int j = 0; j<NUM_INODES-1; j++){
        if (strcmp(name, root[j].names) == 0){
            // Check if file is already opened
            //printf("File is already open in FDT!\n");
            int i_n = j+1; // Inode number
            for(int k = 0; k<NUM_INODES; k++){
                file_descriptor* fd = &fdt[k];
                if (fd->inode == i_n){
                    //printf("File is already open in FDT!\n");
                    return -1;
                }
            }
        }
    }
    // Open unopened file.
    int inode_table_count = -1;
    for (int k=0; k<NUM_INODES-1; k++){
        if (strcmp(name, root[k].names) == 0){ // Check if file already exists in memory.
            inode_table_count = k+1;
            //printf("File %s found. Inode position: %d.\n", name, inode_table_count);
            for (int l = 1; l<NUM_INODES; l++){
                // file_descriptor* fd = &fdt[l];
                if (fdt[l].inode == -1){
                    fdt[l].inode = inode_table_count; // Update Inode
                    fdt[l].read_write_pointer = sfs_getfilesize(name); // Place pointer at the end of the file
                    //printf("File pointer: %d\n", fd->read_write_pointer);
                    root[k].mode = 1; // Mark file as opened.
                    table[inode_table_count].taken = 1; // Mark Inode table position as taken.
                    if (inode_table_count <0){
                        //printf("File Descriptor Error (Negative Value).\n");
                        return -1;
                    }
                    return l; // Return index of FDT inode.
                }
            }
        }
    }
    // FILE DOES NOT EXIST
    for(int m = 1; m<NUM_INODES; m++){
        if (table[m].taken == 0){ // Find free inode.
            inode_table_count = m; 
            //printf("Inode Table Position: %d\n", inode_table_count);
            if (inode_table_count >= 0){
                for (int n = 1; n<NUM_INODES; n++){
                    file_descriptor* fd = &fdt[n];
                    if(fd->inode == -1){
                        fd->inode = inode_table_count; // Update Inode value.
                        fd->read_write_pointer = 0; // Position is 0 since the file is being created.
                        
                        num_files = num_files +1;
                        table[inode_table_count].taken = 1;
                        table[inode_table_count].mode = 1;

                        int name_pointer = 0;
                        while(name[name_pointer] != '\0'){
                            name_pointer = name_pointer+1;
                        }
                        int min = 0;
                        int name_length = name_pointer;
                        root[inode_table_count-1].names  = (char*) malloc(name_length); // Allocate space for name in root directory.
                        for(min = 0; min<name_length;min++){
                            root[inode_table_count-1].names[min] = name[min];
                        } // Copy name. The position is decremented because the root directory does not have the root Inode.
                        root[inode_table_count-1].names[min] = '\0'; // Add end of name character.
                        root[inode_table_count-1].mode = 1;

                        // Write blocks to disk
                        write_blocks(1+NUM_INODE_BLOCKS, sizeof(directory_table)*(NUM_INODES-1)/BLOCK_SIZE +1, root);
                        write_blocks(1,NUM_INODE_BLOCKS, table);
                        return n; // Return index of FDT inode.
                    }
                }
            }
        }
    }
    return -1;
}

// Returns 0 if the file is closed successfully and -1 if the file is already closed or if the fileID is not correct.
int sfs_fclose(int fileID){
    //printf("Closing file.\n");
    if (fileID > 0 && fileID < NUM_INODES){ // Check if fileID is correct.
        file_descriptor* fd = &fdt[fileID];
        if (fd->inode == -1){ // File is already closed
            return -1;
        }
        fd->inode = -1;
        fd->read_write_pointer = 0;
        fdt[fileID].read_write_pointer = 0;
        //printf("Pointer location: %d\n", fdt[fileID].read_write_pointer);
        fdt[fileID].inode = -1;
        //printf("Inode Value: %d\n", fdt[fileID].inode);

        return 0;

    }
    return -1;
}

// Returns the number of bytes of data written.
int sfs_fwrite(int fileID, char* buf, int length){
    num_files = 0;
    for(int i = 1; i<NUM_INODES;i++){
        if(table[i].taken == 1){
            num_files = num_files+1;
        }
    }
    // Initialize variables
    NUMBER_BLOCKS_TAKEN_BY_ROOT = sizeof(root)/BLOCK_SIZE + 1;
    int write_flag = -1;
    int byte_qty = length;
    int bytes_written = 0;
    int pointers_blocks = 12 + BLOCK_SIZE/(sizeof(unsigned int));
    //printf("Number of pointers: %d\n", pointers_blocks);
    //printf("First entry of free list: %d\n", free_blocks[0].number);
    file_descriptor* fd = &fdt[fileID];
    
    if (fd->inode == -1){
        //printf("Empty file descriptor!\n");
        return 0;
    }
    if (fd->inode == 0){
        //printf("Root File Descriptor...\n");
        return 0;
    }
    //printf("Number of bytes: %d\n", length);
    //printf("Inode: %d\n", fd-> inode);
    //printf("Write to file %s\n", root[fd->inode-1].names);

    int file_size = sfs_getfilesize(root[fd->inode -1].names);
    //printf("File size: %d bytes\n", root[fd->inode-1].names);
    int capacity = BLOCK_SIZE*(12+ BLOCK_SIZE/sizeof(unsigned));
    //printf("File Capacity: %d\n", capacity);

    if (file_size >= capacity){ // Return no bytes written if the file size is larger or equal to the capacity of the file in bytes.
        return 0;
    }

    int block = fd->read_write_pointer/BLOCK_SIZE + 1; // Check from which block we start writing.
    int current_block = block-1;

    if (current_block <0){
        //printf("ERROR: Negative current block.\n");
        return 0;
    }
    if (fd->read_write_pointer >= (BLOCK_SIZE*pointers_blocks)){
        //printf("ERROR: Not possible to go over the address space.\n");
        return 0;
    }
    if (fd->read_write_pointer < 0){
        //printf("ERROR: Negative pointer...\n");
        return 0;
    }
    //printf("START WRITING PROCESS.\n");
    while((byte_qty>0) && (capacity>0) && (current_block < pointers_blocks)){
        int writing_to_block= fd->read_write_pointer%BLOCK_SIZE; // Where within the block
        char buff[BLOCK_SIZE];
        // Direct pointers. Update variables Only. No writing.
        if (current_block < 12){
            if (table[fd->inode].data_ptrs[current_block]>0){
                fprintf(stderr,"Opening previously written block.\n");
                read_blocks(table[fd->inode].data_ptrs[current_block],1,(void*) buff); // Must consider if there is already data in the block.
            }
            if (table[fd->inode].data_ptrs[current_block] == 0){ // Writing to unallocated block.
                for (int j = 0;j<BLOCK_SIZE;j++){
                    buff[j] = '\0'; // Set everything to \0.
                }
            }
            

            // Update variables in a block until the end of the block
            while ((current_block<pointers_blocks) && (writing_to_block<BLOCK_SIZE) && (byte_qty>0) && (capacity>0)){
                if (buf[bytes_written] == '\0'){ // Null buf
                    table[fd->inode].size = table[fd->inode].size+1;
                    //printf("Null buf");
                }
                buff[writing_to_block] = buf[bytes_written]; // length
                writing_to_block = writing_to_block +1;
                bytes_written = bytes_written +1; 
                byte_qty = byte_qty-1; // Bytes left to write
                capacity = capacity-1; // Capacity of file that can be written
                fd->read_write_pointer = fd->read_write_pointer+1;
            }
        }
        // Indirect pointers. Update variables.
        if (current_block >= 12 && current_block < pointers_blocks){
            if (table[fd->inode].indirect[current_block-12] > 0){ // Data block has been allocated before.
                read_blocks(table[fd->inode].indirect[current_block-12], 1, (void*) buff); // Consider if there is already data in the block.
            }
            if (table[fd->inode].indirect[current_block-12] == 0){
                table[fd->inode].indirect[current_block-12] = 0;
                for (int k = 0;k<BLOCK_SIZE;k++){
                    buff[k] = '\0';
                }
            }
            // Update variables in a block until the end of the block
            while ((current_block<pointers_blocks) && (writing_to_block < BLOCK_SIZE) && (byte_qty>0) && (capacity > 0)){
                if (buf[bytes_written] == '\0'){
                    table[fd->inode].size = table[fd->inode].size + 1;
                }
                buff[writing_to_block] = buf[bytes_written];
                writing_to_block = writing_to_block +1;
                bytes_written = bytes_written +1;
                byte_qty = byte_qty-1;
                capacity = capacity-1;
                fd->read_write_pointer = fd->read_write_pointer+1;
            }
        }

        // Exceeded the number of blocks allowed for the file.
        if (current_block > pointers_blocks){
            //printf("Exceeded file writing capacity.\n");
            return bytes_written;
        }
        // Writing
        if (current_block < 12){
            if (current_block >= pointers_blocks){ // Max number of blocks reached.
                return bytes_written;
            }
            if (table[fd->inode].data_ptrs[current_block] > 0){
                write_blocks(table[fd->inode].data_ptrs[current_block],1, (void*) buff); // Write data
                write_flag = 1;
            }
            // Writing new data
            if (table[fd->inode].data_ptrs[current_block] == 0){
                int pointer = 0;
                int free_block = -1;
                // Find free block
                for(pointer = 0; pointer<NUM_ENTRIES_FREE_BLOCKS;pointer++){
                    uint64_t l = 0;
                    uint64_t m = free_blocks[pointer].number;
                    uint64_t o = 1;
                    for (int n = 63; n>= 0; n--){ // Iterate through free data blocks
                        l = 0;
                        l |= o<<n; //Bitwise OR  and left shift 
                        if (((l & m) >> n) == 0){ // Check if the n-th bit is 0.
                            //printf("Free data block: %d\n", 63-n);
                            free_block = 63-n + 1 + NUM_INODE_BLOCKS+NUMBER_BLOCKS_TAKEN_BY_ROOT; // Calculate the block number

                            // Set the n-th bit in free_blocks blocks to 1
                            uint64_t p = 1;
                            free_blocks[pointer].number = free_blocks[pointer].number |(p << n);
                            break;
                        }
                    }
                    if (free_block >= 0){ // Free block found
                        break;
                    }
                }   
                if (free_block < 0 ){ // Blocks have all been taken
                    //printf("Unable to write in a data block since they have all been taken.\n");
                    if (write_flag == 1){
                        return bytes_written;
                    }
                    return 0;
                }
                if (free_block >= 0){
                    //printf("Found data block: %d\n", free_block*(pointer+1));
                    table[fd->inode].data_ptrs[current_block] = free_block + (pointer*64); // Allocate to file

                    write_blocks(table[fd->inode].data_ptrs[current_block], 1, (void*) buff); // Write to block
                    // Update inode table
                    write_blocks(1, sb.inode_table_length, table); 
                    // Update free blocks on the disk
                    write_blocks(1+sb.inode_table_length+NUM_FREE_BLOCKS_LIST+NUMBER_BLOCKS_TAKEN_BY_ROOT, sb.number_free_blocks, free_blocks);
                    write_flag = 1;
                }
            }
        }
        // SAME PROCEDURE FOR INDIRECT POINTERS
        if (current_block >= 12){
            if (current_block >= pointers_blocks){
                //printf("Exceeded file writing capacity.\n");
                return bytes_written;
            }
            if (table[fd->inode].indirect[current_block-12] > 0){
                write_blocks(table[fd->inode].indirect[current_block-12], 1, (void*) buff); // Write data
                write_flag = 1;
            }
            // Find new free block
            if (table[fd->inode].indirect[current_block-12] == 0){
                int pointer = 0;
                int free_block = -1;
                for (pointer = 0; pointer<NUM_ENTRIES_FREE_BLOCKS;pointer++){
                    uint64_t l = 0;
                    uint64_t m = free_blocks[pointer].number;
                    uint64_t o = 1;
                    for(int n = 63; n >= 0; n--){ // Iterate through free data blocks
                        l = 0;
                        l |= o<< n;
                        if (((l & m) >> n) == 0){ // Check if the n-th bit is 0
                            //printf("Free data block: %d\n", 63-n);
                            free_block = 63-n + 1 + NUM_INODE_BLOCKS + NUMBER_BLOCKS_TAKEN_BY_ROOT; // Calculate the free block number
                            
                            // Set the n-th bit in free_blocks to 1.
                            uint64_t p = 1;
                            free_blocks[pointer].number = free_blocks[pointer].number |(p<<n);
                            break;
                        }
                    }
                    if (free_block >= 0){ // Free block found
                        break;
                    }
                }
                if (free_block < 0){ // Blocks have all been taken
                    //printf("Unable to write in a data block since they have all been taken.\n");
                    if (write_flag == 1){
                        return bytes_written;
                    }
                    return 0;
                }
                if (free_block >= 0){
                    //printf("Found data block: %d\n", free_block*(pointer+1));
                    table[fd->inode].indirect[current_block-12] = free_block + (pointer*64); // Allocate to file

                    write_blocks(table[fd->inode].indirect[current_block-12], 1, (void*) buff); // Write
                    // Update inode table
                    write_blocks(1, sb.inode_table_length, table);
                    // Update free_blocks table
                    write_blocks(1+sb.inode_table_length+NUM_FREE_BLOCKS_LIST+NUMBER_BLOCKS_TAKEN_BY_ROOT, sb.number_free_blocks, free_blocks);

                    write_flag = 1;
                }

            }
        }
        current_block = current_block +1;
    }
    // Update pointer and inode value
    fdt[fileID].read_write_pointer = fd->read_write_pointer;
    //printf("New pointer: %d\n", fdt[fileID].read_write_pointer);
    fdt[fileID].inode = fd->inode;
    //printf("New inode: %d\n", fdt[fileID].inode);

    return bytes_written;
}

// Returns the number of bytes read.
int sfs_fread(int fileID, char* buf, int length){
    num_files = 0;
    for(int i = 1; i<NUM_INODES;i++){
        if(table[i].taken == 1){
            num_files = num_files+1;
        }
    }
    // Initialize variables
    NUMBER_BLOCKS_TAKEN_BY_ROOT = sizeof(root)/BLOCK_SIZE + 1; //Divide to find the full blocks + 1 not full block
    int read_flag = -1;
    int byte_qty = length;
    int bytes_read = 0;
    int pointers_blocks = 12 + BLOCK_SIZE/(sizeof(unsigned int)); // Number of pointers (512+12)
    int capacity = BLOCK_SIZE*(12+BLOCK_SIZE/(sizeof(unsigned))); // Capacity of the file in bytes
    //printf("Number of pointers: %d\n", pointers_blocks);
    //printf("File capacity: %d bytes\n", capacity);
    file_descriptor* fd = &fdt[fileID];
    
    if (fd->inode == -1){
        //printf("Empty file descriptor!\n");
        return 0;
    }
    if (fd->inode == 0){
        //printf("Root File Descriptor...\n");
        return 0;
    }
    //printf("File size: %d bytes\n", root[fd->inode-1].names);

    int block = fd->read_write_pointer/BLOCK_SIZE +1; // Check in which block the pointer is.
    int current_block = block-1;

    if (current_block <0){
        //printf("ERROR: Negative current block.\n");
        return 0;
    }
    if (fd->read_write_pointer >= (BLOCK_SIZE*pointers_blocks)){
        //printf("ERROR: Not possible to go over the address space.\n");
        return 0;
    }
    if (fd->read_write_pointer < 0){
        //printf("ERROR: Negative pointer...\n");
        return 0;
    }

    //printf("START READING PROCESS.\n");
    while((byte_qty>0) && (capacity>0) && (current_block < pointers_blocks)){
        int reading_from_block = fd->read_write_pointer % BLOCK_SIZE; // Check where the pointer is in the block.

        char buff[BLOCK_SIZE];

        // Read from Direct Pointers
        if (current_block < 12){
            // Check if data block has been allocated before
            if (table[fd->inode].data_ptrs[current_block] > 0){
                read_blocks(table[fd->inode].data_ptrs[current_block], 1, (void*) buff);
            }
            if (table[fd->inode].data_ptrs[current_block] == 0){
                for (int j = 0; j<BLOCK_SIZE;j++){
                    buff[j] = '\0';
                }
            }
            // Update values
            while((current_block < pointers_blocks) && (reading_from_block < BLOCK_SIZE) && (byte_qty>0) && (capacity > 0)){
                buf[bytes_read] = buff[reading_from_block];
                bytes_read = bytes_read + 1;
                byte_qty = byte_qty - 1;
                read_flag = 1;
                capacity = capacity - 1;
                reading_from_block = reading_from_block + 1;
                fd->read_write_pointer = fd->read_write_pointer+1;
            }
        }
        // Indirect pointers
        if ((current_block >= 12) && (current_block < pointers_blocks)){
            if (table[fd->inode].indirect[current_block-12] > 0){
                read_blocks(table[fd->inode].indirect[current_block-12], 1, (void*) buff);
            }
            if (table[fd->inode].indirect[current_block-12] == 0){
                table[fd->inode].data_ptrs[current_block] = 0;
                for (int k = 0; k<BLOCK_SIZE;k++){
                    buff[k] = '\0';
                }
            }
            // Update for indirect pointers too
            while ((current_block < pointers_blocks) && (reading_from_block < BLOCK_SIZE) && (byte_qty > 0) && (capacity > 0)){
                buf[bytes_read] = buff[reading_from_block];
                bytes_read = bytes_read + 1;
                byte_qty = byte_qty -1;
                read_flag = 1;
                capacity = capacity -1;
                reading_from_block = reading_from_block + 1;
                fd->read_write_pointer = fd->read_write_pointer+1;

            }
        }
        if (current_block > pointers_blocks){
            //printf("Exceeded file reading capacity.\n");
            return bytes_read;
        }
        current_block = current_block + 1;
    }
    // Update pointer and inode value
    fdt[fileID].read_write_pointer = fd->read_write_pointer;
    //printf("New pointer: %d\n", fdt[fileID].read_write_pointer);
    fdt[fileID].inode = fd->inode;
    //printf("New inode: %d\n", fdt[fileID].inode);

    int chars = 0;
    // Check buf from input and return number of characters in buf.
    if (table[fd->inode].size == 0){
        for (int a = 0; a<length;a++){
            if (buf[a] != '\0'){ 
                chars++;
            }
        }
        return chars;
    }

    if (read_flag == 1){
        return bytes_read;
    }

    return 0;
}

// Returns 0 if the pointer is moved successfully and -1 if the pointer is not moved successfully.
int sfs_fseek(int fileID, int loc){
    file_descriptor* fd = &fdt[fileID];
    int capacity = BLOCK_SIZE*(12+BLOCK_SIZE/sizeof(unsigned)); // File capacity
    if (loc <0 ){
        //printf("Location is out of range (negative)!\n");
        return -1;
    }
    if (loc >= capacity){
        //printf("Location is out of range (capacity)!\n");
        return -1;
    }
    if (fd->inode == 0){
        //printf("Root cannot be opened.\n");
        return -1;
    }
    if (fd->inode == -1){
        //printf("File descriptor is empty.\n");
        return -1;
    }
    // Update pointer and inode value
    fdt[fileID].read_write_pointer = loc;
    //printf("New pointer location: %d\n", fdt[fileID].read_write_pointer);
    fdt[fileID].inode = fd->inode;
    //printf("New inode: %d\n", fdt[fileID].inode);
    return 0;
    
}

// Returns the index of the removed file, 0 if the root is trying to be removed, and -1 if the remove was unsuccessful.
int sfs_remove(char* file){

    // Variable initialization
    int special = 0;
    int removed = 0;
    num_files = 0;

    // Number of files in the system
    for (int i = 1; i<NUM_INODES;i++){
        if (table[i].taken == 1){
            num_files = num_files + 1;
        }
    }
    
    //printf("REMOVE FILE\n");
    // Search for the file in the File Descriptor Table and in the Root Table
    int pointers_blocks = 12 + BLOCK_SIZE/(sizeof(unsigned int));
    for (int j = 0; j<NUM_INODES-1;j++){
        int search = 0;
        while(root[j].names[search] != '\0'){ // Search until there are no file names in the Root Table
            search = search + 1;
        }
        if (root[j].names[0] == '/'){ // Absolute path in the root directory since the names field starts with '/'
            special = 1;
            //printf("Special case.\n");
            for (int k = 1; k<search; k++){ // Compare remaining characters
                if (root[j].names[k] != file[k-1]){
                    special = 0;
                    break;
                }
            }
        } // File found or special case
        if (strcmp(root[j].names, file) == 0 || special){
            root[j].mode = 0; // Set mode to 0 (close)
            int index = -1;
            int remove_node = j+1; // Inode 

            current_file = 0;

            for (int l = 1; l < NUM_INODES; l++){
                if (fdt[l].inode == remove_node){
                    index = l;
                    removed = index; // Removed <- index in the file descriptor table
                    //printf("File removed.\n");
                    break;
                }
            }
            if (index == 0){
                //printf("Trying to remove the Root!\n");
                return 0;
            }
            // Remove data blocks associated with the file
            if (remove_node > 0){
                // Direct pointers
                for (int m = 0; m<12; m++){
                    if (table[remove_node].taken == 1){
                        char buff[BLOCK_SIZE];
                        if (table[remove_node].data_ptrs[m] > 0){ // Already allocated.
                            int data_position = table[remove_node].data_ptrs[m];
                            if (data_position > 0){
                                // Fill buffer with zeroes
                                for (int p = 0; p<BLOCK_SIZE; p++){
                                    buff[p] = '\0';
                                }
                                // Pointer of data blocks to be freed
                                int pointer_data_block = -(data_position-1-NUM_INODE_BLOCKS-NUMBER_BLOCKS_TAKEN_BY_ROOT-63);
                                // Free block index
                                int index_free_block = pointer_data_block/64;
                                // Index in free block
                                int free_data_block_index = pointer_data_block % 64;

                                if (free_data_block_index < 0){
                                    //printf("Cannot free data block from file.\n");
                                    //return -1;
                                }
                                // Set the corresponding bit in the free block list to 0
                                int block_pointer = index_free_block;
                                uint64_t q = 0;
                                uint64_t r = free_blocks[index_free_block].number;
                                uint64_t t = 1;
                                // 64 integers used
                                for (int s = 63 ; s>= 0; s--){ // Iterate through the free data blocks
                                    if ( s == free_data_block_index){
                                        q = 0;
                                        q |= t<< s;
                                        if (((q & r) >> s) == 1){ // Check if the n-th bit is 1
                                            //printf("Freeing data block.\n");
                                            uint64_t u = 1;
                                            // Set the n-th bit to 0.
                                            free_blocks[block_pointer].number = free_blocks[block_pointer].number ^ (u<< s);
                                            break;
                                        }
                                    }
                                }
                                // Write zeroes to the data block to erase the content
                                write_blocks(table[remove_node].data_ptrs[m], 1, (void*) buff);
                                // Set the data pointer to 0 and the size to 0 to indicate that the block is no longer associated with the file and to set the amount of data in the file to 0.
                                table[remove_node].data_ptrs[m] = 0;
                                table[remove_node].size = 0;
                            }                       
                        }
                    }
                }
                // SAME PROCEDURE FOR INDIRECT BLOCKS
                for (int k = 0;k<pointers_blocks-12;k++){
                    if (table[remove_node].taken == 1){
                        char buff[BLOCK_SIZE];
                        if (table[remove_node].indirect[k] > 0){
                            
                            int data_position = table[remove_node].indirect[k];
                            // Pointer of data blocks to be freed
                            int pointer_data_block = -(data_position-1-NUM_INODE_BLOCKS-NUMBER_BLOCKS_TAKEN_BY_ROOT-63);
                            // Free block index
                            int index_free_block = pointer_data_block/64;
                            // Index in free block
                            int free_data_block_index = pointer_data_block%64;
                            // Fill buffer with zeroes
                            for (int l = 0; l<BLOCK_SIZE; l++){
                                buff[l] = '\0';
                            }
                            // Set the corresponding bit in the free block list to 0
                            int block_pointer = index_free_block;
                            uint64_t q = 0;
                            uint64_t r = free_blocks[index_free_block].number;
                            uint64_t t = 1;
                            // 64 integers used
                            for (int s = 63 ; s>= 0; s--){ // Iterate through the free data blocks
                                if ( s == free_data_block_index){
                                    q = 0;
                                    q |= t<< s;
                                    if (((q & r) >> s) == 1){ // Check if the n-th bit is 1
                                        //printf("Freeing data block.\n");
                                        // Set the n-th bit to 0
                                        uint64_t u = 1;
                                        free_blocks[block_pointer].number = free_blocks[block_pointer].number ^ (u<< s);
                                        break;
                                    }
                                }
                            }
                            // Write zeroes from buffer to the data block to erase the content
                            write_blocks(table[remove_node].indirect[k], 1, (void*) buff);
                            // Set the data pointer to 0 and the size to 0 to indicate that the block is no longer associated with the file and to set the amount of data in the file to 0.
                            table[remove_node].indirect[k] = 0;
                            table[remove_node].size = 0;
                        }
                    }
                }
                // Restore inode values (not in use and no size)
                table[remove_node].taken = 0;
                table[remove_node].size = 0;
                // Write updated inode table to the disk
                write_blocks(1,NUM_INODE_BLOCKS, table);
            }
            // Allocate memory for a new names array in the root directory.
            root[j].names = (char*)malloc(MAXFILENAME);
            root[j].names[0] = '\0';
            // Update Inode table and Root table on the disk
            write_blocks(1, NUM_INODE_BLOCKS, table);
            write_blocks(1+NUM_INODE_BLOCKS, sizeof(directory_table)*(NUM_INODES-1)/BLOCK_SIZE +1, root);
            break;
        }
    }
    // Update the free blocks on the disk
    write_blocks(1+NUM_INODE_BLOCKS+NUM_FREE_BLOCKS_LIST+NUMBER_BLOCKS_TAKEN_BY_ROOT, sb.number_free_blocks, free_blocks);
    // Update the root directory on the disk
    write_blocks(1+NUM_INODE_BLOCKS, sizeof(directory_table)*(NUM_INODES-1)/BLOCK_SIZE +1, root);
    // If a file was successfully removed, returns the index of the removed file in the file descriptor table.
    if (removed > 0){
        return removed;
    }
    // File removal unsuccessful
    return -1;
}