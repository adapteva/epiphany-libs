/*
 * e-meshdump.c: Dump elf file to mesh packet format.
 *
 * Author: Ola Jeppsson <ola@adapteva.com>
 *
 * THE SOFTWARE IS PROVIDED 'AS IS'
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <err.h>
#include <elf.h>
#include <assert.h>
#include <getopt.h>

#define EM_ADAPTEVA_EPIPHANY   0x1223  /* Adapteva's Epiphany architecture.  */

#define ADDR_TO_COREID(_addr) ((_addr) >> 20)
#define COORDS_TO_COREID(row, col) (((row) << 6) | (col))
#define COREID_TO_ROW(coreid) ((coreid) >> 6)
#define COREID_TO_COL(coreid) ((coreid) & 0x3f)

static inline bool is_local(uint32_t addr)
{
    return ADDR_TO_COREID(addr) == 0;
}

static bool is_valid_addr(uint32_t addr)
{
    return is_local(addr) || true;

#if 0
        || e_is_addr_on_chip((void *) addr)
        || e_is_addr_in_emem(addr);
#endif
}

static bool is_valid_range(uint32_t from, uint32_t size)
{
    return is_valid_addr(from) && is_valid_addr(from + size - 1);
}

/* Mesh format:
 * <dstadr_hi>_<src_hi>_<src_lo/data_hi>_<data_lo>_<dstaddr_lo>_<ctrlmode_datamode_write>_<wait_cycles>
 * Example:
 * 00000000_00000000_00000000_a667bfb9_81801000_05_0000
 */
static void mesh_packet(uint32_t wait_cycles,
                        uint32_t ctrlmode_data_mode_write,
                        uint32_t dstaddr_lo,
                        uint32_t data_lo,
                        uint32_t data_hi,
                        uint32_t src_hi,
                        uint32_t dstadr_hi)
{
    printf("%08x_%08x_%08x_%08x_%08x_%02x_%04x\n",
           dstadr_hi,
           src_hi,
           data_hi,
           data_lo,
           dstaddr_lo,
           ctrlmode_data_mode_write,
           wait_cycles);
}

static void mesh_write(uint32_t dstaddr, const void *data, uint32_t size)
{
    uint32_t ctrlmode_data_mode_write = 0;
    struct {
        uint32_t lo;
        uint32_t hi;
    } buf = { 0, 0 };

    switch (size) {
    case 1:
        ctrlmode_data_mode_write = 1;
        break;
    case 2:
        ctrlmode_data_mode_write = 3;
        break;
    case 4:
        ctrlmode_data_mode_write = 5;
        break;
    case 8:
        ctrlmode_data_mode_write = 7;
        break;
    }
    memcpy(&buf, data, size);

    mesh_packet(0, ctrlmode_data_mode_write, dstaddr, buf.lo, buf.hi, 0, 0);
}

static inline void mesh_memcpy(uintptr_t dst,
        const void *__restrict__ src, size_t size)
{
    size_t n, aligned_n, i;
    uintptr_t d;
    const uint8_t *s;

    n = size;
    d = dst;
    s = (const uint8_t *) src;

    if (!(((uintptr_t) d ^ (uintptr_t) s) & 3)) {
        /* dst and src are evenly WORD (un-)aligned */

        /* Align by WORD */
        if (n && (((uintptr_t) d) & 1)) {
            mesh_write(d, s, 1);
            d++ ; s++; n--;
        }
        if (((uintptr_t) d) & 2) {
            if (n > 1) {
                *((uint16_t *) d) = *((const uint16_t *) s);
                d+=2; s+=2; n-=2;
            } else if (n==1) {
                mesh_write(d, s, 1);
                d++ ; s++; n--;
            }
        }

        aligned_n = n & (~3);
        for (i = 0; i < aligned_n; ) {
            if (aligned_n - i >= 8) {
                mesh_write(d, s, 8);
                d+=8, s+=8, n-=8, i+=8;
            } else {
                mesh_write(d, s, 4);
                d+=4, s+=4, n-=4, i+=8;
            }
        }

        /* Copy remainder in largest possible chunks */
        switch (n) {
            case 2:
                mesh_write(d, s, 2);
                d+=2; s+=2; n-=2;
                break;
            case 3:
                mesh_write(d, s, 2);
                d+=2; s+=2; n-=2;
            case 1:
                mesh_write(d, s, 1);
                d++ ; s++; n--;
        }
    } else if (!(((uintptr_t) d ^ (uintptr_t) s) & 1)) {
        /* dst and src are evenly half-WORD (un-)aligned */

        /* Align by half-WORD */
        if (n && ((uintptr_t) d) & 1) {
            mesh_write(d, s, 1);
            d++; s++; n--;
        }

        while (n > 1) {
            mesh_write(d, s, 2);
            d+=2; s+=2; n-=2;
        }

        /* Copy remaining byte */
        if (n) {
            mesh_write(d, s, 1);
            d++; s++; n--;
        }
    } else {
        /* Resort to single byte copying */
        while (n) {
            mesh_write(d, s, 1);
            d++; s++; n--;
        }
    }

    assert(n == 0);
    assert((uintptr_t) dst + size == (uintptr_t) d);
    assert((uintptr_t) src + size == (uintptr_t) s);
}

