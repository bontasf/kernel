#include "fat32_partition_manager.h"

#include "system_physical_memory.h"
#include "system_mass_storage.h"
#include "system_log.h"
#include "utils.h"

#define MODULE_TAG u"FAT32_PARTITION_MANAGER"

#define ATTRIBUTE_READ_ONLY 0x01
#define ATTRIBUTE_HIDDEN 0x02
#define ATTRIBUTE_SYSTEM 0x04
#define ATTRIBUTE_VOLUME_ID 0x08
#define ATTRIBUTE_DIRECTORY 0x10
#define ATTRIBUTE_ARCHIVE 0x20

#define FIRST_USABLE_SECTOR 2048
#define BYTES_PER_SECTOR 512
#define SECTORS_PER_CLUSTER 8
#define TOTAL_SECTORS 4ULL * 1024 * 1024 * 1024 / BYTES_PER_SECTOR
#define FAT_SIZE 8ULL * 1024 * 1024 / BYTES_PER_SECTOR

#define NUMBER_OF_ENTRIES_IN_A_CLUSTERS 4096 / 32

typedef struct _BIOS_PARAMETER_BLOCK
{
    UINT8 JumpBoot[3];
    CHAR8 OEMName[8];
    UINT16 BytesPerSector;
    UINT8 SectorsPerCluster;
    UINT16 ReservedSectorsCount;
    UINT8 NumberFATs;
    UINT16 RootEntryCount;
    UINT16 TotalSectors16;
    UINT8 Media;
    UINT16 FATSize16;
    UINT16 SectorsPerTrack;
    UINT16 NumberOfHeads;
    UINT32 HidenSectorsCount;
    UINT32 TotalSectors32;
    UINT32 FATSize32;
    UINT16 Flags;
    UINT16 FSVersion;
    UINT32 RootCluster;
    UINT16 FSInfo;
    UINT16 BackupBootSector;
    UINT8 Reserved[12];
    UINT8 DriverNumber;
    UINT8 Reserved1;
    UINT8 ExtendedBootSignature;
    UINT32 VolumeID;
    CHAR8 VolumeLabel[11];
    CHAR8 FileSystemType[8];
    UINT8 BootCode[510 - 90];
    UINT16 BootSignature;
} PACKED BIOS_PARAMETER_BLOCK;

typedef struct _FILE_SECTOR_INFO
{
    UINT32 LeadSignature;
    UINT8 Reserved1[480];
    UINT32 StructureSignature;
    UINT32 FreeCount;
    UINT32 NextFreeCluster;
    UINT8 Reserved2[12];
    UINT32 TrailSignature;
} __attribute__((packed)) FILE_SECTOR_INFO;

typedef struct _DIRECTORY_ENTRY
{
    CHAR8 Name[11];
    UINT8 Attribute;
    UINT8 NTReserved;
    UINT8 CreationTimeTenth;
    UINT16 CreationTime;
    UINT16 CreationDate;
    UINT16 LastAccessDate;
    UINT16 FirstClusterHigh;
    UINT16 WriteTime;
    UINT16 WriteDate;
    UINT16 FirstClusterLow;
    UINT32 FileSize;
} PACKED DIRECTORY_ENTRY;

typedef struct _LONG_DIRECTORY_ENTRY
{
    UINT8 Order;
    CHAR8 Name1[10];
    UINT8 Attributes;
    UINT8 Type;
    UINT8 Checksum;
    CHAR8 Name2[12];
    UINT16 FirstClusterLow;
    CHAR8 Name3[4];
} PACKED LONG_DIRECTORY_ENTRY;

typedef struct _FAT32_PARTITION_MANAGER_INTERNAL
{
    CHAR16 DevicePath[32];
    CHAR16 PartitionName[32];
    BIOS_PARAMETER_BLOCK *BiosParameterBlock;
    FILE_SECTOR_INFO *FileSectorInfo;
    UINT32 *FATs;
    UINT32 *MirrorFATs;
    UINT64 FirstSector;
    UINT64 FirstDataSector;
} FAT32_PARTITION_MANAGER_INTERNAL;

typedef struct _FAT32_PARTITION_MANAGER_HANDLE
{
    UINT64 EntrySize;
    UINT64 FirstCluster;
    UINT64 CurrentCluster;
    UINT64 ClusterOffset;
} FAT32_PARTITION_MANAGER_HANDLE;

STATUS API Fat32PartitionManagerGetPartitionName(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    OUT CHAR16 *PartitionName);

STATUS API Fat32PartitionManagerCreateDirectory(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *DirectoryPath);

