#ifndef MINI_WHPX_H
#define MINI_WHPX_H

/* Minimal vendored Windows Hypervisor Platform ABI (subset of Microsoft's
 * winhvplatform.h / winhvplatformdefs.h), distilled for dynamic use via
 * dlopen("WinHvPlatform.dll") -- no Windows SDK or import library needed,
 * so the same source compiles on Linux/cosmopolitan too. All type layouts
 * match the public SDK headers. */

#include <stdint.h>

typedef void *WHV_PARTITION_HANDLE;
typedef uint64_t WHV_GUEST_PHYSICAL_ADDRESS;
typedef uint64_t WHV_GUEST_VIRTUAL_ADDRESS;
typedef int32_t WHV_HRESULT;
#define WHV_SUCCEEDED(hr) ((hr) >= 0)

typedef enum WHV_CAPABILITY_CODE {
  WHvCapabilityCodeHypervisorPresent = 0x00000000
} WHV_CAPABILITY_CODE;

typedef enum WHV_PARTITION_PROPERTY_CODE {
  WHvPartitionPropertyCodeExtendedVmExits = 0x00000001,
  WHvPartitionPropertyCodeProcessorCount = 0x00001fff
} WHV_PARTITION_PROPERTY_CODE;

typedef enum WHV_MAP_GPA_RANGE_FLAGS {
  WHvMapGpaRangeFlagRead = 0x00000001,
  WHvMapGpaRangeFlagWrite = 0x00000002,
  WHvMapGpaRangeFlagExecute = 0x00000004
} WHV_MAP_GPA_RANGE_FLAGS;

typedef enum WHV_REGISTER_NAME {
  WHvX64RegisterRax = 0x00000000,
  WHvX64RegisterRcx = 0x00000001,
  WHvX64RegisterRdx = 0x00000002,
  WHvX64RegisterRbx = 0x00000003,
  WHvX64RegisterRsp = 0x00000004,
  WHvX64RegisterRbp = 0x00000005,
  WHvX64RegisterRsi = 0x00000006,
  WHvX64RegisterRdi = 0x00000007,
  WHvX64RegisterR8 = 0x00000008,
  WHvX64RegisterR9 = 0x00000009,
  WHvX64RegisterR10 = 0x0000000A,
  WHvX64RegisterR11 = 0x0000000B,
  WHvX64RegisterR12 = 0x0000000C,
  WHvX64RegisterR13 = 0x0000000D,
  WHvX64RegisterR14 = 0x0000000E,
  WHvX64RegisterR15 = 0x0000000F,
  WHvX64RegisterRip = 0x00000010,
  WHvX64RegisterRflags = 0x00000011,
  WHvX64RegisterEs = 0x00000012,
  WHvX64RegisterCs = 0x00000013,
  WHvX64RegisterSs = 0x00000014,
  WHvX64RegisterDs = 0x00000015,
  WHvX64RegisterFs = 0x00000016,
  WHvX64RegisterGs = 0x00000017,
  WHvX64RegisterLdtr = 0x00000018,
  WHvX64RegisterTr = 0x00000019,
  WHvX64RegisterIdtr = 0x0000001A,
  WHvX64RegisterGdtr = 0x0000001B,
  WHvX64RegisterCr0 = 0x0000001C,
  WHvX64RegisterCr2 = 0x0000001D,
  WHvX64RegisterCr3 = 0x0000001E,
  WHvX64RegisterCr4 = 0x0000001F,
  WHvX64RegisterCr8 = 0x00000020,
  WHvX64RegisterEfer = 0x00002001,
  WHvX64RegisterApicBase = 0x00002003
} WHV_REGISTER_NAME;

typedef struct WHV_X64_SEGMENT_REGISTER {
  uint64_t Base;
  uint32_t Limit;
  uint16_t Selector;
  uint16_t Attributes;  /* Type:4, NonSystem:1, Dpl:2, Present:1, Rsvd:4, Avl:1, Long:1, Default:1, Granularity:1. */
} WHV_X64_SEGMENT_REGISTER;

typedef struct WHV_X64_TABLE_REGISTER {
  uint16_t Pad[3];
  uint16_t Limit;
  uint64_t Base;
} WHV_X64_TABLE_REGISTER;

typedef union WHV_REGISTER_VALUE {
  uint64_t Reg64;
  uint32_t Reg32;
  uint16_t Reg16;
  uint8_t Reg8;
  WHV_X64_SEGMENT_REGISTER Segment;
  WHV_X64_TABLE_REGISTER Table;
} WHV_REGISTER_VALUE;

typedef enum WHV_RUN_VP_EXIT_REASON {
  WHvRunVpExitReasonNone = 0x00000000,
  WHvRunVpExitReasonMemoryAccess = 0x00000001,
  WHvRunVpExitReasonX64IoPortAccess = 0x00000002,
  WHvRunVpExitReasonUnrecoverableException = 0x00000004,
  WHvRunVpExitReasonInvalidVpRegisterValue = 0x00000005,
  WHvRunVpExitReasonUnsupportedFeature = 0x00000006,
  WHvRunVpExitReasonX64InterruptWindow = 0x00000007,
  WHvRunVpExitReasonX64Halt = 0x00000008,
  WHvRunVpExitReasonException = 0x00001002,
  WHvRunVpExitReasonCanceled = 0x00002001
} WHV_RUN_VP_EXIT_REASON;

