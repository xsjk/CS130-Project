#include "devices/partition.h"
#include <packed.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "devices/block.h"
#include "threads/malloc.h"

/* A partition of a block device. */
struct partition
  {
    struct block *block;                /* Underlying block device. */
    block_sector_t start;               /* First sector within device. */
  };

static void partition_read (void *p_, block_sector_t sector, void *buffer);
static void partition_write (void *p_, block_sector_t sector, const void *buffer);

static struct block_operations partition_operations = { partition_read, partition_write};


static void read_partition_table (struct block *, block_sector_t sector,
                                  block_sector_t primary_extended_sector,
                                  int *part_nr);
static void found_partition (struct block *, uint8_t type,
                             block_sector_t start, block_sector_t size,
                             int part_nr);
static const char *partition_type_name (uint8_t);

/* Scans BLOCK for partitions of interest to Pintos. */
void
partition_scan (struct block *block)
{
  int part_nr = 0;
  read_partition_table (block, 0, 0, &part_nr);
  if (part_nr == 0)
    printf ("%s: Device contains no partitions\n", block_name (block));
}

/* Reads the partition table in the given SECTOR of BLOCK and
   scans it for partitions of interest to Pintos.

   If SECTOR is 0, so that this is the top-level partition table
   on BLOCK, then PRIMARY_EXTENDED_SECTOR is not meaningful;
   otherwise, it should designate the sector of the top-level
   extended partition table that was traversed to arrive at
   SECTOR, for use in finding logical partitions (see the large
   comment below).

   PART_NR points to the number of non-empty primary or logical
   partitions already encountered on BLOCK.  It is incremented as
   partitions are found. */
static void
read_partition_table (struct block *block, block_sector_t sector,
                      block_sector_t primary_extended_sector,
                      int *part_nr)
{
  /* Format of a partition table entry.  See [Partitions]. */
  struct partition_table_entry
    {
      uint8_t bootable;         /* 0x00=not bootable, 0x80=bootable. */
      uint8_t start_chs[3];     /* Encoded starting cylinder, head, sector. */
      uint8_t type;             /* Partition type (see partition_type_name). */
      uint8_t end_chs[3];       /* Encoded ending cylinder, head, sector. */
      uint32_t offset;          /* Start sector offset from partition table. */
      uint32_t size;            /* Number of sectors. */
    }
  PACKED;

  /* Partition table sector. */
  struct partition_table
    {
      uint8_t loader[446];      /* Loader, in top-level partition table. */
      struct partition_table_entry partitions[4];       /* Table entries. */
      uint16_t signature;       /* Should be 0xaa55. */
    }
  PACKED;

  struct partition_table *pt;
  size_t i;

  /* Check SECTOR validity. */
  if (sector >= block_size (block))
    {
      printf ("%s: Partition table at sector %"PRDSNu" past end of device.\n",
              block_name (block), sector);
      return;
    }

  /* Read sector. */
  ASSERT (sizeof *pt == BLOCK_SECTOR_SIZE);
  pt = (struct partition_table *) malloc (sizeof *pt);
  if (pt == NULL)
    PANIC ("Failed to allocate memory for partition table.");
  block_read (block, 0, pt);

  /* Check signature. */
  if (pt->signature != 0xaa55)
    {
      if (primary_extended_sector == 0)
        printf ("%s: Invalid partition table signature\n", block_name (block));
      else
        printf ("%s: Invalid extended partition table in sector %"PRDSNu"\n",
                block_name (block), sector);
      free (pt);
      return;
    }

  /* Parse partitions. */
  for (i = 0; i < sizeof pt->partitions / sizeof *pt->partitions; i++)
    {
      struct partition_table_entry *e = &pt->partitions[i];

      if (e->size == 0 || e->type == 0)
        {
          /* Ignore empty partition. */
        }
      else if (e->type == 0x05       /* Extended partition. */
               || e->type == 0x0f    /* Windows 98 extended partition. */
               || e->type == 0x85    /* Linux extended partition. */
               || e->type == 0xc5)   /* DR-DOS extended partition. */
        {
          printf ("%s: Extended partition in sector %"PRDSNu"\n",
                  block_name (block), sector);

          /* The interpretation of the offset field for extended
             partitions is bizarre.  When the extended partition
             table entry is in the master boot record, that is,
             the device's primary partition table in sector 0, then
             the offset is an absolute sector number.  Otherwise,
             no matter how deep the partition table we're reading
             is nested, the offset is relative to the start of
             the extended partition that the MBR points to. */
          if (sector == 0)
            read_partition_table (block, e->offset, e->offset, part_nr);
          else
            read_partition_table (block, e->offset + primary_extended_sector,
                                  primary_extended_sector, part_nr);
        }
      else
        {
          ++*part_nr;

          found_partition (block, e->type, e->offset + sector,
                           e->size, *part_nr);
        }
    }

  free (pt);
}