STATUS API Fat32PartitionManagerOpenDirectory(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *DirectoryPath,
    OUT HANDLE *Handle);

STATUS API Fat32PartitionManagerCloseDirectory(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE *Handle);

STATUS API Fat32PartitionManagerReadDirectoryContent(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE Handle,
    OUT DIRECTORY_CONTENT **DirectoryContent);

STATUS API Fat32PartitionManagerRemoveDirectory(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *DirectoryPath);

STATUS API Fat32PartitionManagerCreateFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *FilePath);

STATUS API Fat32PartitionManagerOpenFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *FilePath,
    OUT HANDLE *Handle);

STATUS API Fat32PartitionManagerCloseFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE *Handle);

STATUS API Fat32PartitionManagerReadFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE Handle,
    OUT UINT8 *Buffer,
    IN OUT UINT64 *BufferSize);

STATUS API Fat32PartitionManagerWriteFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE Handle,
    IN CONST UINT8 *Buffer,
    IN OUT UINT64 *BufferSize);

STATUS API Fat32PartitionManagerRemoveFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *FilePath);

static STATUS API UpdateFATs(IN FAT32_PARTITION_MANAGER_INTERNAL *Internal);

static STATUS API GetNextFreeCluster(IN FAT32_PARTITION_MANAGER_INTERNAL *Internal, OUT UINT64 *FreeCluster);

static STATUS API FormatDirectoryName(IN CONST CHAR16 *DirectoryName, OUT CHAR8 *FormattedDirectoryName);

static STATUS API CreateDirectoryEntry(DIRECTORY_ENTRY *DirectoryEntry,
                                       CONST CHAR8 *EntryName,
                                       BOOLEAN IsDirectory,
                                       UINT32 ClusterNumber);

static STATUS API CreateDefaultDirectoryEntry(IN FAT32_PARTITION_MANAGER_INTERNAL *Internal,
                                              UINT32 CurrentCluster,
                                              UINT32 ParrentDirectoryCluster);

static STATUS API FindEntryCluster(IN FAT32_PARTITION_MANAGER_INTERNAL *Internal,
                                   IN CONST CHAR8 *DirectoryName,
                                   IN CONST UINT64 CurrentCluster,
                                   IN CONST UINT64 ParentDirectoryCluster,
                                   IN CONST BOOLEAN IsDirectory,
                                   IN CONST BOOLEAN Create,
                                   OUT UINT64 *EntryCluster,
                                   OUT OPTIONAL UINT64 *EntrySize);

STATUS API Fat32PartitionManagerInit(IN CONST CHAR16 *DevicePath,
                                     IN CONST UINT16 *PartitionName,
                                     IN CONST UINT64 StartSector,
                                     IN CONST UINT64 EndSector,
                                     OUT FAT32_PARTITION_MANAGER **Fat32PartitionManager)
{
    FAT32_PARTITION_MANAGER *Result = NULL_PTR;
    SystemPhysicalMemoryAllocatePool((VOID **)&Result, sizeof(*Result));

    FAT32_PARTITION_MANAGER_INTERNAL *Internal = NULL_PTR;
    SystemPhysicalMemoryAllocatePool((VOID **)&Internal, sizeof(*Internal));

    BIOS_PARAMETER_BLOCK *BiosParameterBlock = NULL_PTR;
    SystemPhysicalMemoryAllocatePool((VOID **)&BiosParameterBlock, sizeof(*BiosParameterBlock));
    SystemMassStorageReadSectors(DevicePath, StartSector + 0, sizeof(*BiosParameterBlock) / BYTES_PER_SECTOR, BiosParameterBlock);

    FILE_SECTOR_INFO *FileSectorInfo = NULL_PTR;
    SystemPhysicalMemoryAllocatePool((VOID **)&FileSectorInfo, sizeof(*FileSectorInfo));
    SystemMassStorageReadSectors(DevicePath, StartSector + 1, sizeof(*FileSectorInfo) / BYTES_PER_SECTOR, FileSectorInfo);

    UINT32 *FATs = NULL_PTR;
    SystemPhysicalMemoryAllocatePool((VOID **)&FATs, BiosParameterBlock->FATSize32 * BYTES_PER_SECTOR);
    SystemMassStorageReadSectors(DevicePath, StartSector + BiosParameterBlock->ReservedSectorsCount, BiosParameterBlock->FATSize32, FATs);

    UINT32 *MirrorFATs = NULL_PTR;
    SystemPhysicalMemoryAllocatePool((VOID **)&MirrorFATs, BiosParameterBlock->FATSize32 * BYTES_PER_SECTOR);
    SystemMassStorageReadSectors(DevicePath, StartSector + BiosParameterBlock->ReservedSectorsCount + BiosParameterBlock->FATSize32, BiosParameterBlock->FATSize32, MirrorFATs);

    UINT64 FirstDataSector = StartSector + BiosParameterBlock->ReservedSectorsCount + BiosParameterBlock->FATSize32 * BiosParameterBlock->NumberFATs;

    MemoryCopy(DevicePath, Internal->DevicePath, 32 * sizeof(*DevicePath));
    MemoryCopy(PartitionName, Internal->PartitionName, 32 * sizeof(*PartitionName));
    Internal->BiosParameterBlock = BiosParameterBlock;
    Internal->FileSectorInfo = FileSectorInfo;
    Internal->FATs = FATs;
    Internal->MirrorFATs = MirrorFATs;
    Internal->FirstSector = StartSector;
    Internal->FirstDataSector = FirstDataSector;

    Result->Internal = Internal;
    Result->GetPartitionName = Fat32PartitionManagerGetPartitionName;
    Result->CreateDirectory = Fat32PartitionManagerCreateDirectory;
    Result->OpenDirectory = Fat32PartitionManagerOpenDirectory;
    Result->CloseDirectory = Fat32PartitionManagerCloseDirectory;
    Result->ReadDirectoryContent = Fat32PartitionManagerReadDirectoryContent;
    Result->RemoveDirectory = Fat32PartitionManagerRemoveDirectory;
    Result->CreateFile = Fat32PartitionManagerCreateFile;
    Result->OpenFile = Fat32PartitionManagerOpenFile;
    Result->CloseFile = Fat32PartitionManagerCloseFile;
    Result->ReadFile = Fat32PartitionManagerReadFile;
    Result->WriteFile = Fat32PartitionManagerWriteFile;
    Result->RemoveFile = Fat32PartitionManagerRemoveFile;

    *Fat32PartitionManager = Result;
    return E_OK;
}