typedef struct WHV_X64_VP_EXIT_CONTEXT {
  uint16_t ExecutionState;  /* Cpl:2, Cr0Pe:1, Cr0Am:1, EferLma:1, ... */
  uint8_t InstructionLengthCr8;  /* InstructionLength:4, Cr8:4. */
  uint8_t Reserved;
  uint32_t Reserved2;
  WHV_X64_SEGMENT_REGISTER Cs;
  uint64_t Rip;
  uint64_t Rflags;
} WHV_X64_VP_EXIT_CONTEXT;  /* 40 bytes */

typedef struct WHV_MEMORY_ACCESS_CONTEXT {
  uint8_t InstructionByteCount;
  uint8_t Reserved[3];
  uint8_t InstructionBytes[16];
  uint32_t AccessInfo;  /* AccessType:2 (0=read 1=write 2=exec), GpaUnmapped:1, GvaValid:1. */
  WHV_GUEST_PHYSICAL_ADDRESS Gpa;
  WHV_GUEST_VIRTUAL_ADDRESS Gva;
} WHV_MEMORY_ACCESS_CONTEXT;  /* 40 bytes */

typedef struct WHV_X64_IO_PORT_ACCESS_CONTEXT {
  uint8_t InstructionByteCount;
  uint8_t Reserved[3];
  uint8_t InstructionBytes[16];
  uint32_t AccessInfo;  /* IsWrite:1, AccessSize:3, StringOp:1, RepPrefix:1. */
  uint16_t PortNumber;
  uint16_t Reserved2[3];
  uint64_t Rax;
  uint64_t Rcx;
  uint64_t Rsi;
  uint64_t Rdi;
  WHV_X64_SEGMENT_REGISTER Ds;
  WHV_X64_SEGMENT_REGISTER Es;
} WHV_X64_IO_PORT_ACCESS_CONTEXT;  /* 96 bytes */

typedef struct WHV_RUN_VP_EXIT_CONTEXT {
  int32_t ExitReason;
  uint32_t Reserved;
  WHV_X64_VP_EXIT_CONTEXT VpContext;
  union {
    WHV_MEMORY_ACCESS_CONTEXT MemoryAccess;
    WHV_X64_IO_PORT_ACCESS_CONTEXT IoPortAccess;
    uint64_t AsUINT64[40];  /* Largest member is ~256B; extra headroom is safe
                             * since WHvRunVirtualProcessor takes ctx size. */
  } u;
} WHV_RUN_VP_EXIT_CONTEXT;  /* 368 bytes (SDK's own is ~300). */

/* Function pointer table for WinHvPlatform.dll exports. */
typedef struct whpx_api {
  WHV_HRESULT (*GetCapability)(int code, void *capbuf, uint32_t bufsize, uint32_t *written);
  WHV_HRESULT (*CreatePartition)(WHV_PARTITION_HANDLE *partition);
  WHV_HRESULT (*SetupPartition)(WHV_PARTITION_HANDLE partition);
  WHV_HRESULT (*SetPartitionProperty)(WHV_PARTITION_HANDLE partition, int code, const void *propbuf, uint32_t bufsize);
  WHV_HRESULT (*DeletePartition)(WHV_PARTITION_HANDLE partition);
  WHV_HRESULT (*MapGpaRange)(WHV_PARTITION_HANDLE partition, void *source,
                             WHV_GUEST_PHYSICAL_ADDRESS gpa, uint64_t size, uint32_t flags);
  WHV_HRESULT (*UnmapGpaRange)(WHV_PARTITION_HANDLE partition,
                               WHV_GUEST_PHYSICAL_ADDRESS gpa, uint64_t size);
  WHV_HRESULT (*CreateVirtualProcessor)(WHV_PARTITION_HANDLE partition, uint32_t vpindex, uint32_t flags);
  WHV_HRESULT (*DeleteVirtualProcessor)(WHV_PARTITION_HANDLE partition, uint32_t vpindex);
  WHV_HRESULT (*RunVirtualProcessor)(WHV_PARTITION_HANDLE partition, uint32_t vpindex,
                                     void *exitctx, uint32_t ctxsize);
  WHV_HRESULT (*GetVirtualProcessorRegisters)(WHV_PARTITION_HANDLE partition, uint32_t vpindex,
                                              const int *names, uint32_t count, WHV_REGISTER_VALUE *values);
  WHV_HRESULT (*SetVirtualProcessorRegisters)(WHV_PARTITION_HANDLE partition, uint32_t vpindex,
                                              const int *names, uint32_t count, const WHV_REGISTER_VALUE *values);
} whpx_api;

#endif
