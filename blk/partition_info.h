#define BR 1                                            /*Extented partition info*/
#define SECTOR_SIZE               512      
#define MBR_SIZE SECTOR_SIZE
#define MBR_DISK_SIGNATURE_OFFSET 440			/* 4-byte disk signature is placed at the offset 440.*/
#define MBR_DISK_SIGNATURE_SIZE     4                   /* DISK OFFSET SIZE*/                                                                              
#define PARTITION_TABLE_OFFSET    446                   /* (SECTOR_SIZE -(4*PARTITION_ENTRY_SIZE + MBR_SIGNATURE_OFFSE)):::-EX-512 – (4 * 16 + 2) = 446*/
#define PARTITION_ENTRY_SIZE       16                   /* sizeof(PartEntry)  */
#define PARTITION_TABLE_SIZE       64                   /* sizeof(PartTable) */
#define MBR_SIGNATURE_OFFSET      510                   /* MBR_SIZE - MBR_SIGNATURE_SIZE*/
#define MBR_SIGNATURE_SIZE          2
#define MBR_SIGNATURE          0xAA55

#if BR
#define BR_SIZE SECTOR_SIZE
#define BR_SIGNATURE_OFFSET      510
#define BR_SIGNATURE_SIZE          2
#define BR_SIGNATURE          0xAA55
#endif

typedef struct
{
	unsigned char boot_type; 		         /* 0x00 - Inactive; 0x80 - Active (Bootable) */                                                			
	unsigned char start_head;                        /* Starting head*/
	unsigned char start_sec:6;                       /* 6 bits  Starting sector : Bits 6-7 are the upper two bits for the Starting Cylinder field*/
	unsigned char start_cyl_hi:2;                    /* 6 bits  Starting sector : Bits 6-7 are the upper two bits for the Starting Cylinder field*/
	unsigned char start_cyl;                         /* 10 bits Starting Cylinder*/
	unsigned char part_type;                         /* System ID : Follow Note: section  ,LINUX: 0x83 */
	unsigned char end_head;                          /* Ending Head*/
	unsigned char end_sec:6;                         /* 6 bits  Ending Sector (Bits 6-7 are the upper two bits for the ending cylinder field)*/
	unsigned char end_cyl_hi:2;                      /* 6 bits  Ending Sector (Bits 6-7 are the upper two bits for the ending cylinder field)*/
	unsigned char end_cyl;                           /* 10 bits Ending Cylinder*/
	unsigned int abs_start_sec;                      /* Relative Sector (to start of partition -- also equals the partition's starting LBA value)*/
	unsigned int sec_in_part;		         /* Total Sectors in partition */	
} PartitionEntry;

/*
Element (offset) 	Size	 	Description

0 	                byte	 	Boot indicator bit flag: 0 = no, 0x80 = bootable (or "active")
1			byte	 	Starting head
2 			byte	 	6 bits 	Starting sector (Bits 6-7 are the upper two bits for the Starting Cylinder field.)
3 			byte	 	10 bits Starting Cylinder
4 			byte	 	System ID
5 			byte	 	Ending Head
6 			byte	 	6 bits 	Ending Sector (Bits 6-7 are the upper two bits for the ending cylinder field)
7 			byte	 	10 bits Ending Cylinder
8 			uint32_t 	Relative Sector (to start of partition -- also equals the partition's starting LBA value)
12 			uint32_t 	Total Sectors in partition 

Note: The System ID byte is supposed to indicate what filesystem is contained on the partition (ie. Ext2, ReiserFS, FAT32, NTFS, ...).
Extended partitions are a way of adding more than 4 partitions to a partition table.The partition table may have one and only one entry 
that has the SystemID 0x5 (or 0xF). This describes an extended partition. */

typedef PartitionEntry PartitionTable[4];

static PartitionTable different_partition_table =
{
	{
		boot_type: 0x00,				
		start_head: 0x00,                                                    
		start_sec: 0x2,                                                      
		start_cyl: 0x00,                                                     
		part_type: 0x83,                                /*Linux Partition*/                     
		end_head: 0x00,                                 
		end_sec: 0x20,                                                       
		end_cyl: 0x09,                                                       
		abs_start_sec: 0x00000001,                                           
		sec_in_part: 0x0000013F                          /*319 Sector*/                     
	},                                                                           
	{                                                        
		boot_type: 0x00,                                 
		start_head: 0x00,                                                    
		start_sec: 0x1,                                                      
		start_cyl: 0x0A, 				 // extended partition start cylinder (BR location)
		part_type: 0x05,                                 // extended partitio                    
		end_head: 0x00,                                  
		end_sec: 0x20,                                                       
		end_cyl: 0x13,                                                       
		abs_start_sec: 0x00000140,                                           
		sec_in_part: 0x00000140                          /*320 Sector*/                     
	},                                                                           
	{                                                                            
		boot_type: 0x00,                                
		start_head: 0x00,
		start_sec: 0x1,
		start_cyl: 0x14,
		part_type: 0x83,                                  /*Linux Partition*/
		end_head: 0x00,
		end_sec: 0x20,
		end_cyl: 0x1F,
		abs_start_sec: 0x00000280,
		sec_in_part: 0x00000180                         /*319 Sector*/
	},
	{
	}
};

#if BR
static unsigned int different_log_part_br_cyl[] = {0x0A, 0x0E, 0x12};
static const PartitionTable different_log_part_table[] =
{
	{
		{
			boot_type: 0x00,
			start_head: 0x00,
			start_sec: 0x2,
			start_cyl: 0x0A,
			part_type: 0x83,
			end_head: 0x00,
			end_sec: 0x20,
			end_cyl: 0x0D,
			abs_start_sec: 0x00000001,
			sec_in_part: 0x0000007F
		},
		{
			boot_type: 0x00,
			start_head: 0x00,
			start_sec: 0x1,
			start_cyl: 0x0E,
			part_type: 0x05,
			end_head: 0x00,
			end_sec: 0x20,
			end_cyl: 0x11,
			abs_start_sec: 0x00000080,
			sec_in_part: 0x00000080
		},
	},
	{
		{
			boot_type: 0x00,
			start_head: 0x00,
			start_sec: 0x2,
			start_cyl: 0x0E,
			part_type: 0x83,
			end_head: 0x00,
			end_sec: 0x20,
			end_cyl: 0x11,
			abs_start_sec: 0x00000001,
			sec_in_part: 0x0000007F
		},
		{
			boot_type: 0x00,
			start_head: 0x00,
			start_sec: 0x1,
			start_cyl: 0x12,
			part_type: 0x05,
			end_head: 0x00,
			end_sec: 0x20,
			end_cyl: 0x13,
			abs_start_sec: 0x00000100,
			sec_in_part: 0x00000040
		},
	},
	{
		{
			boot_type: 0x00,
			start_head: 0x00,
			start_sec: 0x2,
			start_cyl: 0x12,
			part_type: 0x83,
			end_head: 0x00,
			end_sec: 0x20,
			end_cyl: 0x13,
			abs_start_sec: 0x00000001,
			sec_in_part: 0x0000003F
		},
	}
};

#endif 
