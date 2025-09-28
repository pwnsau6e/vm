
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <Windows.h>
#include <conio.h>
#include <signal.h>

HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX]; // 65536 locations

/* REGISTERS  */
enum {
    R_REG0 = 0,
    R_REG1,
    R_REG2,
    R_REG3,
    R_REG4,
    R_REG5,
    R_REG6,
    R_REG7,
    R_PROG_COUNTER, // program counter
    R_COND,         // condition flag
    R_COUNT
};

uint16_t registers[R_COUNT];

/* OPERATIONS */
enum {
    BR = 0, // branch
    ADD,    // add
    LD,     // load 
    ST,     // store
    JPR,    // jump register
    AND,    // bitwise and
    LDR,    // load register
    STR,    // store register
    NOT,    // bitwise not
    LDI,    // load indirect
    STI,    // store indirect
    JMP,    // jump
    RES,    // reserved (unused)
    LEA,    // load effective address
    TRAP    // execute trap
};

/* Condition Flags  */
enum {
    PF = 1 << 0, // positive
    ZF = 1 << 1, // zero
    NF = 1 << 2  // negative
};

/* TRAP CODES */
enum {
    TRAP_GETC = 0x20,  // get char from keyboard, not echoed
    TRAP_OUT = 0x21,   // output a char 
    TRAP_PUTS = 0x22,  // output a word string 
    TRAP_IN = 0x23,    // get char, echoed
    TRAP_PUTSP = 0x24, // output a byte string
    TRAP_HALT = 0x25   // halt
};

/* Memory mapped Registers*/
enum {
    MR_KBSR = 0xFE00, // keyboard status
    MR_KBDR = 0xFE02  // key pressed 
};

// Function declarations
void handle_interrupt(int signal);
void update_flags(uint16_t r);
uint16_t sign_extend(uint16_t x, int bit_count);
uint16_t swap16(uint16_t x);
void read_image_file(FILE* file);
int read_image(const char* image_path);
void mem_write(uint16_t address, uint16_t val);
uint16_t mem_read(uint16_t address);
void disable_input_buffering();
void restore_input_buffering();
uint16_t check_key();

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "vm: usage: %s <image-file1> ...\n", argv[0]);
        exit(2);
    }
    
    for (int j = 1; j < argc; ++j) {
        if (!read_image(argv[j])) {
            fprintf(stderr, "vm: failed to load image %s\n", argv[j]);
            exit(1);
        }
    }
    
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* Disable output buffering so graphics update instantly */
    setvbuf(stdout, NULL, _IONBF, 0);

    registers[R_COND] = ZF;

    enum { START_POS = 0x3000 };
    registers[R_PROG_COUNTER] = START_POS;

    int running = true;
    while (running) {
        uint16_t instruction = mem_read(registers[R_PROG_COUNTER]++);
        uint16_t op = instruction >> 12;

        switch (op) {
            case ADD: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t r1 = (instruction >> 6) & 0x7;
                uint16_t imm_flag = (instruction >> 5) & 0x1;
                if (imm_flag) {
                    uint16_t imm5 = sign_extend(instruction & 0x1F, 5);
                    registers[r0] = registers[r1] + imm5;
                } else {
                    uint16_t r2 = instruction & 0x7;
                    registers[r0] = registers[r1] + registers[r2];
                }
                update_flags(r0);
                break;
            }

            case AND: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t r1 = (instruction >> 6) & 0x7;
                uint16_t imm_flag = (instruction >> 5) & 0x1;
                if (imm_flag) {
                    uint16_t imm5 = sign_extend(instruction & 0x1F, 5);
                    registers[r0] = registers[r1] & imm5;
                } else {
                    uint16_t r2 = instruction & 0x7;
                    registers[r0] = registers[r1] & registers[r2];
                }
                update_flags(r0);
                break;
            }

            case NOT: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t r1 = (instruction >> 6) & 0x7;
                registers[r0] = ~registers[r1];
                update_flags(r0);
                break;
            }

            case BR: {
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                uint16_t cond_flag = (instruction >> 9) & 0x7;
                if (cond_flag & registers[R_COND]) {
                    registers[R_PROG_COUNTER] += pc_offset;
                }
                break;
            }

            case JMP: {
                uint16_t r1 = (instruction >> 6) & 0x7;
                registers[R_PROG_COUNTER] = registers[r1];
                break;
            }

            case JPR: {
                uint16_t long_flag = (instruction >> 11) & 1;
                registers[R_REG7] = registers[R_PROG_COUNTER];
                if (long_flag) {
                    uint16_t pc_offset = sign_extend(instruction & 0x7FF, 11);
                    registers[R_PROG_COUNTER] += pc_offset; // JSR 
                } else {
                    uint16_t r1 = (instruction >> 6) & 0x7;
                    registers[R_PROG_COUNTER] = registers[r1]; // JSRR
                }
                break;
            }

            case LD: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                registers[r0] = mem_read(registers[R_PROG_COUNTER] + pc_offset);
                update_flags(r0);
                break;
            }

            case LDR: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t r1 = (instruction >> 6) & 0x7;
                uint16_t offset = sign_extend(instruction & 0x3F, 6);
                registers[r0] = mem_read(registers[r1] + offset);
                update_flags(r0);
                break;
            }

            case LEA: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                registers[r0] = registers[R_PROG_COUNTER] + pc_offset;
                update_flags(r0);
                break;
            }

            case ST: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                mem_write(registers[R_PROG_COUNTER] + pc_offset, registers[r0]);
                break;
            }

            case STI: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                mem_write(mem_read(registers[R_PROG_COUNTER] + pc_offset), registers[r0]);
                break;
            }

            case STR: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t r1 = (instruction >> 6) & 0x7;
                uint16_t offset = sign_extend(instruction & 0x3F, 6);
                mem_write(registers[r1] + offset, registers[r0]);
                break;
            }

            case LDI: {
                uint16_t r0 = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instruction & 0x1FF, 9);
                registers[r0] = mem_read(mem_read(registers[R_PROG_COUNTER] + pc_offset));
                update_flags(r0);
                break;
            }

            case TRAP: {
                registers[R_REG7] = registers[R_PROG_COUNTER];
                switch (instruction & 0xFF) {
                    case TRAP_GETC:
                        registers[R_REG0] = (uint16_t)_getch();
                        update_flags(R_REG0);
                        break;

                    case TRAP_OUT:
                        putc((char)registers[R_REG0], stdout);
                        fflush(stdout);
                        break;

                    case TRAP_PUTS: {
                        uint16_t* c = memory + registers[R_REG0];
                        while (*c) {
                            putc((char)*c, stdout);
                            ++c;
                        }
                        fflush(stdout);
                        break;
                    }

                    case TRAP_IN:
                        putc('>', stdout);
                        fflush(stdout);
                        registers[R_REG0] = (uint16_t)_getch();  // fixed
                        putc((char)registers[R_REG0], stdout);   // echo
                        fflush(stdout);
                        update_flags(R_REG0);
                        break;

                    case TRAP_PUTSP: {
                        uint16_t *c = memory + registers[R_REG0];
                        while (*c) {
                            char chr1 = (*c) & 0xFF;
                            char chr2 = (*c) >> 8;
                            if (chr1) putc(chr1, stdout);
                            if (chr2) putc(chr2, stdout);
                            ++c;
                        }
                        fflush(stdout);
                        break;
                    }

                    case TRAP_HALT:
                        puts("HALTING :( ");
                        fflush(stdout);
                        running = false;
                        break;
                }
                break;
            }

            default:
                /* Invalid opcode */
                break;
        }
    }
    
    restore_input_buffering();
    return 0;
}

