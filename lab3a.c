#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include "ext2_fs.h"


int BLOCKSIZE;
int full_inodes[24];
int group_num = 0;
__u8 superblock_read[EXT2_MAX_BLOCK_SIZE];

/****************************/
/*Block pointer declarations*/
/***************************/
struct ext2_super_block *superblock_ptr;
struct ext2_group_desc *groupdescriptor_ptr;
/********************************/
/*End block pointer declarations*/
/*******************************/

void exit_1(char *str)
{
  fprintf(stderr, "Error: ");
  if(strlen(str) != 0)
    fprintf(stderr, "%s\n", str);
  if(errno != 0)
    perror("");
  fprintf(stderr, "Exiting with error code 1.\n");
  exit(1);
  
   
}


void timestamp_to_date(__u32 timestamp, char time_buf[])
{
  char buf[80];
  time_t time = (int) timestamp;
  struct tm ts;  
  ts = *localtime(&time);

  strftime(buf, sizeof(buf), "%m/%d/%y %H:%M:%S", &ts);

  strcpy(time_buf, buf);
}

void superblock_output()
{
   int blocksize = 1024 << superblock_ptr->s_log_block_size;
   BLOCKSIZE = blocksize;


    printf("SUPERBLOCK,%d,%d,%d,%d,%d,%d,%d\n", superblock_ptr->s_blocks_count,
	   superblock_ptr->s_inodes_count, blocksize, superblock_ptr->s_inode_size,
	   superblock_ptr->s_blocks_per_group, superblock_ptr->s_inodes_per_group,
	   superblock_ptr->s_first_ino);

}

void group_output (int fd)
{
    groupdescriptor_ptr = malloc(32);
    if (pread (fd, groupdescriptor_ptr, 32, 2048) == -1)
    {
        exit_1("Could not read descriptor table");
    }

    printf("GROUP,0,%d,%d,%d,%d,%d,%d,%d\n", superblock_ptr->s_blocks_count, superblock_ptr->s_inodes_count,
           groupdescriptor_ptr->bg_free_blocks_count, groupdescriptor_ptr->bg_free_inodes_count,
           groupdescriptor_ptr->bg_block_bitmap, groupdescriptor_ptr->bg_inode_bitmap,groupdescriptor_ptr->bg_inode_table);
}


int is_bit_set(__u8 byte, int index)
{
  switch(index)
    {
    case 0: return (byte & 0x80) >> 7;
    case 1: return (byte & 0x40) >> 6;
    case 2: return (byte & 0x20) >> 5;
    case 3: return (byte & 0x10) >> 4;
    case 4: return (byte & 0x8) >> 3;
    case 5: return (byte & 0x4) >> 2;
    case 6: return (byte & 0x2) >> 1;
    case 7: return (byte & 0x1);
    }
  return -1;
}

void superblock_info(int fd, __u8 superblock_read[])
{
  if(pread(fd, superblock_read, EXT2_MAX_BLOCK_SIZE, 1024) == -1)
    exit_1("");
  superblock_ptr = (struct ext2_super_block*) superblock_read;
  superblock_output();
}

void free_bits(__u8 map_read[], int block_flag, int full_inodes[])
{
  int i;
  int num_iterations;
  /*Divide by 8 because a byte has 8 bits*/
  if(block_flag)
    num_iterations = superblock_ptr->s_blocks_per_group / 8;
  
  else
    num_iterations = superblock_ptr->s_inodes_per_group / 8;
  
  for(i = 0; i < num_iterations; i++)
    {
      /*Cycle through each of the bits in a given byte*/
    int j;
    for(j = 0; j < 8; j++)
      {
  	int bit = is_bit_set(map_read[i],j);
      	int map_index = i * 8 + j;
  	if(! bit)
  	  {

	    if(block_flag && !full_inodes)
		printf("BFREE,%d\n", map_index);
	    
	    else if(full_inodes && !block_flag)
	      {
		printf("IFREE,%d\n", map_index);
		
	        full_inodes[map_index] = 0;
	      }
	       
  	  }
	
	else if(bit == 1 && full_inodes)
          full_inodes[map_index] = 1;
	
  	else if(bit == -1)
  	  exit_1("Error with bit-map.");
	       
      }

    }
  
}