STATUS API Fat32PartitionManagerGetPartitionName(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    OUT CHAR16 *PartitionName)
{
    STATUS Status = E_OK;
    FAT32_PARTITION_MANAGER_INTERNAL *Internal = NULL_PTR;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Internal = Fat32PartitionManager->Internal;
    if (NULL_PTR == Internal)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    MemoryCopy(Internal->PartitionName, PartitionName, 32 * sizeof(*PartitionName));

Cleanup:
    return Status;
}

STATUS API Fat32PartitionManagerCreateDirectory(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *DirectoryPath)
{
    STATUS Status = E_OK;
    FAT32_PARTITION_MANAGER_INTERNAL *Internal = NULL_PTR;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Internal = Fat32PartitionManager->Internal;
    if (NULL_PTR == Internal)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
    CHAR16 Path[128];
    CHAR16 DirectoryName[32];
    CHAR16 RestOfThePath[128];
    UINT8 FormattedDirectoryName[12];
    UINT64 ParentDirectoryClusterNumber = 2;
    UINT64 CurrentClusterNumber = 2;
    UINT64 EntryClusterNumber = 0;
    StringCopy(DirectoryPath, Path);
    SplitPath(Path, DirectoryName, RestOfThePath);
    FormatDirectoryName(DirectoryName, FormattedDirectoryName);

    while (u'\0' != DirectoryName[0])
    {
        if (u'\0' != RestOfThePath[0])
        {
            Status = FindEntryCluster(Internal, FormattedDirectoryName, CurrentClusterNumber, ParentDirectoryClusterNumber, TRUE, FALSE, &EntryClusterNumber, NULL_PTR);
            if (E_OK != Status)
            {
                goto Cleanup;
            }
        }
        else
        {
            Status = FindEntryCluster(Internal, FormattedDirectoryName, CurrentClusterNumber, ParentDirectoryClusterNumber, TRUE, TRUE, &EntryClusterNumber, NULL_PTR);
            if (E_OK != Status)
            {
                goto Cleanup;
            }
        }

        ParentDirectoryClusterNumber = CurrentClusterNumber;
        CurrentClusterNumber = EntryClusterNumber;
        StringCopy(RestOfThePath, Path);
        SplitPath(Path, DirectoryName, RestOfThePath);
        FormatDirectoryName(DirectoryName, FormattedDirectoryName);
    }

Cleanup:
    return Status;
}