void handle_interrupt(int signal) {
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

void update_flags(uint16_t r) {
    if (registers[r] == 0) {
        registers[R_COND] = ZF;
    }
    else if (registers[r] >> 15) {
        registers[R_COND] = NF;
    }
    else {
        registers[R_COND] = PF;
    }
}

uint16_t sign_extend(uint16_t x, int bit_count) {
    if ((x >> (bit_count - 1)) & 1) {
        x |= 0xFFFF << bit_count;
    }
    return x;
}

uint16_t swap16(uint16_t x) {
    return (x << 8) | (x >> 8);
}

void read_image_file(FILE* file) {
    uint16_t start_addr;
    fread(&start_addr, sizeof(start_addr), 1, file);
    start_addr = swap16(start_addr);
    uint16_t max_read = MEMORY_MAX - start_addr;
    uint16_t* p = memory + start_addr;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);
    while (read-- > 0) {
        *p = swap16(*p);
        ++p;
    }
}

int read_image(const char* image_path) {
    FILE* file = fopen(image_path, "rb");
    if (!file) {
        return false;
    }
    read_image_file(file);
    fclose(file);
    return true;
}

void mem_write(uint16_t address, uint16_t val) {
    memory[address] = val;
}

uint16_t mem_read(uint16_t address) {
    if (address == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = 1 << 15;
            memory[MR_KBDR] = (uint16_t)_getch();
        } else {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

void disable_input_buffering() {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode);
    fdwMode = fdwOldMode
            ^ ENABLE_ECHO_INPUT
            ^ ENABLE_LINE_INPUT;
    SetConsoleMode(hStdin, fdwMode);
    FlushConsoleInputBuffer(hStdin);
}

void restore_input_buffering() {
    SetConsoleMode(hStdin, fdwOldMode);
}

uint16_t check_key() {
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