void dump_bytes(__u8 table[])
{
  int i;
  for(i = 0; i < 1024; i++)
    {
      printf("%02x ", table[i]);
      if(i%7 == 0 && i != 0)
	printf("\n");
      if(i%127 == 0)
	printf("\n\n NEW INODE \n\n");
      
    }
  printf("\n");
}

char file_type(__u16 i_mode)
{

  __u8 byte = i_mode >> 12;
  switch(byte)
    {
    case 0xA: return 's';
    case 0x8: return 'f';
    case 0x4: return 'd';
    default: return '?';      
      
    }
}

void inode_table_analysis(__u8 inode_table_read[], int full_inodes[])
{
  int i;
  int NUM_INODES = superblock_ptr->s_inodes_per_group;
  __u16 INODE_SIZE = superblock_ptr->s_inode_size;
  printf("inode_size = %d\n", INODE_SIZE);
  struct ext2_inode *inode_table = malloc(INODE_SIZE * NUM_INODES);
  inode_table = (struct ext2_inode*) inode_table_read;

  /*Every 128 bytes contains an inode*/

  for(i = 0; i < NUM_INODES; i++)
    {
      

      if(inode_table[i].i_mode != 0 && inode_table[i].i_links_count != 0)
	{
	  
	  int file_size = inode_table[i].i_size;
	  __u16 mode = inode_table[i].i_mode;
	  char filetype = file_type(mode);
	  mode &= 0x0fff;
	  __u16 group = inode_table[i].i_gid;
	  __u16 link_count = inode_table[i].i_links_count;
	  
	  __u32 m_time = inode_table[i].i_mtime;
	  char mod_time[80];
	  timestamp_to_date(m_time, mod_time);
	  
	  char access_time[80];
	  __u32 a_time = inode_table[i].i_atime;
	  timestamp_to_date(a_time, access_time);

	  __u32 numblocks = inode_table[i].i_blocks;
	  printf("INODE,%d,%c,0%o,%s,%d,%d,%s,%s,%s,%d,%d\n",i, filetype,mode, "owner", group, link_count, "change_time", mod_time, access_time, file_size, numblocks);

	}
    }


}


int main(int argc, char **argv)
{

  char *file = argv[1];
  int fd = open(file, O_RDONLY, 0444);
  if(fd == -1)
    exit_1("");

  
  /*************/
  /*SUPER BLOCK*/
  /************/
  superblock_info(fd, superblock_read);


  /************************/
  /*BLOCK GROUP DESCRIPTOR TABLE*/
  /***********************/
  
  group_output(fd);
  
  
  /**********************/
  /*BITMAP AND INODE MAP*/
  /*********************/
  
  /*Get the block number where the bitmap is*/

  __u8 blockmap[BLOCKSIZE];
  int blockmap_byte = groupdescriptor_ptr->bg_block_bitmap * BLOCKSIZE;
  if(pread(fd, blockmap, BLOCKSIZE, blockmap_byte) == -1)
    exit_1("");
  
  /*Account for variable group size*/
  //each byte is 8 bits


  int block_flag = 1;
  free_bits(blockmap, block_flag, NULL);

  
  /*Get the free inodes, pretty much the same implementation as free blocks*/
  __u8 inodemap[BLOCKSIZE];
  

  int NUM_INODES = superblock_ptr->s_inodes_per_group;
  /*0 means empty, 1 means full*/
  int full_inodes[NUM_INODES];

  int inodemap_byte = groupdescriptor_ptr->bg_inode_bitmap * BLOCKSIZE;

  
  if(pread(fd, inodemap, BLOCKSIZE, inodemap_byte) == -1)
    exit_1("");
  

  
  block_flag = 0;
  
  free_bits(inodemap, block_flag, full_inodes);
  
  /**************************/
  /*START INODE SUMMARY HERE*/
  /*************************/


  __u8 inode_table_read[BLOCKSIZE];
  memset(inode_table_read, 0, BLOCKSIZE);

  int inode_table_byte = groupdescriptor_ptr->bg_inode_table * BLOCKSIZE;


  if(pread(fd, inode_table_read, BLOCKSIZE, inode_table_byte) == -1)
    exit_1("");

  inode_table_analysis(inode_table_read, full_inodes);



  exit(0);
  
       
  
}