STATUS API Fat32PartitionManagerOpenDirectory(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *DirectoryPath,
    OUT HANDLE *Handle)
{
    STATUS Status = E_OK;
    FAT32_PARTITION_MANAGER_INTERNAL *Internal = NULL_PTR;
    FAT32_PARTITION_MANAGER_HANDLE *Fat32PartitionManagerHandle = NULL_PTR;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Internal = Fat32PartitionManager->Internal;
    if (NULL_PTR == Internal)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    SystemPhysicalMemoryAllocatePool((VOID **)&Fat32PartitionManagerHandle, sizeof(*Fat32PartitionManagerHandle));
    if (NULL_PTR == Fat32PartitionManagerHandle)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    CHAR16 Path[128];
    CHAR16 DirectoryName[32];
    CHAR16 RestOfThePath[128];
    UINT8 FormattedDirectoryName[12];
    UINT64 ParentDirectoryClusterNumber = 2;
    UINT64 CurrentClusterNumber = 2;
    UINT64 EntryClusterNumber = 0;
    StringCopy(DirectoryPath, Path);
    SplitPath(Path, DirectoryName, RestOfThePath);
    FormatDirectoryName(DirectoryName, FormattedDirectoryName);

    while (u'\0' != DirectoryName[0])
    {

        Status = FindEntryCluster(Internal, FormattedDirectoryName, CurrentClusterNumber, ParentDirectoryClusterNumber, TRUE, FALSE, &EntryClusterNumber, NULL_PTR);
        if (E_OK != Status)
        {
            goto Cleanup;
        }

        ParentDirectoryClusterNumber = CurrentClusterNumber;
        CurrentClusterNumber = EntryClusterNumber;
        StringCopy(RestOfThePath, Path);
        SplitPath(Path, DirectoryName, RestOfThePath);
        FormatDirectoryName(DirectoryName, FormattedDirectoryName);
    }

    Fat32PartitionManagerHandle->EntrySize = 0;
    Fat32PartitionManagerHandle->FirstCluster = EntryClusterNumber;
    Fat32PartitionManagerHandle->CurrentCluster = EntryClusterNumber;
    Fat32PartitionManagerHandle->ClusterOffset = 0;

Cleanup:
    *Handle = (HANDLE)Fat32PartitionManagerHandle;
    return Status;
}

STATUS API Fat32PartitionManagerCloseDirectory(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE *Handle)
{
    STATUS Status = E_OK;

    if (NULL_PTR == Handle)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
    Status = SystemPhysicalMemoryFreePool((VOID **)&Handle, sizeof(FAT32_PARTITION_MANAGER_HANDLE));

Cleanup:
    (VOID) Fat32PartitionManager;
    return Status;
}

STATUS API Fat32PartitionManagerReadDirectoryContent(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE Handle,
    OUT DIRECTORY_CONTENT **DirectoryContent)
{
    STATUS Status = E_OK;
    FAT32_PARTITION_MANAGER_HANDLE *Fat32PartitionManagerHandle = NULL_PTR;
    FAT32_PARTITION_MANAGER_INTERNAL *Internal = NULL_PTR;
    DIRECTORY_CONTENT *Result = NULL_PTR;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Fat32PartitionManagerHandle = (FAT32_PARTITION_MANAGER_HANDLE *)Handle;
    if (NULL_PTR == Fat32PartitionManagerHandle)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
    if (0x0FFFFFFF == Fat32PartitionManagerHandle->CurrentCluster)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Internal = Fat32PartitionManager->Internal;
    if (NULL_PTR == Internal)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    SystemPhysicalMemoryAllocatePool((VOID **)&Result, sizeof(*Result));
    if (NULL_PTR == Result)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    DIRECTORY_ENTRY *DirectoryEntries = NULL_PTR;
    DIRECTORY_ENTRY *OppenedDirectory = &DirectoryEntries[Fat32PartitionManagerHandle->ClusterOffset / sizeof(*DirectoryEntries)];
    SystemPhysicalMemoryAllocatePool((VOID **)&DirectoryEntries, SECTORS_PER_CLUSTER * BYTES_PER_SECTOR * 4096);
    SystemMassStorageReadSectors(Internal->DevicePath, Internal->FirstDataSector + (Fat32PartitionManagerHandle->CurrentCluster - 2) * SECTORS_PER_CLUSTER, SECTORS_PER_CLUSTER, DirectoryEntries);

    if (0 != OppenedDirectory->Name[0])
    {
        CHAR16 SystemName[32];
        Char8ToChar16(OppenedDirectory->Name, SystemName, 11);
        MemoryCopy(SystemName, Result->DirectoryEntryName, 11);
        if (OppenedDirectory->Attribute | ATTRIBUTE_DIRECTORY)
        {
            Result->IsDirectory = TRUE;
        }
        else
        {
            Result->IsDirectory = FALSE;
        }
        Fat32PartitionManagerHandle->ClusterOffset += sizeof(*DirectoryEntries);
        if (SECTORS_PER_CLUSTER * BYTES_PER_SECTOR == Fat32PartitionManagerHandle->ClusterOffset)
        {
            Fat32PartitionManagerHandle->CurrentCluster = Internal->FATs[Fat32PartitionManagerHandle->CurrentCluster];
            Fat32PartitionManagerHandle->ClusterOffset = 0;
        }
    }

Cleanup:
    *DirectoryContent = Result;
    return Status;
}