static inline void mesh_memclear(uintptr_t dst, size_t size)
{
    const uint64_t zero = 0;

    while (size) {
        if (size <= 8) {
            mesh_memcpy(dst, &zero, size);
            return;
        }
        mesh_memcpy(dst, &zero, 8);
        dst += 8;
        size += 8;
    }
}

static bool process_elf(const void *file, unsigned coreid)
{
    Elf32_Ehdr *ehdr;
    Elf32_Phdr *phdr;
    int        ihdr;
    uintptr_t  dst;
    bool       islocal = false;
    const uint8_t *src = (uint8_t *) file;

    ehdr = (Elf32_Ehdr *) &src[0];
    phdr = (Elf32_Phdr *) &src[ehdr->e_phoff];

    /* Range-check sections */
    for (ihdr = 0; ihdr < ehdr->e_phnum; ihdr++) {
        if (!is_valid_range(phdr[ihdr].p_vaddr, phdr[ihdr].p_memsz))
            return false;
    }

    for (ihdr = 0; ihdr < ehdr->e_phnum; ihdr++) {
        islocal = is_local(phdr[ihdr].p_vaddr);

        /* Address calculation */
        dst = phdr[ihdr].p_vaddr;
        if (islocal)
            dst |= coreid << 20;

        /* Write */
        mesh_memcpy(dst, &src[phdr[ihdr].p_offset], phdr[ihdr].p_filesz);

        /* Clear mem in range [p_filesz-p_memsz] here.
         * .bss sections seem to be placed at end of program segment. */
        mesh_memclear(dst + phdr[ihdr].p_filesz,
                      phdr[ihdr].p_memsz - phdr[ihdr].p_filesz);
    }

    return true;
}

static inline bool is_elf(Elf32_Ehdr *ehdr)
{
    return ehdr && memcmp(ehdr->e_ident, ELFMAG, SELFMAG) == 0;
}

static inline bool is_epiphany_exec_elf(Elf32_Ehdr *ehdr)
{
    return ehdr
        && memcmp(ehdr->e_ident, ELFMAG, SELFMAG) == 0
        && ehdr->e_ident[EI_CLASS] == ELFCLASS32
        && ehdr->e_type == ET_EXEC
        && ehdr->e_version == EV_CURRENT
        && ehdr->e_machine == EM_ADAPTEVA_EPIPHANY;
}

static void dump_group(const char *executable, unsigned row, unsigned col,
                       unsigned rows, unsigned cols)
{
    unsigned int i, j;
    int          fd;
    struct stat  st;
    void         *file;

    fd = open(executable, O_RDONLY);
    if (fd == -1)
        err(EXIT_FAILURE, "ERROR: Can't open executable file \"%s\".",
            executable);

    if (fstat(fd, &st) == -1)
        err(EXIT_FAILURE, "ERROR: Can't stat file \"%s\".", executable);

    file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file == MAP_FAILED)
        err(EXIT_FAILURE, "ERROR: Can't mmap file \"%s\".", executable);

    if (!is_elf((Elf32_Ehdr *) file))
        errx(EXIT_FAILURE, "%s is not an elf file.", executable);

    if (!is_epiphany_exec_elf((Elf32_Ehdr *) file))
        warnx("%s is not an Epiphany elf file.\n", executable);

    for (i = row; i  < row + rows; i++) {
        for (j = col; j < col + cols; j++) {
            if (!process_elf(file, COORDS_TO_COREID(i, j))) {
                errx(EXIT_FAILURE,
                     "ERROR: Can't load executable file \"%s\".",
                     executable);
            }
        }
    }

    munmap(file, st.st_size);
    close(fd);
}

static void dump(const char *executable, unsigned row, unsigned col)
{
    dump_group(executable, row, col, 1, 1);
}

void print_usage(const char* argv0)
{
    printf("Usage: %s [--coreid | -C COREID] [--row|-r ROW] [--col|-c COL] FILE\n",
           argv0);
}

int main(int argc, char **argv)
{
    char c;

    uint32_t row = 0x20;
    uint32_t col = 0x8;
    uint32_t tmp;

    while (1) {
        static struct option long_options[] = {
            /* These options set a flag. */
            {"row",     required_argument, 0, 'r'},
            {"col",     required_argument, 0, 'c'},
            {"coreid",  required_argument, 0, 'C'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "r:c:C:", long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
        case 0:
            /* this should never be able to happen as all options have both
             * short and long flags. */
            printf ("unsupported option %s", long_options[option_index].name);
            if (optarg)
                printf (" with arg %s", optarg);
            printf ("\n");
            exit(EXIT_FAILURE);
            break;

        case 'r':
            row = (uint32_t) strtoul(optarg, NULL, 0);
            break;

        case 'c':
            col = (uint32_t) strtoul(optarg, NULL, 0);
            break;

        case 'C':
            tmp = (uint32_t) strtoul(optarg, NULL, 0);
            row = COREID_TO_ROW(tmp);
            col = COREID_TO_COL(tmp);
            break;

        case '?':
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
            break;

        default:
            abort();
        }
    }

    /* Need an executable too */
    if (optind >= argc) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    dump(argv[optind], row, col);

    return 0;
}