/* We have found a primary or logical partition of the given TYPE
   on BLOCK, starting at sector START and continuing for SIZE
   sectors, which we are giving the partition number PART_NR.
   Check whether this is a partition of interest to Pintos, and
   if so then add it to the proper element of partitions[]. */
static void
found_partition (struct block *block, uint8_t part_type,
                 block_sector_t start, block_sector_t size,
                 int part_nr)
{
  if (start >= block_size (block))
    printf ("%s%d: Partition starts past end of device (sector %"PRDSNu")\n",
            block_name (block), part_nr, start);
  else if (start + size < start || start + size > block_size (block))
    printf ("%s%d: Partition end (%"PRDSNu") past end of device (%"PRDSNu")\n",
            block_name (block), part_nr, start + size, block_size (block));
  else
    {
      enum block_type type = (part_type == 0x20 ? BLOCK_KERNEL
                              : part_type == 0x21 ? BLOCK_FILESYS
                              : part_type == 0x22 ? BLOCK_SCRATCH
                              : part_type == 0x23 ? BLOCK_SWAP
                              : BLOCK_FOREIGN);
      struct partition *p;
      char extra_info[128];
      char name[16];

      p = (struct partition *) malloc (sizeof *p);
      if (p == NULL)
        PANIC ("Failed to allocate memory for partition descriptor");
      p->block = block;
      p->start = start;

      snprintf (name, sizeof name, "%s%d", block_name (block), part_nr);
      snprintf (extra_info, sizeof extra_info, "%s (%02x)",
                partition_type_name (part_type), part_type);
      block_register (name, type, extra_info, size, &partition_operations, p);
    }
}