STATUS API Fat32PartitionManagerRemoveDirectory(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *DirectoryPath)
{
    STATUS Status = E_OK;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
Cleanup:
    return Status;
}

STATUS API Fat32PartitionManagerCreateFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *FilePath)
{
    STATUS Status = E_OK;
    FAT32_PARTITION_MANAGER_INTERNAL *Internal = NULL_PTR;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Internal = Fat32PartitionManager->Internal;
    if (NULL_PTR == Internal)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
    CHAR16 Path[128];
    CHAR16 DirectoryName[32];
    CHAR16 RestOfThePath[128];
    UINT8 FormattedDirectoryName[12];
    UINT64 ParentDirectoryClusterNumber = 2;
    UINT64 CurrentClusterNumber = 2;
    UINT64 EntryClusterNumber = 0;
    StringCopy(FilePath, Path);
    SplitPath(Path, DirectoryName, RestOfThePath);
    FormatDirectoryName(DirectoryName, FormattedDirectoryName);

    while (u'\0' != DirectoryName[0])
    {
        if (u'\0' != RestOfThePath[0])
        {
            Status = FindEntryCluster(Internal, FormattedDirectoryName, CurrentClusterNumber, ParentDirectoryClusterNumber, FALSE, FALSE, &EntryClusterNumber, NULL_PTR);
            if (E_OK != Status)
            {
                goto Cleanup;
            }
        }
        else
        {
            Status = FindEntryCluster(Internal, FormattedDirectoryName, CurrentClusterNumber, ParentDirectoryClusterNumber, FALSE, TRUE, &EntryClusterNumber, NULL_PTR);
            if (E_OK != Status)
            {
                goto Cleanup;
            }
        }

        ParentDirectoryClusterNumber = CurrentClusterNumber;
        CurrentClusterNumber = EntryClusterNumber;
        StringCopy(RestOfThePath, Path);
        SplitPath(Path, DirectoryName, RestOfThePath);
        FormatDirectoryName(DirectoryName, FormattedDirectoryName);
    }

Cleanup:
    return Status;
}

STATUS API Fat32PartitionManagerOpenFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *FilePath,
    OUT HANDLE *Handle)
{
    STATUS Status = E_OK;
    FAT32_PARTITION_MANAGER_INTERNAL *Internal = NULL_PTR;
    FAT32_PARTITION_MANAGER_HANDLE *Fat32PartitionManagerHandle = NULL_PTR;
    UINT64 EntrySize = 0;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Internal = Fat32PartitionManager->Internal;
    if (NULL_PTR == Internal)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    SystemPhysicalMemoryAllocatePool((VOID **)&Fat32PartitionManagerHandle, sizeof(*Fat32PartitionManagerHandle));
    if (NULL_PTR == Fat32PartitionManagerHandle)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    CHAR16 Path[128];
    CHAR16 DirectoryName[32];
    CHAR16 RestOfThePath[128];
    UINT8 FormattedDirectoryName[12];
    UINT64 ParentDirectoryClusterNumber = 2;
    UINT64 CurrentClusterNumber = 2;
    UINT64 EntryClusterNumber = 0;
    StringCopy(FilePath, Path);
    SplitPath(Path, DirectoryName, RestOfThePath);
    FormatDirectoryName(DirectoryName, FormattedDirectoryName);

    while (u'\0' != DirectoryName[0])
    {

        Status = FindEntryCluster(Internal, FormattedDirectoryName, CurrentClusterNumber, ParentDirectoryClusterNumber, FALSE, FALSE, &EntryClusterNumber, &EntrySize);
        if (E_OK != Status)
        {
            goto Cleanup;
        }

        ParentDirectoryClusterNumber = CurrentClusterNumber;
        CurrentClusterNumber = EntryClusterNumber;
        StringCopy(RestOfThePath, Path);
        SplitPath(Path, DirectoryName, RestOfThePath);
        FormatDirectoryName(DirectoryName, FormattedDirectoryName);
    }

    Fat32PartitionManagerHandle->EntrySize = 0;
    Fat32PartitionManagerHandle->FirstCluster = EntryClusterNumber;
    Fat32PartitionManagerHandle->CurrentCluster = EntryClusterNumber;
    Fat32PartitionManagerHandle->ClusterOffset = 0;
    Fat32PartitionManagerHandle->EntrySize = EntrySize;

Cleanup:
    *Handle = (HANDLE)Fat32PartitionManagerHandle;
    return Status;
}

