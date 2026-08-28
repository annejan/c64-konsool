/*
 Runs the emulated 6502 against Klaus Dormann's functional test suite.

     make -C main/src/test run

 The suite is the standard way to check a 6502: it walks every documented
 opcode and addressing mode, the flag behaviour of each, and decimal mode
 arithmetic, trapping on the spot when something is wrong. Both the C64 and
 the emulated 1541 run on this same core, so a fault here would show up as
 software misbehaving in ways that are very hard to pin down on hardware.

 The test image is Copyright (C) 2012-2015 Klaus Dormann, distributed under
 the GNU General Public License version 3 or later, the same licence as this
 project.
*/

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "CPU6502.hpp"

// Where the image is loaded, where execution starts, and the address the
// suite spins on once every test has passed. All three come from the
// assembler listing that ships with the suite.
static const uint16_t TEST_LOAD_ADDR    = 0x0000;
static const uint16_t TEST_START_ADDR   = 0x0400;
static const uint16_t TEST_SUCCESS_ADDR = 0x3469;

// The suite runs tens of millions of instructions. This is generous enough to
// finish and small enough to notice a runaway.
static const long MAX_INSTRUCTIONS = 500L * 1000L * 1000L;

// A bare 6502 with 64K of RAM and nothing else attached.
class TestCpu : public CPU6502 {
   public:
    uint8_t mem[65536];

    TestCpu()
    {
        memset(mem, 0, sizeof(mem));
    }

    void run() override
    {
    }
    uint8_t getMem(uint16_t addr) override
    {
        return mem[addr];
    }
    void setMem(uint16_t addr, uint8_t val) override
    {
        mem[addr] = val;
    }

    void begin(uint16_t addr)
    {
        pc = addr;
        sp = 0xfd;
        a = x = y   = 0;
        cflag       = false;
        zflag       = false;
        iflag       = true;
        dflag       = false;
        bflag       = false;
        vflag       = false;
        nflag       = false;
        cpuhalted   = false;
        numofcycles = 0;
    }

    // Runs one instruction. Returns the address it started at, so the caller
    // can spot a jump to itself.
    uint16_t stepOne()
    {
        uint16_t at = pc;
        execute(getMem(pc++));
        return at;
    }

    uint16_t currentPc() const
    {
        return pc;
    }
    bool isHalted() const
    {
        return cpuhalted;
    }

    // Handles for the targeted tests below.
    void setPc(uint16_t v)
    {
        pc = v;
    }
    uint8_t getSp() const
    {
        return sp;
    }
    void setSp(uint8_t v)
    {
        sp = v;
    }
    bool getI() const
    {
        return iflag;
    }
    void setI(bool v)
    {
        iflag = v;
    }
    bool getD() const
    {
        return dflag;
    }
    void setD(bool v)
    {
        dflag = v;
    }
    bool getC() const
    {
        return cflag;
    }
    void setC(bool v)
    {
        cflag = v;
    }
    void enterInterrupt(uint16_t vec, bool fromBrk)
    {
        setPCToIntVec(vec, fromBrk);
    }
};