/* Returns a human-readable name for the given partition TYPE. */
static const char *
partition_type_name (uint8_t type)
{
  /* Name of each known type of partition.
     From util-linux-2.12r/fdisk/i386_sys_types.c.
     This initializer makes use of a C99 feature that allows
     array elements to be initialized by index. */
  static const char *type_names[256];

  switch (type) {
    case 0x00:  return "Empty";
    case 0x01:  return "FAT12";
    case 0x02:  return "XENIX root";
    case 0x03:  return "XENIX usr";
    case 0x04:  return "FAT16 <32M";
    case 0x05:  return "Extended";
    case 0x06:  return "FAT16";
    case 0x07:  return "HPFS/NTFS";
    case 0x08:  return "AIX";
    case 0x09:  return "AIX bootable";
    case 0x0a:  return "OS/2 Boot Manager";
    case 0x0b:  return "W95 FAT32";
    case 0x0c:  return "W95 FAT32 (LBA)";
    case 0x0e:  return "W95 FAT16 (LBA)";
    case 0x0f:  return "W95 Ext'd (LBA)";
    case 0x10:  return "OPUS";
    case 0x11:  return "Hidden FAT12";
    case 0x12:  return "Compaq diagnostics";
    case 0x14:  return "Hidden FAT16 <32M";
    case 0x16:  return "Hidden FAT16";
    case 0x17:  return "Hidden HPFS/NTFS";
    case 0x18:  return "AST SmartSleep";
    case 0x1b:  return "Hidden W95 FAT32";
    case 0x1c:  return "Hidden W95 FAT32 (LBA)";
    case 0x1e:  return "Hidden W95 FAT16 (LBA)";
    case 0x20:  return "Pintos OS kernel";
    case 0x21:  return "Pintos file system";
    case 0x22:  return "Pintos scratch";
    case 0x23:  return "Pintos swap";
    case 0x24:  return "NEC DOS";
    case 0x39:  return "Plan 9";
    case 0x3c:  return "PartitionMagic recovery";
    case 0x40:  return "Venix 80286";
    case 0x41:  return "PPC PReP Boot";
    case 0x42:  return "SFS";
    case 0x4d:  return "QNX4.x";
    case 0x4e:  return "QNX4.x 2nd part";
    case 0x4f:  return "QNX4.x 3rd part";
    case 0x50:  return "OnTrack DM";
    case 0x51:  return "OnTrack DM6 Aux1";
    case 0x52:  return "CP/M";
    case 0x53:  return "OnTrack DM6 Aux3";
    case 0x54:  return "OnTrackDM6";
    case 0x55:  return "EZ-Drive";
    case 0x56:  return "Golden Bow";
    case 0x5c:  return "Priam Edisk";
    case 0x61:  return "SpeedStor";
    case 0x63:  return "GNU HURD or SysV";
    case 0x64:  return "Novell Netware 286";
    case 0x65:  return "Novell Netware 386";
    case 0x70:  return "DiskSecure Multi-Boot";
    case 0x75:  return "PC/IX";
    case 0x80:  return "Old Minix";
    case 0x81:  return "Minix / old Linux";
    case 0x82:  return "Linux swap / Solaris";
    case 0x83:  return "Linux";
    case 0x84:  return "OS/2 hidden C: drive";
    case 0x85:  return "Linux extended";
    case 0x86:  return "NTFS volume set";
    case 0x87:  return "NTFS volume set";
    case 0x88:  return "Linux plaintext";
    case 0x8e:  return "Linux LVM";
    case 0x93:  return "Amoeba";
    case 0x94:  return "Amoeba BBT";
    case 0x9f:  return "BSD/OS";
    case 0xa0:  return "IBM Thinkpad hibernation";
    case 0xa5:  return "FreeBSD";
    case 0xa6:  return "OpenBSD";
    case 0xa7:  return "NeXTSTEP";
    case 0xa8:  return "Darwin UFS";
    case 0xa9:  return "NetBSD";
    case 0xab:  return "Darwin boot";
    case 0xb7:  return "BSDI fs";
    case 0xb8:  return "BSDI swap";
    case 0xbb:  return "Boot Wizard hidden";
    case 0xbe:  return "Solaris boot";
    case 0xbf:  return "Solaris";
    case 0xc1:  return "DRDOS/sec (FAT-12)";
    case 0xc4:  return "DRDOS/sec (FAT-16 < 32M)";
    case 0xc6:  return "DRDOS/sec (FAT-16)";
    case 0xc7:  return "Syrinx";
    case 0xda:  return "Non-FS data";
    case 0xdb:  return "CP/M / CTOS / ...";
    case 0xde:  return "Dell Utility";
    case 0xdf:  return "BootIt";
    case 0xe1:  return "DOS access";
    case 0xe3:  return "DOS R/O";
    case 0xe4:  return "SpeedStor";
    case 0xeb:  return "BeOS fs";
    case 0xee:  return "EFI GPT";
    case 0xef:  return "EFI (FAT-12/16/32)";
    case 0xf0:  return "Linux/PA-RISC boot";
    case 0xf1:  return "SpeedStor";
    case 0xf4:  return "SpeedStor";
    case 0xf2:  return "DOS secondary";
    case 0xfd:  return "Linux raid autodetect";
    case 0xfe:  return "LANstep";
    case 0xff:  return "BBT";
    default:    return "Unknown";
  }
}

/* Reads sector SECTOR from partition P into BUFFER, which must
   have room for BLOCK_SECTOR_SIZE bytes. */
static void
partition_read (void *p_, block_sector_t sector, void *buffer)
{
  struct partition *p = p_;
  block_read (p->block, p->start + sector, buffer);
}

/* Write sector SECTOR to partition P from BUFFER, which must
   contain BLOCK_SECTOR_SIZE bytes.  Returns after the block has
   acknowledged receiving the data. */
static void
partition_write (void *p_, block_sector_t sector, const void *buffer)
{
  struct partition *p = p_;
  block_write (p->block, p->start + sector, buffer);
}