STATUS API Fat32PartitionManagerCloseFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE *Handle)
{
    STATUS Status = E_OK;

    if (NULL_PTR == Handle)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
    Status = SystemPhysicalMemoryFreePool((VOID **)&Handle, sizeof(FAT32_PARTITION_MANAGER_HANDLE));

Cleanup:
    (VOID) Fat32PartitionManager;
    return Status;
}

STATUS API Fat32PartitionManagerReadFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE Handle,
    OUT UINT8 *Buffer,
    IN OUT UINT64 *BufferSize)
{
    STATUS Status = E_OK;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
Cleanup:
    return Status;
}

STATUS API Fat32PartitionManagerWriteFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN OUT HANDLE Handle,
    IN CONST UINT8 *Buffer,
    IN OUT UINT64 *BufferSize)
{
    STATUS Status = E_OK;
    FAT32_PARTITION_MANAGER_INTERNAL *Internal = NULL_PTR;
    FAT32_PARTITION_MANAGER_HANDLE *Fat32PartitionManagerHandle = NULL_PTR;
    UINT8 *ClusterBuffer = NULL_PTR;
    UINT64 NumberOfWrittenBytes = 0;
    UINT64 NumberOfBytesToCopy = 0;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Internal = Fat32PartitionManager->Internal;
    if (NULL_PTR == Internal)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    Fat32PartitionManagerHandle = (FAT32_PARTITION_MANAGER_HANDLE *)Handle;
    if (NULL_PTR == Fat32PartitionManagerHandle)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    SystemPhysicalMemoryAllocatePool((VOID **)&ClusterBuffer, SECTORS_PER_CLUSTER * BYTES_PER_SECTOR) *4096;
    if (NULL_PTR == ClusterBuffer)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }

    LOG_INFO(u"%s", u"HERE");

    NumberOfWrittenBytes = *BufferSize;
    while (NumberOfWrittenBytes != 0)
    {
        if (0x0FFFFFFF == Fat32PartitionManagerHandle->CurrentCluster || BYTES_PER_SECTOR * SECTORS_PER_CLUSTER == Fat32PartitionManagerHandle->ClusterOffset)
        {
            GetNextFreeCluster(Internal, &Fat32PartitionManagerHandle->CurrentCluster);
            Fat32PartitionManagerHandle->ClusterOffset = 0;
        }

        if (0 != Fat32PartitionManagerHandle->ClusterOffset)
        {
            SystemMassStorageReadSectors(Internal->DevicePath,
                                         Internal->FirstDataSector + (Fat32PartitionManagerHandle->CurrentCluster - 2) * SECTORS_PER_CLUSTER,
                                         SECTORS_PER_CLUSTER,
                                         ClusterBuffer);
        }

        if (SECTORS_PER_CLUSTER * BYTES_PER_SECTOR - Fat32PartitionManagerHandle->ClusterOffset <= NumberOfWrittenBytes)
        {
            NumberOfBytesToCopy = SECTORS_PER_CLUSTER * BYTES_PER_SECTOR - Fat32PartitionManagerHandle->ClusterOffset;
        }
        else
        {
            NumberOfBytesToCopy = NumberOfWrittenBytes;
        }

        MemoryCopy(Buffer,
                   ClusterBuffer + Fat32PartitionManagerHandle->ClusterOffset,
                   NumberOfBytesToCopy);
    }
    MemoryCopy(Buffer, ClusterBuffer, SECTORS_PER_CLUSTER * BYTES_PER_SECTOR);
    SystemMassStorageWriteSectors(Internal->DevicePath,
                                  Internal->FirstDataSector + (Fat32PartitionManagerHandle->CurrentCluster - 2) * SECTORS_PER_CLUSTER,
                                  SECTORS_PER_CLUSTER,
                                  ClusterBuffer);

Cleanup:
    if (NULL_PTR != ClusterBuffer)
    {
        SystemPhysicalMemoryFreePool((VOID **)&ClusterBuffer, SECTORS_PER_CLUSTER * BYTES_PER_SECTOR);
    }

    return Status;
}