static int checks   = 0;
static int failures = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        checks++;                                         \
        if (!(cond)) {                                    \
            failures++;                                   \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

// The functional test suite deliberately leaves interrupts alone, since a
// bare 6502 has nothing to raise them. Both the C64 and the drive lean on
// this path constantly, so it is worth checking separately.
static void testInterruptEntry()
{
    printf("6502: interrupt entry pushes the right state\n");
    TestCpu cpu;
    cpu.begin(0x1000);
    cpu.setSp(0xff);
    cpu.setI(false);
    cpu.setD(true);
    cpu.setC(true);

    cpu.enterInterrupt(0x9000, false);

    CHECK(cpu.currentPc() == 0x9000, "did not jump to the vector, pc is $%04x", cpu.currentPc());
    CHECK(cpu.getI(), "the interrupt disable flag was not set on entry");
    CHECK(!cpu.getD(), "decimal mode was not cleared on entry");

    // The return address and status sit on the stack, high byte first.
    CHECK(cpu.getSp() == 0xfc, "three bytes should have been pushed, sp is $%02x", cpu.getSp());
    CHECK(cpu.mem[0x01ff] == 0x10, "return address high byte was $%02x", cpu.mem[0x01ff]);
    CHECK(cpu.mem[0x01fe] == 0x00, "return address low byte was $%02x", cpu.mem[0x01fe]);

    uint8_t pushed = cpu.mem[0x01fd];
    CHECK((pushed & 0x10) == 0, "the break flag was set on a hardware interrupt");
    CHECK((pushed & 0x20) != 0, "the unused status bit was not set");
    CHECK((pushed & 0x01) != 0, "carry was lost from the pushed status");
    CHECK((pushed & 0x08) != 0, "decimal was lost from the pushed status");
}

static void testBrkSetsBreakFlag()
{
    printf("6502: BRK marks the pushed status and skips the padding byte\n");
    TestCpu cpu;
    cpu.begin(0x2000);
    cpu.setSp(0xff);
    cpu.setI(false);

    cpu.mem[0xfffe] = 0x34;
    cpu.mem[0xffff] = 0x12;
    cpu.mem[0x2000] = 0x00;  // BRK
    cpu.mem[0x2001] = 0xea;  // the byte BRK skips over

    cpu.stepOne();

    CHECK(cpu.currentPc() == 0x1234, "BRK did not take the vector, pc is $%04x", cpu.currentPc());
    CHECK((cpu.mem[0x01fd] & 0x10) != 0, "BRK did not set the break flag in the pushed status");
    // BRK is one byte but pushes the address after the byte that follows it.
    uint16_t pushedPc = static_cast<uint16_t>(cpu.mem[0x01fe] | (cpu.mem[0x01ff] << 8));
    CHECK(pushedPc == 0x2002, "BRK pushed $%04x, expected $2002", pushedPc);
}

static void testRtiRestores()
{
    printf("6502: RTI puts back the flags and the return address\n");
    TestCpu cpu;
    cpu.begin(0x3000);
    cpu.setSp(0xff);
    cpu.setI(false);
    cpu.setC(true);
    cpu.setD(false);

    cpu.mem[0xfffe] = 0x00;
    cpu.mem[0xffff] = 0x80;
    cpu.mem[0x3000] = 0x00;  // BRK
    cpu.mem[0x8000] = 0x40;  // RTI at the handler

    cpu.stepOne();  // BRK
    CHECK(cpu.getI(), "interrupts were not disabled by BRK");

    cpu.setC(false);  // the handler clobbers a flag
    cpu.stepOne();    // RTI

    CHECK(cpu.currentPc() == 0x3002, "RTI returned to $%04x, expected $3002", cpu.currentPc());
    CHECK(cpu.getC(), "RTI did not restore carry");
    CHECK(!cpu.getI(), "RTI did not restore the interrupt flag");
    CHECK(cpu.getSp() == 0xff, "RTI did not unwind the stack, sp is $%02x", cpu.getSp());
}

static void testStackWraps()
{
    printf("6502: the stack pointer wraps inside page one\n");
    TestCpu cpu;
    cpu.begin(0x4000);
    cpu.setSp(0x00);

    cpu.mem[0x4000] = 0x48;  // PHA
    cpu.stepOne();
    CHECK(cpu.getSp() == 0xff, "sp did not wrap to $ff, it is $%02x", cpu.getSp());

    cpu.mem[0x4001] = 0x68;  // PLA
    cpu.stepOne();
    CHECK(cpu.getSp() == 0x00, "sp did not wrap back to $00, it is $%02x", cpu.getSp());
}

static bool loadImage(TestCpu& cpu, const std::string& path)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;

    size_t total = 0;
    while (total < sizeof(cpu.mem)) {
        ssize_t got = read(fd, cpu.mem + TEST_LOAD_ADDR + total, sizeof(cpu.mem) - TEST_LOAD_ADDR - total);
        if (got <= 0) break;
        total += static_cast<size_t>(got);
    }
    close(fd);
    return total > 0;
}

int main(int argc, char** argv)
{
    std::string path = (argc > 1) ? argv[1] : "6502_functional_test.bin";

    TestCpu cpu;
    if (!loadImage(cpu, path)) {
        printf("cannot read %s\n", path.c_str());
        printf("the suite is vendored next to this test; pass a path to use another copy\n");
        return 1;
    }

    printf("6502: Klaus Dormann functional test suite\n");
    cpu.begin(TEST_START_ADDR);

    long     executed = 0;
    uint16_t trapAt   = 0;
    bool     trapped  = false;

    while (executed < MAX_INSTRUCTIONS) {
        uint16_t at = cpu.stepOne();
        executed++;

        // Every trap in the suite, pass or fail, is a jump to itself.
        if (cpu.currentPc() == at) {
            trapAt  = at;
            trapped = true;
            break;
        }
        if (cpu.isHalted()) {
            printf("  FAIL: hit an illegal opcode at $%04x after %ld instructions\n", at, executed);
            return 1;
        }
    }

    if (!trapped) {
        printf("  FAIL: ran %ld instructions without reaching a trap\n", executed);
        printf("        the suite never finished, so something is looping\n");
        return 1;
    }

    checks++;
    if (trapAt == TEST_SUCCESS_ADDR) {
        printf("  passed, %ld instructions executed\n", executed);
    } else {
        failures++;
        printf("  FAIL: trapped at $%04x after %ld instructions\n", trapAt, executed);
        printf("        that address is where the suite gave up; look it up in\n");
        printf("        6502_functional_test.lst to see which test failed\n");
    }

    testInterruptEntry();
    testBrkSetsBreakFlag();
    testRtiRestores();
    testStackWraps();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
