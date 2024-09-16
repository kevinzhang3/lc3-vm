#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <Windows.h>
#include <conio.h> // _kbhit

#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX]; // memory array 16-bit sizes because LC-3 uses 16-bit sized instructions

/* registers for the LC-3 */
enum
{
    // R0-R7 are the eight general purpose regs 16 bits wide each
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,

    // other non-accessibles regs
    R_PC,   // program counter
    R_COND, // condi codes
    R_COUNT
};

// store regs in array
uint16_t reg[R_COUNT];

/* the 16 opcodes for LC-3 */
enum
{
    // 16 bit 2's complement int
    OP_BR = 0, // branch
    OP_ADD,    // add
    OP_LD,     // load pc-rel
    OP_ST,     // store pc-rel
    OP_JSR,    // jump reg
    OP_AND,    // bitwise AND
    OP_LDR,    // load reg base+offset
    OP_STR,    // store reg base+offset
    OP_RTI,    // return from interrupt
    OP_NOT,    // bitwise NOT
    OP_LDI,    // load indirect
    OP_STI,    // store indirect
    OP_JMP,    // jump
    OP_RES,    // reserved
    OP_LEA,    // load effective address: imm mode
    OP_TRAP    // changes the PC to the address of OS service routine
};

/* LC-3 has 3 condition flags that store the result of the previous operation */
enum
{
    FL_POS = 1 << 0, // P
    FL_ZRO = 1 << 1, // Z
    FL_NEG = 1 << 2, // N
};

/* keyboard access functions */

HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void disable_input_buffering()
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode);     // save old mode
    fdwMode = fdwOldMode ^ ENABLE_ECHO_INPUT // no input echo
              ^ ENABLE_LINE_INPUT;           // return when one or more characters are available

    SetConsoleMode(hStdin, fdwMode); // set new mode
    FlushConsoleInputBuffer(hStdin); // clear buffer
}

/* restore terminal settings */
void restore_input_buffering()
{
    SetConsoleMode(hStdin, fdwOldMode);
}

uint16_t check_key()
{
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

/* if an interrupt occurs, restore terminal settings */
void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

/* sign extend values to be 16 bits 2's complement, fill 1's for -ve 0's for +ve  */
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1)
    {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

/* updating condition flags any time a register is written */
void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15)
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

/* simulate processing instructions in main loop */
int main(int argc, const char *argv[])
{

    /* handles command line input */
    if (argc < 2)
    {
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }
    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }
    /* setup */
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* init condition flags and PC starting position */
    reg[R_COND] = FL_ZRO;
    enum
    {
        PC_START = 0x3000
    };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        /* instruction fetch from memory and store in instr, then increment PC (each address points to a 16 bit word because LC-3 uses word addressing) */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12; // extract opcode (bits 15-12)

        switch (op)
        {
        /* add */
        case OP_ADD:
        {
            /* DR */
            uint16_t r0 = (instr >> 9) & 0x7;
            /* SR1 */
            uint16_t r1 = (instr >> 6) & 0x7;
            /* whether we are in immediate mode (if the 6th bit is 1) */
            uint16_t imm_flag = (instr >> 5) & 0x1;

            /* if an immediate is used, sign extend the value and add*/
            if (imm_flag)
            {
                uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                reg[r0] = reg[r1] + imm5;
            }
            /* otherwise just add the registers and store */
            else
            {
                uint16_t r2 = instr & 0x7;
                reg[r0] = reg[r1] + reg[r2];
            }
            /* update condition flags */
            update_flags(r0);
            break;
        }
        case OP_AND:
        {
            /* Bit-wise Logical AND */
            uint16_t r0 = (instr >> 9) & 0x7;
            uint16_t r1 = (instr >> 6) & 0x7;
            uint16_t imm_flag = (instr >> 5) & 0x1;

            if (imm_flag)
            {
                uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                reg[r0] = reg[r1] & imm5;
            }
            else
            {
                uint16_t r2 = instr & 0x7;
                reg[r0] = reg[r1] & reg[r2];
            }
            update_flags(r0);
            break;
        }
        case OP_NOT:
            /* bitwise complement */
            {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;

                reg[r0] = ~reg[r1];
                update_flags(r0);
                break;
            }
        case OP_BR:
            /* branch */
            {
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                uint16_t cond_flag = (instr >> 9) & 0x7;
                if (cond_flag & reg[R_COND])
                {
                    reg[R_PC] += pc_offset;
                }
                break;
            }
        case OP_JMP:
            /* jump */
            {
                uint16_t r1 = (instr >> 6) & 0x7;
                reg[R_PC] = reg[r1];
                break;
            }
        case OP_JSR:
            // jump reg
            break;
        case OP_LD:
            // load pc-rel
            break;
        case OP_LDI:
            /* load indirect */
            {
                /* destination register (DR) */
                uint16_t r0 = (instr >> 9) & 0x7;
                /* PCoffset9, sign extend to 16 bits*/
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                /* add pc_offset to the current PC, look at that memory location to get the final address */
                reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                update_flags(r0);
                break;
            }
        case OP_LDR:
            /* load base+offset */
            {
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                uint16_t offset = sign_extend(instr & 0x3F, 6);
                reg[r0] = mem_read(mem_read(reg[r1] + offset));
                update_flags(r0);
                break;
            }
        case OP_LEA:
        {
            uint16_t r0 = (instr >> 9) & 0x7;
            uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
            reg[r0] = reg[R_PC] + pc_offset;
            update_flags(r0);
            break;
        }
        case OP_ST:
        {
            uint16_t r0 = (instr >> 9) & 0x7;
            uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
            mem_write(reg[R_PC] + pc_offset, reg[r0]);
            break;
        }
        case OP_STI:
        {
            uint16_t r0 = (instr >> 9) & 0x7;
            uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
            mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
            break;
        }
        case OP_STR:
        {
            uint16_t r0 = (instr >> 9) & 0x7;
            uint16_t r1 = (instr >> 6) & 0x7;
            uint16_t offset = sign_extend(instr & 0x3F, 6);
            mem_write(reg[r1] + offset, reg[r0]);
            break;
        }
        case OP_TRAP:
            // trap
            break;
        case OP_RES:
            // res (not used)
            {
                abort();
                break;
            }
        case OP_RTI:
            // return from interrupt (not used)
            {
                abort();
                break;
            }
        default:
            // invalid op
            break;
        }
    }
}