STATUS API Fat32PartitionManagerRemoveFile(
    FAT32_PARTITION_MANAGER *Fat32PartitionManager,
    IN CONST CHAR16 *FilePath)
{
    STATUS Status = E_OK;

    if (NULL_PTR == Fat32PartitionManager)
    {
        Status = E_NOT_OK;
        goto Cleanup;
    }
Cleanup:
    return Status;
}

static STATUS API UpdateFATs(IN FAT32_PARTITION_MANAGER_INTERNAL *Internal)
{
    SystemMassStorageWriteSectors(Internal->DevicePath, Internal->FirstSector + Internal->BiosParameterBlock->ReservedSectorsCount, 1, Internal->FATs);
    SystemMassStorageWriteSectors(Internal->DevicePath, Internal->FirstSector + Internal->BiosParameterBlock->ReservedSectorsCount + Internal->BiosParameterBlock->FATSize32, 1, Internal->MirrorFATs);
    return E_OK;
}

static STATUS API GetNextFreeCluster(IN FAT32_PARTITION_MANAGER_INTERNAL *Internal, OUT UINT64 *FreeCluster)
{
    Internal->FileSectorInfo->FreeCount--;
    Internal->FileSectorInfo->NextFreeCluster++;

    *FreeCluster = Internal->FileSectorInfo->NextFreeCluster - 1;
    return E_OK;
}

static STATUS API FormatDirectoryName(IN CONST CHAR16 *DirectoryName, OUT CHAR8 *FormattedDirectoryName)
{
    for (UINT8 i = 0; i <= 11; ++i)
    {
        FormattedDirectoryName[i] = ' ';
    }
    UINT64 Length = 0;
    for (; u'\0' != DirectoryName[Length]; ++Length)
        ;

    for (UINT8 i = 0; i < Length; ++i)
    {
        if (DirectoryName[i] == u'.')
        {
            UINT8 offset = 11;
            for (UINT8 j = Length; j > i; j--)
            {
                FormattedDirectoryName[offset] = DirectoryName[j];
                offset--;
            }

            return E_OK;
        }
        FormattedDirectoryName[i] = DirectoryName[i];
    }
    return E_OK;
}

static STATUS API CreateDirectoryEntry(DIRECTORY_ENTRY *DirectoryEntry,
                                       CONST CHAR8 *EntryName,
                                       BOOLEAN IsDirectory,
                                       UINT32 ClusterNumber)
{
    MemoryCopy(EntryName, DirectoryEntry->Name, sizeof(DirectoryEntry->Name));
    DirectoryEntry->Attribute = 0;
    if (IsDirectory)
    {
        DirectoryEntry->Attribute |= ATTRIBUTE_DIRECTORY;
    }
    DirectoryEntry->NTReserved = 0;
    DirectoryEntry->CreationTimeTenth = 0;
    DirectoryEntry->CreationTime = 0;
    DirectoryEntry->CreationDate = 0;
    DirectoryEntry->LastAccessDate = 0;
    DirectoryEntry->FirstClusterHigh = (ClusterNumber >> 16) & 0xFFFF;
    DirectoryEntry->WriteDate = 0;
    DirectoryEntry->WriteDate = 0;
    DirectoryEntry->FirstClusterLow = (ClusterNumber >> 0) & 0xFFFF;
    DirectoryEntry->FileSize = 0;

    return E_OK;
}

static STATUS API CreateDefaultDirectoryEntry(IN FAT32_PARTITION_MANAGER_INTERNAL *Internal,
                                              UINT32 CurrentCluster,
                                              UINT32 ParrentDirectoryCluster)
{
    DIRECTORY_ENTRY *DirectoryEntries = NULL_PTR;
    SystemPhysicalMemoryAllocatePool((VOID **)&DirectoryEntries, SECTORS_PER_CLUSTER * BYTES_PER_SECTOR * 4096);
    SystemMassStorageReadSectors(Internal->DevicePath, Internal->FirstDataSector + (CurrentCluster - 2) * SECTORS_PER_CLUSTER, SECTORS_PER_CLUSTER, DirectoryEntries);

    if (2 == ParrentDirectoryCluster)
    {
        ParrentDirectoryCluster = 0;
    }

    CreateDirectoryEntry(&DirectoryEntries[0], (CONST CHAR8 *)".          ", TRUE, CurrentCluster);
    CreateDirectoryEntry(&DirectoryEntries[1], (CONST CHAR8 *)"..         ", TRUE, ParrentDirectoryCluster);

    SystemMassStorageWriteSectors(Internal->DevicePath, Internal->FirstDataSector + (CurrentCluster - 2) * SECTORS_PER_CLUSTER, SECTORS_PER_CLUSTER, DirectoryEntries);
    return E_OK;
}

