#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <Windows.h>
#include <conio.h> // _kbhit

#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX]; // memory array

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
    OP_TRAP    // changes PC to address of OS service routine
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
}