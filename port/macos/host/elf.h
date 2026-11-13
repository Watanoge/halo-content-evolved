/*
ELF.H

The ELF file's layouts and constants that the guest image's loader
(port/android/host/host_loader.c) reads, for macOS, whose C library has no
<elf.h>: the System V ABI's definitions, as glibc's and bionic's give them.
*/

#ifndef HALO_MACOS_ELF_H
#define HALO_MACOS_ELF_H

#include <stdint.h>

#define EI_NIDENT 16
#define EI_CLASS 4
#define EI_DATA 5
#define ELFMAG "\177ELF"
#define SELFMAG 4
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PF_X 1
#define PF_W 2
#define PF_R 4
#define SHN_UNDEF 0
#define SHT_SYMTAB 2
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define ELF32_ST_BIND(info) ((info) >> 4)
#define ELF64_ST_BIND(info) ((info) >> 4)

typedef struct
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type, e_machine;
	uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct
{
	uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} Elf32_Phdr;

typedef struct
{
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

typedef struct
{
	uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize;
} Elf32_Shdr;

typedef struct
{
	uint32_t sh_name, sh_type;
	uint64_t sh_flags, sh_addr, sh_offset, sh_size;
	uint32_t sh_link, sh_info;
	uint64_t sh_addralign, sh_entsize;
} Elf64_Shdr;

typedef struct
{
	uint32_t st_name, st_value, st_size;
	unsigned char st_info, st_other;
	uint16_t st_shndx;
} Elf32_Sym;

typedef struct
{
	uint32_t st_name;
	unsigned char st_info, st_other;
	uint16_t st_shndx;
	uint64_t st_value, st_size;
} Elf64_Sym;

#endif