static STATUS API FindEntryCluster(IN FAT32_PARTITION_MANAGER_INTERNAL *Internal,
                                   IN CONST CHAR8 *DirectoryName,
                                   IN CONST UINT64 CurrentCluster,
                                   IN CONST UINT64 ParentDirectoryCluster,
                                   IN CONST BOOLEAN IsDirectory,
                                   IN CONST BOOLEAN Create,
                                   OUT UINT64 *EntryCluster,
                                   OUT OPTIONAL UINT64 *EntrySize)
{
    DIRECTORY_ENTRY *DirectoryEntries = NULL_PTR;
    SystemPhysicalMemoryAllocatePool((VOID **)&DirectoryEntries, SECTORS_PER_CLUSTER * BYTES_PER_SECTOR * 4096);
    SystemMassStorageReadSectors(Internal->DevicePath, Internal->FirstDataSector + (CurrentCluster - 2) * SECTORS_PER_CLUSTER, SECTORS_PER_CLUSTER, DirectoryEntries);

    for (UINT64 i = 0; i < NUMBER_OF_ENTRIES_IN_A_CLUSTERS; ++i)
    {
        if (DirectoryEntries[i].Name[0] == 0x00)
        {
            if (Create)
            {
                // The directory was not found
                // Creating new directory
                UINT64 FreeClusterNumber = 0;
                GetNextFreeCluster(Internal, &FreeClusterNumber);
                Internal->FATs[FreeClusterNumber] = 0x0FFFFFFF;
                Internal->MirrorFATs[FreeClusterNumber] = 0x0FFFFFFF;
                UpdateFATs(Internal);

                CreateDirectoryEntry(&DirectoryEntries[i], DirectoryName, IsDirectory, FreeClusterNumber);
                if (IsDirectory)
                {
                    CreateDefaultDirectoryEntry(Internal, FreeClusterNumber, ParentDirectoryCluster);
                }

                SystemMassStorageWriteSectors(Internal->DevicePath, Internal->FirstDataSector + (CurrentCluster - 2) * SECTORS_PER_CLUSTER, SECTORS_PER_CLUSTER, DirectoryEntries);
                *EntryCluster = FreeClusterNumber;
                return E_OK;
            }
            else
            {
                *EntryCluster = 0;
                return E_NOT_OK;
            }
        }

        if (0 == MemoryCompare(DirectoryEntries[i].Name, DirectoryName, sizeof(DirectoryEntries[i].Name)))
        {
            if (!Create)
            {
                if ((IsDirectory &&
                     (0 != (DirectoryEntries[i].Attribute | ATTRIBUTE_DIRECTORY))) ||
                    (!IsDirectory &&
                     (0 != (DirectoryEntries[i].Attribute | ATTRIBUTE_DIRECTORY))))
                {
                    UINT64 Cluster = DirectoryEntries[i].FirstClusterHigh;
                    Cluster = Cluster << 16;
                    Cluster = Cluster | DirectoryEntries[i].FirstClusterLow;

                    *EntryCluster = Cluster;
                    if (NULL_PTR != EntrySize)
                    {
                        *EntrySize = DirectoryEntries[i].FileSize;
                    }
                    return E_OK;
                }
                else
                {
                    return E_NOT_OK;
                }
            }
            else
            {
                return E_NOT_OK;
            }
        }
    }

    // If this point was reached without return, check if there is a linked cluster if not alocate one and go to that cluster.
    if (Create && (Internal->FATs[CurrentCluster] == 0x0FFFFFFF))
    {
        UINT32 FreeClusterNumber = 0;
        GetNextFreeCluster(Internal, &FreeClusterNumber);
        Internal->FATs[CurrentCluster] = FreeClusterNumber;
        Internal->MirrorFATs[CurrentCluster] = Internal->FATs[CurrentCluster];
        Internal->FATs[Internal->FATs[CurrentCluster]] = 0x0FFFFFFF;
        Internal->MirrorFATs[Internal->FATs[CurrentCluster]] = 0x0FFFFFFF;
        UpdateFATs(Internal);
    }

    if (Internal->FATs[CurrentCluster] != 0x0FFFFFFF)
    {
        return FindEntryCluster(Internal, DirectoryName, Internal->FATs[CurrentCluster], ParentDirectoryCluster, IsDirectory, Create, EntryCluster, EntrySize);
    }

    return E_NOT_OK;
}
